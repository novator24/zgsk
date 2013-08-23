#ifndef __GSK_STREAM_CLIENT_H_
#define __GSK_STREAM_CLIENT_H_

#include "gsksocketaddress.h"
#include "gskstreamfd.h"

G_BEGIN_DECLS

GskStream *gsk_stream_new_connecting (GskSocketAddress  *address,
				      GError           **error);

/* see also: gsk_socket_address_connect_fd
        and  gsk_socket_address_finish_fd  (which has nothing to do with socket-addresses,
                                            but they're designed as a pair) */

G_END_DECLS

#endif
