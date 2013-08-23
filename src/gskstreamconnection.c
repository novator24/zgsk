#include "gskstreamconnection.h"
#include "gskerror.h"
#include "gskghelpers.h"
#include "gskmacros.h"

static GObjectClass *parent_class = NULL;

typedef struct _GskStreamConnectionClass GskStreamConnectionClass;
#define GSK_STREAM_CONNECTION_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_STREAM_CONNECTION, GskStreamConnectionClass))
#define GSK_STREAM_CONNECTION_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_STREAM_CONNECTION, GskStreamConnectionClass))
#define GSK_IS_STREAM_CONNECTION_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_STREAM_CONNECTION))

struct _GskStreamConnectionClass 
{
  GObjectClass object_class;
};
/*
 * strategy:
 * - When attaching, trap the relevant read and write hooks.
 * - If a buffer underflow occurs (the number of buffered bytes is 0),
 *   block the readable side of this connection.
 * - If a buffer overflow occurs (the number of buffered bytes greater than max_buffered),
 *   block the writable side of this connection.
 */

#define DEFAULT_MAX_BUFFERED		4096
#define DEFAULT_MAX_ATOMIC_READ		4096
#define MAX_READ_ON_STACK		8192

/* runtime-able? */
#if 0
#define DEBUG(args) g_message args
#else
#define DEBUG(args) 
#endif

#define D(object, fctname) DEBUG(("stream-attach: %s[%p]: %s", G_OBJECT_TYPE_NAME (object), object, fctname))

static inline void
stream_connection_set_internal_write_block (GskStreamConnection *stream_connection,
				      gboolean    block)
{
  if (stream_connection->write_side == NULL)
    return;
  if (block && !stream_connection->blocking_write_side)
    {
      stream_connection->blocking_write_side = 1;
      gsk_io_block_write (GSK_IO (stream_connection->write_side));
    }
  else if (!block && stream_connection->blocking_write_side)
    {
      stream_connection->blocking_write_side = 0;
      gsk_io_unblock_write (GSK_IO (stream_connection->write_side));
    }
}

static inline void
stream_connection_set_internal_read_block (GskStreamConnection *stream_connection,
				      gboolean    block)
{
  if (stream_connection->read_side == NULL)
    return;
  if (block && !stream_connection->blocking_read_side)
    {
      stream_connection->blocking_read_side = 1;
      gsk_io_block_read (GSK_IO (stream_connection->read_side));
    }
  else if (!block && stream_connection->blocking_read_side)
    {
      stream_connection->blocking_read_side = 0;
      gsk_io_unblock_read (GSK_IO (stream_connection->read_side));
    }
}
static inline void
check_internal_blocks (GskStreamConnection *stream_connection)
{
  guint size = stream_connection->buffer.size;
  stream_connection_set_internal_read_block (stream_connection, size > stream_connection->max_buffered);
  stream_connection_set_internal_write_block (stream_connection, size == 0);
}

static void
handle_error (GskStreamConnection *stream_connection,
	      GError     *error)
{
  gsk_stream_connection_shutdown (stream_connection);
  g_warning ("got error: %s", error->message);
  g_error_free (error);
}

/**
 * gsk_stream_connection_set_max_buffered:
 * @connection: the connection to affect.
 * @max_buffered: maximum of data to hold from the input stream
 * for the output stream.  After this much data has built up,
 * we will no longer read from the input stream.
 *
 * Adjust the maximum amount of memory buffer to use between these streams.
 *
 * Sometimes, we will buffer more data, either because set_max_buffer was run
 * to make the amount allowed smaller than the amount currently buffered,
 * or because there was a buffer-to-buffer transfer (which are allowed
 * to be large).
 */
void     gsk_stream_connection_set_max_buffered   (GskStreamConnection *connection,
				                   guint                max_buffered)
{
  connection->max_buffered = max_buffered;
  check_internal_blocks (connection);
}

/**
 * gsk_stream_connection_get_max_buffered:
 * @connection: the connection to query.
 *
 * Get the maximum number of bytes of data to buffer between the input and
 * output ends of the connection.
 *
 * The actual number of bytes of data can be found with gsk_stream_connection_get_cur_buffered().
 *
 * returns: the maximum number of bytes.
 */
guint    gsk_stream_connection_get_max_buffered   (GskStreamConnection *connection)
{
  return connection->max_buffered;
}

/**
 * gsk_stream_connection_get_cur_buffered:
 * @connection: the connection to query.
 *
 * Get the number of bytes of data currently buffered; 
 * that is, bytes we have read from the input and not yet written to the
 * output.
 *
 * returns: the current number of bytes.
 */
guint    gsk_stream_connection_get_cur_buffered   (GskStreamConnection *connection)
{
  return connection->buffer.size;
}


/**
 * gsk_stream_connection_set_atomic_read_size:
 * @connection: the connection to affect.
 * @atomic_read_size: the size to read at a time.
 *
 * Set the number of bytes to read atomically from
 * an underlying source.  This is only
 * used if the input stream has no read_buffer method.
 */
void     gsk_stream_connection_set_atomic_read_size(GskStreamConnection *connection,
				                   guint                atomic_read_size)
{
  connection->atomic_read_size = atomic_read_size;
}

/**
 * gsk_stream_connection_get_atomic_read_size:
 * @connection: the connection to query.
 *
 * Set the number of bytes to read atomically from
 * an underlying source.
 *
 * returns: the size to read at a time.
 */
guint
gsk_stream_connection_get_atomic_read_size(GskStreamConnection *connection)
{
  return connection->atomic_read_size;
}

static gboolean
handle_input_is_readable (GskIO         *io,
			  gpointer       data)
{
  char *buf;
  GskStreamConnection *stream_connection = data;
  GskStream *read_side = stream_connection->read_side;
  GskStream *write_side = stream_connection->write_side;
  GError *error = NULL;
  guint num_read, num_written = 0;
  guint atomic_read_size = stream_connection->atomic_read_size;
  gboolean must_free_buf = FALSE;
  g_return_val_if_fail (read_side == GSK_STREAM (io), FALSE);
  g_return_val_if_fail (write_side != NULL, FALSE);

  D (io, "handle_input_is_readable");

  /* TODO: too harsh a penalty for big atomic reads...
   * maybe we should cache a big one.
   */
  if (stream_connection->use_read_buffer)
    buf = NULL;
  else if (stream_connection->atomic_read_size > MAX_READ_ON_STACK)
    {
      buf = g_malloc (atomic_read_size);
      must_free_buf = TRUE;
    }
  else
    buf = g_alloca (atomic_read_size);

  if (stream_connection->use_read_buffer)
    num_read = gsk_stream_read_buffer (read_side, &stream_connection->buffer, &error);
  else
    num_read = gsk_stream_read (read_side, buf, atomic_read_size, &error);
  if (error != NULL)
    {
      handle_error (stream_connection, error);
      if (must_free_buf)
        g_free (buf);
      return TRUE;
    }
  if (num_read == 0)
    {
      if (must_free_buf)
        g_free (buf);
      return TRUE;
    }
  if (buf != NULL)
    {
      if (stream_connection->buffer.size == 0)
        {
          num_written = gsk_stream_write (write_side, buf, num_read, &error);
          if (error != NULL)
            {
              handle_error (stream_connection, error);
              if (must_free_buf)
                g_free (buf);
              return TRUE;
            }
        }
      if (num_written < num_read)
        gsk_buffer_append (&stream_connection->buffer,
                           buf + num_written,
                           num_read - num_written);
    }
  else if (stream_connection->buffer.size > 1024)
    {
      gsk_stream_write_buffer (write_side,
                               &stream_connection->buffer,
                               &error);
      if (error != NULL)
        {
          handle_error (stream_connection, error);
          return TRUE;
        }
    }

  check_internal_blocks (stream_connection);

  if (must_free_buf)
    g_free (buf);

  return TRUE;
}

static gboolean
handle_input_shutdown_read (GskIO     *io,
			    gpointer   data)
{
  GskStreamConnection *stream_connection = data;
  D (io, "handle_input_shutdown_read");
  if (stream_connection->write_side != NULL)
    {
      GError *error = NULL;
      if (stream_connection->buffer.size == 0)
	{
	  if (!gsk_io_write_shutdown (GSK_IO (stream_connection->write_side), &error)
	      && error != NULL)
	    {
	      const char *msg = error->message;
	      gsk_g_debug ("stream-attach: handle-read-shutdown: doing write-shutdown: %s", msg);
	      if (error)
		g_error_free (error);
	    }
	}
    }
  return FALSE;
}

static void
handle_input_is_readable_destroy (gpointer data)
{
  GskStreamConnection *stream_connection = data;
  GskStream *read_side = stream_connection->read_side;
  D (read_side, "handle_input_is_readable_destroy");
  stream_connection->read_side = NULL;
  g_object_unref (stream_connection);
  if (read_side != NULL)
    g_object_unref (read_side);
}

static gboolean
handle_output_is_writable (GskIO         *io,
			   gpointer       data)
{
  GskStreamConnection *stream_connection = data;
  GskStream *write_side = stream_connection->write_side;
  GskStream *read_side = stream_connection->read_side;
  GError *error = NULL;
  g_return_val_if_fail (write_side == GSK_STREAM (io), FALSE);

  /* The read-side of the connection in fact may be shut-down,
     but we still flush the outgoing data before shutting down
     the write-side.  So, this assertion is wrong.
     Nonetheless, it's so old that it's unclear how it can be wrong,
     so we'll leave it here for further reflection.
     (daveb, Mar 8, 2005) */
  /*g_return_val_if_fail (read_side != NULL, FALSE);*/

  D (write_side, "handle_output_is_writable");

  if (stream_connection->buffer.size > 0)
    {
      gsk_stream_write_buffer (write_side, &stream_connection->buffer, &error);
      if (error)
	{
	  handle_error (stream_connection, error);
	  return TRUE;
	}
    }
  if (stream_connection->buffer.size == 0
   && read_side == NULL)
    {
      if (!gsk_io_write_shutdown (GSK_IO (stream_connection->write_side), &error)
	  && error != NULL)
	{
	  const char *msg = error->message;
	  gsk_g_debug ("stream-attach: handle-output-is-writable, shutting down write end: %s", msg);
	  if (error)
	    g_error_free (error);
	}
    }

  check_internal_blocks (stream_connection);

  return TRUE;
}

static gboolean
handle_output_shutdown_write (GskIO     *io,
			      gpointer   data)
{
  GskStreamConnection *stream_connection = data;
  D (stream_connection->write_side, "handle_output_shutdown_write");
  if (stream_connection->read_side != NULL)
    {
      GError *error = NULL;
      if (!gsk_io_read_shutdown (GSK_IO (stream_connection->read_side), &error)
	  && error != NULL)
        {
	  const char *msg = error->message;
	  g_error ("stream-attach: handle-write-shutdown: doing read-shutdown: %s", msg);
	  if (error)
	    g_error_free (error);
	}
    }
  return FALSE;
}

static void
handle_output_is_writable_destroy (gpointer data)
{
  GskStreamConnection *stream_connection = data;
  GskStream *write_side = stream_connection->write_side;
  D (write_side, "handle_output_is_writable_destroy");
  stream_connection->write_side = NULL;
  if (stream_connection->read_side != NULL)
    gsk_io_untrap_readable (GSK_IO (stream_connection->read_side));

  g_object_unref (stream_connection);

  if (write_side != NULL)
    g_object_unref (write_side);
}

static void
gsk_stream_connection_finalize (GObject *object)
{
  GskStreamConnection *connection = GSK_STREAM_CONNECTION (object);
  gsk_buffer_destruct (&connection->buffer);
  parent_class->finalize (object);
}

static void
gsk_stream_connection_init (GskStreamConnection *stream_connection)
{
  stream_connection->max_buffered = DEFAULT_MAX_BUFFERED;
  stream_connection->atomic_read_size = DEFAULT_MAX_ATOMIC_READ;
  gsk_buffer_construct (&stream_connection->buffer);
}

static void
gsk_stream_connection_class_init (GskStreamConnectionClass *class)
{
  G_OBJECT_CLASS (class)->finalize = gsk_stream_connection_finalize;
  parent_class = g_type_class_peek_parent (class);
}

GType gsk_stream_connection_get_type()
{
  static GType stream_connection_type = 0;
  if (!stream_connection_type)
    {
      static const GTypeInfo stream_connection_info =
      {
	sizeof(GskStreamConnectionClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_stream_connection_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskStreamConnection),
	16,		/* n_preallocs */
	(GInstanceInitFunc) gsk_stream_connection_init,
	NULL		/* value_table */
      };
      stream_connection_type = g_type_register_static (G_TYPE_OBJECT,
                                                  "GskStreamConnection",
						  &stream_connection_info, 0);
    }
  return stream_connection_type;
}

/**
 * gsk_stream_connection_new:
 * @input_stream: the input stream whose read-end will be trapped.
 * @output_stream: the output stream whose write-end will be trapped.
 * @error: optional error return location.
 *
 * Attach the read end of @input_stream
 * to the write end of @output_stream,
 * returning an error if anything goes wrong.
 *
 * returns: a reference at the connection.
 * You should use eventually call g_object_unref() on the connection.
 */
GskStreamConnection *
gsk_stream_connection_new   (GskStream        *input_stream,
			     GskStream        *output_stream,
			     GError          **error)
{
  GskStreamConnection *stream_connection;

  DEBUG (("stream-attach: attach: input=%s[%p], output=%s[%p]",
          G_OBJECT_TYPE_NAME (input_stream), input_stream,
          G_OBJECT_TYPE_NAME (output_stream), output_stream));
  
  g_return_val_if_fail (input_stream != NULL, NULL);
  g_return_val_if_fail (output_stream != NULL, NULL);
  g_return_val_if_fail (gsk_stream_get_is_readable (input_stream), NULL);
  g_return_val_if_fail (gsk_stream_get_is_writable (output_stream), NULL);
  g_return_val_if_fail (!gsk_io_has_read_hook (input_stream), NULL);
  g_return_val_if_fail (!gsk_io_has_write_hook (output_stream), NULL);

  if (error && *error)
    return NULL;


  g_object_ref (input_stream);
  g_object_ref (output_stream);

  stream_connection = g_object_new (GSK_TYPE_STREAM_CONNECTION, NULL);
  g_object_ref (stream_connection);
  stream_connection->read_side = input_stream;
  g_object_ref (stream_connection);
  stream_connection->write_side = output_stream;
  gsk_io_trap_readable (GSK_IO (input_stream),
			handle_input_is_readable,
			handle_input_shutdown_read,
			stream_connection,
			handle_input_is_readable_destroy);
  gsk_io_trap_writable (GSK_IO (output_stream),
			handle_output_is_writable,
			handle_output_shutdown_write,
			stream_connection,
			handle_output_is_writable_destroy);
  if (GSK_STREAM_GET_CLASS (input_stream)->raw_read_buffer != NULL)
    stream_connection->use_read_buffer = 1;

  return stream_connection;
}

/**
 * gsk_stream_connection_detach:
 * @connection: the connection to detach.
 *
 * Disconnects the input/output pair of a connection.
 * Data held in the buffer will be lost.
 */
void
gsk_stream_connection_detach (GskStreamConnection *connection)
{
  g_object_ref (connection);

  if (connection->read_side)
    gsk_stream_untrap_readable (connection->read_side);
  
  if (connection->write_side)
    gsk_stream_untrap_writable (connection->write_side);

  gsk_buffer_destruct (&connection->buffer);

  g_object_unref (connection);
}

/**
 * gsk_stream_connection_shutdown:
 * @connection: the connection to shut down.
 *
 * Shut down both ends of a connection.
 */
void
gsk_stream_connection_shutdown (GskStreamConnection *connection)
{
  GskStream *read_side = connection->read_side;
  GskStream *write_side = connection->write_side;
  if (write_side)
    g_object_ref (write_side);
  if (read_side)
    gsk_io_read_shutdown (read_side, NULL);
  if (write_side)
    {
      gsk_io_write_shutdown (write_side, NULL);
      g_object_unref (write_side);
    }
}
