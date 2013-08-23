#include <string.h>
#include <ctype.h>
#include "gskmimemultipartdecoder.h"
#include "gskmimeencodings.h"
#include "../gskbufferstream.h"
#include "../gskmemory.h"
#include "../gskmacros.h"

/* Relevant RFC's.
2045: MIME: Format of Message Bodies
2046: MIME: Media Types
 */

#define DEBUG_MIME_DECODER 0

/* Relevant, but the extensions are mostly unimplemented:
2183: Content-Disposition header type
2387: mime type: MultipartDecoder/Related
2388: mime type: MultipartDecoder/Form-Data
 */

/* TODO: the lines in headers can not be broken up with newlines yet! */

static GObjectClass *parent_class = NULL;

enum
{
  /* multipart_decoder has been constructed,
     but has not yet gotten an array of options.
     (this distinction is a bit of a hack.) */
  STATE_INITED,

  /* has had set_options() called on it, successfully */
  STATE_WAITING_FOR_FIRST_SEP_LINE,

  /* Gathering header of next piece in 'buffer'.

     When the full header is gathered, a new 
     last_piece will be allocated.
     (this may cause writability notification to start.)

     However, if the 'part' is in memory mode,
     but the full data has not been received,
     then it is NOT appropriate to notify the user
     of the availability of this part, because
     memory mode implies that we gather the 
     full content before notifying the user.  */
  STATE_READING_HEADER,

  /* In these state, content must be written to
     FEED_STREAM, noting that we must
     scan for the "boundary".

     We generally process the entire buffer,
     except if there is ambiguity and then we 
     keep the whole beginning of the line around.

     So the parse algorithm is:
       while we have data to be processed:
         Scan for line starts,
	 noting what they are followed with:
	   - Is Boundary?
	     Process pending data, shutdown and break.
	   - Is Boundary prefix?
	     Process pending data, and break.
	   - Cannot be boundary?
	     Continue scanning for next line start.
	     The line is considered pending data.
   */
  STATE_CONTENT_LINE_START,
  STATE_CONTENT_MIDLINE,

  STATE_ENDED
};

/* --- functions --- */

static inline void
update_idle_notify_writeable (GskMimeMultipartDecoder *multipart_decoder)
{
  gboolean is_writable;
  if (multipart_decoder->feed_stream != NULL)
    is_writable = gsk_io_is_polling_for_read (multipart_decoder->feed_stream);
  else
    is_writable = multipart_decoder->first_piece == NULL;
  //is_writable = gsk_hook_get_last_poll_state (relevant_hook);
#if 0
  g_message ("update_idle_notify_writeable: (feed_stream=%p, feed_stream->read_hook: on: %s; first_piece=%p) => %u",
             multipart_decoder->feed_stream, 
             (multipart_decoder->feed_stream ? (is_writable ? "yes" : "no") : "n/a"),
             multipart_decoder->first_piece,
             is_writable);
#endif
  gsk_io_set_idle_notify_write (multipart_decoder, is_writable);
}

static void
gsk_mime_multipart_decoder_set_poll_write  (GskIO      *io,
			            gboolean    do_poll)
{
  GskMimeMultipartDecoder *multipart_decoder = GSK_MIME_MULTIPART_DECODER (io);
  g_assert (do_poll == gsk_hook_get_last_poll_state (GSK_IO_WRITE_HOOK (io)));
  update_idle_notify_writeable (multipart_decoder);
}

static gboolean
gsk_mime_multipart_decoder_shutdown_write (GskIO *io, GError **error)
{
  GskMimeMultipartDecoder *decoder = GSK_MIME_MULTIPART_DECODER (io);
  decoder->is_shutdown = 1;
  if (decoder->n_pieces_alloced == decoder->n_pieces_obtained)
    {
      gsk_hook_notify_shutdown (_GSK_MIME_MULTIPART_DECODER_HOOK (decoder));
    }
  if (decoder->state != STATE_ENDED)
    {
      gsk_io_set_error (io, GSK_IO_ERROR_WRITE, GSK_ERROR_BAD_FORMAT,
			_("did not end with terminal boundary"));
      return FALSE;
    }
  return TRUE;
}

#if DEBUG_MIME_DECODER
static const char *
state_to_string (guint state)
{
  switch (state)
    {
#define CASE_RET_AS_STRING(st)  case st: return #st
    CASE_RET_AS_STRING(STATE_INITED);
    CASE_RET_AS_STRING(STATE_WAITING_FOR_FIRST_SEP_LINE);
    CASE_RET_AS_STRING(STATE_READING_HEADER);
    CASE_RET_AS_STRING(STATE_CONTENT_LINE_START);
    CASE_RET_AS_STRING(STATE_CONTENT_MIDLINE);
    CASE_RET_AS_STRING(STATE_ENDED);
#undef CASE_RET_AS_STRING
    }
  g_return_val_if_reached (NULL);
}
#define YELL(decoder) g_message ("at line %u, state=%s [buf-size=%u]",__LINE__,state_to_string ((decoder)->state), (decoder)->buffer.size)
#endif

static inline void
raw_append_to_list (GskMimeMultipartDecoder *decoder,
		    GskMimeMultipartPiece   *piece)
{
      decoder->last_piece = g_slist_append (decoder->last_piece, piece);
      if (decoder->first_piece)
	decoder->last_piece = decoder->last_piece->next;
      else
	decoder->first_piece = decoder->last_piece;
}

static void
append_to_list (GskMimeMultipartDecoder  *decoder,
		GskMimeMultipartPiece    *piece,
		guint                     piece_index)
{
  gsk_mime_multipart_piece_ref (piece);
  if (piece_index == decoder->next_piece_index_to_append)
    {
      guint npi;
      
      raw_append_to_list (decoder, piece);

      npi = ++(decoder->next_piece_index_to_append);
      if (decoder->piece_index_to_piece != NULL)
	while (
	  (piece = g_hash_table_lookup (decoder->piece_index_to_piece,
					GUINT_TO_POINTER (npi))) != NULL
	      )
	  {
	    g_hash_table_remove (decoder->piece_index_to_piece,
				 GUINT_TO_POINTER (npi));
	    raw_append_to_list (decoder, piece);
	    npi = ++(decoder->next_piece_index_to_append);
	  }

      if (decoder->first_piece != NULL)
	gsk_hook_mark_idle_notify (_GSK_MIME_MULTIPART_DECODER_HOOK (decoder));
    }
  else
    {
      if (decoder->piece_index_to_piece == NULL)
	decoder->piece_index_to_piece = g_hash_table_new (NULL, NULL);
      g_hash_table_insert (decoder->piece_index_to_piece,
			   GUINT_TO_POINTER (piece_index),
			   piece);
    }
}

/* Copy data from buffer into current_piece/feed_stream,
   scanning for lines starting with "--boundary",
   where "boundary" is replaced with multipart_decoder->boundary.
   
   If we reach the end of this piece,
   we must shutdown feed_stream, current_piece,
   and advance back to STATE_READING_HEADER.
   
   If we reach the terminal boundary (which has two terminal hyphens),
   do the above and raise "got_terminal_boundary",
   shutting down the multipart_decoder hook if or when first_piece is empty. 
 */
static gboolean
feed_buffer_into_feed_stream (GskMimeMultipartDecoder *multipart_decoder)
{
  GskBufferIterator iterator;

  /* The number of bytes to feed into feed-stream. */
  guint n_pending = 0;

  /* The number of bytes to throw away after feeding n_pending. */
  guint n_discard = 0;

  gboolean terminated = FALSE;
  gboolean at_line_start;
  char *bdy_tmp;
  gsk_buffer_iterator_construct (&iterator, &multipart_decoder->buffer);
  if (multipart_decoder->state == STATE_CONTENT_MIDLINE)
    {
      at_line_start = gsk_buffer_iterator_find_char (&iterator, '\n');
      if (at_line_start)
	gsk_buffer_iterator_skip (&iterator, 1);
      n_pending = gsk_buffer_iterator_offset (&iterator);
    }
  else if (multipart_decoder->state == STATE_CONTENT_LINE_START)
    {
      at_line_start = TRUE;
    }
  else
    g_return_val_if_reached (FALSE);
  
  bdy_tmp = g_alloca (multipart_decoder->boundary_str_len + 4 + 1);
  while (at_line_start)
    {
      unsigned n_peeked = gsk_buffer_iterator_peek (&iterator, bdy_tmp, multipart_decoder->boundary_str_len + 4);
      gboolean could_be_bdy = TRUE;
      if (n_peeked == 0)
	break;
      bdy_tmp[n_peeked] = 0;
      if (n_peeked > 0 && bdy_tmp[0] != '-')
	could_be_bdy = FALSE;
      if (n_peeked > 1 && bdy_tmp[1] != '-')
	could_be_bdy = FALSE;
      if (n_peeked > 2
       && memcmp (bdy_tmp + 2,
		  multipart_decoder->boundary_str,
		  MIN (multipart_decoder->boundary_str_len, n_peeked - 2)) != 0)
	could_be_bdy = FALSE;
      if (!could_be_bdy)
	{
	  at_line_start = gsk_buffer_iterator_find_char (&iterator, '\n');
	  if (at_line_start)
	    {
	      gsk_buffer_iterator_skip (&iterator, 1);
	      n_pending = gsk_buffer_iterator_offset (&iterator);
	    }
	  else
	    {
	      multipart_decoder->state = STATE_CONTENT_MIDLINE;
	      n_pending = multipart_decoder->buffer.size;
	    }
	}
      else if (n_peeked <= multipart_decoder->boundary_str_len + 2)
	{
	  /* ambiguous */
	  multipart_decoder->state = STATE_CONTENT_LINE_START;
	  break;
	}
      else
	{
	  /* We need to get the newline after the boundary.
	     If we don't, treat it like it's ambiguous. */
	  if (!gsk_buffer_iterator_find_char (&iterator, '\n'))
	    {
	      multipart_decoder->state = STATE_CONTENT_LINE_START;
	      break;
	    }

	  if (bdy_tmp[multipart_decoder->boundary_str_len + 2] == '-'
	   && bdy_tmp[multipart_decoder->boundary_str_len + 3] == '-')
	    terminated = TRUE;
	  multipart_decoder->state = STATE_READING_HEADER;
	  n_discard = gsk_buffer_iterator_offset (&iterator) + 1 - n_pending;
	  break;
	}
    }

  if (n_pending > 0)
    {
      GskBufferStream *feed = GSK_BUFFER_STREAM (multipart_decoder->feed_stream);
      gsk_io_mark_is_readable (feed);
      if (multipart_decoder->swallowed_crlf)
	gsk_buffer_append (gsk_buffer_stream_peek_read_buffer (feed), "\r\n", 2);
      if (n_pending >= 2)
	{
	  char tmp[2];
	  guint bufdisc = 2;
	  gsk_buffer_transfer (gsk_buffer_stream_peek_read_buffer (feed), &multipart_decoder->buffer, n_pending - 2);
	  gsk_buffer_peek (&multipart_decoder->buffer, tmp, 2);
	  if (memcmp (tmp, "\r\n", 2) == 0)
	    multipart_decoder->swallowed_crlf = TRUE;
	  else if (tmp[1] == '\r')
	    {
	      g_assert (n_discard == 0);
	      gsk_buffer_append (gsk_buffer_stream_peek_read_buffer (feed), tmp, 1);
	      /* if we have a \r then we cannot transfer it,
		 since it may be a CRLF that we will want to discard. */
	      bufdisc = 1;
	    }
	  else
	    {
	      gsk_buffer_append (gsk_buffer_stream_peek_read_buffer (feed), tmp, 2);
	      multipart_decoder->swallowed_crlf = FALSE;
	    }
	  gsk_buffer_discard (&multipart_decoder->buffer, bufdisc);
	}
      else
	{
	  gsk_buffer_transfer (gsk_buffer_stream_peek_read_buffer (feed), &multipart_decoder->buffer, n_pending);
	  multipart_decoder->swallowed_crlf = FALSE;
	}
      gsk_buffer_stream_read_buffer_changed (feed);
    }
  if (n_discard > 0)
    gsk_buffer_discard (&multipart_decoder->buffer, n_discard);

  if (multipart_decoder->state == STATE_READING_HEADER
   || multipart_decoder->state == STATE_ENDED)
    {
      // XXX: maybe we should just write directly into
      // the content-encoding decoder filter.
      // (If that was feed_stream, then gsk_io_write_shutdown()
      // would work, be faster and cleaner)
      //gsk_io_write_shutdown (multipart_decoder->feed_stream, NULL);
      gsk_buffer_stream_read_shutdown (GSK_BUFFER_STREAM (multipart_decoder->feed_stream));
      g_object_unref (multipart_decoder->feed_stream);
      multipart_decoder->feed_stream = NULL;

      gsk_mime_multipart_piece_unref (multipart_decoder->current_piece);
      multipart_decoder->current_piece = NULL;

      multipart_decoder->swallowed_crlf = FALSE;
    }
  if (terminated)
    {
      multipart_decoder->state = STATE_ENDED;
      if (multipart_decoder->n_pieces_alloced == multipart_decoder->n_pieces_obtained)
	{
	  gsk_hook_notify_shutdown (_GSK_MIME_MULTIPART_DECODER_HOOK (multipart_decoder));
	}
    }
  return TRUE;
}

typedef struct _PieceDecoder PieceDecoder;
struct _PieceDecoder
{
  GskMimeMultipartDecoder *decoder;
  GskMimeMultipartPiece *piece;
  guint piece_index;
};

static PieceDecoder *
piece_decoder_alloc (GskMimeMultipartDecoder *decoder,
		     GskMimeMultipartPiece *piece,
		     guint piece_index)
{
  PieceDecoder *rv = g_new (PieceDecoder, 1);
  rv->decoder = g_object_ref (decoder);
  rv->piece = gsk_mime_multipart_piece_ref (piece);
  rv->piece_index = piece_index;
  return rv;
}

static void
handle_mime_piece_done (GskBuffer *buffer, gpointer data)
{
  PieceDecoder *pd = data;
  gpointer slab;
  guint size = buffer->size;
  slab = g_malloc (buffer->size);
  gsk_buffer_peek (buffer, slab, buffer->size);
  gsk_mime_multipart_piece_set_data (pd->piece, slab, size, g_free, slab);
  append_to_list (pd->decoder, pd->piece, pd->piece_index);
  update_idle_notify_writeable (pd->decoder);
}

static void
piece_decoder_free (PieceDecoder *pd)
{
  g_object_unref (pd->decoder);
  gsk_mime_multipart_piece_unref (pd->piece);
  g_free (pd);
}

/* Called when the terminal whitespace
   after the multipart_decoder message has been reached.
   
   Returns FALSE if an error occurred */
static gboolean
done_header (GskMimeMultipartDecoder *multipart_decoder)
{
  /* Define the stack of streams which
     data can be written into. */
  GskBufferStream *feed_stream;
  GskStream *write_end, *read_end;
  GError *error = NULL;
  const char *transfer_encoding = multipart_decoder->current_piece->transfer_encoding;

  g_assert (multipart_decoder->feed_stream == NULL);
  g_assert (multipart_decoder->current_piece != NULL);
  g_assert (multipart_decoder->state == STATE_CONTENT_LINE_START);
  
  feed_stream = gsk_buffer_stream_new ();
  multipart_decoder->feed_stream = GSK_STREAM (feed_stream);

  if (!gsk_mime_make_transfer_encoding_decoders (transfer_encoding,
						 &write_end, &read_end, &error))
    {
      g_message ("error making decoder chain for '%s': %s", 
		 multipart_decoder->current_piece->transfer_encoding,
		 error->message);
      g_error_free (error);
      return FALSE;
    }

  if (!gsk_stream_attach (GSK_STREAM (feed_stream), write_end, &error))
    {
      g_message ("error attaching to decoder chain for '%s': %s", 
		 multipart_decoder->current_piece->transfer_encoding,
		 error->message);
      g_error_free (error);
    }
  g_object_unref (write_end);
  write_end = NULL;

  if (multipart_decoder->mode == GSK_MIME_MULTIPART_DECODER_MODE_ALWAYS_MEMORY)
    {
      GskStream *sink;
      PieceDecoder *data;
      data = piece_decoder_alloc (multipart_decoder,
				  multipart_decoder->current_piece,
				  multipart_decoder->n_pieces_alloced - 1),
      sink = gsk_memory_buffer_sink_new (handle_mime_piece_done,
					 data,
				         (GDestroyNotify) piece_decoder_free);
      if (!gsk_stream_attach (read_end, sink, NULL))
	return FALSE;
      g_object_unref (sink);
    }
  else
    {
      /* append this part to the stack and ready notification */
      multipart_decoder->current_piece->is_memory = 0;
      multipart_decoder->current_piece->content = read_end;
      /* NOTE: Don't unref the read_end: multipart_decoder->content should own a reference. */

      /* Append current_piece to the list of pieces. */
      append_to_list (multipart_decoder, multipart_decoder->current_piece,
		      multipart_decoder->next_piece_index_to_append);
    }

  /* Feed data from the buffer into the buffer stream. */
  if (!feed_buffer_into_feed_stream (multipart_decoder))
    {
      g_message ("error writing multipart_decoder content to feed_stream");
      return FALSE;
    }

  return TRUE;
}

/* Returns TRUE if the header was successfully parsed,
   either as the end-of-header or as just another header
   line.

   Therefore, it returns FALSE only on an error condition.

   The part-available hook is invoked, if appropriate. */
static gboolean
parse_header_line (GskMimeMultipartDecoder *multipart_decoder,
		   const char       *line,
		   GError          **error)
{
  GskMimeMultipartPiece *piece;
  if (multipart_decoder->current_piece == NULL)
    {
      multipart_decoder->current_piece = gsk_mime_multipart_piece_alloc ();
      ++multipart_decoder->n_pieces_alloced;
    }
  piece = multipart_decoder->current_piece;
  if (g_ascii_isspace (line[0]))
    {
      const char *t = line + 1;
      while (*t && g_ascii_isspace (*t))
	t++;
      if (*t)
	{
	  g_set_error (error, GSK_G_ERROR_DOMAIN,
		       GSK_ERROR_BAD_FORMAT,
		       _("multipart_decoder header line began with whitespace"));
	  return FALSE;
	}
      multipart_decoder->state = STATE_CONTENT_LINE_START;
      return done_header (multipart_decoder);
    }
  if (g_ascii_strncasecmp (line, "content-type:", 13) == 0
   && piece->type == NULL)
    {
      /* Example:
	 text/plain; charset=latin-1

	 See RFC 2045, Section 5.
       */
      const char *start = line + 13;
      const char *slash = strchr (start, '/');
      const char *at;
      GPtrArray *fields = NULL;
      GSK_SKIP_WHITESPACE (start);
      if (slash == NULL)
	{
	  g_set_error (error, GSK_G_ERROR_DOMAIN,
		       GSK_ERROR_BAD_FORMAT,
		       _("content-type expected to contain a '/'"));
	  return FALSE;
	}
      piece->type = g_strndup (start, slash-start);
      slash++;
      at = slash;
#define IS_SUBTYPE_CHAR(c) (isalpha(c) || isdigit(c) || ((c)=='-'))
#define IS_SEP_TYPE_CHAR(c) (isspace(c) || ((c)==';'))
      GSK_SKIP_CHAR_TYPE (at, IS_SUBTYPE_CHAR);
      piece->subtype = g_strndup (slash, at-slash);
      for (;;)
	{
	  const char *value_start, *value_end;
	  const char *equals;
	  char *key, *value;
	  GSK_SKIP_CHAR_TYPE (at, IS_SEP_TYPE_CHAR);
	  if (*at == '\0')
	    break;
	  equals = at;
	  GSK_SKIP_CHAR_TYPE (equals, IS_SUBTYPE_CHAR);
	  if (*equals != '=')
	    {
	      g_set_error (error, GSK_G_ERROR_DOMAIN,
			   GSK_ERROR_BAD_FORMAT,
			   _("expected '=' in key-value pairs on content-type"));
	      return FALSE;
	    }
	  value_start = equals + 1;
	  value_end = value_start;
	  if (*value_start == '"')
	    {
	      value_end = strchr (value_start + 1, '"');
	      if (value_end == NULL)
		{
		  g_set_error (error, GSK_G_ERROR_DOMAIN,
			       GSK_ERROR_BAD_FORMAT,
			       _("missing terminal '\"' in key/value pair in content-type"));
		  return FALSE;
		}
	      value = g_strndup (value_start + 1, value_end - (value_start + 1));
	      value_end++;
	    }
	  else
	    {
	      GSK_SKIP_CHAR_TYPE (value_end, IS_SUBTYPE_CHAR);
	      value = g_strndup (value_start, value_end - value_start);
	    }
	  key = g_strndup (at, equals - at);
	  at = value_end;
	  if (g_ascii_strcasecmp (key, "charset") == 0)
	    {
	      g_free (piece->charset);
	      piece->charset = value;
	      g_free (key);
	      continue;
	    }
	  if (!fields)
	    fields = g_ptr_array_new ();
	  g_ptr_array_add (fields, key);
	  g_ptr_array_add (fields, value);
	}
      if (fields != NULL)
	{
	  g_ptr_array_add (fields, NULL);
	  piece->other_fields = (char**) g_ptr_array_free (fields, FALSE);
	}
      else
	{
	  piece->other_fields = NULL;
	}
    }
  else if (g_ascii_strncasecmp (line, "content-id:", 11) == 0
       && piece->id == NULL)
    {
      const char *start = strchr(line, ':') + 1;
      GSK_SKIP_WHITESPACE (start);
      piece->id = g_strchomp (g_strdup (start));
    }
  else if (g_ascii_strncasecmp (line, "content-location:", 17) == 0
       && piece->location == NULL)
    {
      const char *start = strchr(line, ':') + 1;
      GSK_SKIP_WHITESPACE (start);
      piece->location = g_strchomp (g_strdup (start));
    }
  else if (g_ascii_strncasecmp (line, "content-transfer-encoding:", 26) == 0)
    {
      const char *start = strchr(line, ':') + 1;
      GSK_SKIP_WHITESPACE (start);
      piece->transfer_encoding = g_strchomp (g_strdup (start));
    }
  else if (g_ascii_strncasecmp (line, "content-description:", 20) == 0)
    {
      const char *start = strchr(line, ':') + 1;
      GSK_SKIP_WHITESPACE (start);
      piece->description = g_strchomp (g_strdup (start));
    }
  else if (g_ascii_strncasecmp (line, "content-disposition:", 20) == 0)
    {
      const char *start = strchr(line, ':') + 1;
      GSK_SKIP_WHITESPACE (start);
      piece->disposition = g_strchomp (g_strdup (start));
    }
  else
    {
      g_message ("WARNING: could not part multipart_decoder message line: '%s'", line);
      return FALSE;
    }
  return TRUE;
}

static void
multipart_decoder_process_buffer (GskMimeMultipartDecoder *multipart_decoder,
			  GError          **error)
{
  char buf[4096];
  /* The last iteration buffer size.
     Set to zero initially, since we don't want
     to run the following loop unless there is something
     in the incoming buffer to process; all subsequent times we
     are principally interested in whether any data was processed. */
  guint last_iter = 0;

  while (multipart_decoder->buffer.size != last_iter)
    {
      last_iter = multipart_decoder->buffer.size;
      switch (multipart_decoder->state)
	{
	case STATE_INITED:
	  g_set_error (error, GSK_G_ERROR_DOMAIN,
		       GSK_ERROR_INVALID_STATE,
		       _("mime-multipart_decoder not fully constructed"));
	  return;

	case STATE_WAITING_FOR_FIRST_SEP_LINE:
	  {
	    int index = gsk_buffer_index_of (&multipart_decoder->buffer, '\n');
	    if (index < 0)
	      return;
            if ((guint)index+1 < sizeof (buf))
	      {
		gsk_buffer_read (&multipart_decoder->buffer, buf, index + 1);
		buf[index] = 0;
		g_strchomp (buf);
		if (buf[0] != '-' || buf[1] != '-')
		  continue;
		if (strncmp (buf + 2, multipart_decoder->boundary_str,
			     multipart_decoder->boundary_str_len) == 0)
		  {
		    if (buf[2 + multipart_decoder->boundary_str_len] == '-'
		     && buf[3 + multipart_decoder->boundary_str_len] == '-')
		      multipart_decoder->state = STATE_ENDED;
		    else
		      multipart_decoder->state = STATE_READING_HEADER;
		    continue;
		  }
	      }
	    else
	      {
		// XXX: this whole branch is basically an error:
		// very long lines don't really make sense.
		char *line = gsk_buffer_read_line (&multipart_decoder->buffer);
		g_strchomp (line);
		if (line[0] != '-' || line[1] != '-')
		  continue;
		if (strncmp (line + 2, multipart_decoder->boundary_str,
			     multipart_decoder->boundary_str_len) == 0)
		  {
		    if (line[2 + multipart_decoder->boundary_str_len] == '-'
		     && line[3 + multipart_decoder->boundary_str_len] == '-')
		      multipart_decoder->state = STATE_ENDED;
		    else
		      multipart_decoder->state = STATE_READING_HEADER;
		    g_free (line);
		    continue;
		  }
		g_free (line);
	      }
	    continue;
	  }

	case STATE_READING_HEADER:
	  {
	    char *line = gsk_buffer_read_line (&multipart_decoder->buffer);
	    if (!parse_header_line (multipart_decoder, line, error))
              {
                g_free (line);
                return;
              }
            g_free (line);
	    continue;
	  }

	case STATE_CONTENT_MIDLINE:
	case STATE_CONTENT_LINE_START:
	  {
	    /* Figure out if the end-of-content delimiter is
	       anywhere to be seen.

	       We may see it, or we may only see the start of
	       it, in which case we must wait for more
	       data to determine if it is really the end-of-content. */
	    if (!feed_buffer_into_feed_stream (multipart_decoder))
	      {
		//g_set_error().
		return;
	      }
	    break;
	  }
	case STATE_ENDED:
	  return;
	}
    }
}

/* XXX: these routines assume that writing a nonzero amount
        while returning an error is reasonable. */

/* TODO: optimize for zero-copy in the large-content-body case */
static guint
gsk_mime_multipart_decoder_raw_write  (GskStream     *stream,
			       gconstpointer  data,
			       guint          length,
			       GError       **error)
{
  GskMimeMultipartDecoder *multipart_decoder = GSK_MIME_MULTIPART_DECODER (stream);
  gsk_buffer_append (&multipart_decoder->buffer, data, length);
  multipart_decoder_process_buffer (multipart_decoder, error);
  return length;
}

static guint
gsk_mime_multipart_decoder_raw_write_buffer(GskStream    *stream,
			            GskBuffer    *buffer,
			            GError      **error)
{
  GskMimeMultipartDecoder *multipart_decoder = GSK_MIME_MULTIPART_DECODER (stream);
  unsigned rv = gsk_buffer_drain (&multipart_decoder->buffer, buffer);
  multipart_decoder_process_buffer (multipart_decoder, error);
  // XXX: if *error, return 0 ???
  return rv;
}

static void
gsk_mime_multipart_decoder_init (GskMimeMultipartDecoder *mime_multipart_decoder)
{
  GSK_HOOK_INIT (mime_multipart_decoder,
		 GskMimeMultipartDecoder,
		 new_part_available,
		 GSK_HOOK_CAN_HAVE_SHUTDOWN_ERROR,
		 set_poll_new_part, shutdown_new_part);
  GSK_HOOK_SET_FLAG (_GSK_MIME_MULTIPART_DECODER_HOOK (mime_multipart_decoder),
		     IS_AVAILABLE);
  gsk_io_mark_is_writable (mime_multipart_decoder);
  gsk_io_set_idle_notify_write (mime_multipart_decoder, TRUE);
}

static void
unref_piece_value (gpointer key, gpointer value, gpointer data)
{
  gsk_mime_multipart_piece_unref (value);
}

static void
gsk_mime_multipart_decoder_finalize (GObject *object)
{
  GskMimeMultipartDecoder *decoder = GSK_MIME_MULTIPART_DECODER (object);
  gsk_hook_destruct (_GSK_MIME_MULTIPART_DECODER_HOOK (decoder));
  gsk_buffer_destruct (&decoder->buffer);
  while (decoder->first_piece != NULL)
    {
      GskMimeMultipartPiece *piece = decoder->first_piece->data;
      decoder->first_piece = g_slist_remove (decoder->first_piece, piece);

      gsk_mime_multipart_piece_unref (piece);
    }
  if (decoder->piece_index_to_piece != NULL)
    {
      g_hash_table_foreach (decoder->piece_index_to_piece,
			    unref_piece_value,
			    NULL);
      g_hash_table_destroy (decoder->piece_index_to_piece);
    }
  decoder->last_piece = NULL;
  (*parent_class->finalize) (object);
}

static void
gsk_mime_multipart_decoder_class_init (GskMimeMultipartDecoderClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GskIOClass *io_class = GSK_IO_CLASS (class);
  GskStreamClass *stream_class = GSK_STREAM_CLASS (class);
  parent_class = g_type_class_peek_parent (class);
  object_class->finalize = gsk_mime_multipart_decoder_finalize;
  io_class->set_poll_write = gsk_mime_multipart_decoder_set_poll_write;
  io_class->shutdown_write = gsk_mime_multipart_decoder_shutdown_write;
  stream_class->raw_write = gsk_mime_multipart_decoder_raw_write;
  stream_class->raw_write_buffer = gsk_mime_multipart_decoder_raw_write_buffer;
  GSK_HOOK_CLASS_INIT (object_class, "new-part-available", GskMimeMultipartDecoder, new_part_available);
}

GType gsk_mime_multipart_decoder_get_type()
{
  static GType mime_multipart_decoder_type = 0;
  if (!mime_multipart_decoder_type)
    {
      static const GTypeInfo mime_multipart_decoder_info =
      {
	sizeof(GskMimeMultipartDecoderClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_mime_multipart_decoder_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskMimeMultipartDecoder),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_mime_multipart_decoder_init,
	NULL		/* value_table */
      };
      mime_multipart_decoder_type = g_type_register_static (GSK_TYPE_STREAM,
                                                  "GskMimeMultipartDecoder",
						  &mime_multipart_decoder_info, 0);
    }
  return mime_multipart_decoder_type;
}

/**
 * gsk_mime_multipart_decoder_new:
 * @kv_pairs: key,value pairs of auxillary information parsed along
 * with the Content-Type field.
 *
 * Allocate a new MIME multipart decoder,
 * which is a write-only #GskStream
 * that converts to a stream of individual pieces.
 */
GskMimeMultipartDecoder      *
gsk_mime_multipart_decoder_new         (char **kv_pairs)
{
  GskMimeMultipartDecoder *rv = g_object_new (GSK_TYPE_MIME_MULTIPART_DECODER, NULL);
  unsigned i;
  for (i = 0; kv_pairs[i] != NULL; i += 2)
    {
      const char *key = kv_pairs[i+0];
      const char *value = kv_pairs[i+1];
      if (g_ascii_strcasecmp (key, "start") == 0)
	{
	  g_free (rv->start);
	  rv->start = g_strdup (value);
	}
      else if (g_ascii_strcasecmp (key, "start-info") == 0)
	{
	  g_free (rv->start_info);
	  rv->start_info = g_strdup (value);
	}
      else if (g_ascii_strcasecmp (key, "boundary") == 0)
	{
	  g_free (rv->boundary_str);
	  rv->boundary_str = g_strdup (value);
	  rv->boundary_str_len = strlen (rv->boundary_str);
	}
      else
	{
	  g_message ("WARNING: mime-multipart_decoder: ignoring Key %s", key);
	}
    }
  if (rv->boundary_str == NULL)
    {
      g_object_unref (rv);
      return NULL;
    }

  rv->state = STATE_WAITING_FOR_FIRST_SEP_LINE;
  return rv;
}

/**
 * gsk_mime_multipart_decoder_get_piece:
 * @decoder: the multipart decoder to retrieve the part.
 *
 * Retrieve a piece of multipart content.
 * Pieces will be retrieve in the order they are
 * received.
 *
 * returns: a piece that the caller must
 * gsk_mime_multipart_piece_unref(), or NULL if there
 * is no remaining piece.
 */
GskMimeMultipartPiece *
gsk_mime_multipart_decoder_get_piece (GskMimeMultipartDecoder *decoder)
{
  GskMimeMultipartPiece *piece;
  if (decoder->first_piece == NULL)
    return NULL;
  piece = decoder->first_piece->data;
  decoder->first_piece = g_slist_remove (decoder->first_piece, piece);
  decoder->n_pieces_obtained++;
  if (decoder->first_piece == NULL)
    {
      decoder->last_piece = NULL;
      gsk_hook_clear_idle_notify (_GSK_MIME_MULTIPART_DECODER_HOOK (decoder));
      if (decoder->n_pieces_alloced == decoder->n_pieces_obtained
       && decoder->is_shutdown)
	{
	  gsk_hook_notify_shutdown (_GSK_MIME_MULTIPART_DECODER_HOOK (decoder));
	}
      update_idle_notify_writeable (decoder);
    }
  return piece;
}

/**
 * gsk_mime_multipart_decoder_set_mode:
 * @decoder: the multipart decoder to affect.
 * @mode: the operating mode of the decoder.
 *
 * Set whether to force the decoder to convert
 * everything to memory or a stream, or it may
 * specify to use whatever the MultipartDecoder
 * internals prefer.
 *
 * This function should only be called immediately after
 * construction.
 */
/* XXX: is the above really true?  maybe we can allow
 * changing modes midstream, for when we know the
 * order of the content. */
void
gsk_mime_multipart_decoder_set_mode (GskMimeMultipartDecoder *decoder,
				     GskMimeMultipartDecoderMode mode)
{
  decoder->mode = mode;
}
