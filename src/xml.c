#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ylib/list.h>
#include <ylib/yxml.h>
#include <ndmtelnet/xml.h>

#define NDM_XML_VALUE_RESET_SIZE				1024

static inline void __ndm_xml_value_init(struct ndm_xml_value_t *v)
{
	v->data = v->static_data;
	v->size = 0;
	v->cap = 0;
}

static inline size_t __ndm_xml_value_cap(const size_t size,
										 const size_t align)
{
	return size + align - (size + align) % align;
}

static inline bool __ndm_xml_value_append(struct ndm_xml_value_t *v,
										  const char* const value)
{
	const size_t value_size = strlen(value);

	if (v->size + value_size > v->cap) {
		const size_t cap = __ndm_xml_value_cap(v->size + value_size,
											   NDM_XML_VALUE_ALLOC_STEP);
		char *prev = (v->data == v->static_data) ? NULL : v->data;
		char *data = (char *) realloc(prev, cap);

		if (data == NULL) {
			return false;
		}

		if (v->data == v->static_data) {
			memcpy(data, v->static_data, v->size);
		}

		v->data = data;
		v->cap = cap;
	}

	memcpy(v->data + v->size, value, value_size);
	v->size += value_size;

	return true;
}

static inline bool __ndm_xml_value_flush(struct ndm_xml_value_t *v,
										 char **value)
{
	const size_t value_size = (*value == NULL) ? 0 : strlen(*value);
	char *val = (char *) realloc(*value, value_size + v->size + 1);

	if (val == NULL) {
		return false;
	}

	memcpy(val + value_size, v->data, v->size);
	val[value_size + v->size] = 0;

	*value = val;

	/* reset a value data */
	if (v->data != v->static_data && v->cap > NDM_XML_VALUE_RESET_SIZE) {
		char *data = (char *) realloc(v->data, NDM_XML_VALUE_RESET_SIZE);

		if (data != NULL) {
			v->data = data;
			v->cap = NDM_XML_VALUE_RESET_SIZE;
		}
	}

	v->size = 0;

	return true;
}

static inline void __ndm_xml_value_free(struct ndm_xml_value_t *v)
{
	if (v->data != v->static_data) {
		free(v->data);
		v->size = 0;
		v->cap = 0;
	}
}

void ndm_xml_dom_init(struct ndm_xml_dom_t *dom)
{
	yxml_init(&dom->parser, dom->parser_buf, sizeof(dom->parser_buf));
	dom->root = NULL;
	dom->e = NULL;
	dom->a = NULL;
	__ndm_xml_value_init(&dom->value);
};

enum ndm_xml_err_t ndm_xml_dom_parse(const char *const text,
									 const size_t text_size,
									 struct ndm_xml_dom_t *dom,
									 size_t *parsed_size,
									 struct ndm_xml_elem_t **root)
{
	const char *t = text;
	const char *tend = text + text_size;
	yxml_t *p = &dom->parser;
	enum ndm_xml_err_t err = NDM_XML_ERR_OK;

	*root = NULL;

	while (t < tend) {
		switch (yxml_parse(p, *t)) {
			case YXML_EEOF: {
				err = NDM_XML_ERR_EOF;
				goto stop;
			}

			case YXML_EREF: {
				err = NDM_XML_ERR_REF;
				goto stop;
			}

			case YXML_ECLOSE: {
				err = NDM_XML_ERR_CLOSE;
				goto stop;
			}

			case YXML_ESTACK: {
				err = NDM_XML_ERR_SYNTAX;
				goto stop;
			}

			case YXML_ESYN: {
				err = NDM_XML_ERR_SYNTAX;
				goto stop;
			}

			case YXML_OK: {
				break;
			}

			case YXML_ELEMSTART: {
				const size_t name_size = yxml_symlen(p, p->elem);
				size_t elem_size;
				struct ndm_xml_elem_t *e;

				if (dom->e != NULL &&
					!__ndm_xml_value_flush(&dom->value,
										   &dom->e->value)) {
					err = NDM_XML_ERR_NOMEM;
					goto stop;
				}

				elem_size = sizeof(*e) + name_size;
				e = (struct ndm_xml_elem_t *) malloc(elem_size);

				if (e == NULL) {
					err = NDM_XML_ERR_NOMEM;
					goto stop;
				}

				memcpy(e->name, p->elem, name_size);
				e->name[name_size] = 0;

				e->value = NULL;
				e->attributes.head = NULL;
				e->attributes.tail = NULL;
				e->children.head = NULL;
				e->children.tail = NULL;
				e->next = NULL;
				e->prev = NULL;

				if (dom->root == NULL) {
					dom->root = e;
				} else {
					list_append(dom->e->children, e);
				}

				e->parent = dom->e;
				dom->e = e;

				break;
			}

			case YXML_CONTENT:
			case YXML_ATTRVAL: {
				if (!__ndm_xml_value_append(&dom->value, p->data)) {
					err = NDM_XML_ERR_NOMEM;
					goto stop;
				}

				break;
			}

			case YXML_ELEMEND: {
				if (!__ndm_xml_value_flush(&dom->value, &dom->e->value)) {
					err = NDM_XML_ERR_NOMEM;
					goto stop;
				}

				if (dom->e->parent == NULL) {
					*root = dom->root;
					dom->root = NULL;

					t++;

					goto stop;
				}

				dom->e = dom->e->parent;
				dom->a = NULL;

				break;
			}

			case YXML_ATTRSTART: {
				const size_t name_size = yxml_symlen(p, p->attr);
				struct ndm_xml_attr_t *a;
				const size_t attr_size = sizeof(*a) + name_size;

				a = (struct ndm_xml_attr_t *) malloc(attr_size);

				if (a == NULL) {
					err = NDM_XML_ERR_NOMEM;
					goto stop;
				}

				memcpy(a->name, p->attr, name_size);
				a->name[name_size] = 0;

				a->value = NULL;
				a->next = NULL;
				a->prev = NULL;

				list_append(dom->e->attributes, a);
				dom->a = a;

				break;
			}

			case YXML_ATTREND: {
				if (!__ndm_xml_value_flush(&dom->value, &dom->a->value)) {
					err = NDM_XML_ERR_NOMEM;
					goto stop;
				}

				dom->a = NULL;

				break;
			}

			case YXML_PISTART:
			case YXML_PICONTENT:
			case YXML_PIEND: {
				err = NDM_XML_ERR_PI;
				goto stop;
			}

			default: {
				err = NDM_XML_ERR_INTERNAL;
				goto stop;
			}
		}

		t++;
	}

stop:
	*parsed_size = (size_t) (t - text);

	return err;
}

void ndm_xml_dom_free(struct ndm_xml_dom_t *dom)
{
	ndm_xml_doc_free(&dom->root);
	__ndm_xml_value_free(&dom->value);
}

void ndm_xml_doc_free(struct ndm_xml_elem_t **root)
{
	struct ndm_xml_elem_t *e;
	struct ndm_xml_elem_t *parent = NULL;

	if (root == NULL) {
		return;
	}

	e = *root;

	while (e != NULL) {
		struct ndm_xml_elem_t *r;

		while (e->children.head != NULL) {
			parent = e;
			e = e->children.head;
		}

		r = e;

		if (r->next != NULL) {
			e = e->next;
			list_remove(r->parent->children, r);
		} else {
			e = parent;

			if (parent != NULL) {
				list_remove(r->parent->children, r);
				parent = e->parent;
			}
		}

		while (r->attributes.head != NULL) {
			struct ndm_xml_attr_t *a = r->attributes.head;

			list_remove(r->attributes, a);
			free(a->value);
			free(a);
		}

		free(r->value);
		free(r);
	}

	*root = NULL;
}

static struct ndm_xml_elem_t *
__ndm_xml_elem_find(const struct ndm_xml_elem_t *const elem,
					const char *const name)
{
	struct ndm_xml_elem_t *e = (struct ndm_xml_elem_t *) elem;

	while (e != NULL) {
		if (strcmp(e->name, name) == 0) {
			return e;
		}

		e = e->next;
	}

	return NULL;
}

struct ndm_xml_elem_t *
ndm_xml_elem_find_child(const struct ndm_xml_elem_t *const elem,
						const char *const name)
{
	return __ndm_xml_elem_find(elem->children.head, name);
}

struct ndm_xml_elem_t *
ndm_xml_elem_find_next(const struct ndm_xml_elem_t *const elem,
					   const char *const name)
{
	return __ndm_xml_elem_find(elem->next, name);
}

struct ndm_xml_attr_t *
ndm_xml_elem_find_attr(const struct ndm_xml_elem_t *const elem,
					   const char *const name)
{
	struct ndm_xml_attr_t *a = elem->attributes.head;

	while (a != NULL) {
		if (strcmp(a->name, name) == 0) {
			return a;
		}

		a = a->next;
	}

	return NULL;
}
