#include "gskstreamclient.h"

/**
 * gsk_stream_new_connecting:
 * @address: the address to connect to.
 * @error: optional place to put an error response.
 *
 * Create a stream connecting to a given address.
 *
 * returns: the newly created stream.
 */
GskStream *
gsk_stream_new_connecting (GskSocketAddress  *address,
			   GError           **error)
{
  gboolean is_connected;
  int fd;

  /* symbolic addresses are handled internally to GskStreamFd */
  if (GSK_IS_SOCKET_ADDRESS_SYMBOLIC (address))
    return gsk_stream_fd_new_from_symbolic_address (GSK_SOCKET_ADDRESS_SYMBOLIC (address), error);

  /* try connecting to the (presumably native) address */
  fd = gsk_socket_address_connect_fd (address, &is_connected, error);
  if (fd < 0)
    return NULL;

  if (is_connected)
    {
      /* finished connecting */
      return gsk_stream_fd_new (fd, GSK_STREAM_FD_FOR_NEW_SOCKET);
    }
  else
    {
      /* connecting still pending */
      return gsk_stream_fd_new_connecting (fd);
    }
}
