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

#include "gsknetworkinterface.h"
#include "config.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>

/* needed under solaris */
#define BSD_COMP

#if HAVE_NET_IF_H
#include <net/if.h>
#endif
#if HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#include <errno.h>
#include <netdb.h>
#include <unistd.h>



static int
get_IPPROTO_IP ()
{
  static int proto = -1;
  if (proto < 0)
    {
      struct protoent *entry;
      entry = getprotobyname ("ip");
      if (entry == NULL)
	{
	  g_warning ("The ip protocol was not found in /etc/services...");
	  proto = 0;
	}
      else
	{
	  proto = entry->p_proto;
	}
    }
  return proto;
}

/**
 * gsk_network_interface_set_new:
 * @flags: constraints on the interfaces to return.  All the constraints
 * must be satisfied.
 *
 * Create a new list of interfaces, subject to the constraints given.
 *
 * Note that the constraints must all be satified, so
 * using GSK_NETWORK_INTERFACE_NO_LOOKBACK and GSK_NETWORK_INTERFACE_LOOKBACK
 * will always return an empty set.
 *
 * returns: a newly allocated list of interfaces that
 * must be freed with gsk_network_interface_set_destroy().
 */
GskNetworkInterfaceSet *
gsk_network_interface_set_new(GskNetworkInterfaceFlags  flags)
{
  GArray *ifreq_array;
  GArray *rv;
  int tmp_socket;
  guint i;
  tmp_socket = socket (AF_INET, SOCK_DGRAM, get_IPPROTO_IP ());
  if (tmp_socket < 0)
    {
      g_warning ("gsk_network_interface: error creating internal ns socket: %s",
		 g_strerror (errno));
      return NULL;
    }
  ifreq_array = g_array_new (FALSE, FALSE, sizeof (struct ifreq));
  g_array_set_size (ifreq_array, 16);
  for (;;)
    {
      struct ifconf all_interface_config;
      guint num_got;
      all_interface_config.ifc_len = ifreq_array->len * sizeof (struct ifreq);
      all_interface_config.ifc_buf = ifreq_array->data;
      if (ioctl (tmp_socket, SIOCGIFCONF, (char *) &all_interface_config) < 0)
	{ 
	  g_warning ("gsk_network_interface:"
		     "error getting interface configuration: %s",
		     g_strerror (errno));
	  close (tmp_socket);
	  g_array_free (ifreq_array, TRUE);
	  return NULL;
	}
      num_got = all_interface_config.ifc_len / sizeof (struct ifreq);
      if (num_got == ifreq_array->len)
	g_array_set_size (ifreq_array, ifreq_array->len * 2);
      else
	{
	  g_array_set_size (ifreq_array, num_got);
	  break;
	}
    }

  /* now query each of those interfaces. */
  rv = g_array_new (FALSE, FALSE, sizeof (GskNetworkInterface));
  for (i = 0; i < ifreq_array->len; i++)
    {
      struct ifreq *req_array = (struct ifreq *)(ifreq_array->data) + i;
      struct ifreq tmp_req;
      gboolean is_up;
      gboolean is_loopback;
      gboolean has_broadcast;
      gboolean has_multicast;
      gboolean is_p2p;
      guint if_flags;
      GskNetworkInterface interface;

      /* XXX: we don't at all no how to handle a generic interface. */
      /* XXX: is this an IPv6 problem? */
      if (req_array->ifr_addr.sa_family != AF_INET)
	continue;

      memcpy (tmp_req.ifr_name, req_array->ifr_name, sizeof (tmp_req.ifr_name));
      if (ioctl (tmp_socket, SIOCGIFFLAGS, (char *) &tmp_req) < 0) 
	{	
	  g_warning ("error getting information about interface %s",
		     tmp_req.ifr_name);
	  continue;
	}

      if_flags = tmp_req.ifr_flags;
      is_up = (if_flags & IFF_UP) == IFF_UP;
      is_loopback = (if_flags & IFF_LOOPBACK) == IFF_LOOPBACK;
      has_broadcast = (if_flags & IFF_BROADCAST) == IFF_BROADCAST;
      has_multicast = (if_flags & IFF_MULTICAST) == IFF_MULTICAST;
      is_p2p = (if_flags & IFF_POINTOPOINT) == IFF_POINTOPOINT;

      if ((flags & GSK_NETWORK_INTERFACE_UP) != 0 && !is_up)
	continue;
      if ((flags & GSK_NETWORK_INTERFACE_LOOPBACK) != 0 && !is_loopback)
	continue;
      if ((flags & GSK_NETWORK_INTERFACE_NON_LOOPBACK) != 0 && is_loopback)
	continue;
      if ((flags & GSK_NETWORK_INTERFACE_HAS_BROADCAST) != 0 && !has_broadcast)
	continue;
      if ((flags & GSK_NETWORK_INTERFACE_HAS_MULTICAST) != 0 && !has_multicast)
	continue;

      interface.supports_multicast = has_multicast ? 1 : 0;
      interface.is_promiscuous = (if_flags & IFF_PROMISC) ? 1 : 0;

      if (is_up)
	{
	  struct sockaddr *saddr;
	  if (ioctl (tmp_socket, SIOCGIFADDR, (char *) &tmp_req) < 0)
	    {
	      g_warning ("error getting the ip address for interface %s",
			 tmp_req.ifr_name);
	      continue;
	    }
	  saddr = &tmp_req.ifr_addr;
	  interface.address = gsk_socket_address_from_native (saddr, sizeof (*saddr));
	}
      else
	interface.address = NULL;

      interface.is_loopback = is_loopback ? 1 : 0;
#ifdef SIOCGIFHWADDR
      if (!interface.is_loopback)
	interface.hw_address = NULL;
      else
	{
	  if (ioctl (tmp_socket, SIOCGIFHWADDR, (char *) &tmp_req) < 0)
	    {
	      g_warning ("error getting the hardware address for interface %s",
			 tmp_req.ifr_name);
	      continue;
	    }
	  interface.hw_address = gsk_socket_address_ethernet_new ((guint8*)tmp_req.ifr_addr.sa_data);
	}
#else
      interface.hw_address = NULL;
#endif

      if (is_p2p)
	{
	  struct sockaddr *saddr;
	  if (ioctl (tmp_socket, SIOCGIFDSTADDR, (char *) &tmp_req) < 0)
	    {
	      g_warning ("error getting the ip address for interface %s",
			 tmp_req.ifr_name);
	      continue;
	    }
	  saddr = &tmp_req.ifr_addr;
	  interface.p2p_address = gsk_socket_address_from_native (saddr,
								  sizeof (struct sockaddr));
	}
      else
	interface.p2p_address = NULL;

      if (has_broadcast)
	{
	  struct sockaddr *saddr;
	  if (ioctl (tmp_socket, SIOCGIFBRDADDR, (char *) &tmp_req) < 0)
	    {
	      g_warning ("error getting the broadcast address for interface %s",
			 tmp_req.ifr_name);
	      continue;
	    }
	  saddr = &tmp_req.ifr_addr;
          interface.broadcast = gsk_socket_address_from_native (saddr,
								sizeof (struct sockaddr));
	}
      else
	interface.broadcast = NULL;

      interface.ifname = g_strdup (tmp_req.ifr_name);
      g_array_append_val (rv, interface);
    }

  close (tmp_socket);

  g_array_free (ifreq_array, TRUE);
  {
    GskNetworkInterfaceSet *set;
    set = g_new (GskNetworkInterfaceSet, 1);
    set->num_interfaces = rv->len;
    set->interfaces = (GskNetworkInterface *) rv->data;
    return set;
  }
}

/**
 * gsk_network_interface_set_destroy:
 * @set: the list of interfaces to destroy.
 *
 * Free the memory used by the list of interfaces.
 */
void
gsk_network_interface_set_destroy(GskNetworkInterfaceSet    *set)
{
  guint i;
  for (i = 0; i < set->num_interfaces; i++)
    {
      g_free ((char*) (set->interfaces[i].ifname));

      if (set->interfaces[i].address != NULL)
	g_object_unref (set->interfaces[i].address);
      if (set->interfaces[i].hw_address != NULL)
        g_object_unref (set->interfaces[i].hw_address);
      if (set->interfaces[i].p2p_address != NULL)
        g_object_unref (set->interfaces[i].p2p_address);
      if (set->interfaces[i].broadcast != NULL)
        g_object_unref (set->interfaces[i].broadcast);
    }
  g_free (set->interfaces);
  g_free (set);
}

