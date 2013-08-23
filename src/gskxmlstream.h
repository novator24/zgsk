/* Insert header here. */
#ifndef __GSK_XML_STREAM_H_
#define __GSK_XML_STREAM_H_

#include "gskstream.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskXmlStream GskXmlStream;
typedef struct _GskXmlStreamClass GskXmlStreamClass;
/* --- type macros --- */
GType gsk_xml_stream_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_XML_STREAM			(gsk_xml_stream_get_type ())
#define GSK_XML_STREAM(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_XML_STREAM, GskXmlStream))
#define GSK_XML_STREAM_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_XML_STREAM, GskXmlStreamClass))
#define GSK_XML_STREAM_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_XML_STREAM, GskXmlStreamClass))
#define GSK_IS_XML_STREAM(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_XML_STREAM))
#define GSK_IS_XML_STREAM_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_XML_STREAM))

/* --- structures --- */
struct _GskXmlStreamClass 
{
  GskStreamClass stream_class;
};
struct _GskXmlStream 
{
  GskStream      stream;

};
/* --- prototypes --- */
G_END_DECLS

#endif
