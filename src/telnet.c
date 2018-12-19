#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <libtelnet/libtelnet.h>
#include <ndmtelnet/xml.h>
#include <ndmtelnet/str.h>
#include <ndmtelnet/code.h>
#include <ndmtelnet/telnet.h>

struct ndm_telnet_t {
	int sock;
	int64_t io_deadline;
	telnet_t *stream;
	enum ndm_telnet_err_t stream_err;
	char *buf_r;
	char *buf_w;
	char *buf_e;
	char buf[1];
};

#if defined(_WIN32) || defined(_WIN64)
#include <WinSock2.h>

#define close									closesocket

#if defined(_MSC_VER)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

#define poll									WSAPoll
#define io_error_get()							WSAGetLastError()
#define io_error_set(e)							WSASetLastError(e)

#define IO_ERROR_EINTR							WSAEINTR
#define IO_ERROR_EAGAIN							WSAEWOULDBLOCK
#define IO_ERROR_EWOULDBLOCK					WSAEWOULDBLOCK
#define IO_ERROR_EINPROGRESS					WSAEINPROGRESS
#define IO_ERROR_EIO							WSAECONNRESET
#define IO_ERROR_EINVAL							WSAEINVAL

typedef int socklen_t;

int64_t ndm_telnet_now()
{
	static LARGE_INTEGER freq;
	LARGE_INTEGER current;

	if (freq.QuadPart == 0) {
		if (!QueryPerformanceFrequency(&freq)) {
			return 0;
		}

		freq.QuadPart /= 1000;
	}

	QueryPerformanceCounter(&current);

	return (int64_t) (current.QuadPart / freq.QuadPart);
}

static inline bool
__ndm_telnet_set_non_blocking(struct ndm_telnet_t *telnet)
{
	u_long non_block = 1;

	return ioctlsocket(telnet->sock, FIONBIO, &non_block) == NO_ERROR;
}

#else /* _WIN32 || _WIN64 */

#include <time.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define io_error_get()							errno
#define io_error_set(e)							(errno = (e))

#define IO_ERROR_EINTR							EINTR
#define IO_ERROR_EAGAIN							EAGAIN
#define IO_ERROR_EWOULDBLOCK					EWOULDBLOCK
#define IO_ERROR_EINPROGRESS					EINPROGRESS
#define IO_ERROR_EIO							EIO
#define IO_ERROR_EINVAL							EINVAL

int64_t ndm_telnet_now()
{
	struct timespec t;

	if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) {
		return 0;
	}

	return ((int64_t) t.tv_sec) * 1000 + ((int64_t) t.tv_nsec) / 1000000;
}

static inline bool
__ndm_telnet_set_non_blocking(struct ndm_telnet_t *telnet)
{
	const int flags = fcntl(telnet->sock, F_GETFL, 0);

	if (flags < 0) {
		return false;
	}

	return fcntl(telnet->sock, F_SETFL, flags | O_NONBLOCK) >= 0;
}

#endif /* _WIN32 || _WIN64 */

#ifndef SOL_TCP
#define SOL_TCP									IPPROTO_TCP
#endif

#define NDM_TELNET_RAW_MODE						"!raw"
#define NDM_TELNET_BUFFER_SIZE					4096
#define NDM_TELNET_STR_STP						64
#define NDM_TELNET_ESC							"\033[K"
#define NDM_TELNET_ESC_LEN						(sizeof(NDM_TELNET_ESC) - 1)
#define NDM_TELNET_LOGIN						"Login: "
#define NDM_TELNET_PASSWORD						"Password: "
#define NDM_TELNET_CONFIG						"(config)> "
#define NDM_TELNET_CONFIG_LEN					\
	(sizeof(NDM_TELNET_CONFIG) - 1)
#define NDM_TELNET_RESPONSE						"<response>"
#define NDM_TELNET_RESPONSE_LEN					\
	(sizeof(NDM_TELNET_RESPONSE) - 1)

static inline bool
__ndm_telnet_interrupted(const int io_error)
{
	return
		io_error == IO_ERROR_EINTR ||
		io_error == IO_ERROR_EAGAIN ||
		io_error == IO_ERROR_EWOULDBLOCK;
}

static ssize_t __ndm_telnet_poll(const struct ndm_telnet_t *telnet,
								 const short events)
{
	struct pollfd pfd;
	const int64_t now = ndm_telnet_now();
	int timeout = 0;
	ssize_t n;

	pfd.fd = telnet->sock;
	pfd.events = events;
	pfd.revents = 0;

	if (telnet->io_deadline > now) {
		timeout = (int) (telnet->io_deadline - now);
	}

	n = (ssize_t) poll(&pfd, 1, timeout);

	if (n > 0 && (pfd.revents & (POLLNVAL | POLLERR))) {
		if (pfd.revents & POLLNVAL) {
			io_error_set(IO_ERROR_EINVAL);
		} else {
			io_error_set(IO_ERROR_EIO);
		}

		return -1;
	}

	return n;
}

static enum ndm_telnet_err_t __ndm_telnet_send(struct ndm_telnet_t *telnet,
											   const void *const data,
											   const size_t data_size)
{
	const char *p = (const char *) data;
	const char *pend = p + data_size;

	while (p < pend) {
		ssize_t n = __ndm_telnet_poll(telnet, POLLWRNORM);

		if (n > 0) {
			n = send(telnet->sock, p, (size_t) (pend - p), 0);
		}

		if (n < 0) { /* poll or send failed */
			if (__ndm_telnet_interrupted(io_error_get())) {
				continue;
			}

			return NDM_TELNET_ERR_SEND;
		}

		if (n == 0) {
			return NDM_TELNET_ERR_IO_TIMEOUT;
		}

		p += (size_t) n;
	}

	return NDM_TELNET_ERR_OK;
}

static void __ndm_telnet_event(telnet_t *telnet,
							   telnet_event_t *ev,
							   void *ud)
{
	struct ndm_telnet_t *client = (struct ndm_telnet_t *) ud;

	if (ev->type == TELNET_EV_DATA) {
		const size_t avail = (size_t) (client->buf_e - client->buf_w);

		if (ev->data.size > avail) {
			const size_t shift = (size_t) (client->buf_r - client->buf);

			if (ev->data.size > avail + shift) {
				/* buffer overflow, should never occur really */
				client->stream_err = NDM_TELNET_ERR_BUFFER_OVERFLOW;
				return;
			}

			memmove(client->buf, client->buf_r,
					(size_t) (client->buf_w - client->buf_r));
			client->buf_r -= shift;
			client->buf_w -= shift;
		}

		memcpy(client->buf_w, ev->data.buffer, ev->data.size);
		client->buf_w += ev->data.size;

		return;
	}

	if (ev->type == TELNET_EV_SEND) {
		client->stream_err = __ndm_telnet_send(client,
											   ev->data.buffer,
											   ev->data.size);
		return;
	}

	if (ev->type == TELNET_EV_ERROR) {
		/* unrecoverable telnet error */
		client->stream_err = NDM_TELNET_ERR_TELNET_ERROR;
	}
}

static enum ndm_telnet_err_t __ndm_telnet_fill(struct ndm_telnet_t *telnet)
{
	ssize_t n;
	size_t size;
	char buf[NDM_TELNET_BUFFER_SIZE];

	if (telnet->buf_w == telnet->buf_e) {
		telnet->buf_r = telnet->buf;
		telnet->buf_w = telnet->buf;
	}

	size = (size_t) (telnet->buf_e - telnet->buf_w);

	if (size > sizeof(buf)) {
		size = sizeof(buf);
	}

	do {
		n = __ndm_telnet_poll(telnet, POLLRDNORM | POLLRDBAND);

		if (n == 0) {
			return NDM_TELNET_ERR_IO_TIMEOUT;
		}

		if (n > 0) {
			n = recv(telnet->sock, buf, size, 0);

			if (n == 0) {
				return NDM_TELNET_ERR_DISCONNECTED;
			}
		}

		if (n < 0) { /* poll or receive failed */
			if (__ndm_telnet_interrupted(io_error_get())) {
				continue;
			}

			return NDM_TELNET_ERR_IO_ERROR;
		}
	} while (n < 0);

	telnet_recv(telnet->stream, buf, (size_t) n);

	return telnet->stream_err;
}

static enum ndm_telnet_err_t
__ndm_telnet_send_cmd(struct ndm_telnet_t *telnet,
					  const char *const cmd)
{
	static const char NEW_LINE = '\n';

	telnet_send_text(telnet->stream, cmd, strlen(cmd));

	if (telnet->stream_err != NDM_TELNET_ERR_OK) {
		return telnet->stream_err;
	}

	telnet_send_text(telnet->stream, &NEW_LINE, sizeof(NEW_LINE));

	return telnet->stream_err;
}

static inline bool
__ndm_telnet_get_ulong(const char *const arg,
					   unsigned long *l)
{
	char *end = NULL;

	if (strlen(arg) == 0 || !isdigit(arg[0])) {
		return false;
	}

	errno = 0;
	*l = strtoul(arg, &end, 10);

	if (errno != 0 || end == NULL || *end != '\0') {
		return false;
	}

	return true;
}

static inline bool
__ndm_telnet_get_code(struct ndm_xml_elem_t *elem,
					  uint32_t *group,
					  uint32_t *local)
{
	unsigned long l = 0;
	struct ndm_xml_attr_t *a_code = ndm_xml_elem_find_attr(elem, "code");

	*group = 0;
	*local = 0;

	if (a_code == NULL) {
		/* just <message> or <error> node without a code */
		return true;
	}

	if (!__ndm_telnet_get_ulong(a_code->value, &l) || l > UINT32_MAX) {
		/* should be a 32-bit decimal unsigned integer */
		return false;
	}

	*group = NDM_CODEGROUP((uint32_t) l);
	*local = NDM_CODELOCAL((uint32_t) l);

	return true;
}

static enum ndm_telnet_err_t
__ndm_telnet_recv(struct ndm_telnet_t *telnet,
				  bool *continued,
				  ndm_code_t *response_code,
				  const char **response_text,
				  struct ndm_xml_elem_t **response)
{
	struct ndm_xml_dom_t dom;
	struct ndm_xml_elem_t *e;
	enum ndm_telnet_err_t err = NDM_TELNET_ERR_OK;

	ndm_xml_dom_init(&dom);

	*continued = false;
	*response_code = 0;
	*response_text = NULL;
	*response = NULL;

	while (*response == NULL) {
		enum ndm_xml_err_t xml_err = NDM_XML_ERR_OK;
		size_t parsed_size = 0;
		size_t avail;

		if (telnet->buf_r == telnet->buf_w) {
			err = __ndm_telnet_fill(telnet);

			if (err != NDM_TELNET_ERR_OK) {
				goto error;
			}
		}

		avail = (size_t) (telnet->buf_w - telnet->buf_r);
		xml_err = ndm_xml_dom_parse(telnet->buf_r, avail,
									&dom, &parsed_size, response);

		switch (xml_err) {
			case NDM_XML_ERR_OK: {
				telnet->buf_r += parsed_size;
				break;
			}

			case NDM_XML_ERR_NOMEM: {
				err = NDM_TELNET_ERR_OOM;
				goto error;
			}

			case NDM_XML_ERR_EOF: {
				err = NDM_TELNET_ERR_RESPONSE_EOS;
				goto error;
			}

			case NDM_XML_ERR_REF:
			case NDM_XML_ERR_CLOSE:
			case NDM_XML_ERR_SYNTAX:
			case NDM_XML_ERR_PI: {
				err = NDM_TELNET_ERR_RESPONSE_SYNTAX;
				goto error;
			}

			case NDM_XML_ERR_STACK: {
				err = NDM_TELNET_ERR_BUFFER_OVERFLOW;
				goto error;
			}

			case NDM_XML_ERR_INTERNAL: {
				err = NDM_TELNET_ERR_INTERNAL_ERROR;
				goto error;
			}

			default: {
				err = NDM_TELNET_ERR_UNKNOWN_ERROR;
				goto error;
			}
		}
	}

	if (strcmp((*response)->name, "event") == 0) {
		*response_text = "";
		goto done;
	}

	if (strcmp((*response)->name, "response") != 0) {
		err = NDM_TELNET_ERR_RESPONSE_FORMAT;
		goto error;
	}

	e = ndm_xml_elem_find_child(*response, "message");

	while (e != NULL) {
		uint32_t group = 0;
		uint32_t local = 0;
		struct ndm_xml_attr_t *a_warn = NULL;

		if (!__ndm_telnet_get_code(e, &group, &local)) {
			err = NDM_TELNET_ERR_RESPONSE_FORMAT;
			goto error;
		}

		a_warn = ndm_xml_elem_find_attr(e, "warning");
		*response_code = NDM_CODE_I(group, local);

		if (a_warn != NULL) {
			if (strcmp(a_warn->value, "yes") == 0) {
				*response_code = NDM_CODE_W(group, local);
			} else if (strcmp(a_warn->value, "no") != 0) {
				err = NDM_TELNET_ERR_RESPONSE_FORMAT;
				goto error;
			}
		}

		*response_text = e->value;

		if (*response_code != 0) {
			break;
		}

		e = ndm_xml_elem_find_next(e, "message");
	}

	if (*response_code == 0) {
		e = ndm_xml_elem_find_child(*response, "error");

		while (e != NULL) {
			uint32_t group = 0;
			uint32_t local = 0;
			struct ndm_xml_attr_t *a_crit = NULL;

			if (!__ndm_telnet_get_code(e, &group, &local)) {
				err = NDM_TELNET_ERR_RESPONSE_FORMAT;
				goto error;
			}

			a_crit = ndm_xml_elem_find_attr(e, "critical");
			*response_code = NDM_CODE_E(group, local);

			if (a_crit != NULL) {
				if (strcmp(a_crit->value, "yes") == 0) {
					*response_code = NDM_CODE_C(group, local);
				} else if (strcmp(a_crit->value, "no") != 0) {
					err = NDM_TELNET_ERR_RESPONSE_FORMAT;
					goto error;
				}
			}

			*response_text = e->value;

			if (*response_code != 0) {
				break;
			}

			e = ndm_xml_elem_find_next(e, "error");
		}
	}

	if (*response_text == NULL) {
		e = ndm_xml_elem_find_child(*response, "prompt");

		if (e != NULL) {
			*response_text = "";
		}
	}

	e = ndm_xml_elem_find_child(*response, "continued");

	if (e != NULL) {
		*continued = true;

		if (*response_text == NULL) {
			*response_text = "";
		}
	}

	if (*response_text == NULL) {
		err = NDM_TELNET_ERR_RESPONSE_FORMAT;
		goto error;
	}

done:
	ndm_xml_dom_free(&dom);

	return NDM_TELNET_ERR_OK;

error:
	ndm_xml_dom_free(&dom);
	ndm_xml_doc_free(response);

	*continued = false;
	*response_code = 0;
	*response_text = NULL;
	*response = NULL;

	return err;
}

static inline void
__ndm_telnet_remove_esc(struct ndm_str_t *str)
{
	size_t l = ndm_str_len(str);
	size_t i = 0;

	while (i + NDM_TELNET_ESC_LEN <= l) {
		const char *ptr = ndm_str_ptr(str);
		size_t j = 0;

		for (; j < NDM_TELNET_ESC_LEN; j++) {
			if (ptr[i + j] != NDM_TELNET_ESC[j]) {
				break;
			}
		}

		if (j == NDM_TELNET_ESC_LEN) {
			ndm_str_erase(str, i, NDM_TELNET_ESC_LEN);
			l -= NDM_TELNET_ESC_LEN;
		} else {
			i++;
		}
	}
}

static inline bool
__ndm_telnet_has_lf(const char *const str)
{
	const char *p = str;

	while (*p != '\0') {
		if (*p == '\n') {
			return true;
		}

		p++;
	}

	return false;
}

static inline bool
__ndm_telnet_is_unicast(const struct in_addr *const addr)
{
	const unsigned long a = ntohl(addr->s_addr);

	if (a == 0x00000000 || a == 0xffffffff) {
		return false;
	}

	return (a & 0xf0000000) != 0xe0000000;
}

enum ndm_telnet_err_t ndm_telnet_open(struct ndm_telnet_t **telnet,
									  const struct sockaddr_in *const sin,
									  const char *const user,
									  const char *const password,
									  const unsigned int timeout)
{
	struct ndm_str_t str;
	enum ndm_telnet_err_t err = NDM_TELNET_ERR_OK;
	bool user_sent = false;
	bool password_sent = false;
	bool raw_sent = false;
	bool raw_recv = false;
	bool continued = 0;
	ndm_code_t response_code = 0;
	const char *response_text = NULL;
	struct ndm_xml_elem_t *response = NULL;
	struct ndm_telnet_t *t = NULL;
	int enable = 1;
	static const telnet_telopt_t TELOPTS[] = {
		{ -1, 0, 0 }
	};

	ndm_str_init(&str, NDM_TELNET_STR_STP);

	if (!__ndm_telnet_is_unicast(&sin->sin_addr)) {
		return NDM_TELNET_ERR_ADDRESS;
	}

	if (sin->sin_port == 0) {
		return NDM_TELNET_ERR_PORT;
	}

	if (__ndm_telnet_has_lf(user)) {
		return NDM_TELNET_ERR_USER;
	}

	if (__ndm_telnet_has_lf(password)) {
		return NDM_TELNET_ERR_PASSWORD;
	}

	if (timeout < NDM_TELNET_MIN_TIMEOUT) {
		return NDM_TELNET_ERR_TIMEOUT_SMALL;
	}

	if (timeout > NDM_TELNET_MAX_TIMEOUT) {
		return NDM_TELNET_ERR_TIMEOUT_LARGE;
	}

	t = (struct ndm_telnet_t *) malloc(sizeof(*t) + NDM_TELNET_BUFFER_SIZE);

	if (t == NULL) {
		return NDM_TELNET_ERR_OOM;
	}

	t->stream = telnet_init(TELOPTS, __ndm_telnet_event, 0, t);

	if (t->stream == NULL) {
		err = NDM_TELNET_ERR_TELNET;
		goto error;
	}

	t->stream_err = NDM_TELNET_ERR_OK;
	t->io_deadline = ndm_telnet_now() + timeout;
	t->buf_r = t->buf;
	t->buf_w = t->buf;
	t->buf_e = t->buf + NDM_TELNET_BUFFER_SIZE;
	t->sock = socket(sin->sin_family, SOCK_STREAM, 0);

	if (t->sock < 0) {
		err = NDM_TELNET_ERR_SOCKET;
		goto error;
	}

	if (setsockopt(t->sock, SOL_TCP, TCP_NODELAY,
				   (char *) &enable, sizeof(enable)) < 0) {
		err = NDM_TELNET_ERR_SETUP;
		goto error;
	}

	if (!__ndm_telnet_set_non_blocking(t)) {
		err = NDM_TELNET_ERR_NON_BLOCK;
		goto error;
	}

	if (connect(t->sock, (struct sockaddr *) sin, sizeof(*sin)) < 0) {
		int ret = 0;
		int error = 0;
		socklen_t error_len = sizeof(error);
		ssize_t n = 0;

		if (io_error_get() != IO_ERROR_EWOULDBLOCK &&
			io_error_get() != IO_ERROR_EINPROGRESS) {
			err = NDM_TELNET_ERR_CONNECT;
			goto error;
		}

		do {
			n = __ndm_telnet_poll(t, POLLWRNORM);

			if (n < 0) {
				if (__ndm_telnet_interrupted(io_error_get())) {
					continue;
				}

				err = NDM_TELNET_ERR_CONNECT;
				goto error;
			}
		} while (n < 0);

		if (n == 0) {
			err = NDM_TELNET_ERR_IO_TIMEOUT;
			goto error;
		}

		ret = getsockopt(t->sock, SOL_SOCKET, SO_ERROR,
						 (char *) &error, &error_len);

		if (ret < 0 || error_len != sizeof(error) || error != 0) {
			err = NDM_TELNET_ERR_CONNECT;
			goto error;
		}
	}

	while (!raw_recv) {
		char *e;
		bool clear = false;

		if (t->buf_r == t->buf_w) {
			err = __ndm_telnet_fill(t);

			if (err != NDM_TELNET_ERR_OK) {
				goto error;
			}
		}

		e = t->buf_r;

		while (e < t->buf_w && *e != '\n') {
			e++;
		}

		if (!ndm_str_append(&str, t->buf_r, (size_t) (e - t->buf_r))) {
			err = NDM_TELNET_ERR_OOM;
			goto error;
		}

		if (e < t->buf_w) {
			t->buf_r = e + 1; /* skip a whole string with a newline */
			clear = true;
		} else {
			t->buf_r = t->buf_w;
		}

		__ndm_telnet_remove_esc(&str);

		if (strcmp(ndm_str_ptr(&str), NDM_TELNET_LOGIN) == 0) {
			if (user_sent) {
				err = NDM_TELNET_ERR_WRONG_CREDENTIALS;
				goto error;
			}

			if (password_sent) {
				err = NDM_TELNET_ERR_WRONG_STATE;
				goto error;
			}

			err = __ndm_telnet_send_cmd(t, user);

			if (err != NDM_TELNET_ERR_OK) {
				goto error;
			}

			clear = true;
			user_sent = true;
		} else if (strcmp(ndm_str_ptr(&str), NDM_TELNET_PASSWORD) == 0) {
			if (!user_sent) {
				err = NDM_TELNET_ERR_WRONG_STATE;
				goto error;
			}

			err = __ndm_telnet_send_cmd(t, password);

			if (err != NDM_TELNET_ERR_OK) {
				goto error;
			}

			clear = true;
			password_sent = true;
		} else if (strcmp(ndm_str_ptr(&str), NDM_TELNET_CONFIG) == 0) {
			if (( user_sent && !password_sent) ||
				(!user_sent &&  password_sent)) {
				err = NDM_TELNET_ERR_WRONG_STATE;
				goto error;
			}

			err = __ndm_telnet_send_cmd(t, NDM_TELNET_RAW_MODE);

			if (err != NDM_TELNET_ERR_OK) {
				goto error;
			}

			if (raw_sent) {
				err = NDM_TELNET_ERR_RAW_NOT_SUPPORTED;
				goto error;
			}

			clear = true;
			raw_sent = true;
		} else if (strcmp(ndm_str_ptr(&str), NDM_TELNET_RAW_MODE "\r") == 0) {
			if (!raw_sent) {
				err = NDM_TELNET_ERR_WRONG_STATE;
				goto error;
			}

			clear = true;
			raw_recv = true;
		}

		if (clear) {
			ndm_str_clear(&str);
		}
	}

	while (true) {
		char *p;

		/* move read data to a buffer start */
		if (t->buf_r == t->buf_w) {
			t->buf_r = t->buf;
			t->buf_w = t->buf;
		} else {
			const size_t shift = (size_t) (t->buf_r - t->buf);

			memmove(t->buf, t->buf_r, (size_t) (t->buf_w - t->buf_r));
			t->buf_r -= shift;
			t->buf_w -= shift;
		}

		if ((size_t) (t->buf_w - t->buf_r) >= NDM_TELNET_CONFIG_LEN &&
			strncmp(t->buf_r, NDM_TELNET_CONFIG,
					NDM_TELNET_CONFIG_LEN) == 0) {
			err = NDM_TELNET_ERR_RAW_NOT_SUPPORTED;
			goto error;
		}

		p = t->buf_r;

		while (p < t->buf_w && *p != '\n') {
			p++;
		}

		if (p == t->buf_w) {
			if (t->buf_w == t->buf_e) {
				/* no ESC sequence or string without it */
				err = NDM_TELNET_ERR_UNKNOWN_PROTOCOL;
				goto error;
			}

			err = __ndm_telnet_fill(t);

			if (err != NDM_TELNET_ERR_OK) {
				goto error;
			}

			/* continue to fill the buffer */
			continue;
		}

		/* a newline delimiter found */
		char *s = t->buf_r;

		while (s < p && isspace(*s)) {
			s++;
		}

		if (*s == '\n') {
			t->buf_r = s + 1; /* skip an empty string */
			continue;
		}

		if ((size_t) (t->buf_w - s) < NDM_TELNET_RESPONSE_LEN ||
			strncmp(s, NDM_TELNET_RESPONSE, NDM_TELNET_RESPONSE_LEN) != 0) {
			err = NDM_TELNET_ERR_RAW_NOT_SUPPORTED;
			goto error;
		}

		break;
	}

	err = __ndm_telnet_recv(t, &continued, &response_code,
							&response_text, &response);

	if (err != NDM_TELNET_ERR_OK) {
		goto error;
	}

	if (NDM_FAILED(response_code)) {
		err = NDM_TELNET_ERR_RAW_FAILED;
	}

error:
	if (err != NDM_TELNET_ERR_OK) {
		ndm_telnet_close(&t);
	}

	*telnet = t;
	ndm_str_free(&str);
	ndm_xml_doc_free(&response);

	return err;
}

enum ndm_telnet_err_t ndm_telnet_send(struct ndm_telnet_t *telnet,
									  const char *const command,
									  const unsigned int timeout)
{
	const char *p = command;

	while (isspace(*p)) {
		p++;
	}

	if (*p == '\0') {
		return NDM_TELNET_ERR_NO_COMMAND;
	}

	if (__ndm_telnet_has_lf(command)) {
		return NDM_TELNET_ERR_COMMAND;
	}

	telnet->io_deadline = ndm_telnet_now() + timeout;

	return __ndm_telnet_send_cmd(telnet, command);
}


enum ndm_telnet_err_t ndm_telnet_recv(struct ndm_telnet_t *telnet,
									  bool *continued,
									  ndm_code_t *response_code,
									  const char **response_text,
									  struct ndm_xml_elem_t **response,
									  const unsigned int timeout)
{
	*continued = false;
	*response_code = 0;
	*response_text = NULL;
	*response = NULL;

	telnet->io_deadline = ndm_telnet_now() + timeout;

	return __ndm_telnet_recv(telnet, continued, response_code,
							 response_text, response);
}

void ndm_telnet_close(struct ndm_telnet_t **telnet)
{
	if (telnet == NULL || *telnet == NULL) {
		return;
	}

	telnet_free((*telnet)->stream);
	close((*telnet)->sock);
	free(*telnet);
	*telnet = NULL;
}

const char *ndm_telnet_strerror(const enum ndm_telnet_err_t err)
{
	switch (err) {
		case NDM_TELNET_ERR_OK: {
			return "no error";
		}

		case NDM_TELNET_ERR_BUFFER_OVERFLOW: {
			return "internal buffer overflow";
		}

		case NDM_TELNET_ERR_TELNET: {
			return "unable to create a telnet client";
		}

		case NDM_TELNET_ERR_TELNET_ERROR: {
			return "unrecoverable telnet error";
		}

		case NDM_TELNET_ERR_USER: {
			return "user name has a newline character";
		}

		case NDM_TELNET_ERR_PASSWORD: {
			return "password has a newline character";
		}

		case NDM_TELNET_ERR_TIMEOUT_SMALL: {
			return "timeout is too small";
		}

		case NDM_TELNET_ERR_TIMEOUT_LARGE: {
			return "timeout is too large";
		}

		case NDM_TELNET_ERR_ADDRESS: {
			return "invalid device address";
		}

		case NDM_TELNET_ERR_PORT: {
			return "client port is zero";
		}

		case NDM_TELNET_ERR_SETUP: {
			return "unable to setup a socket";
		}

		case NDM_TELNET_ERR_NON_BLOCK: {
			return "unable to setup a non-blocking connection";
		}

		case NDM_TELNET_ERR_CONNECT: {
			return "unable to connect to a device";
		}

		case NDM_TELNET_ERR_NO_COMMAND: {
			return "no command specified";
		}

		case NDM_TELNET_ERR_COMMAND: {
			return "command has a newline character";
		}

		case NDM_TELNET_ERR_IO_ERROR: {
			return "I/O error";
		}

		case NDM_TELNET_ERR_SEND: {
			return "data send error";
		}

		case NDM_TELNET_ERR_IO_TIMEOUT: {
			return "I/O timeout";
		}

		case NDM_TELNET_ERR_OOM: {
			return "out of memory";
		}

		case NDM_TELNET_ERR_RESPONSE_EOS: {
			return "unexpected end of stream";
		}

		case NDM_TELNET_ERR_RESPONSE_SYNTAX: {
			return "wrong response syntax";
		}

		case NDM_TELNET_ERR_RESPONSE_FORMAT: {
			return "wrong response format";
		}

		case NDM_TELNET_ERR_INTERNAL_ERROR: {
			return "internal error";
		}

		case NDM_TELNET_ERR_UNKNOWN_ERROR: {
			return "unknown error";
		}

		case NDM_TELNET_ERR_SOCKET: {
			return "unable to open a socket";
		}

		case NDM_TELNET_ERR_SET_TIMEOUT: {
			return "unable to setup a socket timeout";
		}

		case NDM_TELNET_ERR_WRONG_CREDENTIALS: {
			return "invalid a user name or password";
		}

		case NDM_TELNET_ERR_WRONG_STATE: {
			return "wrong authentication state";
		}

		case NDM_TELNET_ERR_UNKNOWN_PROTOCOL: {
			return "unknown telnet protocol";
		}

		case NDM_TELNET_ERR_RAW_NOT_SUPPORTED: {
			return "the raw mode not supported";
		}

		case NDM_TELNET_ERR_RAW_FAILED: {
			return "unable to enter the raw mode";
		}

		case NDM_TELNET_ERR_DISCONNECTED: {
			return "disconnected by peer";
		}

		default: {
			break;
		}
	}

	return "unknown error";
}
