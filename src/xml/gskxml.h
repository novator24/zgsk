/* GskXml: A reference-counted, immutable XML node.  */
#ifndef __GSK_XML_H_
#define __GSK_XML_H_

typedef struct _GskXml GskXml;
typedef struct _GskXmlElement GskXmlElement;

#include <glib-object.h>
#include "../gskbuffer.h"

GType gsk_xml_get_type (void) G_GNUC_CONST;
#define GSK_TYPE_XML             (gsk_xml_get_type())

typedef enum
{
  GSK_XML_ELEMENT,
  GSK_XML_TEXT
} GskXmlType;

struct _GskXml
{
  GskXmlType type;
  guint ref_count;
};

/* NOTE: this macro assumes that xml->type is GSK_XML_TEXT */
#define GSK_XML_PEEK_TEXT(xml) ((char*)((GskXml*)(xml) + 1))

struct _GskXmlElement
{
  /*< private >*/
  GskXml base;

  /*< public (readonly) >*/
  char *name;
  guint n_attrs;
  char **attrs;                 /* not NULL terminated */
  guint n_children;
  GskXml **children;
};
/* NOTE: these macros assume that xml->type is GSK_XML_ELEMENT */
/* NOTE: attrs is NULL terminated */
#define GSK_XML_PEEK_NAME(xml)       (((GskXmlElement*)(xml))->name)
#define GSK_XML_PEEK_N_ATTRS(xml)    (((GskXmlElement*)(xml))->n_attrs)
#define GSK_XML_PEEK_ATTRS(xml)      ((char**)((GskXmlElement*)(xml) + 1))
#define GSK_XML_PEEK_CHILDREN(xml)   (((GskXmlElement*)(xml))->children)
#define GSK_XML_PEEK_N_CHILDREN(xml) (((GskXmlElement*)(xml))->n_children)
#define GSK_XML_PEEK_CHILD(xml,i)    ((((GskXmlElement*)(xml))->children)[i])

GskXml      *gsk_xml_text_new           (const char    *text);
GskXml      *gsk_xml_text_new_len       (const char    *text,
                                         guint          len);
GskXml      *gsk_xml_element_new        (const char    *name,
                                         guint          n_kv_pairs,
                                         char         **attr_kv_pairs,
                                         guint          n_children,
                                         GskXml       **children);
GskXml      *gsk_xml_ref                (GskXml        *xml);
void        gsk_xml_unref               (GskXml        *xml);
gboolean    gsk_xml_is_element          (const GskXml  *xml,
                                         const char    *name);
gboolean    gsk_xml_is_whitespace       (const GskXml  *xml);
#define gsk_xml_element_new_empty(name) gsk_xml_element_new(name,0,NULL,0,NULL)
#define     gsk_xml_text_child_new(name, text) \
  gsk_xml_element_new_take_1(name, gsk_xml_text_new (text))
GskXml      *gsk_xml_text_new_printf    (const char    *format,
                                         ...) G_GNUC_PRINTF(1,2);
GskXml      *gsk_xml_text_new_vprintf   (const char    *format,
                                         va_list        args);
GskXml      *gsk_xml_text_child_printf  (const char    *name,
                                         const char    *format,
                                         ...) G_GNUC_PRINTF(2,3);

const char  *gsk_xml_find_attr          (GskXml        *xml,
                                         const char    *attr_name);
GskXml      *gsk_xml_parse_file         (const char    *filename,
                                         GError       **error);
GskXml      *gsk_xml_parse_str          (const char    *str,
                                         GError       **error);
GskXml      *gsk_xml_parse_str_len      (const char    *str,
                                         gssize         len,
                                         GError       **error);
GskXml      *gsk_xml_find_child         (GskXml        *xml,
                                         const char    *child_node_name,
                                         guint          instance);
gboolean     gsk_xml_peek_child_text    (GskXml        *xml,
                                         const char   *name,
                                         gboolean      required,
                                         const char  **p_out,
                                         GError      **error);
gboolean     gsk_xml_peek_path_text     (GskXml       *xml,
				         const char   *path,
				         gboolean      required,
				         const char  **p_out,
				         GError      **error);

GskXml      *gsk_xml_lookup_path        (GskXml       *xml,
                                         const char   *path);

typedef gboolean (*GskXmlForeachFunc) (GskXml         *xml,
                                       gpointer        foreach_data);

/* returns FALSE if any invocations of func return FALSE;
   we stop iterating at that point. */
typedef enum
{
  GSK_XML_FOREACH_REVERSED = (1<<0)
} GskXmlForeachFlags;
gboolean     gsk_xml_foreach_by_path    (GskXml       *xml,
                                         const char   *path,
                                         GskXmlForeachFunc func,
                                         gpointer      foreach_data,
                                         GskXmlForeachFlags flags);
GskXml      *gsk_xml_index              (GskXml       *xml,
                                         guint         n_indices,
                                         const guint  *indices);

gboolean     gsk_xml_dump               (const GskXml *xml,
                                         int           fd,
                                         GError      **error);
gboolean     gsk_xml_dump_file          (const GskXml *xml,
                                         const char   *filename,
                                         GError      **error);
void         gsk_xml_dump_buffer        (const GskXml *xml,
                                         GskBuffer    *buffer);
void         gsk_xml_dump_buffer_formatted (const GskXml *xml,
                                         GskBuffer    *buffer);
char         *gsk_xml_to_string         (const GskXml *xml);

char         *gsk_xml_get_all_text      (const GskXml *xml);
void    gsk_xml_append_text_to_string   (const GskXml *xml,
                                         GString      *str);

const char   *gsk_xml_find_solo_text    (const GskXml  *xml,
                                         GError       **error);
GskXml       *gsk_xml_find_solo_child   (GskXml        *xml,
                                         GError      **error);

/* helpers */
GskXml       *gsk_xml_text_new_int      (int           value);
GskXml       *gsk_xml_element_new_take_1(const char    *name,
                                         GskXml        *a);
GskXml       *gsk_xml_element_new_take_2(const char    *name,
                                         GskXml        *a,
                                         GskXml        *b);
GskXml       *gsk_xml_element_new_take_3(const char    *name,
                                         GskXml        *a,
                                         GskXml        *b,
                                         GskXml        *c);
GskXml       *gsk_xml_element_new_1     (const char    *name,
                                         GskXml        *a);
GskXml       *gsk_xml_element_new_2     (const char    *name,
                                         GskXml        *a,
                                         GskXml        *b);
GskXml       *gsk_xml_element_new_3     (const char    *name,
                                         GskXml        *a,
                                         GskXml        *b,
                                         GskXml        *c);
GskXml  *gsk_xml_element_new_take_list  (const char   *name,
                                         GskXml        *first_node_or_null,
                                         ...);

/* NOTE: only the elements of 'children' are taken:
   the attributes are copied, as is the array 'children' itself.
   consecutive text nodes are coalesced. */
GskXml      *gsk_xml_element_new_take   (const char    *name,
                                         guint          n_kv_pairs,
                                         char         **attr_kv_pairs,
                                         guint          n_children,
                                         GskXml       **children);

GskXml    *gsk_xml_element_new_contents (GskXml        *base_xml,
                                         guint          n_children,
                                         GskXml       **children);
GskXml      *gsk_xml_element_new_append (GskXml        *base_xml,
                                         guint          n_add_children,
                                         GskXml       **add_children);
GskXml      *gsk_xml_element_new_prepend(GskXml        *base_xml,
                                         guint          n_add_children,
                                         GskXml       **add_children);

GskXml     *gsk_xml_element_replace_name(GskXml        *base,
                                         const char    *new_name);

/* for creating a hash-table of GskXml */
guint        gsk_xml_hash               (gconstpointer xml_node);
gboolean     gsk_xml_equal              (gconstpointer a_node,
                                         gconstpointer b_node);

gsize        gsk_xml_estimate_size      (const GskXml  *xml);



typedef enum
{
  /* if set, element attribute ordering is ignored */
  GSK_XML_EQUAL_SORT_ATTRIBUTES  = (1<<0),

  /* if set, whitespace-only text nodes are ignored */
  GSK_XML_EQUAL_IGNORE_TRIVIAL_WHITESPACE = (1<<1),

  /* if set, whitespace at the beginning and ending of text is ignored */
  GSK_XML_EQUAL_IGNORE_END_WHITESPACE = (1<<2),

  GSK_XML_EQUAL_IGNORE_WHITESPACE = (GSK_XML_EQUAL_IGNORE_TRIVIAL_WHITESPACE
                                    | GSK_XML_EQUAL_IGNORE_END_WHITESPACE)
} GskXmlEqualFlags;

gboolean     gsk_xml_equal_with_flags  (const GskXml    *a,
                                        const GskXml    *b,
                                        GskXmlEqualFlags flags);

#endif
