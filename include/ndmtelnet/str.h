#ifndef __NDM_STR_H__
#define __NDM_STR_H__

#include <stddef.h>
#include <stdbool.h>
#include "config.h"

struct ndm_str_t {
	char *ptr;	/* string data pointer */
	size_t len; /* string length */
	size_t cap; /* storage capacity */
	size_t stp;	/* capacity increase step */
};

#ifdef __cplusplus
extern "C" {
#endif

static inline void
ndm_str_init(struct ndm_str_t *s, const size_t stp)
{
	s->ptr = NULL;
	s->len = 0;
	s->cap = 0;
	s->stp = stp;
}

bool ndm_str_append(struct ndm_str_t *s,
					const char *str,
					const size_t str_len);

void ndm_str_free(struct ndm_str_t *s);

void ndm_str_erase(struct ndm_str_t *s,
				   const size_t index,
				   const size_t size);

static inline void
ndm_str_clear(struct ndm_str_t *s)
{
	if (s->ptr != NULL && s->cap > 0) {
		s->ptr[0] = 0;
	}

	s->len = 0;
}

static inline const char *
ndm_str_ptr(const struct ndm_str_t *const s)
{
	return s->ptr;
}

static inline size_t
ndm_str_len(const struct ndm_str_t *const s)
{
	return s->len;
}

#ifdef __cplusplus
}
#endif

#endif /* __NDM_STR_H__ */
