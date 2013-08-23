#include "../ssl/gskstreamssl.h"
#include "../gskinit.h"
#include "../gskbufferstream.h"
#include "../gskmainloop.h"
#include <string.h>
#include <stdlib.h>

static void
usage()
{
  g_print ("usage: %s --cert=CERT-FILE --key=KEY-FILE\n"
	   "\n"
	   "Run the SSL test code with the given key and certificate\n"
	   "files.\n"
	   , g_get_prgname ());
  exit (1);
}

#if 1
#define SERVER_PASSWORD		"qwerty"
#define CLIENT_PASSWORD		"qwerty"
#else
#define SERVER_PASSWORD		NULL
#define CLIENT_PASSWORD		NULL
#endif

int main(int argc, char **argv)
{
  int i;
  const char *client_cert_file = NULL;
  const char *server_cert_file = NULL;
  const char *client_key_file = NULL;
  const char *server_key_file = NULL;
  const char *srcdir = NULL;
  gsk_init_without_threads (&argc, &argv);
  for (i = 1; i < argc; i++)
    {
      const char *arg = argv[i];
      if (strncmp (arg, "--client-cert=", 14) == 0)
	{
	  client_cert_file = arg + 14;
	  continue;
	}
      if (strncmp (arg, "--server-cert=", 14) == 0)
	{
	  server_cert_file = arg + 14;
	  continue;
	}
      if (strncmp (arg, "--client-key=", 13) == 0)
	{
	  client_key_file = arg + 13;
	  continue;
	}
      if (strncmp (arg, "--server-key=", 13) == 0)
	{
	  server_key_file = arg + 13;
	  continue;
	}
      if (strncmp (arg, "--srcdir=", 9) == 0)
	{
	  srcdir = arg + 9;
	  continue;
	}
      usage ();
    }

  if (server_cert_file == NULL && client_cert_file == NULL)
    {
      if (srcdir == NULL)
	srcdir = g_getenv("srcdir");
      if (srcdir == NULL)
	srcdir = ".";
      client_cert_file = g_strdup_printf ("%s/client.pem", srcdir);
      server_cert_file = g_strdup_printf ("%s/server.pem", srcdir);
      client_key_file = g_strdup_printf ("%s/client-key.pem", srcdir);
      server_key_file = g_strdup_printf ("%s/server-key.pem", srcdir);
    }

  if (server_cert_file == NULL || client_cert_file == NULL)
    {
      g_warning ("missing required parameter");
      usage ();
    }

  if (!g_file_test (server_cert_file, G_FILE_TEST_EXISTS))
    g_error ("server_cert_file file %s does not exist", server_cert_file);
  if (!g_file_test (client_cert_file, G_FILE_TEST_EXISTS))
    g_error ("client_cert_file file %s does not exist", client_cert_file);
  if (!g_file_test (server_key_file, G_FILE_TEST_EXISTS))
    g_error ("server_key_file file %s does not exist", server_key_file);
  if (!g_file_test (client_key_file, G_FILE_TEST_EXISTS))
    g_error ("client_key_file file %s does not exist", client_key_file);

  {
    GskBufferStream *client, *server;
    GskStreamSsl *client_ssl, *server_ssl;
    char buf[256];
    GError *error = NULL;
    server = gsk_buffer_stream_new ();
    client = gsk_buffer_stream_new ();
    server_ssl = GSK_STREAM_SSL (gsk_stream_ssl_new_server (server_cert_file, server_key_file, SERVER_PASSWORD, NULL, &error));
    if (server_ssl == NULL)
      g_error (error->message);
    client_ssl = GSK_STREAM_SSL (gsk_stream_ssl_new_client (client_cert_file, client_key_file, CLIENT_PASSWORD, gsk_stream_ssl_peek_backend (server_ssl), &error));
    if (client_ssl == NULL)
      g_error (error->message);
    if (!gsk_stream_attach_pair (GSK_STREAM (server), GSK_STREAM (server_ssl), NULL)
     || !gsk_stream_attach_pair (GSK_STREAM (client), GSK_STREAM (client_ssl), NULL))
      {
	g_error ("error doing internal attachments");
      }

    gsk_buffer_append_string (gsk_buffer_stream_peek_read_buffer (client), "hi mom");
    gsk_buffer_stream_read_buffer_changed (client);
    while (gsk_buffer_stream_peek_write_buffer (server)->size < 6)
      gsk_main_loop_run (gsk_main_loop_default (), -1, NULL);
    g_assert (gsk_buffer_stream_peek_write_buffer (server)->size == 6);
    gsk_buffer_read (gsk_buffer_stream_peek_write_buffer (server), buf, 6);
    g_assert (memcmp (buf, "hi mom", 6) == 0);
    gsk_buffer_stream_write_buffer_changed (server);
    gsk_io_shutdown (GSK_IO (client), NULL);
    while (gsk_io_get_is_writable (server) || gsk_io_get_is_readable (server))
      gsk_main_loop_run (gsk_main_loop_default (), -1, NULL);
    g_object_unref (server);
    g_object_unref (client);
    g_object_unref (server_ssl);
    g_object_unref (client_ssl);
  }

  return 0;
}
