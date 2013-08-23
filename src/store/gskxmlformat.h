#ifndef __GSK_XML_FORMAT_H_
#define __GSK_XML_FORMAT_H_

/*
 * GskXmlFormat -- implements GskStorageFormat, uses GskXmlValueReader
 * and GskXmlValueWriter to deserialize/serialize values from/to XML.
 */

#include "gskstorageformat.h"
#include "gskgtypeloader.h"

G_BEGIN_DECLS

typedef GObjectClass         GskXmlFormatClass;
typedef struct _GskXmlFormat GskXmlFormat;

GType gsk_xml_format_get_type (void) G_GNUC_CONST;

#define GSK_TYPE_XML_FORMAT (gsk_xml_format_get_type ())
#define GSK_XML_FORMAT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_XML_FORMAT, GskXmlFormat))
#define GSK_IS_XML_FORMAT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_XML_FORMAT))

struct _GskXmlFormat
{
  GObject object;

  GskGtypeLoader *type_loader;
};

/* Flag for properties that should be ignored by the serialize method.
 */
#define GSK_XML_FORMAT_PARAM_IGNORE (1 << G_PARAM_USER_SHIFT)

G_END_DECLS

#endif
