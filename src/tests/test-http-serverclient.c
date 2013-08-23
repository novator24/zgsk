#include "../http/gskhttpserver.h"
#include "../http/gskhttpclient.h"
#include "../gskmainloop.h"
#include "../gskmemory.h"
#include "../gskinit.h"
#include <string.h>

static GskHttpClient *client = NULL;
static GskHttpServer *server = NULL;

static GskHttpRequest *server_request = NULL;
static gboolean        had_post_content;
static GskBuffer       post_content_buffer = GSK_BUFFER_STATIC_INIT;
static gboolean        server_got_request = FALSE;

static GskHttpResponse *client_response = NULL;
static GskBuffer       client_content = GSK_BUFFER_STATIC_INIT;
static gboolean        has_response_content;
static gboolean        client_got_response = FALSE;

#if 0
#define DEBUG g_message
#else
#define DEBUG(args...)
#endif

static gboolean
handle_post_content_readable (GskStream *stream, gpointer data)
{
  g_assert (data == NULL);
  gsk_stream_read_buffer (stream, &post_content_buffer, NULL);
  return TRUE;
}
static gboolean
handle_post_content_shutdown (GskStream *stream, gpointer data)
{
  g_assert (data == NULL);
  server_got_request = TRUE;
  return FALSE;
}

static gboolean
handle_server_trap (GskHttpServer *server,
		    gpointer       data)
{
  GskStream *server_post_content = NULL;
  DEBUG ("handle_server_trap");
  g_assert (data == NULL);
  g_assert (!server_got_request);
  g_assert (server_request == NULL);
  g_assert (gsk_http_server_get_request (server, &server_request, &server_post_content));
  had_post_content = (server_post_content != NULL);
  gsk_buffer_destruct (&post_content_buffer);

  if (!had_post_content)
    server_got_request = TRUE;
  else
    {
      gsk_stream_trap_readable (server_post_content, handle_post_content_readable, handle_post_content_shutdown, NULL, NULL);
      g_object_unref (server_post_content);
    }

  DEBUG ("handle_post_content_shutdown: server_got_request = %d", server_got_request);

  return TRUE;
}
static gboolean
handle_server_shutdown (GskHttpServer *server,
		        gpointer       data)
{
  g_assert (data == NULL);
  g_warning ("server shutdown");
  return FALSE;
}

static gboolean
client_handle_content_body (GskStream *stream, gpointer data)
{
  DEBUG("client_handle_content_body");
  gsk_stream_read_buffer (stream, &client_content, NULL);
  return TRUE;
}

static gboolean
client_handle_content_shutdown (GskStream *stream, gpointer data)
{
  DEBUG("client_handle_content_shutdown");
  client_got_response = TRUE;
  return FALSE;
}

static void
client_handle_server_response (GskHttpRequest  *request,
			       GskHttpResponse *response,
			       GskStream       *input,
			       gpointer         hook_data)
{
  g_assert (hook_data == NULL);
  g_assert (!client_got_response);
  client_response = g_object_ref (response);
  has_response_content = (input != NULL);
  if (has_response_content)
    {
      gsk_stream_trap_readable (g_object_ref (input),
				client_handle_content_body,
				client_handle_content_shutdown,
				input,
				g_object_unref);
    }
  else
    {
      client_got_response = TRUE;
    }
}

static void
reset_transaction ()
{
  if (server_request)
    {
      g_object_unref (server_request);
      server_request = NULL;
    }
  if (client_response)
    {
      g_object_unref (client_response);
      client_response = NULL;
    }
  gsk_buffer_destruct (&client_content);
  gsk_buffer_destruct (&post_content_buffer);
  client_got_response = FALSE;
  server_got_request = FALSE;
}

static void
new_client_server ()
{
  client = gsk_http_client_new ();
  server = gsk_http_server_new ();
  gsk_stream_attach_pair (GSK_STREAM (client), GSK_STREAM (server), NULL);
  gsk_http_server_trap (server, handle_server_trap, handle_server_shutdown, NULL, NULL);
}

static void
clear_client_server ()
{
  gsk_http_server_untrap (server);
  gsk_io_shutdown (GSK_IO (client), NULL);
  gsk_io_shutdown (GSK_IO (server), NULL);
  g_object_unref (client);
  g_object_unref (server);
  client = NULL;
  server = NULL;
}

int main(int argc, char **argv)
{
  GskHttpRequest *client_request;
  GskHttpResponse *response;
  GskStream *content;
  GskMainLoop *loop;
  int pass, i;
  gsk_init_without_threads (&argc,&argv);
  loop = gsk_main_loop_default ();

  new_client_server ();

  /* Do a trivial GET and successful response. */
  for (pass = 0; pass < 2; pass++)
    {
      for (i = 0; i < 5; i++)
	{
	  char buf[256];
	  g_printerr ("GET and response using %s...[iter %d] ",
		   pass == 0 ? "Transfer-Encoding: chunked" : "Content-Length",
		   i);

	  client_request = gsk_http_request_new (GSK_HTTP_VERB_GET, "/");
	  gsk_http_client_request (client, client_request, NULL, client_handle_server_response, NULL, NULL);
	  g_object_unref (client_request);
	  while (!server_got_request)
	    gsk_main_loop_run (loop, -1, NULL);
	  DEBUG ("done server_got_request loop");
	  g_assert (!had_post_content);
	  g_assert (post_content_buffer.size == 0);
	  g_assert (server_request->verb == GSK_HTTP_VERB_GET);
	  g_assert (strcmp (server_request->path, "/") == 0);
	  g_snprintf (buf, sizeof (buf), "hi mom %d", i);
	  response = gsk_http_response_from_request (server_request,
						     GSK_HTTP_STATUS_OK,
						     pass == 0 ? -1 : 8);
	  gsk_http_response_set_content_type (response, "text");
	  gsk_http_response_set_content_subtype (response, "plain");
	  if (pass == 0)
	    g_assert (GSK_HTTP_HEADER (response)->transfer_encoding_type == GSK_HTTP_TRANSFER_ENCODING_CHUNKED);
	  else
	    g_assert (GSK_HTTP_HEADER (response)->transfer_encoding_type == GSK_HTTP_TRANSFER_ENCODING_NONE);
	  content = gsk_memory_source_new_printf ("%s", buf);
	  gsk_http_server_respond (server, server_request, response, content);
	  g_object_unref (content);
	  g_object_unref (response);
	  content = NULL;
	  response = NULL;
	  while (!client_got_response)
	    gsk_main_loop_run (loop, -1, NULL);
	  g_assert (client_content.size == 8);
	  {
	    char buf[8]; gsk_buffer_read (&client_content, buf, 8);
	    g_assert (memcmp (buf, "hi mom ", 7) == 0);
	    g_assert (buf[7] == '0' + i);
	  }
	  g_assert (client_response->status_code == GSK_HTTP_STATUS_OK);
	  reset_transaction ();
	  g_printerr ("Ok\n");
	}
    }

  clear_client_server ();

  /* Test one-shot connections.  This can happen if:
   * pass=0: an HTTP 1.0 client is detected.
   * pass=1: the server deliberately sets the connection-type to close.
   */
  for (pass = 0; pass < 2; pass++)
    {
      for (i = 0; i < 5; i++)
	{
	  char buf[256];
	  new_client_server ();

	  g_printerr ("GET and response using %s...[iter %d] ",
		   pass == 0 ? "HTTP 1.0" : "server close",
		   i);

	  client_request = gsk_http_request_new (GSK_HTTP_VERB_GET, "/");
	  if (pass == 0)
	    gsk_http_header_set_version (GSK_HTTP_HEADER (client_request), 1, 0);
	  gsk_http_client_request (client, client_request, NULL, client_handle_server_response, NULL, NULL);
	  g_object_unref (client_request);
	  while (!server_got_request)
	    gsk_main_loop_run (loop, -1, NULL);
	  DEBUG ("done server_got_request loop");
	  g_assert (!had_post_content);
	  g_assert (post_content_buffer.size == 0);
	  g_assert (server_request->verb == GSK_HTTP_VERB_GET);
	  g_assert (strcmp (server_request->path, "/") == 0);
	  g_snprintf (buf, sizeof (buf), "hi mom %d", i);
	  response = gsk_http_response_from_request (server_request,
						     GSK_HTTP_STATUS_OK,
						     pass == 0 ? -1 : 8);
	  if (pass == 1)
	    {
	      GSK_HTTP_HEADER (response)->transfer_encoding_type = GSK_HTTP_TRANSFER_ENCODING_NONE;
	      GSK_HTTP_HEADER (response)->connection_type = GSK_HTTP_CONNECTION_CLOSE;
	    }
	  g_assert (GSK_HTTP_HEADER (response)->transfer_encoding_type == GSK_HTTP_TRANSFER_ENCODING_NONE);
	  content = gsk_memory_source_new_printf ("%s", buf);
	  gsk_http_response_set_content_type (response, "text");
	  gsk_http_response_set_content_subtype (response, "plain");
	  gsk_http_server_respond (server, server_request, response, content);
	  g_object_unref (content);
	  g_object_unref (response);
	  content = NULL;
	  response = NULL;
	  while (!client_got_response)
	    gsk_main_loop_run (loop, -1, NULL);
	  g_assert (client_content.size == 8);
	  {
	    char buf[8]; gsk_buffer_read (&client_content, buf, 8);
	    g_assert (memcmp (buf, "hi mom ", 7) == 0);
	    g_assert (buf[7] == '0' + i);
	  }
	  g_assert (client_response->status_code == GSK_HTTP_STATUS_OK);
	  g_assert (gsk_http_client_is_requestable (client));
	  reset_transaction ();
	  g_printerr ("Ok\n");
	  clear_client_server ();
	}
    }

  /* Test HEAD request */
  {
    static GskHttpStatus codes[2] = { 200, 404 };
    new_client_server ();

    g_printerr ("HEAD request and response... ");

    for (i = 0; i < 2; i++)
      {
	client_request = gsk_http_request_new (GSK_HTTP_VERB_HEAD, "/random-url");
	gsk_http_client_request (client, client_request, NULL, client_handle_server_response, NULL, NULL);
	g_object_unref (client_request);
	while (!server_got_request)
	  gsk_main_loop_run (loop, -1, NULL);
	g_assert (!had_post_content);
	g_assert (post_content_buffer.size == 0);
	g_assert (server_request->verb == GSK_HTTP_VERB_HEAD);
	g_assert (strcmp (server_request->path, "/random-url") == 0);
	response = gsk_http_response_from_request (server_request, codes[i], 0);
	gsk_http_server_respond (server, server_request, response, NULL);
	while (!client_got_response)
	  gsk_main_loop_run (loop, -1, NULL);
	g_assert (client_response->status_code == codes[i]);
	g_assert (gsk_http_client_is_requestable (client));
	g_object_unref (response);
	response = NULL;
	reset_transaction ();
	g_printerr (".");
      }
    g_printerr (" Ok.\n");
    clear_client_server ();
  }

  return 0;
}
