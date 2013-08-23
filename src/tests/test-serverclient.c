#include "../gskstreamclient.h"
#include "../gskstreamlistenersocket.h"
#include "../gskmemory.h"
#include "../gskinit.h"
#include <unistd.h>

static GskSocketAddress *addr = 0;
static GskStreamListener *listener = NULL;
static gboolean done_test = FALSE;
static guint client_count = 0;

static gboolean
accept_connection (GskStream    *stream,
		   gpointer      data,
		   GError      **error)
{
  guint8 c8 = client_count;
  guint8 *d = g_memdup (&c8, 1);
  GskStream *source = gsk_memory_slab_source_new (d, 1, g_free, d);
  gsk_stream_attach (source, stream, NULL);
  gsk_io_read_shutdown (stream, NULL);
  g_object_unref (stream);
  return TRUE;
}

static void
handle_listener_error (GError       *err,
		       gpointer      data)
{
  g_assert (GSK_IS_STREAM_LISTENER (data));
  g_error ("handle_listener_error: error: %s", err->message);
}

static void
create_server ()
{
  GError *error = NULL;
  g_assert (listener == NULL);
  listener = gsk_stream_listener_socket_new_bind (addr, &error);
  if (listener == NULL)
    g_error ("error creating listener: %s", error->message);
  if (GSK_IS_SOCKET_ADDRESS_LOCAL (addr))
    g_object_set (G_OBJECT (listener), "unlink-when-done", TRUE, NULL);
  gsk_stream_listener_handle_accept (listener, accept_connection, handle_listener_error,
				     listener, NULL);
}

static void create_client();

static gboolean handle_client_readable (GskStream *stream, gpointer data)
{
  guint8 c;
  GError *error = NULL;
  if (gsk_stream_read (stream, &c, 1, &error) == 1)
    {
      g_assert (c == client_count);
      g_printerr (".");
      client_count++;
      if (client_count == 100)
	done_test = TRUE;
      else
	create_client ();
      return FALSE;
    }
  if (error)
    {
      g_error ("handle_client_readable: error reading: %s", error->message);
      return FALSE;
    }
  return TRUE;
}

static void
create_client ()
{
  GError *error = NULL;
  GskStream *client = gsk_stream_new_connecting (addr, &error);
  g_assert (client != NULL);
  gsk_io_trap_readable (client, handle_client_readable, NULL, g_object_ref (client), g_object_unref);
  gsk_io_write_shutdown (client, NULL);
  g_object_unref (client);
}

int main (int argc, char **argv)
{
  gsk_init_without_threads (&argc, &argv);

  g_printerr ("TCP/IP server/client... ");
  addr = gsk_socket_address_ipv4_localhost (10306);
  create_server (addr);
  client_count = 0;
  create_client (addr);
  while (!done_test)
    gsk_main_loop_run (gsk_main_loop_default(), -1, NULL);
  g_object_unref (listener);
  listener = NULL;
  g_object_unref (addr);
  addr = NULL;
  g_printerr (" done.\n");

  client_count = 0;
  done_test = FALSE;

  g_printerr ("Local-socket server/client... ");
#define TEST_LOCAL_SOCKET_FNAME	"./test-socket-sc"
  addr = gsk_socket_address_local_new (TEST_LOCAL_SOCKET_FNAME);
  create_server (addr);
  client_count = 0;
  create_client (addr);
  while (!done_test)
    gsk_main_loop_run (gsk_main_loop_default(), -1, NULL);
  g_object_unref (listener);
  g_object_unref (addr);
  g_printerr (" done.\n");

  return 0;
}
