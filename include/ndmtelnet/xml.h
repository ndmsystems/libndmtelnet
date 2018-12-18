#ifndef __NDM_XML_H__
#define __NDM_XML_H__

#include <inttypes.h>
#include <ylib/yxml.h>

struct ndm_xml_attr_t {
	struct ndm_xml_attr_t *next;
	struct ndm_xml_attr_t *prev;
	char *value;
	char name[1];
};

struct ndm_xml_elem_t {
	struct {
		struct ndm_xml_attr_t *head;
		struct ndm_xml_attr_t *tail;
	} attributes;
	struct {
		struct ndm_xml_elem_t *head;
		struct ndm_xml_elem_t *tail;
	} children;
	struct ndm_xml_elem_t *next;
	struct ndm_xml_elem_t *prev;
	struct ndm_xml_elem_t *parent;
	char *value;
	char name[1];
};

#define NDM_XML_VALUE_ALLOC_STEP				1024

struct ndm_xml_value_t {
	char static_data[NDM_XML_VALUE_ALLOC_STEP];
	char *data;
	size_t size;
	size_t cap;
};

struct ndm_xml_dom_t {
	uint8_t parser_buf[4096];
	yxml_t parser;
	struct ndm_xml_elem_t *root;
	struct ndm_xml_elem_t *e;
	struct ndm_xml_attr_t *a;
	struct ndm_xml_value_t value;
};

enum ndm_xml_err_t
{
	NDM_XML_ERR_OK								= 0,
	NDM_XML_ERR_NOMEM							= 1, /* out of memory */
	NDM_XML_ERR_EOF								= 2, /* unexpected EOF */
	NDM_XML_ERR_REF								= 3, /* invalid reference */
	NDM_XML_ERR_CLOSE							= 4, /* unknown close tag */
	NDM_XML_ERR_STACK							= 5, /* stack overflow */
	NDM_XML_ERR_SYNTAX							= 6, /* syntax error */
	NDM_XML_ERR_PI								= 7, /* PI node not supp. */
	NDM_XML_ERR_INTERNAL						= 8  /* internal error */
};

#ifdef __cplusplus
extern "C" {
#endif

void ndm_xml_dom_init(struct ndm_xml_dom_t *dom);

enum ndm_xml_err_t ndm_xml_dom_parse(const char *const text,
									 const size_t text_size,
									 struct ndm_xml_dom_t *dom,
									 size_t *parsed_size,
									 struct ndm_xml_elem_t **root);

void ndm_xml_dom_free(struct ndm_xml_dom_t *dom);

void ndm_xml_doc_free(struct ndm_xml_elem_t **root);

struct ndm_xml_elem_t *
ndm_xml_elem_find_child(const struct ndm_xml_elem_t *const elem,
						const char *const name);

struct ndm_xml_elem_t *
ndm_xml_elem_find_next(const struct ndm_xml_elem_t *const elem,
					   const char *const name);

struct ndm_xml_attr_t *
ndm_xml_elem_find_attr(const struct ndm_xml_elem_t *const elem,
					   const char *const name);

#ifdef __cplusplus
}
#endif

#endif /* __NDM_XML_H__ */
