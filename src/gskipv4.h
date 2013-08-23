#ifndef __GSK_IPV4_H
#define __GSK_IPV4_H

#include <glib.h>

G_BEGIN_DECLS

extern const guint8 gsk_ipv4_ip_address_any[4];
extern const guint8 gsk_ipv4_ip_address_loopback[4];

#define gsk_ipv4_ip_address_localhost gsk_ipv4_ip_address_loopback

/* hint: To obtain the broadcast address,
   use gsk_network_interface_set_new(). */

/* Parse a numeric IP address. */
gboolean gsk_ipv4_parse (const char *str, guint8 *ip_addr_out);

G_END_DECLS

#endif
