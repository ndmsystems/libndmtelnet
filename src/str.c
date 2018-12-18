#include <stdlib.h>
#include <string.h>
#include <ndmtelnet/str.h>

static inline size_t
__ndm_str_len_align(const size_t len,
					const size_t align)
{
	return len + align - (len + align) % align;
}

bool ndm_str_append(struct ndm_str_t *s,
					const char *str,
					const size_t str_len)
{
	const size_t new_cap = s->len + str_len + 1;

	if (new_cap > s->cap) {
		const size_t cap = __ndm_str_len_align(new_cap, s->stp);
		char *ptr = (char *) realloc(s->ptr, cap);

		if (ptr == NULL) {
			return false;
		}

		s->ptr = ptr;
		s->cap = cap;
	}

	memcpy(s->ptr + s->len, str, str_len);
	s->len += str_len;
	s->ptr[s->len] = 0;

	return true;
}

void ndm_str_free(struct ndm_str_t *s)
{
	free(s->ptr);
	ndm_str_init(s, s->stp);
}

void ndm_str_erase(struct ndm_str_t *s,
				   const size_t index,
				   const size_t count)
{
	memmove(s->ptr + index,
			s->ptr + index + count,
			s->len + 1 - (index + count));
	s->len -= count;
}
