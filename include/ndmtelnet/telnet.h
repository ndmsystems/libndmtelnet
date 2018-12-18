#ifndef __NDM_TELNET_H__
#define __NDM_TELNET_H__

#include <stdbool.h>
#include "code.h"

#define NDM_TELNET_DEF_ADDRESS					0xc0a80101 /* 192.168.1.1 */
#define NDM_TELNET_DEF_PORT						23
#define NDM_TELNET_DEF_USER						"admin"
#define NDM_TELNET_DEF_PASSWORD					""

#define NDM_TELNET_DEF_TIMEOUT					5000
#define NDM_TELNET_MIN_TIMEOUT					1000
#define NDM_TELNET_MAX_TIMEOUT					60000

struct sockaddr_in;

struct ndm_telnet_t;
struct ndm_xml_elem_t;

enum ndm_telnet_err_t
{
	NDM_TELNET_ERR_OK,
	NDM_TELNET_ERR_BUFFER_OVERFLOW,
	NDM_TELNET_ERR_TELNET,
	NDM_TELNET_ERR_TELNET_ERROR,
	NDM_TELNET_ERR_USER,
	NDM_TELNET_ERR_PASSWORD,
	NDM_TELNET_ERR_TIMEOUT_SMALL,
	NDM_TELNET_ERR_TIMEOUT_LARGE,
	NDM_TELNET_ERR_ADDRESS,
	NDM_TELNET_ERR_PORT,
	NDM_TELNET_ERR_SETUP,
	NDM_TELNET_ERR_NON_BLOCK,
	NDM_TELNET_ERR_CONNECT,
	NDM_TELNET_ERR_NO_COMMAND,
	NDM_TELNET_ERR_COMMAND,
	NDM_TELNET_ERR_IO_ERROR,
	NDM_TELNET_ERR_SEND,
	NDM_TELNET_ERR_IO_TIMEOUT,
	NDM_TELNET_ERR_OOM,
	NDM_TELNET_ERR_RESPONSE_EOS,
	NDM_TELNET_ERR_RESPONSE_SYNTAX,
	NDM_TELNET_ERR_RESPONSE_FORMAT,
	NDM_TELNET_ERR_INTERNAL_ERROR,
	NDM_TELNET_ERR_UNKNOWN_ERROR,
	NDM_TELNET_ERR_SOCKET,
	NDM_TELNET_ERR_SET_TIMEOUT,
	NDM_TELNET_ERR_WRONG_CREDENTIALS,
	NDM_TELNET_ERR_WRONG_STATE,
	NDM_TELNET_ERR_UNKNOWN_PROTOCOL,
	NDM_TELNET_ERR_RAW_NOT_SUPPORTED,
	NDM_TELNET_ERR_RAW_FAILED,
	NDM_TELNET_ERR_DISCONNECTED
};

#ifdef __cplusplus
extern "C" {
#endif

enum ndm_telnet_err_t ndm_telnet_open(struct ndm_telnet_t **telnet,
									  const struct sockaddr_in *const sin,
									  const char *const login,
									  const char *const password,
									  const unsigned int timeout);

enum ndm_telnet_err_t ndm_telnet_send(struct ndm_telnet_t *telnet,
									  const char *const command,
									  const unsigned int timeout);

enum ndm_telnet_err_t ndm_telnet_recv(struct ndm_telnet_t *telnet,
									  bool *continued,
									  ndm_code_t *response_code,
									  const char **response_text,
									  struct ndm_xml_elem_t **response,
									  const unsigned int timeout);

void ndm_telnet_close(struct ndm_telnet_t **telnet);

const char *ndm_telnet_strerror(const enum ndm_telnet_err_t err);

int64_t ndm_telnet_now();

#ifdef __cplusplus
}
#endif

#endif /* __NDM_TELNET_H__ */
