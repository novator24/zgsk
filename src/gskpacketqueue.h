#ifndef __GSK_PACKET_QUEUE_H_
#define __GSK_PACKET_QUEUE_H_

#include "gskio.h"
#include "gskpacket.h"
#include "gsksocketaddress.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskPacketQueue GskPacketQueue;
typedef struct _GskPacketQueueClass GskPacketQueueClass;
/* --- type macros --- */
GType gsk_packet_queue_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_PACKET_QUEUE	           (gsk_packet_queue_get_type ())
#define GSK_PACKET_QUEUE(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_PACKET_QUEUE, GskPacketQueue))
#define GSK_PACKET_QUEUE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_PACKET_QUEUE, GskPacketQueueClass))
#define GSK_PACKET_QUEUE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_PACKET_QUEUE, GskPacketQueueClass))
#define GSK_IS_PACKET_QUEUE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_PACKET_QUEUE))
#define GSK_IS_PACKET_QUEUE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_PACKET_QUEUE))

/* --- structures --- */
struct _GskPacketQueueClass 
{
  GskIOClass io_class;
  gboolean   (*bind)  (GskPacketQueue    *queue,
		       GskSocketAddress  *addr,
		       GError           **error);
  GskPacket *(*read)  (GskPacketQueue    *queue,
		       gboolean           save_address,
		       GError           **error);
  gboolean   (*write) (GskPacketQueue    *queue,
		       GskPacket         *out,
		       GError           **error);

};
struct _GskPacketQueue 
{
  GskIO      io;
  guint      allow_address : 1;
  guint      allow_no_address : 1;
  guint      misses_packets : 1;
  GskSocketAddress *bound_address;
};

/* --- prototypes --- */
gboolean   gsk_packet_queue_bind  (GskPacketQueue    *queue,
				   GskSocketAddress  *address,
		                   GError           **error);
GskPacket *gsk_packet_queue_read  (GskPacketQueue    *queue,
				   gboolean           save_address,
		                   GError           **error);
gboolean   gsk_packet_queue_write (GskPacketQueue    *queue,
		                   GskPacket         *out,
		                   GError           **error);
#define gsk_packet_queue_get_allow_address(queue)       _gsk_packet_queue_get(queue,allow_address)
#define gsk_packet_queue_get_allow_no_address(queue)    _gsk_packet_queue_get(queue,allow_no_address)
#define gsk_packet_queue_get_misses_packets(queue)      _gsk_packet_queue_get(queue,misses_packets)
#define gsk_packet_queue_get_is_readable(queue)         _gsk_packet_queue_get_io(queue,is_readable)
#define gsk_packet_queue_get_is_writable(queue)         _gsk_packet_queue_get_io(queue,is_writable)
#define gsk_packet_queue_peek_bound_address(queue)      ((queue)->bound_address)


/*< protected >*/
#define gsk_packet_queue_mark_allow_address(queue)      _gsk_packet_queue_mark(queue,allow_address)
#define gsk_packet_queue_mark_allow_no_address(queue)   _gsk_packet_queue_mark(queue,allow_no_address)
#define gsk_packet_queue_mark_misses_packets(queue)     _gsk_packet_queue_mark(queue,misses_packets)
#define gsk_packet_queue_mark_is_readable(queue)        _gsk_packet_queue_mark_io(queue,is_readable)
#define gsk_packet_queue_mark_is_writable(queue)        _gsk_packet_queue_mark_io(queue,is_writable)
#define gsk_packet_queue_clear_allow_address(queue)     _gsk_packet_queue_clear(queue,allow_address)
#define gsk_packet_queue_clear_allow_no_address(queue)  _gsk_packet_queue_clear(queue,allow_no_address)
#define gsk_packet_queue_clear_misses_packets(queue)    _gsk_packet_queue_clear(queue,misses_packets)
#define gsk_packet_queue_clear_is_readable(queue)       _gsk_packet_queue_clear_io(queue,is_readable)
#define gsk_packet_queue_clear_is_writable(queue)       _gsk_packet_queue_clear_io(queue,is_writable)

/*< private >*/
#define _gsk_packet_queue_get(queue, field)  ((queue)->field != 0)
#define _gsk_packet_queue_mark(queue, field)  G_STMT_START{ (queue)->field = 1; }G_STMT_END
#define _gsk_packet_queue_clear(queue, field)  G_STMT_START{ (queue)->field = 0; }G_STMT_END
#define _gsk_packet_queue_get_io(queue, field)  _gsk_packet_queue_get(queue,io.field)
#define _gsk_packet_queue_mark_io(queue, field)  _gsk_packet_queue_mark(queue,io.field)
#define _gsk_packet_queue_clear_io(queue, field)  _gsk_packet_queue_clear(queue,io.field)
void gsk_packet_queue_set_bound_addresss (GskPacketQueue   *queue,
					  GskSocketAddress *address);

G_END_DECLS

#endif
