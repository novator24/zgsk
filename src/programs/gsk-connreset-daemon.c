#include "../gsk.h"
#include <stdlib.h>


static gboolean shutdown_and_unref (gpointer data)
{
  gsk_io_shutdown (GSK_IO (data), NULL);
  g_object_unref (data);
  return FALSE;
}
static gboolean
handle_accept (GskStream    *stream,
               gpointer      data,
               GError      **error)
{
  gsk_main_loop_add_timer (gsk_main_loop_default (),
                           shutdown_and_unref,
                           g_object_ref (stream),
                           NULL,
                           3000, -1);
  return TRUE;
}

int
main(int argc, char **argv)
{
  GskStreamListener *listener;
  unsigned port;
  GskSocketAddress *address;
  GError *error = NULL;

  gsk_init_without_threads (&argc, &argv);
  if (argc != 2)
    gsk_fatal_user_error ("usage: gsk-connreset-daemon PORT");
  port = atoi (argv[1]);
  if (port <= 0)
    gsk_fatal_user_error ("error parsing port from: %s", argv[1]);

  address = gsk_socket_address_ipv4_new (gsk_ipv4_ip_address_any, port);
  listener = gsk_stream_listener_socket_new_bind (address, &error);
  if (listener == NULL)
    gsk_fatal_user_error ("error binding to port %u: %s", port, error->message);
  gsk_stream_listener_handle_accept (listener, handle_accept, NULL, NULL, NULL);
  return gsk_main_run ();
}
