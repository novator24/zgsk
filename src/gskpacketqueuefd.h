#ifndef __GSK_PACKET_QUEUE_FD_H_
#define __GSK_PACKET_QUEUE_FD_H_

#include "gskpacketqueue.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskPacketQueueFd GskPacketQueueFd;
typedef struct _GskPacketQueueFdClass GskPacketQueueFdClass;
/* --- type macros --- */
GType gsk_packet_queue_fd_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_PACKET_QUEUE_FD			(gsk_packet_queue_fd_get_type ())
#define GSK_PACKET_QUEUE_FD(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_PACKET_QUEUE_FD, GskPacketQueueFd))
#define GSK_PACKET_QUEUE_FD_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_PACKET_QUEUE_FD, GskPacketQueueFdClass))
#define GSK_PACKET_QUEUE_FD_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_PACKET_QUEUE_FD, GskPacketQueueFdClass))
#define GSK_IS_PACKET_QUEUE_FD(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_PACKET_QUEUE_FD))
#define GSK_IS_PACKET_QUEUE_FD_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_PACKET_QUEUE_FD))

#define GSK_PACKET_QUEUE_FD_USE_GLIB_MAIN_LOOP	0

#if !GSK_PACKET_QUEUE_FD_USE_GLIB_MAIN_LOOP
#include "gskmainloop.h"
#endif

/* --- structures --- */
struct _GskPacketQueueFdClass 
{
  GskPacketQueueClass packet_queue_class;
};
struct _GskPacketQueueFd 
{
  GskPacketQueue      packet_queue;

  int fd;
  GskSocketAddress *bound_address;
#if GSK_PACKET_QUEUE_FD_USE_GLIB_MAIN_LOOP
  GPollFD poll_fd;
  GSource *source;
#else
  GskSource *source;
#endif
};

/* --- prototypes --- */
GskPacketQueue *gsk_packet_queue_fd_new           (int               fd);
GskPacketQueue *gsk_packet_queue_fd_new_by_family (int        addr_family,
						   GError   **error);
GskPacketQueue *gsk_packet_queue_fd_new_bound     (GskSocketAddress *address,
						   GError   **error);
gboolean gsk_packet_queue_fd_set_broadcast (GskPacketQueueFd *packet_queue_fd,
					    gboolean          allow_broadcast,
					    GError          **error);

G_END_DECLS

#endif
