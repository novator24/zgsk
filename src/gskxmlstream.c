/* Insert header here. */
#include "gskxmlstream.h"
static GObjectClass *parent_class = NULL;

/* --- GskStream methods --- */
static guint
gsk_xml_stream_raw_read (GskStream     *stream,
                         gpointer       data,
                         guint          length,
                         GError       **error)
{
  ...
}

static guint
gsk_xml_stream_raw_write (GskStream     *stream,
                          gconstpointer  data,
                          guint          length,
                          GError       **error)
{
  ...
}

static guint
gsk_xml_stream_raw_read_buffer (GskStream     *stream,
                                GskBuffer     *buffer,
                                GError       **error)
{
  ...
}

/* --- functions --- */
static void
gsk_xml_stream_init (GskXmlStream *xml_stream)
{
}
static void
gsk_xml_stream_class_init (GskXmlStreamClass *class)
{
  parent_class = g_type_class_peek_parent (class);
  GskStreamClass *stream_class = GSK_STREAM_CLASS (class);
  stream_class->raw_read = gsk_xml_stream_raw_read;
  stream_class->raw_write = gsk_xml_stream_raw_write;
  stream_class->raw_read_buffer = gsk_xml_stream_raw_read_buffer;
}

GType gsk_xml_stream_get_type()
{
  static GType xml_stream_type = 0;
  if (!xml_stream_type)
    {
      static const GTypeInfo xml_stream_info =
      {
	sizeof(GskXmlStreamClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_xml_stream_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskXmlStream),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_xml_stream_init,
	NULL		/* value_table */
      };
      xml_stream_type = g_type_register_static (GSK_TYPE_STREAM,
                                                  "GskXmlStream",
						  &xml_stream_info, 0);
    }
  return xml_stream_type;
}
