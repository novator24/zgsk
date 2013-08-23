#include <ctype.h>
#include "gskstream.h"
#include "gskstreamconnection.h"
#include "gskerror.h"
#include "gskmacros.h"
#include "gskutils.h"	/* for gsk_escape_memory() */
#include "debug.h"

/* parent classes */
static GObjectClass *parent_class = NULL;

/* ugh... */
#define DEBUG_PRINT_HEADER_FLAGS(flags, fctname)			\
	_GSK_DEBUG_PRINTF((flags),					\
			  ("running %s on %s[%p]",			\
		           fctname,					\
			   g_type_name(G_OBJECT_TYPE(stream)),		\
			   stream))
#define DEBUG_PRINT_HEADER(fctname) DEBUG_PRINT_HEADER_FLAGS(GSK_DEBUG_STREAM, fctname)


/* ========== GskStream =========== */

/* --- i/o methods --- */

/**
 * gsk_stream_read:
 * @stream: the stream to attempt to read data from.
 * @buffer: the buffer to fill with data.
 * @buffer_length: the maximum data to copy into the buffer.
 * @error: optional place to store a GError if something goes wrong.
 *
 * Read up to @buffer_length bytes into @buffer,
 * returning the amount read.  It may return 0;
 * this does not indicate an end-of-file.
 *
 * returns: the number of bytes read into @buffer.
 */
gsize
gsk_stream_read             (GskStream        *stream,
			     gpointer          buffer,
			     gsize             buffer_length,
			     GError          **error)
{
  GskStreamClass *class = GSK_STREAM_GET_CLASS (stream);
  gsize rv;
  DEBUG_PRINT_HEADER ("gsk_stream_read");
  if (error != NULL && *error != NULL)
    return 0;
  if (gsk_io_get_is_connecting (stream))
    return 0;
  g_object_ref (stream);
  rv = (*class->raw_read) (stream, buffer, buffer_length, error);
  if (stream->never_partial_reads)
    g_return_val_if_fail (rv == buffer_length, rv);
  if (GSK_IS_DEBUGGING (STREAM_DATA))
    {
      char *text = gsk_escape_memory (buffer, rv);
      g_printerr ("%s::read() = \"%s\"\n", G_OBJECT_TYPE_NAME (stream), text);
      g_free (text);
    }
  g_object_unref (stream);
  return rv;
}

/**
 * gsk_stream_write:
 * @stream: the stream to attempt to write data to.
 * @buffer: the buffer to write from.
 * @buffer_length: the number of bytes in buffer.
 * @error: optional place to store a GError if something goes wrong.
 *
 * Write up to @buffer_length bytes into @buffer,
 * returning the amount written.  It may return 0;
 * this does not indicate an error by itself.
 *
 * returns: the number of bytes written.
 */
gsize
gsk_stream_write            (GskStream        *stream,
			     gconstpointer     buffer,
			     gsize             buffer_length,
			     GError          **error)
{
  guint written = 0;
  GskStreamClass *class = GSK_STREAM_GET_CLASS (stream);
  _GSK_DEBUG_PRINTF(GSK_DEBUG_STREAM,
		    ("running %s on %s[%p].  buffer_length=%u.",
		     "gsk_stream_write",
		     g_type_name(G_OBJECT_TYPE(stream)),
		     stream, (guint)buffer_length));
  if (error != NULL && *error != NULL)
    return 0;
  if (buffer_length == 0)
    return 0;
  if (gsk_io_get_is_connecting (stream))
    {
      _GSK_DEBUG_PRINTF(GSK_DEBUG_STREAM,
			("gsk_stream_write: returning 0 because stream is still connecting"));
      return 0;
    }
  g_object_ref (stream);
  g_return_val_if_fail (class->raw_write != NULL, 0);
  written = (*class->raw_write) (stream, buffer, buffer_length, error);
  if (GSK_IS_DEBUGGING (STREAM_DATA))
    {
      char *text = gsk_escape_memory (buffer, written);
      g_printerr ("%s::write() = \"%s\"\n", G_OBJECT_TYPE_NAME (stream), text);
      g_free (text);
    }
  g_assert (written <= buffer_length);
  if (stream->never_partial_writes)
    g_return_val_if_fail (written == buffer_length, buffer_length);
  g_object_unref (stream);
  return written;
}

/* read into buffer */
/**
 * gsk_stream_read_buffer:
 * @stream: the stream to attempt to read data from.
 * @buffer: the buffer to copy data into.
 * @error: optional place to store a GError if something goes wrong.
 *
 * Read data from the stream directly into a #GskBuffer.
 *
 * returns: the number of bytes read into @buffer.
 */
gsize
gsk_stream_read_buffer       (GskStream        *stream,
			      GskBuffer        *buffer,
			      GError          **error)
{
  GskStreamClass *class = GSK_STREAM_GET_CLASS (stream);
  gboolean debug_data = GSK_IS_DEBUGGING (STREAM_DATA);
  if (gsk_io_get_is_connecting (stream))
    return 0;
  if (!debug_data && class->raw_read_buffer)
    return (*class->raw_read_buffer) (stream, buffer, error);
  else
    {
      char tmp_area[8192];
      guint size = 8192;
      gsize read;
      /*       tmp_area = g_alloca (size); */
      g_object_ref (stream);
      read = gsk_stream_read (stream, tmp_area, size, error);
      if (read > 0)
	gsk_buffer_append (buffer, tmp_area, read);
      if (debug_data)
	{
	  char *str = gsk_escape_memory (tmp_area, read);
	  g_printerr ("gsk_stream_read_buffer: \"%s\"\n", str);
	  g_free (str);
	}
      g_object_unref (stream);
      return read;
    }
}

/* write from buffer */
/**
 * gsk_stream_write_buffer:
 * @stream: the stream to attempt to write data to.
 * @buffer: the buffer to get data from.
 * @error: optional place to store a GError if something goes wrong.
 *
 * Write data to the stream directly from a #GskBuffer.
 * Data written from the buffer is removed from it.
 *
 * returns: the number of bytes written from @buffer.
 */
gsize
gsk_stream_write_buffer      (GskStream        *stream,
			      GskBuffer        *buffer,
			      GError          **error)
{
  GskStreamClass *class = GSK_STREAM_GET_CLASS (stream);
  gboolean debug_data = GSK_IS_DEBUGGING (STREAM_DATA);
  if (gsk_io_get_is_connecting (stream))
    return 0;
  if (!debug_data && class->raw_write_buffer)
    return (*class->raw_write_buffer) (stream, buffer, error);
  else
    {
      char *tmp_area;
      guint size = 8192;
      guint read;
      tmp_area = g_alloca (size);
      g_object_ref (stream);
      read = gsk_buffer_peek (buffer, tmp_area, size);
      if (read > 0)
	{
	  gsize written = gsk_stream_write (stream, tmp_area, read, error);
	  if (written > 0)
	    gsk_buffer_discard (buffer, written);
	  if (debug_data)
	    {
	      char *text = gsk_escape_memory (tmp_area, written);
	      g_printerr ("gsk_stream_write_buffer[%s]: \"%s\"\n",
			 G_OBJECT_TYPE_NAME (stream), text);
	      g_free (text);
	    }
	  g_object_unref (stream);
	  return written;
	}
      else
	{
	  if (debug_data)
	    g_printerr ("gsk_stream_write_buffer[%s]: source buffer was empty\n",
		       G_OBJECT_TYPE_NAME (stream));
	}
      g_object_unref (stream);
      return read;
    }
}

/* --- functions --- */
static void
gsk_stream_init (GskStream *stream)
{
}

static void
gsk_stream_class_init (GskStreamClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  parent_class = g_type_class_peek_parent (object_class);
}


GType gsk_stream_get_type()
{
  static GType stream_type = 0;
  if (!stream_type)
    {
      static const GTypeInfo stream_info =
      {
	sizeof(GskStreamClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_stream_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskStream),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_stream_init,
	NULL		/* value_table */
      };
      stream_type = g_type_register_static (GSK_TYPE_IO,
					    "GskStream",
					    &stream_info, 
					    G_TYPE_FLAG_ABSTRACT);
    }
  return stream_type;
}

/**
 * gsk_stream_attach:
 * @input_stream: the input stream whose read-end will be trapped.
 * @output_stream: the output stream whose write-end will be trapped.
 * @error: optional error return location.
 *
 * Attach the read end of @input_stream
 * to the write end of @output_stream,
 * returning an error if anything goes wrong.
 *
 * returns: whether the connection was successful.
 */
gboolean
gsk_stream_attach           (GskStream        *input_stream,
			     GskStream        *output_stream,
			     GError          **error)
{
  GskStreamConnection *connection = gsk_stream_connection_new (input_stream, output_stream, error);
  if (connection != NULL)
    {
      g_object_unref (connection);
      return TRUE;
    }
  return FALSE;
}


/**
 * gsk_stream_attach_pair:
 * @stream_a: one of the two streams to attach together.
 * @stream_b: one of the two streams to attach together.
 * @error: optional error return location.
 *
 * Attach a's input to b's output and vice versa.
 *
 * returns: whether the connection was successful.
 */
gboolean gsk_stream_attach_pair       (GskStream        *stream_a,
                                       GskStream        *stream_b,
				       GError          **error)
{
  GskStreamConnection *ab = gsk_stream_connection_new (stream_a, stream_b, error);
  GskStreamConnection *ba;
  if (ab == NULL)
    return FALSE;
  ba = gsk_stream_connection_new (stream_b, stream_a, error);
  if (ba == NULL)
    {
      gsk_stream_connection_detach (ab);
      g_object_unref (ab);
      return FALSE;
    }
  g_object_unref (ab);
  g_object_unref (ba);
  return TRUE;
}
