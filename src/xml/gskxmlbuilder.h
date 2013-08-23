/* GskXmlBuilder: incremental xml structure building,
   useful for implementing parser-callbacks */

#ifndef GSK_XML_NODE_H_IN
#error "include this file by including gskxml.h"
#endif

G_BEGIN_DECLS

typedef struct _GskXmlBuilder GskXmlBuilder;
GskXmlBuilder *gsk_xml_builder_new      (GskXmlParseFlags flags);
void           gsk_xml_builder_free     (GskXmlBuilder *builder);

void           gsk_xml_builder_start    (GskXmlBuilder *builder,
                                         const char    *name,
                                         guint          n_attrs,
                                         const char   **attrs);
void           gsk_xml_builder_start_ns (GskXmlBuilder *builder,
                                         GskXmlNamespace*ns,
                                         const char    *ns_abbrev,
                                         const char    *name,
                                         guint          n_attrs,
                                         GskXmlAttribute *attrs);
void           gsk_xml_builder_text     (GskXmlBuilder *builder,
                                         const char    *content);
void           gsk_xml_builder_text_len (GskXmlBuilder *builder,
                                         const char    *content,
                                         gsize          len);
void           gsk_xml_builder_add_node (GskXmlBuilder *builder,
                                         GskXmlNode    *node);

/* NOTE: name is optional
   NOTE: return-value is not a new reference, but a peeked version
   of an internal structure.  you should 'ref' it to hold onto it.
 */
GskXmlNode    *gsk_xml_builder_end   (GskXmlBuilder *builder,
                                      const char    *name);


/* will return NULL until there is a document to return;
   you must unref a non-NULL return value (ie you take ownership) */
GskXmlNode    *gsk_xml_builder_get_doc(GskXmlBuilder *builder);

G_END_DECLS
