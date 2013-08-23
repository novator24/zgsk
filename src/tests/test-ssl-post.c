#include "../http/gskhttpclient.h"
#include "../ssl/gskstreamssl.h"
#include "../gsksocketaddress.h"
#include "../gskstreamclient.h"
#include "../gskmemory.h"
#include "../gsknameresolver.h"
#include "../gskmain.h"

static const char *host;
static const char *path;
static guint port = 443;
static const char *post_data_filename;

static void
print_header_line (const char *text, gpointer data)
{
  g_print ("%s\n", text);
}

static gboolean
handle_input_readable (GskStream *input, gpointer data)
{
  char buf[256];
  GError *error = NULL;
  guint nread = gsk_stream_read (input, buf, sizeof(buf), &error);
  if (error)
    g_error ("error reading from stream: %s", error->message);
  g_message ("got '%.*s'", nread,buf);
  return TRUE;
}

static gboolean
handle_input_shutdown (GskStream *input, gpointer data)
{
  g_message("handle_input_shutdown");
  g_object_unref (input);
  return FALSE;
}
static void
handle_input_destroy (gpointer data)
{
  g_message("handle_input_destroy");
}


static void
handle_response (GskHttpRequest  *request,
                 GskHttpResponse *response,
                 GskStream       *input,
                 gpointer         hook_data)
{
  gsk_http_header_print (GSK_HTTP_HEADER (response),
                         print_header_line, NULL);
  g_message("gsk_io_trap_readable");
  g_object_ref (input);
  gsk_io_trap_readable (GSK_IO (input), handle_input_readable, handle_input_shutdown, NULL, handle_input_destroy);
}

static void
handle_name_lookup_succeeded (GskSocketAddress *address, gpointer func_data)
{
  GskSocketAddress *addr = gsk_socket_address_ipv4_new (GSK_SOCKET_ADDRESS_IPV4 (address)->ip_address, port);
  GError *error = NULL;
  GskStream *client = gsk_stream_new_connecting (addr, &error);
  GskHttpClient *http_client;
  GskHttpRequest *http_request;
  GskStream *ssl;
  char *post_data;
  gsize post_data_len;
  GskStream *post_stream;
  if (client == NULL)
    g_error ("error connecting to %s: %s", gsk_socket_address_to_string (addr), error->message);
  ssl = gsk_stream_ssl_new_client (NULL, NULL, NULL, client, &error);
  if (ssl == NULL)
    g_error ("error creating SSL stream: %s", error->message);
  http_client = gsk_http_client_new ();
  if (!gsk_stream_attach_pair (GSK_STREAM (http_client), GSK_STREAM (ssl), &error))
    g_error ("gsk_stream_attach_path: %s", error->message);

  if (!g_file_get_contents (post_data_filename, &post_data, &post_data_len, &error))
    g_error ("error reading post-data");
  post_stream = gsk_memory_slab_source_new (post_data, post_data_len, g_free, post_data);
  http_request = gsk_http_request_new (GSK_HTTP_VERB_POST, path);
  gsk_http_client_request (http_client, http_request, post_stream,
                           handle_response, NULL, NULL);
}

static void
handle_name_lookup_failed (GError *error, gpointer func_data)
{
  g_error ("name lookup for %s failed: %s",host, error->message);
}

int main (int argc, char **argv)
{
  gsk_init_without_threads (&argc, &argv);
  if (argc != 4)
    g_error("usage: %s HOST PATH POSTDATA", argv[0]);


  host = argv[1];
  path = argv[2];
  post_data_filename = argv[3];

  gsk_name_resolver_lookup (GSK_NAME_RESOLVER_FAMILY_IPV4,
                            host,
                            handle_name_lookup_succeeded,
                            handle_name_lookup_failed,
                            NULL, NULL);
  return gsk_main_run();
}
