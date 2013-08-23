/*
    GSK - a library to write servers
    Copyright (C) 1999-2000 Dave Benson

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA

    Contact:
        daveb@ffem.org <Dave Benson>
*/

/* GskNetworkInterface:  find information about network devices on this host. */

#ifndef __GSK_NETWORK_INTERFACES_H_
#define __GSK_NETWORK_INTERFACES_H_

#include "gsksocketaddress.h"

G_BEGIN_DECLS

typedef struct _GskNetworkInterface GskNetworkInterface;
typedef struct _GskNetworkInterfaceSet GskNetworkInterfaceSet;

typedef enum
{
  GSK_NETWORK_INTERFACE_UP			= (1<<0),
  GSK_NETWORK_INTERFACE_LOOPBACK		= (1<<1),
  GSK_NETWORK_INTERFACE_NON_LOOPBACK		= (1<<2),
  GSK_NETWORK_INTERFACE_HAS_BROADCAST		= (1<<3),
  GSK_NETWORK_INTERFACE_HAS_MULTICAST		= (1<<4)
} GskNetworkInterfaceFlags;

struct _GskNetworkInterface
{
  const char *ifname;

  /* whether this interface is "virtual" -- just connects back to this host */
  unsigned is_loopback : 1;

  /* whether this interface supports broadcasting. */
  unsigned supports_multicast : 1;

  /* whether this interface is receiving packets not intended for it. */
  unsigned is_promiscuous : 1;

  /* ip-address if the interface is up. */
  GskSocketAddress *address;

  /* if !is_loopback, this is the device's MAC address. */
  GskSocketAddress *hw_address;

  /* if is_point_to_point, this is the address of the other end of
   * the connection.
   */
  GskSocketAddress *p2p_address;

  /* if supports_broadcast, this is the broadcast address. */
  GskSocketAddress *broadcast;
};

struct _GskNetworkInterfaceSet
{
  guint num_interfaces;
  GskNetworkInterface *interfaces;
};

GskNetworkInterfaceSet *
           gsk_network_interface_set_new    (GskNetworkInterfaceFlags  flags);
void       gsk_network_interface_set_destroy(GskNetworkInterfaceSet   *set);

G_END_DECLS

#endif
