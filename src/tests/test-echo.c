#include <stdio.h>
#include <stdlib.h>
#include "../gskstreamlistenersocket.h"
#include "../gskdebug.h"
#include "../gskinit.h"
#include "../gskmain.h"

static gboolean
handle_on_accept (GskStream         *stream,
		  gpointer           data,
		  GError           **error)
{
  GError *e = NULL;
  gsk_stream_attach (stream, stream, &e);
  if (e != NULL)
    g_error ("gsk_stream_attach: %s", e->message);
  g_object_unref (stream);
  return TRUE;
}

static void
handle_errors (GError     *error,
	       gpointer    data)
{
  g_error ("error accepting new socket: %s", error->message);
}

int main (int argc, char **argv)
{
  GskStreamListener *listener;
  GskSocketAddress *bind_addr;
  GError *error = NULL;
  gsk_init_without_threads (&argc, &argv);

  if (argc != 2)
    g_error ("%s requires exactly 1 argument, tcp port number", argv[0]);

  bind_addr = gsk_socket_address_ipv4_localhost (atoi (argv[1]));
  listener = gsk_stream_listener_socket_new_bind (bind_addr, &error);
  if (error != NULL)
    g_error ("gsk_stream_listener_tcp_unix failed: %s", error->message);
  g_assert (listener != NULL);

  gsk_stream_listener_handle_accept (listener,
				     handle_on_accept,
				     handle_errors,
				     NULL,		/* data */
				     NULL);		/* destroy */

  return gsk_main_run ();
}
