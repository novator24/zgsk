#ifndef __GSK_XML_CONTEXT_H_
#define __GSK_XML_CONTEXT_H_

typedef struct _GskXmlContext GskXmlContext;

/* parse an XML node into a typed GValue */
typedef gboolean (*GskXmlContextParserFunc)(GskXmlContext *context,
                                            GskXmlNode    *node,
                                            GValue        *value_out,
					    gpointer       data,
					    GError       **error);

/* convert a GValue into an XML node */
typedef GskXmlNode *(*GskXmlContextToXmlFunc)(GskXmlContext *context,
                                              const GValue  *value,
					      gpointer       data,
					      GError       **error);

/* ensure that an object is in a valid state */
typedef gboolean    (*GskXmlValidateFunc)    (GskXmlContext *context,
                                              GObject       *object,
					      gpointer       data,
					      GError       **error);

/* handle a miscellaneous xml node for this object */
typedef gboolean    (*GskXmlObjectHandler)   (GskXmlContext *context,
                                              GObject       *object,
                                              GskXmlNode    *node,
                                              gpointer       data,
                                              GError       **error);

/* produce miscellaneous xml nodes for this object */
typedef gboolean    (*GskXmlObjectWriter)    (GskXmlContext *context,
                                              GObject       *object,
                                              GskXmlBuilder *builder,
                                              gpointer       data,
                                              GError       **error);

/* --- public api --- */
GskXmlNode    *gsk_xml_context_serialize_object  (GskXmlContext         *context,
                                                  gpointer               object,
                                                  GError               **error);
GskXmlNode    *gsk_xml_context_serialize_value   (GskXmlContext         *context,
                                                  GValue                *value,
                                                  GError               **error);
gboolean       gsk_xml_context_deserialize_value (GskXmlContext         *context,
                                                  GskXmlNode            *node,
                                                  GValue                *out,
						  GError               **error);
gpointer       gsk_xml_context_deserialize_object(GskXmlContext         *context,
                                                  GType                  base_type,
                                                  GskXmlNode            *node,
						  GError               **error);

/* constructing contexts and assigning nicknames */
GskXmlContext *gsk_xml_context_new               (void);
GskXmlContext *gsk_xml_context_global            (void);
void           gsk_xml_context_register_nickname (GskXmlContext *context,
                                                  GType          base_type,
						  const char    *nickname,
                                                  GType          type);

/* protected: for type implementations only */
void           gsk_xml_context_register_parser   (GskXmlContext         *context,
                                                  GType                  type,
						  GskXmlContextParserFunc func,
						  GskXmlContextToXmlFunc  to_xml,
						  gpointer               data,
						  GDestroyNotify         destroy);

/* protected: for object implementations only */
void           gsk_xml_context_register_validator(GskXmlContext         *context,
                                                  GType                  object_type,
                                                  GskXmlValidateFunc     func,
                                                  gpointer               data,
                                                  GDestroyNotify         destroy);
void           gsk_xml_context_add_misc_parser   (GskXmlContext         *context,
                                                  GType                  object_type,
                                                  GskXmlString          *node_name,     /* may be NULL */
                                                  GskXmlObjectHandler    handler,
                                                  gpointer               data,
                                                  GDestroyNotify         destroy);
void           gsk_xml_context_add_misc_writer   (GskXmlContext         *context,
                                                  GType                  object_type,
                                                  GskXmlObjectWriter     handler,
                                                  gpointer               data,
                                                  GDestroyNotify         destroy);


#endif
