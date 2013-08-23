#ifndef __GSK_XML_VALUE_WRITER_H_
#define __GSK_XML_VALUE_WRITER_H_

/*
 *
 * GskXmlValueWriter -- a read-only stream that serializes a GValue to XML,
 * in a format which can hopefully be read by a GskXmlValueReader.
 *
 */

#include "../gskstream.h"

G_BEGIN_DECLS

typedef GskStreamClass            GskXmlValueWriterClass;
typedef struct _GskXmlValueWriter GskXmlValueWriter;

GType gsk_xml_value_writer_get_type (void) G_GNUC_CONST;

#define GSK_TYPE_XML_VALUE_WRITER (gsk_xml_value_writer_get_type ())
#define GSK_XML_VALUE_WRITER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
			       GSK_TYPE_XML_VALUE_WRITER, \
			       GskXmlValueWriter))
#define GSK_IS_XML_VALUE_WRITER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_XML_VALUE_WRITER))

struct _GskXmlValueWriter 
{
  GskStream stream;

  /* private */
  gpointer stack;
  GskBuffer buf;
};

GskXmlValueWriter * gsk_xml_value_writer_new (const GValue *value);

G_END_DECLS

#endif
