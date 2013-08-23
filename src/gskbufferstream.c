#include "gskbufferstream.h"

static GObjectClass *parent_class = NULL;


enum
{
  /* WARNING: this flag is hard-coded
     in the macros gsk_buffer_stream_*_strict_max_write(). */
  STRICT_MAX_WRITE = (1<<0),

  /* Whether to shutdown the hook when the buffer empties. */
  DEFERRED_WRITE_SHUTDOWN = (1<<1)
};

enum
{
  /* Whether to shutdown the hook when the buffer empties. */
  DEFERRED_READ_SHUTDOWN = (1<<0)
};
void gsk_buffer_stream_mark_deferred_write_shutdown (GskBufferStream *stream);

#define gsk_buffer_stream_has_deferred_write_shutdown(stream)	\
  GSK_HOOK_TEST_USER_FLAG (gsk_buffer_stream_write_hook(stream), DEFERRED_WRITE_SHUTDOWN)
#define gsk_buffer_stream_mark_deferred_write_shutdown(stream)	\
  GSK_HOOK_MARK_USER_FLAG (gsk_buffer_stream_write_hook(stream), DEFERRED_WRITE_SHUTDOWN)
#define gsk_buffer_stream_clear_deferred_write_shutdown(stream)	\
  GSK_HOOK_CLEAR_USER_FLAG (gsk_buffer_stream_write_hook(stream), DEFERRED_WRITE_SHUTDOWN)

#define gsk_buffer_stream_has_deferred_read_shutdown(stream)	\
  GSK_HOOK_TEST_USER_FLAG (gsk_buffer_stream_read_hook(stream), DEFERRED_READ_SHUTDOWN)
#define gsk_buffer_stream_mark_deferred_read_shutdown(stream)	\
  GSK_HOOK_MARK_USER_FLAG (gsk_buffer_stream_read_hook(stream), DEFERRED_READ_SHUTDOWN)
#define gsk_buffer_stream_clear_deferred_read_shutdown(stream)	\
  GSK_HOOK_CLEAR_USER_FLAG (gsk_buffer_stream_read_hook(stream), DEFERRED_READ_SHUTDOWN)

/**
 * gsk_buffer_stream_read_shutdown:
 * @stream: the stream to gracefully shut-down.
 *
 * Shutdown the read-end of the buffer-stream,
 * waiting for the buffer to be drained first.
 */
void gsk_buffer_stream_read_shutdown (GskBufferStream *stream)
{
  if (stream->read_buffer.size == 0)
    gsk_io_notify_read_shutdown (GSK_IO (stream));
  else
    gsk_buffer_stream_mark_deferred_read_shutdown (stream);
}

/* --- GskStream methods --- */
static guint
gsk_buffer_stream_raw_read (GskStream     *stream,
                            gpointer       data,
                            guint          length,
                            GError       **error)
{
  GskBufferStream *bs = GSK_BUFFER_STREAM (stream);
  GskBuffer *buffer = &bs->read_buffer;
  guint rv = gsk_buffer_read (buffer, data, length);
  if (rv > 0)
    gsk_buffer_stream_read_buffer_changed (bs);
  return rv;
}

static guint
gsk_buffer_stream_raw_write (GskStream     *stream,
                             gconstpointer  data,
                             guint          length,
                             GError       **error)
{
  GskBufferStream *bs = GSK_BUFFER_STREAM (stream);
  GskBuffer *buffer = &bs->write_buffer;
  if (gsk_buffer_stream_has_strict_max_write (bs))
    {
      if (buffer->size >= bs->max_write_buffer)
	return 0;
      if (buffer->size + length > bs->max_write_buffer)
	length = bs->max_write_buffer - buffer->size;
    }
  gsk_buffer_append (buffer, data, length);
  if (length > 0)
    gsk_buffer_stream_write_buffer_changed (bs);
  return length;
}

static guint
gsk_buffer_stream_raw_read_buffer (GskStream     *stream,
                                   GskBuffer     *buffer,
                                   GError       **error)
{
  GskBufferStream *bs = GSK_BUFFER_STREAM (stream);
  GskBuffer *read_buffer = &bs->read_buffer;
  guint rv = gsk_buffer_drain (buffer, read_buffer);
  if (rv > 0)
    gsk_buffer_stream_read_buffer_changed (bs);
  return rv;
}

static guint
gsk_buffer_stream_raw_write_buffer (GskStream    *stream,
                                    GskBuffer     *buffer,
                                    GError       **error)
{
  GskBufferStream *bs = GSK_BUFFER_STREAM (stream);
  GskBuffer *write_buffer = &bs->write_buffer;
  guint length = buffer->size;
  guint rv;
  if (gsk_buffer_stream_has_strict_max_write (bs))
    {
      if (buffer->size >= bs->max_write_buffer)
	return 0;
      if (buffer->size + length > bs->max_write_buffer)
	length = bs->max_write_buffer - buffer->size;
      rv = gsk_buffer_transfer (write_buffer, buffer, length);
    }
  else
    {
      rv = gsk_buffer_drain (write_buffer, buffer);
    }
  if (rv > 0)
    gsk_buffer_stream_write_buffer_changed (bs);
  return rv;
}

/* --- GskIO methods --- */
/**
 * gsk_buffer_stream_read_buffer_changed:
 * @stream: stream whose read buffer has been modified.
 *
 * Called to notify the buffer stream that its
 * read-size has been changed, usually because
 * an implementor has appended data into it
 * for the attached stream to read.
 */
void gsk_buffer_stream_read_buffer_changed  (GskBufferStream *stream)
{
  if (stream->read_buffer.size == 0)
    {
      if (gsk_buffer_stream_has_deferred_read_shutdown (stream))
	gsk_io_notify_read_shutdown (stream);
      else
	gsk_io_clear_idle_notify_read (stream);
      gsk_hook_set_idle_notify (gsk_buffer_stream_read_hook (stream),
                                gsk_io_is_polling_for_read (stream));
    }
  else if (gsk_io_get_is_readable (stream))
    {
      gsk_io_mark_idle_notify_read (stream);
    }
}

/**
 * gsk_buffer_stream_write_buffer_changed:
 * @stream: stream whose write buffer has been modified.
 *
 * Called to notify the buffer stream that its
 * write-buffer has been changed, usually because
 * an implementor has read data from it.
 */
void
gsk_buffer_stream_write_buffer_changed (GskBufferStream *stream)
{
  if (stream->write_buffer.size < stream->max_write_buffer)
    gsk_io_mark_idle_notify_write (stream);
  else
    gsk_io_clear_idle_notify_write (stream);

  if (stream->write_buffer.size > 0)
    gsk_hook_mark_idle_notify (gsk_buffer_stream_write_hook (stream));
  else
    {
      gsk_hook_clear_idle_notify (gsk_buffer_stream_write_hook (stream));
      if (gsk_buffer_stream_has_deferred_write_shutdown (stream))
	{
	  gsk_buffer_stream_clear_deferred_write_shutdown (stream);
	  gsk_hook_notify_shutdown (gsk_buffer_stream_write_hook (stream));
	}
    }
}

/**
 * gsk_buffer_stream_changed:
 * @stream: the stream whose internals have been modified.
 *
 * Do all updates needed to compensate for
 * any user changes to: read_buffer, write_buffer,
 * max_write_buffer.
 */
void
gsk_buffer_stream_changed              (GskBufferStream *stream)
{
  gsk_buffer_stream_read_buffer_changed (stream);
  gsk_buffer_stream_write_buffer_changed (stream);
}

static void
gsk_buffer_stream_set_poll_read (GskIO      *io,
                                 gboolean    do_poll)
{
  GskBufferStream *bs = GSK_BUFFER_STREAM (io);
  if (bs->read_buffer.size == 0)
    {
      gsk_hook_set_idle_notify (gsk_buffer_stream_read_hook (bs), do_poll);
    }
  else if (gsk_io_get_is_readable (bs))
    {
      g_return_if_fail (gsk_io_get_idle_notify_read (bs));
    }
}

static void
gsk_buffer_stream_set_poll_write (GskIO      *io,
                                  gboolean    do_poll)
{
  /* Nothing to do.

     All the work is done by idle-notify hooks
     in gsk_buffer_stream_write_buffer_changed() */
}

static gboolean
gsk_buffer_stream_shutdown_read (GskIO      *io,
                                 GError    **error)
{
  gsk_hook_notify_shutdown (gsk_buffer_stream_read_hook (GSK_BUFFER_STREAM (io)));
  return TRUE;
}

static gboolean
gsk_buffer_stream_shutdown_write (GskIO      *io,
                                  GError    **error)
{
  GskBufferStream *bs = GSK_BUFFER_STREAM (io);
  if (bs->write_buffer.size == 0)
    gsk_hook_notify_shutdown (gsk_buffer_stream_write_hook (bs));
  else
    {
      gsk_buffer_stream_mark_deferred_write_shutdown (bs);

      /* postpone shutdown until it happens for real. */
      return FALSE;
    }

  return TRUE;
}

/* --- GObject methods --- */
static void
gsk_buffer_stream_finalize (GObject        *object)
{
  GskBufferStream *bs = GSK_BUFFER_STREAM (object);
  gsk_buffer_destruct (&bs->read_buffer);
  gsk_buffer_destruct (&bs->write_buffer);
  gsk_hook_destruct (&bs->buffered_read_hook);
  gsk_hook_destruct (&bs->buffered_write_hook);
  (*parent_class->finalize) (object);
}

/* --- functions --- */
static void
gsk_buffer_stream_init (GskBufferStream *buffer_stream)
{
  GSK_HOOK_INIT (buffer_stream,
		 GskBufferStream,
		 buffered_read_hook,
		 GSK_HOOK_IS_AVAILABLE,
		 buffered_read_set_poll, buffered_read_shutdown);
  GSK_HOOK_INIT (buffer_stream,
		 GskBufferStream,
		 buffered_write_hook,
		 GSK_HOOK_IS_AVAILABLE,
		 buffered_write_set_poll, buffered_write_shutdown);

  buffer_stream->max_write_buffer = 4096;
  gsk_stream_mark_is_writable (buffer_stream);
  gsk_stream_mark_is_readable (buffer_stream);
  gsk_buffer_stream_changed (buffer_stream);
}
static void
gsk_buffer_stream_class_init (GskBufferStreamClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GskIOClass *io_class = GSK_IO_CLASS (class);
  GskStreamClass *stream_class = GSK_STREAM_CLASS (class);
  parent_class = g_type_class_peek_parent (class);
  object_class->finalize = gsk_buffer_stream_finalize;
  io_class->set_poll_read = gsk_buffer_stream_set_poll_read;
  io_class->set_poll_write = gsk_buffer_stream_set_poll_write;
  io_class->shutdown_read = gsk_buffer_stream_shutdown_read;
  io_class->shutdown_write = gsk_buffer_stream_shutdown_write;
  stream_class->raw_read = gsk_buffer_stream_raw_read;
  stream_class->raw_write = gsk_buffer_stream_raw_write;
  stream_class->raw_read_buffer = gsk_buffer_stream_raw_read_buffer;
  stream_class->raw_write_buffer = gsk_buffer_stream_raw_write_buffer;
  GSK_HOOK_CLASS_INIT (object_class, "buffered-read-hook", GskBufferStream, buffered_read_hook);
  GSK_HOOK_CLASS_INIT (object_class, "buffered-write-hook", GskBufferStream, buffered_write_hook);
}

GType gsk_buffer_stream_get_type()
{
  static GType buffer_stream_type = 0;
  if (!buffer_stream_type)
    {
      static const GTypeInfo buffer_stream_info =
      {
	sizeof(GskBufferStreamClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_buffer_stream_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskBufferStream),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_buffer_stream_init,
	NULL		/* value_table */
      };
      buffer_stream_type = g_type_register_static (GSK_TYPE_STREAM,
                                                  "GskBufferStream",
						  &buffer_stream_info, 0);
    }
  return buffer_stream_type;
}

/**
 * gsk_buffer_stream_new:
 *
 * Create a new #GskBufferStream.
 *
 * returns: the newly allocated GskBufferStream.
 */
GskBufferStream *
gsk_buffer_stream_new (void)
{
  return g_object_new (GSK_TYPE_BUFFER_STREAM, NULL);
}
