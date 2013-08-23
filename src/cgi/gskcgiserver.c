#include "gskcgiserver.h"

static GObjectClass *parent_class = NULL;

/* --- HTTP server handlers --- */
static gboolean
handle_http_server_ready (GskHttpServer *server)
{
  GskCgiServer *cgi_server = GSK_CGI_SERVER (server);
  GskHttpRequest *request = NULL;
  GskStream *post_data = NULL;
  while (gsk_http_server_get_request (server, &request, &post_data))
    {
      /* What we can handle:
       *    (1) GET methods (with and without query tags)
       *    (2) POST methods (for any non-multipart content-type)
       *    (3) POST methods with multipart post data
       */
      if (request->verb == GSK_HTTP_VERB_GET)
	{
	  /* If query string is NULL, return a generic file request. */
	  if (request->url->query == NULL)
	    return handle_generic_file_request (server, request);
	  ...
	}
      else if (request->verb == GSK_HTTP_VERB_POST)
	{
	  ...
	}
      else
	{
	  (*GSK_CGI_SERVER_CLASS (cgi_server)->handle_illformed_request)
	    (cgi_server, "bad verb in HTTP request");
	  return FALSE;
	}
    }
  return TRUE;
}

static void
maybe_emit_cgi_shutdown (GskCgiServer *cgi_server)
{
  if (GSK_HOOK_TEST_IS_AVAILABLE (GSK_CGI_SERVER_HOOK (cgi_server)))
    {
      if (cgi_server->waiting_requests == NULL)
	gsk_hook_shutdown (GSK_CGI_SERVER_HOOK (cgi_server));
      else
	cgi_server->shutdown_pending = TRUE;
    }
}

static gboolean
handle_http_server_shutdown (GskHttpServer *server)
{
  GskCgiServer *cgi_server = GSK_CGI_SERVER (server);
  maybe_emit_cgi_shutdown (cgi_server);
  return TRUE;
}

/* --- GObject methods --- */
static void
gsk_cgi_server_finalize (GObject *object)
{
  GskCgiServer *cgi_server = GSK_CGI_SERVER (object);
  g_list_foreach (cgi_server->pending_requests->head, (GFunc) g_object_unref, NULL);
  g_list_foreach (cgi_server->waiting_requests->head, (GFunc) g_object_unref, NULL);
  g_queue_free (cgi_server->pending_requests);
  g_queue_free (cgi_server->waiting_requests);
  if (cgi_server->decoder != NULL)
    g_object_unref (cgi_server->decoder);
  (*parent_class->finalize) (object);
}

static void
gsk_cgi_server_init (GskCgiServer *cgi_server)
{
  gsk_http_server_trap (GSK_HTTP_SERVER (cgi_server),
			handle_http_server_ready,
			handle_http_server_shutdown,
			NULL,
			NULL);
}

static void
gsk_cgi_server_class_init (GskCgiServerClass *class)
{
  parent_class = g_type_class_peek_parent (class);
}

GType gsk_cgi_server_get_type()
{
  static GType cgi_server_type = 0;
  if (!cgi_server_type)
    {
      static const GTypeInfo cgi_server_info =
      {
	sizeof(GskCgiServerClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_cgi_server_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskCgiServer),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_cgi_server_init,
	NULL		/* value_table */
      };
      cgi_server_type = g_type_register_static (GSK_TYPE_HTTP_SERVER,
						"GskCgiServer",
						&cgi_server_info, 0);
    }
  return cgi_server_type;
}
