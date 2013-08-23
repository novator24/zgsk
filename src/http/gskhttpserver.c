#include <ctype.h>
#include <string.h>
#include "gskhttpserver.h"
#include "../gskmacros.h"

static GObjectClass *parent_class = NULL;

/* forward declarations for the "POST-stream" */
typedef struct _GskHttpServerPostStream GskHttpServerPostStream;
static GskHttpServerPostStream *
gsk_http_server_post_stream_new (GskHttpServer         *server,
				 gboolean               is_chunked,
				 gint                   length);
static gboolean
gsk_http_server_post_stream_process (GskHttpServerPostStream *post_stream);
static void
gsk_http_server_post_stream_detach (GskHttpServerPostStream *post_stream,
				    gboolean                 is_server_dying);

/* amount of posted data to accumulate in the POST-data stream.
 */
#define MAX_POST_BUFFER		8192

typedef enum
{
  INIT,
  READING_REQUEST_FIRST_LINE,
  READING_REQUEST,
  READING_POST,
  DONE_READING
} ResponseParseState;

struct _GskHttpServerResponse
{
  GskHttpServer *server;
  GHashTable *request_parser_table;

  GskHttpRequest *request;
  GskHttpServerPostStream *post_data;

  ResponseParseState parse_state;

  /* all outgoing data goes here first temporarily */
  GskBuffer      outgoing;
  guint content_received;

  /* members from gsk_http_server_respond */
  GskHttpResponse *response;
  GskStream *content;

  /* have we written all the content out */
  guint is_done_writing : 1;

  /* have we gotten eof from 'content' */
  /* XXX: this flag should be removed: it is never meaningfully used.
          Instead content is set to NULL. */
  guint got_content_eof : 1;

  /* whether the user has queried this response yet */
  guint user_fetched : 1;

  /* whether this response failed for some reason */
  guint failed : 1;
  
  /* number of bytes of content written thus far */
  guint content_written;

  GskHttpServerResponse *next;
};
GSK_DECLARE_POOL_ALLOCATORS(GskHttpServerResponse, gsk_http_server_response, 6)

struct _GskHttpServerPostStreamClass 
{
  GskStreamClass stream_class;
};

struct _GskHttpServerPostStream 
{
  GskStream      stream;
  GskBuffer      buffer;
  GskHttpServer *server;
  guint          blocking_server_write : 1;

  /* for Content-length: handling */
  guint          has_length : 1;

  /* for Transfer-Encoding: chunked */
  guint          is_chunked : 1;
  guint          is_in_chunk_header : 1;

  guint          ended : 1;

  /* for Content-Length/Chunking */
  guint          cur_size;
};

static inline gboolean
gsk_http_server_response_is_done (GskHttpServerResponse *response)
{
  if (response->failed)
    return TRUE;
  return response->parse_state == DONE_READING
      && response->is_done_writing
      && response->outgoing.size == 0
      && response->content == NULL;
}

static inline void
gsk_http_server_response_destroy (GskHttpServerResponse *response,
				  gboolean               is_server_dying)
{
  if (response->request)
    g_object_unref (response->request);
  if (response->post_data)
    {
      gsk_http_server_post_stream_detach (response->post_data, is_server_dying);
      g_object_unref (response->post_data);
    }
  gsk_buffer_destruct (&response->outgoing);

  if (response->response)
    g_object_unref (response->response);
  if (response->content)
    g_object_unref (response->content);
  gsk_http_server_response_free (response);
}

static void gsk_http_server_prune_done_responses (GskHttpServer *server,
                                                  gboolean       may_read_shutdown);
static inline void
gsk_http_server_response_fail (GskHttpServerResponse *response,
			       const char            *explanation)
{
  /* NOTE: someday we're probably going to want a callback here. */ 
  GError *error = response->request
                ? GSK_HTTP_HEADER (response->request)->g_error
                : NULL;
  if (error == NULL)
    error = response->response
          ? GSK_HTTP_HEADER (response->response)->g_error
          : NULL;

#if 0
  if (error)
    g_debug ("gsk_http_server_response_fail: %s: %s", explanation, error->message);
  else
    g_debug ("gsk_http_server_response_fail: %s", explanation);
#endif

  response->failed = 1;
}

static gboolean
handle_content_is_readable (GskStream *content_stream, gpointer data)
{
  GskHttpServer *server = GSK_HTTP_SERVER (data);
  GskHttpServerResponse *trapped_response = server->trapped_response;
  GError *error = NULL;
  gboolean was_empty;
  g_return_val_if_fail (trapped_response != NULL && trapped_response->content == content_stream, FALSE);
  was_empty = trapped_response->outgoing.size == 0;
  if (GSK_HTTP_HEADER (trapped_response->response)->transfer_encoding_type == GSK_HTTP_TRANSFER_ENCODING_CHUNKED)
    {
      /* TODO: temporary buffer pooling?? (so that large reads can be spared a copy...) */
      char buf[4096];
      char len_prefix[64];
      gsize n_read = gsk_stream_read (content_stream, buf, sizeof (buf), &error);
      if (error != NULL)
	goto handle_error;
      if (n_read > 0)
        {
          g_snprintf (len_prefix, sizeof (len_prefix), "%x\r\n", (guint) n_read);
          gsk_buffer_append_string (&trapped_response->outgoing, len_prefix);
          gsk_buffer_append (&trapped_response->outgoing, buf, n_read);
          gsk_buffer_append (&trapped_response->outgoing, "\r\n", 2);
          trapped_response->content_received += n_read;
        }
    }
  else
    {
      trapped_response->content_received += gsk_stream_read_buffer (content_stream, &trapped_response->outgoing, &error);
    }
  if (error != NULL)
    goto handle_error;
  if (was_empty && trapped_response->outgoing.size > 0)
    gsk_io_notify_ready_to_read (server);
  if (trapped_response->outgoing.size > 0)
    gsk_io_mark_idle_notify_read (server);
  return TRUE;

handle_error:
  gsk_io_set_gerror (GSK_IO (server), GSK_IO_ERROR_READ, error);
  server->trapped_response = NULL;
  return FALSE;
}

static gboolean
should_close_after_this_response (GskHttpServerResponse *response)
{
  GskHttpHeader *resp = GSK_HTTP_HEADER (response->response);
  return gsk_http_header_get_connection (resp) == GSK_HTTP_CONNECTION_CLOSE;
}

static gboolean
handle_content_shutdown (GskStream *content_stream, gpointer data)
{
  GskHttpServer *server = GSK_HTTP_SERVER (data);
  GskHttpServerResponse *trapped_response = server->trapped_response;
  gint content_length;
  g_return_val_if_fail (trapped_response != NULL && trapped_response->content == content_stream, FALSE);
  trapped_response->content = NULL;
  server->trapped_response = NULL;
  if (GSK_HTTP_HEADER (trapped_response->response)->transfer_encoding_type == GSK_HTTP_TRANSFER_ENCODING_CHUNKED)
    {
      gboolean was_empty = trapped_response->outgoing.size == 0;
#if BUGGY_SERVER_TRANSFER_ENCODING_CHUNKED
      gsk_buffer_append_string (&trapped_response->outgoing, "0\n");
#else
      /* TODO:  trailer support? (RFC 2616 Section 3.6.1) */
      gsk_buffer_append_string (&trapped_response->outgoing, "0\r\n\r\n");
#endif
      if (was_empty)
	gsk_io_mark_idle_notify_read (server);
    }
  content_length = GSK_HTTP_HEADER (trapped_response->response)->content_length;
  if (content_length >= 0)
    {
      if (trapped_response->content_received != (guint)content_length)
        {
          gsk_io_set_error (GSK_IO (server), GSK_IO_ERROR_READ,
                            GSK_ERROR_INVALID_STATE,
                            "expected %u bytes of data, got %u",
                            content_length,
                            trapped_response->content_received);
          g_object_unref (content_stream);
          return FALSE;
        }
    }
  if (trapped_response->outgoing.size == 0)
    {
      trapped_response->is_done_writing = 1;
      if (should_close_after_this_response (trapped_response))
        {
          gsk_io_notify_read_shutdown (server);
          if (gsk_io_get_is_writable (server))
            gsk_io_write_shutdown (server, NULL);
        }
    }
  gsk_http_server_prune_done_responses (server, TRUE);
  g_object_unref (content_stream);
  return FALSE;
}

static gboolean
handle_keepalive_idle_timeout (gpointer data)
{
  GskHttpServer *server = GSK_HTTP_SERVER (data);
  server->keepalive_idle_timeout = NULL;
  gsk_io_notify_shutdown (GSK_IO (server));
  return FALSE;
}

static inline void
add_keepalive_idle_timeout (GskHttpServer *server)
{
  g_assert (server->keepalive_idle_timeout == NULL);
  g_assert (server->keepalive_idle_timeout_ms >= 0);
  server->keepalive_idle_timeout
    = gsk_main_loop_add_timer (gsk_main_loop_default (),
                               handle_keepalive_idle_timeout, server, NULL,
                               server->keepalive_idle_timeout_ms, -1);
}

static void
gsk_http_server_prune_done_responses (GskHttpServer *server,
                                      gboolean       may_read_shutdown)
{
  GskHttpServerResponse **pthis = &server->first_response;
  GskHttpServerResponse *last = NULL;
  GskHttpServerResponse *at;
  while (*pthis != NULL)
    {
      GskHttpServerResponse *at = *pthis;
      if (gsk_http_server_response_is_done (at))
	{
          if (server->trapped_response == at)
	    {
	      if (at->content != NULL)
		gsk_stream_untrap_readable (at->content);
	      server->trapped_response = NULL;
	    }
	  *pthis = at->next;
	  gsk_http_server_response_destroy (at, FALSE);
	}
      else
	{
	  pthis = &at->next;
	  last = at;
	}
    }
  server->last_response = last;

  for (at = server->first_response; at != NULL; at = at->next)
    {
      if (!at->is_done_writing)
	break;
    }
  
  if ((at == NULL || at->is_done_writing)
   && (server->got_close || !gsk_io_get_is_writable (server)))
    {
      /* The server is no longer readable.
	 This automatically clears the idle-notify flag. */
      if (may_read_shutdown)
        gsk_io_notify_read_shutdown (server);
      else
        gsk_io_set_idle_notify_read (server, TRUE);
      return;
    }

  gsk_io_set_idle_notify_read (server, at != NULL && at->outgoing.size != 0);

  /* if we are blocking on the content-stream for data,
     trap its readability. */
  if (at != NULL && at->outgoing.size == 0 && at->content != NULL
   && server->read_poll && server->trapped_response != at)
    {
      /* Untrap the old stream (if applicable),
         and trap the correct one. */
      if (server->trapped_response != NULL
       && server->trapped_response->content != NULL)
        gsk_stream_untrap_readable (at->content);
      server->trapped_response = at;
      gsk_stream_trap_readable (at->content, handle_content_is_readable,
                                handle_content_shutdown,
                                g_object_ref (server), g_object_unref);
    }

  if (server->first_response == NULL
   && server->keepalive_idle_timeout_ms >= 0
   && server->keepalive_idle_timeout == NULL
   && server->incoming.size == 0)
    {
      add_keepalive_idle_timeout (server);
    }

}

/* --- i/o implementation --- */
static void
gsk_http_server_set_poll_read (GskIO *io, gboolean should_poll)
{
  GSK_HTTP_SERVER (io)->read_poll = should_poll;
}

static gboolean
gsk_http_server_shutdown_read   (GskIO      *io,
				 GError    **error)
{
  GskHttpServer *server = GSK_HTTP_SERVER (io);
  GskHttpServerResponse *at;
  guint n_to_shutdown = 0;
  GskStream **to_shutdown;
  guint i;
  for (at = server->first_response; at != NULL; at = at->next)
    if (!at->is_done_writing)
      {
        gsk_http_server_response_fail (at, "shutdown-read while data is still queued");
        if (at->content != NULL
         && gsk_io_get_is_readable (at->content))
          n_to_shutdown++;
      }
  to_shutdown = g_newa (GskStream *, n_to_shutdown);
  i = 0;
  for (at = server->first_response; at != NULL; at = at->next)
    if (!at->is_done_writing
     && at->content != NULL
     && gsk_io_get_is_readable (at->content))
      to_shutdown[i++] = g_object_ref (at->content);
  g_assert (i == n_to_shutdown);
  for (i = 0; i < n_to_shutdown; i++)
    {
      gsk_io_read_shutdown (to_shutdown[i], NULL);
      g_object_unref (to_shutdown[i]);
    }
  return TRUE;
}

static void
gsk_http_server_set_poll_write (GskIO *io, gboolean should_poll)
{
  GSK_HTTP_SERVER (io)->write_poll = should_poll;
}

static gboolean
gsk_http_server_shutdown_write  (GskIO      *io,
				 GError    **error)
{
  GskHttpServer *server = GSK_HTTP_SERVER (io);
  GskHttpServerResponse *at;
  for (at = server->first_response; at != NULL; at = at->next)
    {
      if (at->parse_state == READING_POST)
        {
          at->parse_state = DONE_READING;
          at->post_data->ended = 1;
          if (at->post_data->buffer.size == 0)
	    gsk_io_notify_read_shutdown (at->post_data);
        }
      else if (at->parse_state != DONE_READING)
        gsk_http_server_response_fail (at, "shutdown when not in done-reading state");
    }

  gsk_http_server_prune_done_responses (server, TRUE);

  gsk_io_read_shutdown (GSK_IO (server), NULL);

  gsk_hook_notify_shutdown (GSK_HTTP_SERVER_HOOK (server));
  return TRUE;
}

static guint
gsk_http_server_raw_read      (GskStream     *stream,
			       gpointer       data,
			       guint          length,
			       GError       **error)
{
  GskHttpServer *server = GSK_HTTP_SERVER (stream);
  GskHttpServerResponse *at;
  guint rv;
  for (at = server->first_response; at != NULL; at = at->next)
    {
      if (!at->is_done_writing)
	break;
      if (at->response == NULL)
	{
	  gsk_io_clear_idle_notify_read (server);
	  return 0;
	}
    }
  if (at == NULL)
    {
      gsk_io_clear_idle_notify_read (server);
      if (server->got_close || !gsk_io_get_is_writable (server))
        gsk_io_notify_read_shutdown (server);
      return 0;
    }

  /* ok, 'at' is a response that may have data which can be read out */
  rv = 0;
  while (at != NULL && at->response != NULL && rv < length)
    {
      if (at->outgoing.size > 0)
	{
	  guint amt  = MIN (length - rv, at->outgoing.size);
	  if (amt)
	    {
	      gsk_buffer_read (&at->outgoing, (char *) data + rv, length - rv);
	      rv += amt;
	    }
	}
      if (at->outgoing.size == 0 && at->content == NULL)
	{
	  /* ok, done with this response */
	  at->is_done_writing = TRUE;
	  if (gsk_http_header_get_connection (GSK_HTTP_HEADER (at->response)) == GSK_HTTP_CONNECTION_CLOSE)
	    {
	      server->got_close = TRUE;
	      break;
	    }
	  at = at->next;
	}
      else
	{
          break;
	}
    }

  gsk_http_server_prune_done_responses (server, rv == 0);

  return rv;
}

static GskHttpServerResponse *
create_new_response (GskHttpServer *server)
{
  GskHttpServerResponse *response = gsk_http_server_response_alloc ();
  response->server = server;
  response->request_parser_table = NULL;
  response->request = NULL;
  response->post_data = NULL;
  response->parse_state = INIT;
  gsk_buffer_construct (&response->outgoing);
  response->content_received = 0;
  response->response = NULL;
  response->content = NULL;
  response->is_done_writing = 0;
  response->got_content_eof = 0;
  response->user_fetched = 0;
  response->content_written = 0;
  response->failed = 0;
  response->next = NULL;

  /* append this response to the queue */
  if (server->last_response)
    server->last_response->next = response;
  else
    server->first_response = response;
  server->last_response = response;

  return response;
}

static void
first_line_parser_callback  (GskHttpServerResponse *response,
			     const char       *text)
{
  GError *error = NULL;
  g_assert (response->request == NULL);
  response->request = gsk_http_request_new_blank ();

  switch (gsk_http_request_parse_first_line (response->request, text, &error))
    {
    case GSK_HTTP_REQUEST_FIRST_LINE_ERROR:
      {
        GskHttpRequest *request = response->request;
        response->request = NULL;
        gsk_io_set_gerror (GSK_IO (response->server), GSK_IO_ERROR_WRITE, error);
        if (request != NULL)
          g_object_unref (request);
      }
      return;

    case GSK_HTTP_REQUEST_FIRST_LINE_SIMPLE:
      response->parse_state = DONE_READING;
      response->request_parser_table = gsk_http_header_get_parser_table (TRUE);
      response->post_data = NULL;
      gsk_hook_notify (GSK_HTTP_SERVER_HOOK (response->server));
      break;

    case GSK_HTTP_REQUEST_FIRST_LINE_FULL:
      response->parse_state = READING_REQUEST;
      response->request_parser_table = gsk_http_header_get_parser_table (TRUE);
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
header_line_parser_callback (GskHttpServerResponse *response,
			     const char            *line)
{
  GskHttpHeaderLineParser *parser;
  char *lowercase;
  const char *colon;
  unsigned i;
  const char *val;
  if (line[0] == 0)
    {
      GskHttpVerb verb = response->request->verb;
      if (verb == GSK_HTTP_VERB_PUT
       || verb == GSK_HTTP_VERB_POST)
	{
	  GskHttpHeader *hdr = GSK_HTTP_HEADER (response->request);
	  gboolean chunked = hdr->transfer_encoding_type == GSK_HTTP_TRANSFER_ENCODING_CHUNKED;
	  gint content_length = hdr->content_length;
	  response->post_data = gsk_http_server_post_stream_new (response->server,
							         chunked,
							         content_length);
	  response->parse_state = READING_POST;
	}
      else
	{
	  response->parse_state = DONE_READING;
	  response->post_data = NULL;
	}
      gsk_hook_notify (GSK_HTTP_SERVER_HOOK (response->server));
      return;
    }

  colon = strchr (line, ':');
  if (colon == NULL)
    {
      g_warning ("no colon in header line");
      return;	/* XXX: error handling! */
    }

  /* lowercase the header */
  lowercase = g_alloca (colon - (char*)line + 1);
  for (i = 0; line[i] != ':'; i++)
    lowercase[i] = g_ascii_tolower (line[i]);
  lowercase[i] = '\0';

  val = colon + 1;
  GSK_SKIP_WHITESPACE (val);
  
  parser = g_hash_table_lookup (response->request_parser_table, lowercase);
  if (parser == NULL)
    {
      /* XXX: error handling */
      gboolean is_nonstandard = (line[0] == 'x' || line[0] == 'X')
                              && line[1] == '-';
      if (!is_nonstandard)
        g_warning ("couldn't handle header line %s", line);
      gsk_http_header_add_misc (GSK_HTTP_HEADER (response->request), lowercase, val);
      return;
    }

  if (! ((*parser->func) (GSK_HTTP_HEADER (response->request), val, parser->data)))
    {
      /* XXX: error handling */
      g_warning ("error parsing header line %s", line);
      return;
    }
}

#define MAX_STACK_ALLOC	4096

static guint
gsk_http_server_raw_write     (GskStream     *stream,
			       gconstpointer  data,
			       guint          length,
			       GError       **error)
{
  GskHttpServer *server = GSK_HTTP_SERVER (stream);
  GskHttpServerResponse *at;
  char stack_buf[MAX_STACK_ALLOC];

  if (length > 0 && server->keepalive_idle_timeout != NULL)
    {
      gsk_source_remove (server->keepalive_idle_timeout);
      server->keepalive_idle_timeout = NULL;
    }

  /* TODO: need a zero-copy strategy */
  gsk_buffer_append (&server->incoming, data, length);

  while (server->incoming.size > 0)
    {
      if (server->last_response != NULL
       && server->last_response->parse_state != DONE_READING)
        at = server->last_response;
      else
        at = create_new_response (server);

      switch (at->parse_state)
        {
        case INIT:
          at->parse_state = READING_REQUEST_FIRST_LINE;
          break;
        case READING_REQUEST_FIRST_LINE:
          {
            int nl = gsk_buffer_index_of (&server->incoming, '\n');
            char *first_line;
            char *free_line = NULL;
            if (nl < 0)
              goto done;
            if (nl > MAX_STACK_ALLOC - 1)
              free_line = first_line = g_malloc (nl + 1);
            else
              first_line = stack_buf;
            gsk_buffer_read (&server->incoming, first_line, nl + 1);
            first_line[nl] = '\0';
            g_strchomp (first_line);
            first_line_parser_callback (at, first_line);
            g_free (free_line);
          }
          break;

        case READING_REQUEST:
          {
            int nl = gsk_buffer_index_of (&server->incoming, '\n');
            char *header_line;
            char *free_line = NULL;
            if (nl < 0)
              goto done;
            if (nl > MAX_STACK_ALLOC - 1)
              free_line = header_line = g_malloc (nl + 1);
            else
              header_line = stack_buf;
            gsk_buffer_read (&server->incoming, header_line, nl + 1);
            header_line[nl] = '\0';
            g_strchomp (header_line);
            header_line_parser_callback (at, header_line);
            g_free (free_line);
          }
          break;
        case READING_POST:
          if (gsk_http_server_post_stream_process (at->post_data))
            at->parse_state = DONE_READING;
          break;
        default:
          goto done;
        }
    }

done:
  return length;
}

static void
gsk_http_server_finalize (GObject *object)
{
  GskHttpServer *server = GSK_HTTP_SERVER (object);
  while (server->first_response)
    {
      GskHttpServerResponse *response = server->first_response;
      server->first_response = response->next;
      gsk_http_server_response_destroy (response, TRUE);
    }
  if (server->keepalive_idle_timeout != NULL)
    {
      gsk_source_remove (server->keepalive_idle_timeout);
      server->keepalive_idle_timeout = NULL;
    }
  gsk_buffer_destruct (&server->incoming);
  gsk_hook_destruct (&server->has_request_hook);
  parent_class->finalize (object);
}

/* --- functions --- */
static void
gsk_http_server_init (GskHttpServer *http_server)
{
  GSK_HOOK_INIT (http_server, GskHttpServer, has_request_hook, 0, 
		 set_poll_request, shutdown_request);
  GSK_HOOK_MARK_FLAG (&http_server->has_request_hook, IS_AVAILABLE);
  http_server->keepalive_idle_timeout_ms = -1;
  gsk_io_mark_is_readable (http_server);
  gsk_io_mark_is_writable (http_server);
  gsk_io_set_idle_notify_write (http_server, TRUE);
}

static void
gsk_http_server_class_init (GskHttpServerClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GskStreamClass *stream_class = GSK_STREAM_CLASS (class);
  GskIOClass *io_class = GSK_IO_CLASS (class);
  parent_class = g_type_class_peek_parent (class);
  stream_class->raw_read = gsk_http_server_raw_read;
  stream_class->raw_write = gsk_http_server_raw_write;
  object_class->finalize = gsk_http_server_finalize;
  io_class->set_poll_read = gsk_http_server_set_poll_read;
  io_class->shutdown_read = gsk_http_server_shutdown_read;
  io_class->set_poll_write = gsk_http_server_set_poll_write;
  io_class->shutdown_write = gsk_http_server_shutdown_write;
  GSK_HOOK_CLASS_INIT (object_class, "request", GskHttpServer, has_request_hook);
}

GType gsk_http_server_get_type()
{
  static GType http_server_type = 0;
  if (!http_server_type)
    {
      static const GTypeInfo http_server_info =
      {
	sizeof(GskHttpServerClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_http_server_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskHttpServer),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_http_server_init,
	NULL		/* value_table */
      };
      http_server_type = g_type_register_static (GSK_TYPE_STREAM,
                                                 "GskHttpServer",
						 &http_server_info, 0);
    }
  return http_server_type;
}

/* === Implementation of Post/Put data streams === */
typedef struct _GskHttpServerPostStreamClass GskHttpServerPostStreamClass;
GType gsk_http_server_post_stream_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_HTTP_SERVER_POST_STREAM			(gsk_http_server_post_stream_get_type ())
#define GSK_HTTP_SERVER_POST_STREAM(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_HTTP_SERVER_POST_STREAM, GskHttpServerPostStream))
#define GSK_HTTP_SERVER_POST_STREAM_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_HTTP_SERVER_POST_STREAM, GskHttpServerPostStreamClass))
#define GSK_HTTP_SERVER_POST_STREAM_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_HTTP_SERVER_POST_STREAM, GskHttpServerPostStreamClass))
#define GSK_IS_HTTP_SERVER_POST_STREAM(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_HTTP_SERVER_POST_STREAM))
#define GSK_IS_HTTP_SERVER_POST_STREAM_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_HTTP_SERVER_POST_STREAM))

static GObjectClass *post_stream_parent_class = NULL;

static inline void
check_unblock_server (GskHttpServerPostStream *post_stream)
{
  if (post_stream->server
   && post_stream->blocking_server_write
   && post_stream->buffer.size < MAX_POST_BUFFER)
    {
      post_stream->blocking_server_write = FALSE;
      gsk_io_unblock_write (post_stream->server);
    }
  if (post_stream->buffer.size == 0)
    {
      if (post_stream->ended)
	gsk_io_notify_read_shutdown (post_stream);
      else
	gsk_io_clear_idle_notify_read (post_stream);
    }
}

static guint
gsk_http_server_post_stream_raw_read        (GskStream     *stream,
					     gpointer       data,
					     guint          length,
					     GError       **error)
{
  GskHttpServerPostStream *post_stream = GSK_HTTP_SERVER_POST_STREAM (stream);
  guint rv = MIN (length, post_stream->buffer.size);
  gsk_buffer_read (&post_stream->buffer, data, rv);
  check_unblock_server (post_stream);
  return rv;
}

static guint
gsk_http_server_post_stream_raw_read_buffer (GskStream     *stream,
					     GskBuffer     *buffer,
					     GError       **error)
{
  GskHttpServerPostStream *post_stream = GSK_HTTP_SERVER_POST_STREAM (stream);
  guint rv = gsk_buffer_drain (buffer, &post_stream->buffer);
  check_unblock_server (post_stream);
  return rv;
}

static void
gsk_http_server_post_stream_finalize (GObject *object)
{
  GskHttpServerPostStream *post_stream = GSK_HTTP_SERVER_POST_STREAM (object);
  gsk_buffer_destruct (&post_stream->buffer);
  post_stream_parent_class->finalize (object);
}

static void
gsk_http_server_post_stream_init (GskHttpServerPostStream *server_post_stream)
{
  gsk_stream_mark_is_readable (server_post_stream);
}

static void
gsk_http_server_post_stream_class_init (GskHttpServerPostStreamClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GskStreamClass *stream_class = GSK_STREAM_CLASS (class);
  post_stream_parent_class = g_type_class_peek_parent (class);
  stream_class->raw_read = gsk_http_server_post_stream_raw_read;
  stream_class->raw_read_buffer = gsk_http_server_post_stream_raw_read_buffer;
  object_class->finalize = gsk_http_server_post_stream_finalize;
}

GType gsk_http_server_post_stream_get_type()
{
  static GType http_server_post_stream_type = 0;
  if (!http_server_post_stream_type)
    {
      static const GTypeInfo http_server_post_stream_info =
      {
	sizeof(GskHttpServerPostStreamClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_http_server_post_stream_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskHttpServerPostStream),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_http_server_post_stream_init,
	NULL		/* value_table */
      };
      http_server_post_stream_type = g_type_register_static (GSK_TYPE_STREAM,
                                                  "GskHttpServerPostStream",
						  &http_server_post_stream_info, 0);
    }
  return http_server_post_stream_type;
}

static GskHttpServerPostStream *
gsk_http_server_post_stream_new (GskHttpServer         *server,
				 gboolean               is_chunked,
				 gint                   length)
{
  GskHttpServerPostStream *rv;
  rv = g_object_new (GSK_TYPE_HTTP_SERVER_POST_STREAM, NULL);
  rv->server = server;
  if (is_chunked)
    { 
      rv->is_chunked = 1;
      rv->is_in_chunk_header = 1;
    }
  else if (length >= 0)
    {
      rv->has_length = 1;
      rv->cur_size = length;
    }
  else
    {
      rv->has_length = 0;
    }
  return rv;
}


/* --- implement post data processing --- */

/* Process data for Transfer-Encoding: chunked.
   Returns whether the stream has ended. */
static inline gboolean
process_chunked (GskHttpServerPostStream *post_stream)
{
  GskBuffer *orig = &post_stream->server->incoming;
  for (;;)
    {
      if (post_stream->is_in_chunk_header)
	{
	  int c = gsk_buffer_read_char (orig);
	  if (c == '\n')
	    {
	      post_stream->is_in_chunk_header = 0;
	      if (post_stream->cur_size == 0)
		return TRUE;
	    }
	  else if ('0' <= c && c <= '9')
	    {
	      post_stream->cur_size <<= 4;
	      post_stream->cur_size += (c - '0');
	      continue;
	    }
	  else if ('a' <= c && c <= 'f')
	    {
	      post_stream->cur_size <<= 4;
	      post_stream->cur_size += (c - 'a' + 10);
	      continue;
	    }
	  else if ('A' <= c && c <= 'F')
	    {
	      post_stream->cur_size <<= 4;
	      post_stream->cur_size += (c - 'A' + 10);
	      continue;
	    }
	  else if (c == -1)
	    break;
	  else
	    continue;
	}
      g_assert (!post_stream->is_in_chunk_header);
      {
	guint xfer = MIN (orig->size, post_stream->cur_size);
	gsk_buffer_transfer (&post_stream->buffer, orig, xfer);
	post_stream->cur_size -= xfer;
	if (post_stream->cur_size == 0)
	  post_stream->is_in_chunk_header = 1;
	else
	  break;
      }
    }
  return FALSE;
}


/* Process data for Transfer-Encoding: identity.
   Returns whether the stream has ended. */
static inline gboolean
process_unencoded (GskHttpServerPostStream *post_stream)
{
  GskBuffer *orig = &post_stream->server->incoming;
  if (post_stream->has_length)
    {
      guint xfer = MIN (orig->size, post_stream->cur_size);
      gsk_buffer_transfer (&post_stream->buffer, orig, xfer);
      post_stream->cur_size -= xfer;
      if (post_stream->cur_size == 0)
	return TRUE;
    }
  else
    gsk_buffer_drain (&post_stream->buffer, orig);

  return FALSE;
}

/* Returns whether the post-data is over */
static gboolean
gsk_http_server_post_stream_process (GskHttpServerPostStream *post_stream)
{
  gboolean ended;
  if (post_stream->is_chunked)
    ended = process_chunked (post_stream);
  else
    ended = process_unencoded (post_stream);
  gsk_io_set_idle_notify_read (GSK_IO (post_stream),
			       post_stream->buffer.size > 0);
  if (post_stream->buffer.size > MAX_POST_BUFFER
   && !post_stream->blocking_server_write
   && post_stream->server != NULL               /* always true, i think */
   && !ended)
    {
      post_stream->blocking_server_write = 1;
      gsk_io_block_write (post_stream->server);
    }
  if (ended)
    {
      post_stream->ended = 1;
      if (post_stream->buffer.size == 0)
	gsk_io_notify_read_shutdown (post_stream);
    }
  return ended;
}

static void
gsk_http_server_post_stream_detach (GskHttpServerPostStream *post_stream,
				    gboolean                 is_server_dying)
{
  if (!is_server_dying && post_stream->blocking_server_write)
    {
      post_stream->blocking_server_write = 0;
      gsk_io_unblock_write (post_stream->server);
    }
  post_stream->server = NULL;
  post_stream->ended = 1;
  if (post_stream->buffer.size == 0)
    gsk_io_notify_read_shutdown (post_stream);
}

/* --- public interface --- */
/**
 * gsk_http_server_new:
 *
 * Allocate a new HTTP server protocol processor.
 * (Note that generally you will need to connect it
 * to an accepted socket)
 *
 * returns: the newly allocated server.
 */
GskHttpServer *
gsk_http_server_new (void)
{
  return g_object_new (GSK_TYPE_HTTP_SERVER, NULL);
}

/**
 * gsk_http_server_get_request:
 * @server: the HTTP server to grab the request from.
 * @request_out: location to store a reference to the HTTP request.
 * @post_data_out: location to store a reference to the HTTP request's POST data,
 * or NULL will be stored if this is not a POST-type request.
 *
 * Grab a client request if available.  Use gsk_http_server_trap()
 * to get notification when a request is available.
 *
 * The corresponding POST data stream must be retrieved at the 
 * same time.
 *
 * returns: whether a request was successfully dequeued.
 */
gboolean
gsk_http_server_get_request (GskHttpServer   *server,
			     GskHttpRequest **request_out,
			     GskStream      **post_data_out)
{
  GskHttpServerResponse *sresponse;
  for (sresponse = server->first_response;
       sresponse != NULL;
       sresponse = sresponse->next)
    {
      if (!sresponse->user_fetched)
	{
	  GskHttpRequest *request = sresponse->request;
	  GskHttpServerPostStream *post_data = sresponse->post_data;
	  *request_out = g_object_ref (request);
	  if (post_data_out)
	    {
	      if (post_data)
		*post_data_out = g_object_ref (post_data);
	      else
		*post_data_out = NULL;
	    }
	  sresponse->user_fetched = 1;
	  return TRUE;
	}
    }
  return FALSE;
}

/**
 * gsk_http_server_respond:
 * @server: the server to write the response to.
 * @request: the request obtained with gsk_http_server_get_request().
 * @response: the response constructed to this request.
 * @content: content data if appropriate to this request.
 *
 * Give a response to a client's request.
 */
void
gsk_http_server_respond     (GskHttpServer   *server,
			     GskHttpRequest  *request,
			     GskHttpResponse *response,
			     GskStream       *content)
{
  GskHttpServerResponse *sresponse;
  g_return_if_fail (content == NULL || !gsk_hook_is_trapped (GSK_IO_READ_HOOK (content)));
  g_return_if_fail (response != NULL);
  for (sresponse = server->first_response;
       sresponse != NULL;
       sresponse = sresponse->next)
    {
      if (sresponse->request == request)
	break;
    }
  g_return_if_fail (sresponse != NULL);
  if (sresponse->response != NULL)
    {
      g_warning ("got multiple responses to request for '%s'", request->path);
      return;
    }
  g_return_if_fail (sresponse->content == NULL);

  if (content != NULL && !GSK_HTTP_HEADER (response)->has_content_type)
    g_warning ("HTTP response has content but no Content-Type header");

  sresponse->response = g_object_ref (response);
  if (content)
    sresponse->content = g_object_ref (content);
  gsk_http_header_to_buffer (GSK_HTTP_HEADER (response), &sresponse->outgoing);
  
  if (!gsk_io_get_idle_notify_read (server))
    {
      for (sresponse = server->first_response;
	   sresponse != NULL;
	   sresponse = sresponse->next)
	{
	  if (!sresponse->is_done_writing)
	    {
	      if (!sresponse->request)
		return;
	      if (sresponse->outgoing.size == 0
		  && sresponse->content != NULL
		  && !sresponse->got_content_eof)
		return;
	      gsk_io_mark_idle_notify_read (server);
	      return;
	    }
	}
    }
}

/* TODO: we should have an idle_time member
   so that we can start the timer from
   when the server went idle, as opposed to from
   the current time. */
void
gsk_http_server_set_idle_timeout (GskHttpServer *server,
                                  gint           millis)
{
  if (millis < 0)
    {
      if (server->keepalive_idle_timeout != NULL)
        {
          gsk_source_remove (server->keepalive_idle_timeout);
          server->keepalive_idle_timeout = NULL;
        }
      server->keepalive_idle_timeout_ms = -1;
    }
  else
    {
      if (server->keepalive_idle_timeout != NULL)
        {
          gsk_source_adjust_timer (server->keepalive_idle_timeout,
                                   millis, -1);
        }
      else if (server->first_response == NULL
            && server->incoming.size == 0)
        {
          add_keepalive_idle_timeout (server);
        }
    }
}

