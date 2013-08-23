#include "gskpacket.h"
#include "gskmacros.h"

GSK_DECLARE_POOL_ALLOCATORS(GskPacket, gsk_packet, 32)

/**
 * gsk_packet_new:
 * @data: binary data in the packet
 * @length: length of binary data
 * @destroy: method to destroy the data.
 * @destroy_data: the argument to the @destroy method.
 *
 * Creates a new packet with the given data.
 * The packet's ref-count is 1; it will be destroyed
 * when it gets to 0.
 *
 * returns: a new GskPacket
 */
GskPacket *
gsk_packet_new   (gpointer             data,
		  guint                length,
		  GskPacketDestroyFunc destroy,
		  gpointer             destroy_data)
{
  GskPacket *rv = gsk_packet_alloc ();
  rv->data = data;
  rv->len = length;
  rv->destroy = destroy;
  rv->destroy_data = destroy_data;
  rv->ref_count = 1;
  rv->src_address = NULL;
  rv->dst_address = NULL;
  return rv;
}

/**
 * gsk_packet_new_copy:
 * @data: binary data to be copied into the packet
 * @length: length of binary data
 *
 * Creates a new packet with a copy of the given data.
 * The packet's ref-count is 1; it will be destroyed
 * when it gets to 0.
 *
 * returns: a new GskPacket
 */
GskPacket *
gsk_packet_new_copy   (gconstpointer        data,
		       guint                length)
{
  gpointer copy = g_memdup (data, length);
  return gsk_packet_new (copy, length, (GskPacketDestroyFunc) g_free, copy);
}

/**
 * gsk_packet_unref:
 * @packet: a packet to remove a reference from.
 *
 * Remove a reference-count from the packet, deleting the packet 
 * if it gets to 0.
 */
void
gsk_packet_unref (GskPacket *packet)
{
  g_return_if_fail (packet->ref_count > 0);
  --(packet->ref_count);
  if (packet->ref_count == 0)
    {
      if (packet->destroy != NULL)
	(*packet->destroy) (packet->destroy_data, packet);
      if (packet->src_address != NULL)
	g_object_unref (packet->src_address);
      if (packet->dst_address != NULL)
	g_object_unref (packet->dst_address);
      gsk_packet_free (packet);
    }
}

/**
 * gsk_packet_ref:
 * @packet: a packet to add a reference to.
 *
 * Add a reference-count to the packet.
 *
 * returns: the @packet, for convenience.
 */
GskPacket *
gsk_packet_ref   (GskPacket *packet)
{
  g_return_val_if_fail (packet->ref_count > 0, packet);
  ++(packet->ref_count);
  return packet;
}

/**
 * gsk_packet_set_src_address:
 * @packet: a packet whose source address should be changed.
 * @address: the new address.
 *
 * Change the source address associated with a packet.
 * This should be the address of the interface
 * the packet was sent from.
 */
void
gsk_packet_set_src_address (GskPacket        *packet,
			    GskSocketAddress *address)
{
  if (address != NULL)
    g_object_ref (address);
  if (packet->src_address != NULL)
    g_object_unref (packet->src_address);
  packet->src_address = address;
}

/**
 * gsk_packet_set_dst_address:
 * @packet: a packet whose destination address should be changed.
 * @address: the new address.
 *
 * Change the destination address associated with a packet.
 * This should be the address of the interface
 * the packet was sent to.
 */
void
gsk_packet_set_dst_address (GskPacket        *packet,
			    GskSocketAddress *address)
{
  if (address != NULL)
    g_object_ref (address);
  if (packet->dst_address != NULL)
    g_object_unref (packet->dst_address);
  packet->dst_address = address;
}
