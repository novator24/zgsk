/*
 * State of an HTTP Client
 *
 * An HTTP client has a queue of requests,
 * maintained in the order in which they
 * are added with gsk_http_client_request().
 *
 * The first request on the queue still is
 * waiting for input from the server.
 * A later request is being read from.
 *
 *   first_request -> .... -> last_request
 *       
 */    
#include <ctype.h>
#include <string.h>
#include "gskhttpclient.h"
#include "../gskmacros.h"
#include "../gsklog.h"

/* TODO: contention on POST data:  right now, we buffer as much
   data as the user feels like giving us... need to block the hook.  */
#define BLOCK_POST_DATA 0       /* unfinished */

/* TODO: contention on content-stream.  right now, we "never_block_on_write"
   which means that if someone gives us gigabytes of data we
   will buffer it, instead of blocking it. */

/* TODO: if the content-stream is shutdown by the user,
   we should automatically drain all the data from it. */


/* TODO: instead of only flushing DONE requests in the raw_write
   call, we should be flushing them whereever we can.  */

/* number of bytes of data to buffer from the content stream */
#define GSK_HTTP_CLIENT_CONTENT_STREAM_MAX_BUFFER		8192

#define TEST_CLIENT_USER_FLAGS(client, flags)  \
  (((GSK_HTTP_CLIENT_HOOK (client)->user_flags) & (flags)) == (flags))
#define TEST_CLIENT_USER_FLAG(client, flag)    \
  TEST_CLIENT_USER_FLAGS(client, GSK_HTTP_CLIENT_##flag)

#if 0
#define DEBUG	g_message
#else
#define DEBUG(args...)
#endif

static GObjectClass *parent_class = NULL;
typedef struct _GskHttpClientContentStream GskHttpClientContentStream;
/* --- prototypes for the implementation of the content-stream --- */
static GskHttpClientContentStream *
gsk_http_client_content_stream_new (GskHttpClient *client);
static guint
gsk_http_client_content_stream_xfer     (GskHttpClientContentStream *stream,
				         GskBuffer                  *buffer,
				         gssize                      max_data);
static void
gsk_http_client_content_stream_shutdown (GskHttpClientContentStream *);


/* --- states that a particular pending request can be in --- */

typedef enum
{
  /* just initialized -- using minimum resources */
  INIT,

  /* transferred header into ascii format in `outgoing' buffer */
  WRITING_HEADER,

  /* the entire header has been read out -- now reading from post_data stream */
  POSTING,

  /* got shutdown from post stream but still have buffered data */
  POSTING_WRITING,

  /* no more data to be read from this request -
   * we're waiting for first line of the response (which is handled
   * by a different command table).
   */
  READING_RESPONSE_HEADER_FIRST_LINE,

  /* we're waiting for the appropriate data to be written into us.
   */
  READING_RESPONSE_HEADER,

  /* header was written in -- now waiting for content to be finished */
  READING_RESPONSE_CONTENT_NO_ENCODING,
  READING_RESPONSE_CONTENT_NO_ENCODING_NO_LENGTH,

  /* for chunked data (Transfer-Encoding: chunked) we use these two states:
   * the first means we're reading a newline-terminated number
   * and the second means we're reading that much data */
  READING_RESPONSE_CONTENT_CHUNK_HEADER,
  READING_RESPONSE_CONTENT_CHUNK_DATA,
  READING_RESPONSE_CONTENT_CHUNK_NEWLINE,	/* empty line after data */

  /* request waiting to be deleted.
   * This is the state when all data for this request/response
   * cycle has been written into us.  It is not necessarily
   * the case that the ContentStream has been finished, however.
   */
  DONE
} RequestState;

/* after the request has been written out we can move on to the
 * next request.   this macro checks to see if a request has reached
 * this point yet, according to its 'state' member.
 */
#define request_state_is_readonly(state)  ((state) >= READING_RESPONSE_HEADER_FIRST_LINE)


struct _GskHttpClientRequest
{
  GskHttpClient *client;
  GskHttpRequest *request;
  GskStream *post_data;
  GskBuffer outgoing;

  GskHttpClientResponse handle_response;
  gpointer handle_response_data;
  GDestroyNotify handle_response_destroy;

  GskHttpResponse *response;
  GskHttpClientContentStream *content_stream;
  GHashTable *response_command_table;

  RequestState state;
  guint64 remaining_data;  /* only during READING_RESPONSE_CONTENT_* */
  GskHttpClientRequest *next;
};
static void gsk_http_client_request_destroy (GskHttpClientRequest *request);
#define GET_REMAINING_DATA_UINT(request) \
  ((guint) MIN ((guint64)(request)->remaining_data, (guint64)G_MAXUINT))
GSK_DECLARE_POOL_ALLOCATORS (GskHttpClientRequest, gsk_http_client_request, 8)

static inline gboolean
had_eof_terminated_post_data (GskHttpClientRequest *request)
{
  GskHttpTransferEncoding te = GSK_HTTP_HEADER (request->request)->transfer_encoding_type;
  return (request->request->verb == GSK_HTTP_VERB_POST
       || request->request->verb == GSK_HTTP_VERB_PUT)
      && te != GSK_HTTP_TRANSFER_ENCODING_CHUNKED
      && GSK_HTTP_HEADER (request->request)->content_length == -1;
}

static inline void
set_state_to_reading_response (GskHttpClientRequest *request)
{
  /*GskHttpHeader *reqheader = GSK_HTTP_HEADER (request->request);*/
  g_return_if_fail (request->state == POSTING_WRITING
		 || request->state == WRITING_HEADER
		 || request->state == POSTING);
  request->state = READING_RESPONSE_HEADER_FIRST_LINE;
  if (request->response)
    g_object_unref (request->response);
  request->response = gsk_http_response_new_blank ();

  /* various conditions in which the HTTP client stream is no longer readable */
  if (had_eof_terminated_post_data (request))
   gsk_io_notify_read_shutdown (request->client);
}

static void
flush_done_requests (GskHttpClient *client)
{
  GskHttpClientRequest *at;
  while (client->first_request != NULL
     &&  client->first_request->state == DONE)
    {
      GskHttpClientRequest *request = client->first_request;
      g_assert (request->client == client);
      client->first_request = request->next;
      if (client->first_request == NULL)
        client->last_request = NULL;
      if (request == client->outgoing_request)
        client->outgoing_request = request->next;

      /* destroy request */
      request->next = NULL;		/* unnecesssary */
      gsk_http_client_request_destroy (request);
    }
  for (at = client->first_request; at; at = at->next)
    g_assert (at->client == client);
}

/* --- POST/PUT data handling --- */
/* XXX: need Transfer-Encoding chunked support here! */
static gboolean
handle_post_data_readable (GskStream            *stream,
			   GskHttpClientRequest *request)
{
  GError *error = NULL;
  if (GSK_HTTP_HEADER (request->request)->transfer_encoding_type == GSK_HTTP_TRANSFER_ENCODING_CHUNKED)
    {
      GskBuffer buffer = GSK_BUFFER_STATIC_INIT;
      if (gsk_stream_read_buffer (stream, &buffer, &error) != 0)
        {
          gsk_buffer_printf(&request->outgoing, "%x\n", (guint)buffer.size);
          gsk_buffer_drain (&request->outgoing, &buffer);
        }
    }
  else
    gsk_stream_read_buffer (stream, &request->outgoing, &error);

  if (error)
    {
      /* XXX: error handling! */
      g_warning ("error from post-data: %s", error->message);
      g_error_free (error);
      return FALSE;
    }

  if (request->client->outgoing_request == request
   && request->outgoing.size > 0)
    {
      gsk_io_mark_idle_notify_read (request->client);
    }

#if BLOCK_POST_DATA
  if (request->outgoing.size > MAX_OUTGOING_BUFFERED_DATA)
    {
      if (!request->blocked_post_data)
        {
          request->blocked_post_data = TRUE;
          gsk_stream_block_readable (stream);
        }
    }
#endif
  return TRUE;
}
static gboolean
handle_post_data_shutdown (GskStream            *stream,
			   GskHttpClientRequest *request)
{
  if (request->state == POSTING)
    {
      request->state = POSTING_WRITING;
      if (GSK_HTTP_HEADER (request->request)->transfer_encoding_type == GSK_HTTP_TRANSFER_ENCODING_CHUNKED)
        gsk_buffer_append (&request->outgoing, "0\n", 2);

      if (request->outgoing.size == 0)
        set_state_to_reading_response (request);
    }

#if BLOCK_POST_DATA
  if (request->blocked_post_data)
    {
      request->blocked_post_data = FALSE;
      gsk_stream_unblock_readable (stream);
    }
#endif

  return FALSE;
}
static void
handle_post_data_destroy (gpointer data)
{
  GskHttpClientRequest *request = data;
  g_return_if_fail (GSK_IS_HTTP_CLIENT (request->client));

  /* this only happens on errors... */
  if (request->state == POSTING)
    request->state = POSTING_WRITING;

  /* free the post-data... */
  if (request->post_data)
    {
      GskStream *old_post_data = request->post_data;
      request->post_data = NULL;
      g_object_unref (old_post_data);
    }
}


/* --- handle data from the response header --- */
static void
handle_firstline_header (GskHttpClientRequest  *request,
			 const char            *line)
{
  DEBUG ("start of handle_firstline_header %s", line);
  if (!gsk_http_response_process_first_line (request->response, line))
    {
      gsk_debug ("Gsk-Http-Parser",
                 "WARNING: couldn't handle initial header line %s",
                 line);
      return;
    }
  request->state = READING_RESPONSE_HEADER;
  request->response_command_table = gsk_http_header_get_parser_table (FALSE);
  DEBUG ("initializing command table for request %p", request);
}

static void
handle_response_header   (GskHttpClientRequest  *request,
			  const char            *line)
{
  GskHttpHeaderLineParser *parser;
  GskHttpHeader *response_header = GSK_HTTP_HEADER (request->response);
  char *lowercase;
  const char *colon;
  const char *val;
  unsigned i;
  GSK_SKIP_WHITESPACE (line);		/* unnecessary */
  if (*line == 0)
    {
      DEBUG ("got whitespace line");
      if (request->response->status_code == GSK_HTTP_STATUS_CONTINUE)
        {
          g_object_unref (request->response);
          request->response = gsk_http_response_new_blank ();
          request->state = READING_RESPONSE_HEADER_FIRST_LINE;
          return;
        }

      request->content_stream = gsk_http_client_content_stream_new (request->client);
      /* done parsing header */
      if (response_header->transfer_encoding_type == GSK_HTTP_TRANSFER_ENCODING_CHUNKED)
	request->state = READING_RESPONSE_CONTENT_CHUNK_HEADER;
      else if (response_header->content_length < 0)
	{
	  request->state = READING_RESPONSE_CONTENT_NO_ENCODING_NO_LENGTH;
	  request->remaining_data = -1;
	}
      else
	{
	  request->state = READING_RESPONSE_CONTENT_NO_ENCODING;
	  request->remaining_data = response_header->content_length;
	}

      if (request->handle_response)
	request->handle_response (request->request, request->response,
				  GSK_STREAM (request->content_stream),
				  request->handle_response_data);

      if (response_header->content_length == 0)
	{
	  request->state = DONE;
	  gsk_http_client_content_stream_shutdown (request->content_stream);
	}

      return;
    }

  colon = strchr (line, ':');
  if (colon == NULL)
    {
      gsk_debug ("Gsk-Http-Parser", "no colon in header line %s", line);
      return;	/* XXX: error handling! */
    }

  /* lowercase the header */
  lowercase = g_alloca (colon - (char*)line + 1);
  for (i = 0; line[i] != ':'; i++)
    lowercase[i] = g_ascii_tolower (line[i]);
  lowercase[i] = '\0';

  val = colon + 1;
  GSK_SKIP_WHITESPACE (val);
  
  parser = g_hash_table_lookup (request->response_command_table, lowercase);
  if (parser == NULL)
    {
      /* XXX: error handling */
      gboolean is_nonstandard = (line[0] == 'x' || line[0] == 'X')
                              && line[1] == '-';
      if (!is_nonstandard)
        gsk_debug ("Gsk-Http-Parser", "couldn't handle header line %s", line);
      gsk_http_header_add_misc (GSK_HTTP_HEADER (request->response),
                                lowercase, 
                                val);
      return;
    }

  if (! ((*parser->func) (GSK_HTTP_HEADER (request->response), val, parser->data)))
    {
      /* XXX: error handling */
      gsk_debug ("Gsk-Http-Parser", "error parsing header line %s", line);
      return;
    }
}


/* --- stream member functions --- */
static void
gsk_http_client_set_poll_read   (GskIO      *io,
				 gboolean    do_poll)
{
  GskHttpClient *client = GSK_HTTP_CLIENT (io);
  g_assert (GSK_IS_HTTP_CLIENT (client));
  //client->has_read_poll = do_poll;
}

static void
gsk_http_client_set_poll_write  (GskIO      *io,
				 gboolean    do_poll)
{
  GskHttpClient *client = GSK_HTTP_CLIENT (io);
  g_assert (GSK_IS_HTTP_CLIENT (client));
  //client->has_write_poll = do_poll;
}

static guint
gsk_http_client_raw_read        (GskStream     *stream,
				 gpointer       data,
				 guint          length,
				 GError       **error)
{
  GskHttpClient *client = GSK_HTTP_CLIENT (stream);
  guint rv = 0;
  while (client->outgoing_request)
    {
      GskHttpClientRequest *request = client->outgoing_request;
      if (request->state == INIT)
	{
	  /* write the header into the buffer */
	  gsk_http_header_to_buffer (GSK_HTTP_HEADER (request->request),
			             &request->outgoing);
	  request->state = WRITING_HEADER;
	}
      if (request->state == WRITING_HEADER)
	{
	  rv += gsk_buffer_read (&request->outgoing,
				 ((char *)data) + rv,
				 length - rv);
	  if (rv == length)
	    break;
	  if (request->outgoing.size == 0)
	    {
	      if (request->post_data == NULL)
		set_state_to_reading_response (request);
	      else
		{
		  request->state = POSTING;
		  gsk_io_trap_readable (request->post_data,
					handle_post_data_readable,
					handle_post_data_shutdown,
					request,
					handle_post_data_destroy);
		}
	    }
	}
      if (request->state == POSTING)
	{
	  rv += gsk_buffer_read (&request->outgoing,
				 ((char *)data) + rv,
				 length - rv);
	  if (rv == length)
	    break;
	}
      if (request->state == POSTING_WRITING)
	{
	  rv += gsk_buffer_read (&request->outgoing,
				 ((char *)data) + rv,
				 length - rv);
	  if (request->outgoing.size == 0)
	    set_state_to_reading_response (request);
	  if (rv == length)
	    break;
	}
      if (request_state_is_readonly (request->state))
	{
	  client->outgoing_request = request->next;
	}
      else
	{
	  break;
	}
    }

  if (client->outgoing_request == NULL
   || (client->outgoing_request != NULL
    && request_state_is_readonly (client->outgoing_request->state)
    && (!GSK_HTTP_CLIENT_IS_FAST (client)
      || client->outgoing_request->next == NULL)))
    {
      DEBUG ("clearing idle-notify-read");
      gsk_io_clear_idle_notify_read (client);

      /* This seems like a pretty good idea all the time,
         but some servers react negatively to halfshutdowns
         of this form, so we only want to do the read shutdown
         if we REALLY have to.  That means only if the
         remote server is being fed EOF-terminated POST data. */
      if (TEST_CLIENT_USER_FLAGS (client, GSK_HTTP_CLIENT_DEFERRED_SHUTDOWN
                                        | GSK_HTTP_CLIENT_REQUIRES_READ_SHUTDOWN))
        gsk_io_notify_read_shutdown (client);
    }

  DEBUG ("gsk_http_client_raw_read: returning %u bytes", rv);

  return rv;
}

#define MAX_STACK_ALLOC	4096

static inline guint
xdigit_to_number (char c)
{
  return ('0' <= c && c <= '9') ? (c - '0')
       : ('a' <= c && c <= 'f') ? (c - 'a' + 10)
       : ('A' <= c && c <= 'F') ? (c - 'A' + 10)
       : 0 /* XXX: should never happen */
       ;
}
static guint
gsk_http_client_raw_write      (GskStream     *stream,
				gconstpointer  data,
				guint          length,
				GError       **error)
{
  GskHttpClient *client = GSK_HTTP_CLIENT (stream);

  char stack_buf[MAX_STACK_ALLOC];

  /* TODO: figure out a zero-copy strategy... */
  GskBuffer *incoming_data = &client->incoming_data;
  gsk_buffer_append (incoming_data, data, length);

  while (client->first_request != NULL
      && incoming_data->size > 0)
    {
      GskHttpClientRequest *request = client->first_request;
      if (request->state == INIT
       || request->state == WRITING_HEADER)
	{
	  /* invalid request state. */
	  g_set_error (error, GSK_G_ERROR_DOMAIN,
		       GSK_ERROR_HTTP_PARSE,
		       _("got data from server before request had been issued "
			 "(request->state = %d)"),
		       request->state);
          flush_done_requests (client);
	  return length;
	}
      if (request->state == DONE)
        {
          flush_done_requests (client);
          continue;
        }
      if (request->state == POSTING
       || request->state == POSTING_WRITING)
        {
          //g_warning ("need to block writes til post done?");
          flush_done_requests (client);
          return length;
        }
      if (request->state == READING_RESPONSE_HEADER_FIRST_LINE)
	{
	  int nl = gsk_buffer_index_of (incoming_data, '\n');
	  char *free_buffer = NULL;
	  char *line;
	  if (nl < 0)
	    break;
	  if (nl > MAX_STACK_ALLOC - 1)
	    line = free_buffer = g_malloc (nl + 1);
	  else
	    line = stack_buf;
	  gsk_buffer_read (incoming_data, line, nl + 1);
	  line[nl] = 0;
	  g_strchomp (line);

          /* HACK HACK HACK:  for compatibility and upgrading
             ignore a newline that occurs after a transfer-encoded chunk.
             Blech, what a horrible problem. */
          if (line[0] == 0)
            {
              g_free (free_buffer);
              continue;
            }

	  handle_firstline_header (request, line);
	  g_free (free_buffer);

	  DEBUG ("gsk_simple_parser_parse_callback succeeded");
	  if (request->state != READING_RESPONSE_HEADER)
	    {
	      g_set_error (error, GSK_G_ERROR_DOMAIN,
			   GSK_ERROR_HTTP_PARSE,
			   _("error parsing first line of response"));
              flush_done_requests (client);
	      return length;
	    }
	}

      while (request->state == READING_RESPONSE_HEADER)
	{
	  int nl = gsk_buffer_index_of (incoming_data, '\n');
	  char *free_buffer = NULL;
	  char *line;
	  if (nl < 0)
	    goto done;
	  if (nl > MAX_STACK_ALLOC - 1)
	    line = free_buffer = g_malloc (nl + 1);
	  else
	    line = stack_buf;
	  gsk_buffer_read (incoming_data, line, nl + 1);
	  line[nl] = 0;
	  g_strchomp (line);

	  DEBUG ("client: trying to handle a normal request line");
	  handle_response_header (request, line);
	  g_free (free_buffer);
	}
      if (request->state == READING_RESPONSE_CONTENT_NO_ENCODING_NO_LENGTH)
	{
	  GskHttpClientContentStream *content_stream = request->content_stream;
	  gsk_http_client_content_stream_xfer (content_stream, incoming_data, -1);
	}
      else if (request->state == READING_RESPONSE_CONTENT_NO_ENCODING)
	{
	  GskHttpClientContentStream *content_stream = request->content_stream;
          guint amt = GET_REMAINING_DATA_UINT (request);
	  amt = gsk_http_client_content_stream_xfer (content_stream, incoming_data, amt);
	  request->remaining_data -= amt;
	  if (request->remaining_data == 0)
	    {
	      request->state = DONE;
	      gsk_http_client_content_stream_shutdown (content_stream);
	    }
	}
chunk_start:
      /* From RFC 2616, Section 3.6.1

         The chunk-size field is a string of hex digits indicating the size of
         the chunk. The chunked encoding is ended by any chunk whose size is
         zero, followed by the trailer, which is terminated by an empty line. */
      if (request->state == READING_RESPONSE_CONTENT_CHUNK_HEADER)
	{
	  int c;
	  while ((c = gsk_buffer_read_char (incoming_data)) != -1)
	    {
	      if (isxdigit (c))
		{
		  request->remaining_data *= 16;
		  request->remaining_data += xdigit_to_number (c);
		}
	      else if (c == '\n')	/* TODO: read spec about which terminal char to use */
		{
		  if (request->remaining_data == 0)
		    {
		      request->state = DONE;
		      DEBUG ("client: got terminal chunk of length 0");
		      gsk_http_client_content_stream_shutdown (request->content_stream);
		    }
		  else
		    {
		      request->state = READING_RESPONSE_CONTENT_CHUNK_DATA;
		      DEBUG ("chunk of data of length %u", request->remaining_data);
		    }
		  break;
		}
	    }
	  if (c == -1)
	    goto done;
	}
      if (request->state == READING_RESPONSE_CONTENT_CHUNK_DATA)
	{
          guint amt = GET_REMAINING_DATA_UINT (request);
	  GskHttpClientContentStream *content_stream = request->content_stream;
	  amt = gsk_http_client_content_stream_xfer (content_stream, incoming_data, amt);
	  request->remaining_data -= amt;
	  if (request->remaining_data == 0)
	    {
	      request->state = READING_RESPONSE_CONTENT_CHUNK_NEWLINE;
	    }
	}
      if (request->state == READING_RESPONSE_CONTENT_CHUNK_NEWLINE)
	{
	  int c;
	  while ((c = gsk_buffer_read_char (incoming_data)) != -1)
	    if (c == '\n')
	      break;
	  if (c == -1)
	    goto done;
	  request->state = READING_RESPONSE_CONTENT_CHUNK_HEADER;
	  goto chunk_start;
	}

    }
done:
  flush_done_requests (client);
  if (client->first_request == NULL
   && TEST_CLIENT_USER_FLAG (client, DEFERRED_SHUTDOWN))
    {
      GError *error = NULL;
      gsk_io_shutdown (GSK_IO (client), &error);
      /* XXX: error handling */
      if (error)
	g_warning ("error obeying deferred shutdown for http-client: %s", error->message);
    }
  return length;
}

#if 0		/* TODO: implement these for efficiency */
static guint
gsk_http_client_raw_read_buffer (GskStream     *stream,
				 GskBuffer     *buffer,
				 GError       **error)
{
  GskHttpClient *client = GSK_HTTP_CLIENT (stream);
  ...
}

static guint
gsk_http_client_raw_write_buffer (GskStream    *stream,
				  GskBuffer     *buffer,
				  GError       **error)
{
  GskHttpClient *client = GSK_HTTP_CLIENT (stream);
  ...
}
#endif

static gboolean
gsk_http_client_shutdown_read  (GskIO      *io,
			        GError    **error)
{
  GskHttpClient *client = GSK_HTTP_CLIENT (io);
  GskHttpClientRequest *request = client->first_request;
  while (request && request->state == DONE)
    request = request->next;
  if (request && request->state == POSTING)
    {
      /* TODO: error handling */
      gsk_io_read_shutdown (request->post_data, NULL);
      request->state = READING_RESPONSE_HEADER_FIRST_LINE;
      request = request->next;
    }

  /* Warn about aborted requests. */
  {
    guint n_aborted = 0;
    while (request != NULL)
      {
	n_aborted++;
	request = request->next;
      }

      /* TODO: error handling */
    if (n_aborted > 0)
      gsk_io_set_error (io, GSK_IO_ERROR_READ,
			GSK_ERROR_END_OF_FILE,
			_("due to transport shutdown, %u requests are being aborted"),
			n_aborted);
  }
  return TRUE;
}

static gboolean   
gsk_http_client_shutdown_write (GskIO      *io,
			        GError    **error)
{
  GskHttpClient *client = GSK_HTTP_CLIENT (io);
  GskHttpClientRequest *request = client->first_request;
  while (request && request->state == DONE)
    request = request->next;
  while (request != NULL
      && request->state >= READING_RESPONSE_HEADER_FIRST_LINE
      && request->state <= READING_RESPONSE_CONTENT_CHUNK_NEWLINE)
    {
      if (request->content_stream != NULL)
        gsk_http_client_content_stream_shutdown (request->content_stream);
      request->state = DONE;
      request = request->next;
    }

  if (gsk_io_get_is_readable (client))
    gsk_io_read_shutdown (GSK_IO (client), NULL);

  flush_done_requests (client);

#if 0   /* XXX */
  while (request != NULL)
    {
      /* TODO: error handling */
      g_warning ("gsk_http_client_shutdown_write causing request %p to abort", request);
      request = request->next;
    }
#endif
  return TRUE;
}

static void
gsk_http_client_finalize (GObject *object)
{
  GskHttpClient *client = GSK_HTTP_CLIENT (object);

  gsk_hook_destruct (GSK_HTTP_CLIENT_HOOK (client));

  while (client->first_request != NULL)
    {
      GskHttpClientRequest *request = client->first_request;
      client->first_request = request->next;
      if (client->first_request == NULL)
	client->last_request = NULL;

      request->next = NULL;		/* unnecesssary */
      gsk_http_client_request_destroy (request);
    }
  gsk_buffer_destruct (&client->incoming_data);

  (*parent_class->finalize) (object);
}

/* --- functions --- */
static void
gsk_http_client_init (GskHttpClient *http_client)
{
  GSK_HOOK_INIT (http_client, GskHttpClient, requestable, 0, 
		 set_poll_requestable, shutdown_requestable);
  GSK_HOOK_SET_FLAG (GSK_HTTP_CLIENT_HOOK (http_client), IS_AVAILABLE);
  gsk_io_mark_is_readable (http_client);
  gsk_io_mark_is_writable (http_client);
  gsk_io_mark_never_blocks_write (http_client);
}

static void
gsk_http_client_class_init (GskHttpClientClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GskIOClass *io_class = GSK_IO_CLASS (class);
  GskStreamClass *stream_class = GSK_STREAM_CLASS (class);
  parent_class = g_type_class_peek_parent (class);
  io_class->set_poll_read = gsk_http_client_set_poll_read;
  io_class->set_poll_write = gsk_http_client_set_poll_write;
  io_class->shutdown_read = gsk_http_client_shutdown_read;
  io_class->shutdown_write = gsk_http_client_shutdown_write;
  stream_class->raw_read = gsk_http_client_raw_read;
  stream_class->raw_write = gsk_http_client_raw_write;
#if 0	/* TODO: implement these! */
  stream_class->raw_read_buffer = gsk_http_client_raw_read_buffer;
  stream_class->raw_write_buffer = gsk_http_client_raw_write_buffer;
#endif
  object_class->finalize = gsk_http_client_finalize;
  GSK_HOOK_CLASS_INIT (object_class, "requestable", GskHttpClient, requestable);
}

GType gsk_http_client_get_type()
{
  static GType http_client_type = 0;
  if (!http_client_type)
    {
      static const GTypeInfo http_client_info =
      {
	sizeof(GskHttpClientClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_http_client_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskHttpClient),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_http_client_init,
	NULL		/* value_table */
      };
      http_client_type = g_type_register_static (GSK_TYPE_STREAM,
                                                  "GskHttpClient",
						  &http_client_info, 0);
    }
  return http_client_type;
}

/* --- public methods --- */

/**
 * gsk_http_client_new:
 *
 * Create a new HTTP client protocol stream.
 * Note that usually you want to hook this up to
 * a transport layer, typically from gsk_stream_new_connecting().
 *
 * returns: the new client protocol stream.
 */
GskHttpClient *
gsk_http_client_new (void)
{
  return g_object_new (GSK_TYPE_HTTP_CLIENT, NULL);
}

/**
 * gsk_http_client_notify_fast:
 * @client: the http client to affect.
 * @is_fast: whether to try to get requests without waiting for responses.
 *
 * Set whether the client should allow and want multiple outgoing requests 
 * before a single response has been received.
 */
void
gsk_http_client_notify_fast (GskHttpClient     *client,
			     gboolean           is_fast)
{
  GSK_HTTP_CLIENT_HOOK (client)->user_flags |= GSK_HTTP_CLIENT_FAST_NOTIFY;
}

/**
 * gsk_http_client_request:
 * @client: the http client to transmit the outgoing request.
 * @request: a request which should be sent from the client.
 * @post_data: for PUT and POST requests, a stream of data to output.
 * @handle_response: function to call once an HTTP response header is received.
 * @hook_data: data to pass to @handle_response.
 * @hook_destroy: method to call when the response is called or the request
 *    is aborted.
 *
 * Queue or send a HTTP client request.
 * The @handle_response function will be called once the response
 * header is available, and the content data will be available as
 * a #GskStream.
 */
void
gsk_http_client_request  (GskHttpClient         *client,
			  GskHttpRequest        *http_request,
			  GskStream             *post_data,
			  GskHttpClientResponse  handle_response,
			  gpointer               hook_data,
			  GDestroyNotify         hook_destroy)
{
  GskHttpClientRequest *request;

  /* you must not issue another request after one which
     uses EOF to mark the end-of-request cycle:
     that request can never by processed. */
  g_return_if_fail (!TEST_CLIENT_USER_FLAG (client, REQUIRES_READ_SHUTDOWN));

  /* you must not issue another request after calling
     gsk_http_client_shutdown_when_done() */
  g_return_if_fail (!TEST_CLIENT_USER_FLAG (client, DEFERRED_SHUTDOWN));

  request = gsk_http_client_request_alloc ();
  request->client = client;
  request->request = g_object_ref (http_request);
  request->post_data = post_data ? g_object_ref (post_data) : NULL;
  gsk_buffer_construct (&request->outgoing);
  request->handle_response = handle_response;
  request->handle_response_data = hook_data;
  request->handle_response_destroy = hook_destroy;
  request->response = NULL;
  request->content_stream = NULL;
  request->response_command_table = NULL;
  request->state = INIT;
  request->remaining_data = 0;
  request->next = NULL;

  if (client->last_request)
    client->last_request->next = request;
  else
    client->first_request = request;
  client->last_request = request;
  if (client->outgoing_request == NULL)
    client->outgoing_request = request;

  DEBUG ("gsk_http_client_request: path=%s", http_request->path);

  if (post_data != NULL
   && GSK_HTTP_HEADER (http_request)->content_length < 0
   && GSK_HTTP_HEADER (http_request)->transfer_encoding_type == GSK_HTTP_TRANSFER_ENCODING_NONE)
    GSK_HTTP_CLIENT_HOOK (client)->user_flags |= GSK_HTTP_CLIENT_REQUIRES_READ_SHUTDOWN;


  gsk_io_mark_idle_notify_read (client);
}

/**
 * gsk_http_client_shutdown_when_done:
 * @client: the http client shut down.
 *
 * Set the client to shutdown after the current requests are done.
 */
void
gsk_http_client_shutdown_when_done (GskHttpClient *client)
{
  GSK_HTTP_CLIENT_HOOK (client)->user_flags |= GSK_HTTP_CLIENT_DEFERRED_SHUTDOWN;
  if (client->first_request == NULL)
    {
      GError *error = NULL;
      gsk_io_shutdown (GSK_IO (client), &error);

      /* TODO: error handling */
      if (error)
	{
	  g_warning ("error shutting down http-client: %s", error->message);
	  g_error_free (error);
	}
    }
}

/**
 * gsk_http_client_propagate_content_read_shutdown:
 * @client: the http client to affect.
 *
 * Propagate shutdowns of the content-stream to the http-client.
 */
void
gsk_http_client_propagate_content_read_shutdown (GskHttpClient *client)
{
  GSK_HTTP_CLIENT_HOOK (client)->user_flags |= GSK_HTTP_CLIENT_PROPAGATE_CONTENT_READ_SHUTDOWN;
}

static void
gsk_http_client_request_destroy (GskHttpClientRequest *request)
{
  if (request->request)
    g_object_unref (request->request);
  if (request->post_data)
    g_object_unref (request->post_data);
  gsk_buffer_destruct (&request->outgoing);
  if (request->handle_response_destroy)
    request->handle_response_destroy (request->handle_response_data);
  if (request->response)
    g_object_unref (request->response);
  if (request->content_stream)
    {
      gsk_http_client_content_stream_shutdown (request->content_stream);
      g_object_unref (request->content_stream);
    }
  gsk_http_client_request_free (request);
}

/* === Implementation of the content-stream === */
typedef struct _GskHttpClientContentStreamClass GskHttpClientContentStreamClass;

GType gsk_http_client_content_stream_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_HTTP_CLIENT_CONTENT_STREAM			(gsk_http_client_content_stream_get_type ())
#define GSK_HTTP_CLIENT_CONTENT_STREAM(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_HTTP_CLIENT_CONTENT_STREAM, GskHttpClientContentStream))
#define GSK_HTTP_CLIENT_CONTENT_STREAM_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_HTTP_CLIENT_CONTENT_STREAM, GskHttpClientContentStreamClass))
#define GSK_HTTP_CLIENT_CONTENT_STREAM_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_HTTP_CLIENT_CONTENT_STREAM, GskHttpClientContentStreamClass))
#define GSK_IS_HTTP_CLIENT_CONTENT_STREAM(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_HTTP_CLIENT_CONTENT_STREAM))
#define GSK_IS_HTTP_CLIENT_CONTENT_STREAM_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_HTTP_CLIENT_CONTENT_STREAM))

struct _GskHttpClientContentStreamClass 
{
  GskStreamClass stream_class;
};
struct _GskHttpClientContentStream 
{
  GskStream      stream;
  GskBuffer      buffer;
  GskHttpClient *http_client;
  guint          has_shutdown : 1;
  guint          has_client_write_block : 1;
};
/* --- prototypes --- */
static GObjectClass *content_stream_parent_class = NULL;

/* propagating write-blockage up the chain */
static inline void
gsk_http_client_content_stream_mark_client_write_block (GskHttpClientContentStream *stream)
{
  if (!stream->has_client_write_block)
    {
      stream->has_client_write_block = 1;
      if (stream->http_client)
	gsk_io_block_write (stream->http_client);
    }
}

static inline void
gsk_http_client_content_stream_clear_client_write_block (GskHttpClientContentStream *stream)
{
  if (stream->has_client_write_block)
    {
      stream->has_client_write_block = 0;
      if (stream->http_client)
	gsk_io_unblock_write (stream->http_client);
    }
}

/* io implementation */
static gboolean
gsk_http_client_content_stream_shutdown_read   (GskIO      *io,
						GError    **error)
{
  GskHttpClientContentStream *content_stream = GSK_HTTP_CLIENT_CONTENT_STREAM  (io);
  /* XXX: why do we need to do this? */
  if (content_stream->http_client != NULL
   && (content_stream->http_client->last_request == NULL
    || content_stream->http_client->last_request->content_stream == content_stream)
   &&    (TEST_CLIENT_USER_FLAG (content_stream->http_client, PROPAGATE_CONTENT_READ_SHUTDOWN)
       || TEST_CLIENT_USER_FLAGS (content_stream->http_client,
                              GSK_HTTP_CLIENT_DEFERRED_SHUTDOWN
                            | GSK_HTTP_CLIENT_REQUIRES_READ_SHUTDOWN)))
    {
      /* this should be true, since gsk_http_client_request()
         does not allow additional requests after DEFERRED_SHUTDOWN
         or REQUIRES_READ_SHUTDOWN. */
      g_assert (content_stream->http_client->last_request == NULL
             || content_stream->http_client->last_request->next == NULL);
      gsk_io_notify_shutdown (GSK_IO (content_stream->http_client));
    }
  gsk_http_client_content_stream_clear_client_write_block (content_stream);
  return TRUE;
}

/* stream implementation */
static inline void
gsk_http_client_content_stream_read_fixups (GskHttpClientContentStream *content_stream)
{
  if (content_stream->buffer.size == 0)
    gsk_io_clear_idle_notify_read (content_stream);
  else
    gsk_io_mark_idle_notify_read (content_stream);
  if (content_stream->buffer.size < GSK_HTTP_CLIENT_CONTENT_STREAM_MAX_BUFFER
   && content_stream->http_client != NULL)
    {
      gsk_http_client_content_stream_clear_client_write_block (content_stream);
    }

  /* hmm, might this be too reentrant? */
  if (content_stream->buffer.size == 0
   && content_stream->has_shutdown)
    {
      gsk_io_notify_read_shutdown (content_stream);
    }
}

static guint
gsk_http_client_content_stream_raw_read        (GskStream     *stream,
			 	                gpointer       data,
			 	                guint          length,
			 	                GError       **error)
{
  GskHttpClientContentStream *content_stream = GSK_HTTP_CLIENT_CONTENT_STREAM (stream);
  guint rv = gsk_buffer_read (&content_stream->buffer, data, length);
  DEBUG ("gsk_http_client_content_stream_raw_read: max-length=%u; got=%u [buffer-size=%u] [has shutdown=%u]", length, rv, content_stream->buffer.size, content_stream->has_shutdown);
  gsk_http_client_content_stream_read_fixups (content_stream);
  return rv;
}

static guint
gsk_http_client_content_stream_raw_read_buffer (GskStream     *stream,
			                        GskBuffer     *buffer,
			                        GError       **error)
{
  GskHttpClientContentStream *content_stream = GSK_HTTP_CLIENT_CONTENT_STREAM (stream);
  guint rv = gsk_buffer_drain (buffer, &content_stream->buffer);
  DEBUG ("gsk_http_client_content_stream_raw_read_buffer: got=%u", rv);
  gsk_http_client_content_stream_read_fixups (content_stream);
  return rv;
}

static void
gsk_http_client_content_stream_finalize (GObject *object)
{
  GskHttpClientContentStream *content_stream = GSK_HTTP_CLIENT_CONTENT_STREAM (object);
  g_return_if_fail (content_stream->http_client == NULL);
  gsk_buffer_destruct (&content_stream->buffer);
  content_stream_parent_class->finalize (object);
}

static void
gsk_http_client_content_stream_init (GskHttpClientContentStream *http_client_content_stream)
{
  gsk_io_mark_is_readable (http_client_content_stream);
}

static void
gsk_http_client_content_stream_class_init (GskStreamClass *class)
{
  GskIOClass *io_class = GSK_IO_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  content_stream_parent_class = g_type_class_peek_parent (class);
  io_class->shutdown_read = gsk_http_client_content_stream_shutdown_read;
  class->raw_read = gsk_http_client_content_stream_raw_read;
  class->raw_read_buffer = gsk_http_client_content_stream_raw_read_buffer;
  object_class->finalize = gsk_http_client_content_stream_finalize;
}

GType gsk_http_client_content_stream_get_type()
{
  static GType http_client_content_stream_type = 0;
  if (!http_client_content_stream_type)
    {
      static const GTypeInfo http_client_content_stream_info =
      {
	sizeof(GskHttpClientContentStreamClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_http_client_content_stream_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskHttpClientContentStream),
	4,		/* n_preallocs */
	(GInstanceInitFunc) gsk_http_client_content_stream_init,
	NULL		/* value_table */
      };
      http_client_content_stream_type = g_type_register_static (GSK_TYPE_STREAM,
                                                  "GskHttpClientContentStream",
						  &http_client_content_stream_info, 0);
    }
  return http_client_content_stream_type;
}

/* forward declared methods on GskHttpClientContentStream */
static GskHttpClientContentStream *
gsk_http_client_content_stream_new (GskHttpClient *client)
{
  GskHttpClientContentStream *rv;
  rv = g_object_new (GSK_TYPE_HTTP_CLIENT_CONTENT_STREAM, NULL);
  rv->http_client = client;
  return GSK_HTTP_CLIENT_CONTENT_STREAM (rv);
}

static guint
gsk_http_client_content_stream_xfer     (GskHttpClientContentStream *stream,
				         GskBuffer                  *buffer,
				         gssize                      max_data)
{
  guint amount;
  g_return_val_if_fail (!stream->has_shutdown, 0);
  if (max_data < 0)
    amount = gsk_buffer_drain (&stream->buffer, buffer);
  else
    amount = gsk_buffer_transfer (&stream->buffer, buffer, max_data);
  DEBUG ("gsk_http_client_content_stream_xfer: content-stream size is %u; buffer now size %u [maxdata=%d]", stream->buffer.size, buffer->size, max_data);
  if (stream->buffer.size > 0)
    {
      gsk_io_mark_idle_notify_read (stream);
    }
  if (stream->buffer.size > GSK_HTTP_CLIENT_CONTENT_STREAM_MAX_BUFFER
   && gsk_io_get_is_readable (stream))
    {
      g_return_val_if_fail (stream->http_client != NULL, 0);
      gsk_http_client_content_stream_mark_client_write_block (stream);
    }
  else if (!gsk_io_get_is_readable (stream))
    {
      /* save memory if the data is just to be discarded */
      gsk_buffer_destruct (&stream->buffer);
    }

  return amount;
}

static void
gsk_http_client_content_stream_shutdown (GskHttpClientContentStream *stream)
{
  DEBUG("gsk_http_client_content_stream_shutdown: has_shutdown=%u; buffer.size=%u; idle-notify-read=%u", stream->has_shutdown,stream->buffer.size, gsk_io_get_idle_notify_read(stream));
  if (stream->has_shutdown)
    return;
  stream->has_shutdown = 1;
  gsk_http_client_content_stream_clear_client_write_block (stream);
  stream->http_client = NULL;
  if (stream->buffer.size == 0)
    gsk_io_notify_read_shutdown (stream);
}
