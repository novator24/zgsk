#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "../gskstreamlistenersocket.h"
#include "../http/gskhttpserver.h"
#include "../gskdebug.h"
#include "../gskinit.h"
#include "../gskmain.h"
#include "../gskmemory.h"
#include "../gskstreamfd.h"

typedef struct _PathInfo PathInfo;
struct _PathInfo
{
  char *url_prefix;
  char *local_path_prefix;
};

static GArray *path_info = NULL;

static gboolean
handle_request (GskHttpServer *server)
{
  GskHttpRequest *request;
  GskStream *posted_data;
  GError *error = NULL;
  GskStream *stream = NULL;
  GskHttpResponse *response = NULL;
  guint i;
  if (!gsk_http_server_get_request (server, &request, &posted_data))
    return TRUE;
  if (request->verb != GSK_HTTP_VERB_GET && request->verb != GSK_HTTP_VERB_HEAD)
    {
      stream = gsk_memory_source_static_string (
		  "<html><head><title>Server Error</title></head>\n"
		  "<body>\n"
		  "  No handling for the given " "request type could be found...\n"
		  "</body></html>\n");
      response = gsk_http_response_from_request (request, GSK_HTTP_STATUS_NOT_IMPLEMENTED, -1);
      goto respond;
    }

  /* this is pretty slow for a real server.
   * but this is test program, not a real server.
   */
  for (i = 0; i < path_info->len; i++)
    {
      PathInfo *pinfo = &g_array_index (path_info, PathInfo, i);
      guint url_prefix_len = strlen (pinfo->url_prefix);
      if (strncmp (pinfo->url_prefix, request->path, url_prefix_len) == 0)
	break;
    }

  if (i < path_info->len)
    {
      PathInfo *pinfo = &g_array_index (path_info, PathInfo, i);
      guint url_prefix_len = strlen (pinfo->url_prefix);
      char *rel_path = request->path + url_prefix_len;
      gboolean slash_start = (*rel_path == G_DIR_SEPARATOR);
      char *filename = g_strdup_printf ("%s%c%s", pinfo->local_path_prefix,
				        G_DIR_SEPARATOR,
					rel_path + (slash_start ? 1 : 0));
      stream = gsk_stream_fd_new_read_file (filename, &error);
      g_free (filename);
    }
  if (stream == NULL)
    {
      if (request->verb == GSK_HTTP_VERB_GET)
	stream = gsk_memory_source_static_string (
		    "<html><head><title>File not found</title></head>\n"
		    "<body>\n"
		    "  File could not be located.  Please check url and retry.\n"
		    "</body></html>\n");
      response = gsk_http_response_from_request (request, GSK_HTTP_STATUS_NOT_FOUND, -1);
      goto respond;
    }
  {
    int fd = GSK_STREAM_FD_GET_FD (stream);
    struct stat sbuf;
    if (fstat (fd, &sbuf) < 0)
      {
	g_object_unref (stream);
	if (request->verb == GSK_HTTP_VERB_GET)
	  stream = gsk_memory_source_static_string (
		      "<html><head><title>Error accessing file</title></head>\n"
		      "<body>\n"
		      "  File could not be stat'd.  Please check url and retry.\n"
		      "</body></html>\n");
	else
	  stream = NULL;
	response = gsk_http_response_from_request (request, GSK_HTTP_STATUS_NOT_FOUND, -1);
	goto respond;
      }
    response = gsk_http_response_from_request (request, GSK_HTTP_STATUS_OK, sbuf.st_size);
    gsk_http_response_set_content_type (response, "text");
    gsk_http_response_set_content_subtype (response, "plain");
  }
  if (request->verb == GSK_HTTP_VERB_HEAD)
    {
      g_object_unref (stream);
      stream = NULL;
    }

respond:
  gsk_http_server_respond (server, request, response, stream);
  g_object_unref (response);
  if (stream)
    g_object_unref (stream);
  g_object_unref (request);
  if (posted_data)
    g_object_unref (posted_data);
  return TRUE;
}

static gboolean
handle_on_accept (GskStream         *stream,
                  gpointer           data,
                  GError           **error)
{
  GError *e = NULL;
  GskHttpServer *http_server;
  g_assert (data == NULL);
  http_server = gsk_http_server_new ();
  gsk_http_server_trap (http_server,
			handle_request,
			NULL,		/* default shutdown handling */
			NULL,		/* no user-data or destroy function */
			NULL);
  gsk_stream_attach (stream, GSK_STREAM (http_server), &e);
  gsk_stream_attach (GSK_STREAM (http_server), stream, &e);
  if (e != NULL)
    g_error ("gsk_stream_attach: %s", e->message);
  g_object_unref (stream);
  g_object_unref (http_server);
  return TRUE;
}


static void
handle_errors (GError     *error,
               gpointer    data)
{
  g_error ("error accepting new socket: %s", error->message);
}

static void
usage (const char *err)
{
  if (err)
    g_warning ("usage error: %s", err);
  g_print ("usage: %s [--location URL PATH]... PORT\n"
	   "\n"
	   "Run a static content http server on PORT with the urls under URL\n"
	   "mapped to directories under PATH.\n", g_get_prgname ());
  exit (1);
}

int main (int argc, char **argv)
{
  GskStreamListener *listener;
  GskSocketAddress *bind_addr;
  GError *error = NULL;
  int i;
  int port = 0;

  gsk_init_without_threads (&argc, &argv);
  path_info = g_array_new (FALSE, FALSE, sizeof (PathInfo));

  for (i = 1; i < argc; i++)
    {
      if (strcmp (argv[i], "--location") == 0)
	{
	  PathInfo info;
	  if (i + 2 >= argc)
	    usage ("--location requires two arguments url-prefix and local-location");
	  info.url_prefix = argv[++i];
	  info.local_path_prefix = argv[++i];
	  g_array_append_val (path_info, info);
	}
      else
	{
	  if (port != 0)
	    usage ("only one port number is allowed");
	  if (!isdigit (argv[i][0]))
	    usage ("expected port number");
	  port = atoi (argv[i]);
	  if (port == 0)
	    usage ("port must be nonzero");
	}
    }

  if (port == 0)
    usage ("must specify PORT");

  bind_addr = gsk_socket_address_ipv4_localhost (port);
  listener = gsk_stream_listener_socket_new_bind (bind_addr, &error);
  if (error != NULL)
    g_error ("gsk_stream_listener_tcp_unix failed: %s", error->message);
  g_assert (listener != NULL);

  gsk_stream_listener_handle_accept (listener,
                                     handle_on_accept,
                                     handle_errors,
                                     NULL,              /* data */
                                     NULL);             /* destroy */

  return gsk_main_run ();
}
