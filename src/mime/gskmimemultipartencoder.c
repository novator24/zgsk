#include "gskmimemultipartencoder.h"
#include "gskmimeencodings.h"
#include "gskmimeencodings.h"
#include "../gskmacros.h"
#include "../gskmemory.h"

static GObjectClass *parent_class = NULL;

#define DEFAULT_MAX_BUFFERED	4096

static gboolean dequeue_next_piece (GskMimeMultipartEncoder *encoder,
		                    GError                 **error);

static void
check_write_terminator (GskMimeMultipartEncoder *encoder)
{
  if (encoder->shutdown 
   && encoder->outgoing_pieces->head == NULL
   && encoder->active_stream == NULL
   && !encoder->wrote_terminator)
    {
      gsk_buffer_printf (&encoder->outgoing_data, "\r\n--%s--\r\n",
			 encoder->boundary_str);
      encoder->wrote_terminator = TRUE;
      gsk_stream_mark_idle_notify_read (GSK_STREAM (encoder));
    }
}

static gboolean
handle_active_stream_readable (GskStream *stream,
			       gpointer   data)
{
  GskMimeMultipartEncoder *encoder = GSK_MIME_MULTIPART_ENCODER (data);
  GError *suberror = NULL;
  if (!gsk_stream_read_buffer (stream, &encoder->outgoing_data, &suberror))
    {
      GskErrorCode code;
      if (suberror->domain == GSK_G_ERROR_DOMAIN)
	code = suberror->code;
      else
	code = GSK_ERROR_IO;
      gsk_io_set_error (GSK_IO (stream), GSK_IO_ERROR_READ, code,
			_("error from encoding stream: %s"),
			suberror->message);
      return FALSE;
    }

  if (encoder->outgoing_data.size > 0)
    gsk_stream_mark_idle_notify_read (GSK_STREAM (encoder));

  if (encoder->outgoing_data.size > encoder->max_buffered
   && !encoder->blocked_active_stream)
    {
      encoder->blocked_active_stream = 1;
      gsk_io_block_read (stream);
      return FALSE;
    }

  return TRUE;
}

static gboolean
handle_active_stream_read_shutdown (GskStream *stream,
				    gpointer   data)
{
  GskMimeMultipartEncoder *encoder = GSK_MIME_MULTIPART_ENCODER (data);
  /* Don't do anything:  instead do the work in the destroy-notify */
  (void) encoder;
  return FALSE;
}

static void
handle_active_stream_read_destroyed (gpointer data)
{
  GskMimeMultipartEncoder *encoder = GSK_MIME_MULTIPART_ENCODER (data);
  g_object_unref (encoder->active_stream);
  encoder->blocked_active_stream = FALSE;
  encoder->active_stream = NULL;
  if (encoder->outgoing_pieces->head != NULL)
    {
      GError *error = NULL;
      if (!dequeue_next_piece (encoder, &error) && error != NULL)
	{
	  gsk_io_set_gerror (GSK_IO (encoder), GSK_IO_ERROR_READ, error);
	  return;
	}
    }
  else
    check_write_terminator (encoder);
}

static gboolean
dequeue_next_piece (GskMimeMultipartEncoder *encoder,
		    GError                 **error)
{
  GskMimeMultipartPiece *piece;
  GskBuffer *buffer = &encoder->outgoing_data;
  GskStream *raw_stream;
  GskStream *read_end, *write_end;
  g_return_val_if_fail (encoder->active_stream == NULL, FALSE);
  piece = g_queue_pop_head (encoder->outgoing_pieces);
  if (piece == NULL)
    {
      check_write_terminator (encoder);
#if 0
      if (encoder->shutdown && !encoder->wrote_terminator)
	{
	  gsk_buffer_printf (buffer, "\r\n--%s--\r\n", encoder->boundary_str);
	  gsk_stream_mark_idle_notify_read (GSK_STREAM (encoder));
	}
#endif
      return FALSE;
    }

  /* append header */
  gsk_buffer_printf (buffer, "\r\n--%s\r\n", encoder->boundary_str);
  if (piece->type != NULL)
    {
      gsk_buffer_printf (buffer, "Content-Type: %s/%s",
			 piece->type, piece->subtype ? piece->subtype : "*");
      if (piece->charset != NULL)
	gsk_buffer_printf (buffer, "; charset=%s", piece->charset);
      if (piece->other_fields)
	{
	  char **at;
	  for (at = piece->other_fields;
	       at[0] != NULL && at[1] != NULL;
	       at += 2)
	    {
	      gsk_buffer_printf (buffer, "; %s=%s", at[0], at[1]);
	    }
	}
      gsk_buffer_append (buffer, "\r\n", 2);
    }
  if (piece->id != NULL)
    gsk_buffer_printf (buffer, "Content-ID: %s\r\n", piece->id);
  if (piece->description != NULL)
    gsk_buffer_printf (buffer, "Content-Description: %s\r\n",
		       piece->description);
  if (piece->location != NULL)
    gsk_buffer_printf (buffer, "Content-Location: %s\r\n", piece->location);
  if (piece->transfer_encoding != NULL)
    gsk_buffer_printf (buffer, "Content-Transfer-Encoding: %s\r\n",
		       piece->transfer_encoding);
  if (piece->disposition != NULL)
    gsk_buffer_printf (buffer, "Content-Disposition: %s\r\n",
		       piece->disposition);
  gsk_buffer_append (buffer, "\r\n", 2);

  /* create encoded stream */
  if (piece->is_memory)
    raw_stream = gsk_memory_slab_source_new (piece->content_data,
					     piece->content_length,
					     (GDestroyNotify) gsk_mime_multipart_piece_unref,
					     gsk_mime_multipart_piece_ref (piece));
  else
    raw_stream = g_object_ref (piece->content);
  
  if (!gsk_mime_make_transfer_encoding_encoders (piece->transfer_encoding,
					         &write_end, &read_end,
					         encoder->boundary_str,
					         error))
    {
      g_object_unref (raw_stream);
      return FALSE;
    }

  if (!gsk_stream_attach (raw_stream, write_end, error))
    {
      g_object_unref (raw_stream);
      g_object_unref (write_end);
      g_object_unref (read_end);
      return FALSE;
    }
  encoder->active_stream = g_object_ref (read_end);
  gsk_stream_trap_readable (read_end,
			    handle_active_stream_readable,
			    handle_active_stream_read_shutdown,
			    encoder,
			    handle_active_stream_read_destroyed);
  gsk_stream_mark_idle_notify_read (GSK_STREAM (encoder));
  g_object_unref (raw_stream);
  g_object_unref (read_end);
  g_object_unref (write_end);
  gsk_mime_multipart_piece_unref (piece);
  
  return TRUE;
}


static void
check_maybe_unblock (GskMimeMultipartEncoder *encoder)
{
  if (encoder->blocked_active_stream
   && encoder->outgoing_data.size < encoder->max_buffered)
    {
      encoder->blocked_active_stream = 0;
      gsk_io_unblock_read (encoder);
    }
  if (encoder->outgoing_data.size > 0)
    gsk_stream_mark_idle_notify_read (GSK_STREAM (encoder));
}

static void
check_shutdown_notify (GskMimeMultipartEncoder *encoder)
{
  if (encoder->outgoing_pieces->head == NULL
   && encoder->active_stream == NULL
   && encoder->shutdown
   && encoder->outgoing_data.size == 0)
    {
      g_assert (encoder->wrote_terminator);
      gsk_io_notify_read_shutdown (GSK_IO (encoder));
    }
}

static void 
gsk_mime_multipart_encoder_new_part_needed_shutdown (GskMimeMultipartEncoder  *encoder)
{
  encoder->shutdown = 1;
  check_write_terminator (encoder);
  check_maybe_unblock (encoder);
  check_shutdown_notify (encoder);
}

/* --- GskStream methods --- */
static guint
gsk_mime_multipart_encoder_raw_read (GskStream     *stream,
                                     gpointer       data,
                                     guint          length,
                                     GError       **error)
{
  GskMimeMultipartEncoder *encoder = GSK_MIME_MULTIPART_ENCODER (stream);
  guint rv = gsk_buffer_read (&encoder->outgoing_data, data, length);
  check_write_terminator (encoder);
  check_maybe_unblock (encoder);
  check_shutdown_notify (encoder);
  if (encoder->outgoing_data.size == 0)
    gsk_stream_clear_idle_notify_read (GSK_STREAM (encoder));
  return rv;
}

static guint
gsk_mime_multipart_encoder_raw_read_buffer (GskStream     *stream,
                                            GskBuffer     *buffer,
                                            GError       **error)
{
  GskMimeMultipartEncoder *encoder = GSK_MIME_MULTIPART_ENCODER (stream);
  guint rv = gsk_buffer_drain (buffer, &encoder->outgoing_data);
  check_write_terminator (encoder);
  check_maybe_unblock (encoder);
  check_shutdown_notify (encoder);
  if (encoder->outgoing_data.size == 0)
    gsk_stream_clear_idle_notify_read (GSK_STREAM (encoder));
  return rv;
}

static void
gsk_mime_multipart_encoder_finalize (GObject *object)
{
  GskMimeMultipartEncoder *encoder = GSK_MIME_MULTIPART_ENCODER (object);
  if (encoder->active_stream != NULL)
    gsk_stream_untrap_readable (encoder->active_stream);
  g_list_foreach (encoder->outgoing_pieces->head, (GFunc) gsk_mime_multipart_piece_unref, NULL);
  g_queue_free (encoder->outgoing_pieces);
  gsk_hook_destruct (&encoder->new_part_needed);
  g_free (encoder->boundary_str);
  gsk_buffer_destruct (&encoder->outgoing_data);
  (*parent_class->finalize) (object);
}

/* --- functions --- */
static void
gsk_mime_multipart_encoder_init (GskMimeMultipartEncoder *encoder)
{
  GSK_HOOK_INIT (encoder,
		 GskMimeMultipartEncoder,
		 new_part_needed,
		 0,
		 new_part_needed_set_poll, new_part_needed_shutdown);
  GSK_HOOK_SET_FLAG (_GSK_MIME_MULTIPART_ENCODER_HOOK (encoder), IS_AVAILABLE);
  gsk_hook_mark_idle_notify (_GSK_MIME_MULTIPART_ENCODER_HOOK (encoder));
  gsk_io_mark_is_readable (encoder);
  encoder->outgoing_pieces = g_queue_new ();
  encoder->max_buffered = DEFAULT_MAX_BUFFERED;
}

static void
gsk_mime_multipart_encoder_class_init (GskMimeMultipartEncoderClass *class)
{
  GskStreamClass *stream_class = GSK_STREAM_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  parent_class = g_type_class_peek_parent (class);

  GSK_HOOK_CLASS_INIT (object_class, "new-part-needed", GskMimeMultipartEncoder, new_part_needed);
  class->new_part_needed_shutdown = gsk_mime_multipart_encoder_new_part_needed_shutdown;
  stream_class->raw_read = gsk_mime_multipart_encoder_raw_read;
  stream_class->raw_read_buffer = gsk_mime_multipart_encoder_raw_read_buffer;
  object_class->finalize = gsk_mime_multipart_encoder_finalize;
}

GType gsk_mime_multipart_encoder_get_type()
{
  static GType mime_multipart_encoder_type = 0;
  if (!mime_multipart_encoder_type)
    {
      static const GTypeInfo mime_multipart_encoder_info =
      {
	sizeof(GskMimeMultipartEncoderClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_mime_multipart_encoder_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskMimeMultipartEncoder),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_mime_multipart_encoder_init,
	NULL		/* value_table */
      };
      mime_multipart_encoder_type = g_type_register_static (GSK_TYPE_STREAM,
                                                  "GskMimeMultipartEncoder",
						  &mime_multipart_encoder_info, 0);
    }
  return mime_multipart_encoder_type;
}

/**
 * gsk_mime_multipart_encoder_new:
 * @boundary: string used to separate pieces of content
 * on the underlying stream. 
 * This only affects quoted-printable and identity 
 * encoded text-- if your content has
 * a line starting with '--' then the boundary string,
 * it will be confused for a content-part separator.
 * You should use GSK_MIME_MULTIPART_ENCODER_GOOD_BOUNDARY
 * for this string-- then quoted-printable may be used
 * without restriction.
 *
 * Create a read-only stream which encodes MIME pieces given
 * to it.
 *
 * returns: the newly allocated encoder.
 */
GskMimeMultipartEncoder *
gsk_mime_multipart_encoder_new (const char *boundary)
{
  GskMimeMultipartEncoder *rv = g_object_new (GSK_TYPE_MIME_MULTIPART_ENCODER, NULL);
  rv->boundary_str = g_strdup (boundary);
  return rv;
}

/**
 * gsk_mime_multipart_encoder_add_part:
 * @encoder: the encoder to add the part to.
 * @piece: the content to append.
 * @error: place to store the error code if something goes wrong.
 *
 * Add a new part to @encoder.  The pieces will be transmitted in
 * the order receieved.
 *
 * returns: whether the part could be added to the stream.
 */ 
gboolean
gsk_mime_multipart_encoder_add_part (GskMimeMultipartEncoder *encoder,
				     GskMimeMultipartPiece   *piece,
				     GError                 **error)
{
  g_return_val_if_fail (encoder->shutdown == FALSE, FALSE);
  g_queue_push_tail (encoder->outgoing_pieces, piece);
  gsk_mime_multipart_piece_ref (piece);
  if (encoder->active_stream == NULL)
    if (!dequeue_next_piece (encoder, error))
      return FALSE;
  return TRUE;
}
