#include "gsksimplefilter.h"
#include "gskmacros.h"

static GObjectClass *parent_class = NULL;

#define DEFAULT_MAX_READ_BUFFER_SIZE		8192
#define DEFAULT_MAX_WRITE_BUFFER_SIZE		8192

static inline void
update_idle_notification (GskSimpleFilter *filter)
{
  if (!gsk_io_get_is_writable (filter) && filter->read_buffer.size == 0)
    gsk_io_notify_read_shutdown (filter);
  else
    {
      gsk_io_set_idle_notify_read (filter, filter->read_buffer.size > 0);
      gsk_io_set_idle_notify_write (filter,
				    filter->write_buffer.size < filter->max_write_buffer_size
				    && filter->read_buffer.size < filter->max_read_buffer_size);
    }
}

static gboolean
process_data (GskSimpleFilter *filter,
	      GError         **error)
{
  GskSimpleFilterClass *class = GSK_SIMPLE_FILTER_GET_CLASS (filter);
  g_return_val_if_fail (class->process != NULL, FALSE);
  if (!(*class->process) (filter, &filter->read_buffer, &filter->write_buffer, error))
    return FALSE;
  return TRUE;
}

/* --- GskStream methods --- */
static guint
gsk_simple_filter_raw_read (GskStream     *stream,
                            gpointer       data,
                            guint          length,
                            GError       **error)
{
  GskSimpleFilter *simple_filter = GSK_SIMPLE_FILTER (stream);
  guint rv = gsk_buffer_read (&simple_filter->read_buffer, data, length);

  update_idle_notification (simple_filter);

  return rv;
}

static guint
gsk_simple_filter_raw_write (GskStream     *stream,
                             gconstpointer  data,
                             guint          length,
                             GError       **error)
{
  GskSimpleFilter *simple_filter = GSK_SIMPLE_FILTER (stream);
  gsk_buffer_append (&simple_filter->write_buffer, data, length);
  if (!process_data (simple_filter, error))
    return length;
  update_idle_notification (simple_filter);
  return length;
}

static guint
gsk_simple_filter_raw_read_buffer (GskStream     *stream,
                                   GskBuffer     *buffer,
                                   GError       **error)
{
  GskSimpleFilter *simple_filter = GSK_SIMPLE_FILTER (stream);
  guint rv = gsk_buffer_drain (buffer, &simple_filter->read_buffer);
  update_idle_notification (simple_filter);
  return rv;
}

static guint
gsk_simple_filter_raw_write_buffer (GskStream    *stream,
                                    GskBuffer     *buffer,
                                    GError       **error)
{
  GskSimpleFilter *simple_filter = GSK_SIMPLE_FILTER (stream);
  guint rv = gsk_buffer_drain (&simple_filter->write_buffer, buffer);
  if (!process_data (simple_filter, error))
    return rv;
  update_idle_notification (simple_filter);
  return rv;
}

/* --- GskIO methods --- */
#if 0
static void
gsk_simple_filter_set_poll_read (GskIO      *io,
                                 gboolean    do_poll)
{
  ...
}

static void
gsk_simple_filter_set_poll_write (GskIO      *io,
                                  gboolean    do_poll)
{
  ...
}
#endif

static gboolean
gsk_simple_filter_shutdown_read (GskIO      *io,
                                 GError    **error)
{
  GskSimpleFilter *simple_filter = GSK_SIMPLE_FILTER (io);
  if (simple_filter->write_buffer.size > 0)
    {
      gsk_io_set_error (io, GSK_IO_ERROR_READ,
			GSK_ERROR_LINGERING_DATA,
			_("shutdown_read lost %u bytes"),
			simple_filter->write_buffer.size);
    }
  gsk_io_notify_write_shutdown (io);
  return (simple_filter->write_buffer.size == 0
       && simple_filter->read_buffer.size == 0);
}

static gboolean
gsk_simple_filter_shutdown_write (GskIO      *io,
                                  GError    **error)
{
  GskSimpleFilter *filter = GSK_SIMPLE_FILTER (io);
  GskSimpleFilterClass *class = GSK_SIMPLE_FILTER_GET_CLASS (io);
  gboolean rv = TRUE;
  if (filter->write_buffer.size > 0)
    if (!(*class->process) (filter, &filter->read_buffer, &filter->write_buffer, error))
      rv = FALSE;
  if (rv && class->flush != NULL)
    if (!(*class->flush) (filter,
			  &filter->read_buffer,
			  &filter->write_buffer,
			  error))
      rv = FALSE;
  update_idle_notification (filter);
  if (filter->read_buffer.size == 0)
    gsk_io_notify_read_shutdown (filter);
  return rv;
}

/* --- GObject methods --- */
static void
gsk_simple_filter_finalize (GObject        *object)
{
  GskSimpleFilter *simple_filter = GSK_SIMPLE_FILTER (object);
  gsk_buffer_destruct (&simple_filter->read_buffer);
  gsk_buffer_destruct (&simple_filter->write_buffer);
  (*parent_class->finalize) (object);
}

/* --- functions --- */
static void
gsk_simple_filter_init (GskSimpleFilter *simple_filter)
{
  GskStream *stream = GSK_STREAM (simple_filter);
  simple_filter->max_read_buffer_size = DEFAULT_MAX_READ_BUFFER_SIZE;
  simple_filter->max_write_buffer_size = DEFAULT_MAX_WRITE_BUFFER_SIZE;
  gsk_stream_mark_is_readable (stream);
  gsk_stream_mark_is_writable (stream);
  gsk_stream_mark_idle_notify_write (stream);
}
static void
gsk_simple_filter_class_init (GskSimpleFilterClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GskIOClass *io_class = GSK_IO_CLASS (class);
  GskStreamClass *stream_class = GSK_STREAM_CLASS (class);
  parent_class = g_type_class_peek_parent (class);
  object_class->finalize = gsk_simple_filter_finalize;
#if 0
  io_class->set_poll_read = gsk_simple_filter_set_poll_read;
  io_class->set_poll_write = gsk_simple_filter_set_poll_write;
#endif
  io_class->shutdown_read = gsk_simple_filter_shutdown_read;
  io_class->shutdown_write = gsk_simple_filter_shutdown_write;
  stream_class->raw_read = gsk_simple_filter_raw_read;
  stream_class->raw_write = gsk_simple_filter_raw_write;
  stream_class->raw_read_buffer = gsk_simple_filter_raw_read_buffer;
  stream_class->raw_write_buffer = gsk_simple_filter_raw_write_buffer;
}

GType gsk_simple_filter_get_type()
{
  static GType simple_filter_type = 0;
  if (!simple_filter_type)
    {
      static const GTypeInfo simple_filter_info =
      {
	sizeof(GskSimpleFilterClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_simple_filter_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskSimpleFilter),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_simple_filter_init,
	NULL		/* value_table */
      };
      simple_filter_type = g_type_register_static (GSK_TYPE_STREAM,
                                                  "GskSimpleFilter",
						  &simple_filter_info, 0);
    }
  return simple_filter_type;
}
