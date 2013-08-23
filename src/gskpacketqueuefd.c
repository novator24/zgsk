#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include "gskpacketqueuefd.h"
#include "gskmacros.h"
#include "gskutils.h"
#include "gskghelpers.h"
#include "gsktypes.h"
#include "gskfork.h"
#include "gskerrno.h"
#include "config.h"

enum
{
  PROP_0,
  PROP_FILE_DESCRIPTOR
};

#define USE_GLIB_MAIN_LOOP	GSK_PACKET_QUEUE_FD_USE_GLIB_MAIN_LOOP


static GObjectClass *parent_class = NULL;

/* This number is due to the header format of UDP.
   The header (see RFC 768) only allows a 16-bit length.
   As of Dec 2003, UDP is the least-updated protocol,
   RFC 768 is from 1980, so hardcoding this constant is probably ok.

   On the other hand, eventually fat pipes may cause demand for
   yet bigger packets. */
#define MAX_UDP_PACKET_SIZE   ((1 << 16) - 1)

/* --- queue methods --- */
static gboolean
gsk_packet_queue_fd_bind (GskPacketQueue    *queue,
			  GskSocketAddress  *addr,
		          GError           **error)
{
  GskPacketQueueFd *queue_fd = GSK_PACKET_QUEUE_FD (queue);
  socklen_t native_len = gsk_socket_address_sizeof_native (addr);
  gpointer native = alloca (native_len);

  if (!gsk_socket_address_to_native (addr, native, error))
    return FALSE;
  if (bind (queue_fd->fd, native, native_len) < 0)
    {
      int e = errno;
      g_set_error (error, GSK_G_ERROR_DOMAIN,
		   gsk_error_code_from_errno (e),
		   _("PacketQueueFd: bind failed: %s"),
		   g_strerror (e));
      return FALSE;
    }

  if (queue_fd->bound_address != NULL)
    g_object_unref (queue_fd->bound_address);
  queue_fd->bound_address = g_object_ref (addr);

  gsk_packet_queue_mark_allow_no_address (queue);
  return TRUE;
}

static GskPacket *
gsk_packet_queue_fd_read (GskPacketQueue    *queue,
			  gboolean           save_address,
		          GError           **error)
{
  GskPacketQueueFd *queue_fd = GSK_PACKET_QUEUE_FD (queue);
  char *tmp = alloca (MAX_UDP_PACKET_SIZE);
  int fd = queue_fd->fd;
  struct sockaddr addr;
  socklen_t addrlen = sizeof (addr);
  int rv;
  GskPacket *packet;
  gpointer data;
  if (save_address)
    rv = recvfrom (fd, tmp, MAX_UDP_PACKET_SIZE, 0, &addr, &addrlen);
  else
    /* according to bsd man page, we should use 'recvfrom' instead of
       'recv', even in this case. 

       XXX: provide more accurate citation.  */
    rv = recvfrom (fd, tmp, MAX_UDP_PACKET_SIZE, 0, NULL, NULL);
  if (rv < 0)
    {
      int e = errno;
      if (!gsk_errno_is_ignorable (e))
	g_set_error (error, GSK_G_ERROR_DOMAIN,
		     gsk_error_code_from_errno (e),
		     _("packet-queue-read failed: %s"),
		     g_strerror (e));
      return NULL;
    }
  data = g_memdup (tmp, rv);
  packet = gsk_packet_new (data, rv, (GskPacketDestroyFunc) g_free, data);
  if (save_address)
    {
      packet->src_address = gsk_socket_address_from_native (&addr, addrlen);
      if (packet->src_address == NULL)
	{
	  g_set_error (error, GSK_G_ERROR_DOMAIN,
		       GSK_ERROR_FOREIGN_ADDRESS,
		       _("received packet had invalid or unknown address"));
	  gsk_packet_unref (packet);
	  return NULL;
	}
    }
  if (queue_fd->bound_address != NULL)
    packet->dst_address = g_object_ref (queue_fd->bound_address);
  return packet;
}

static gboolean
gsk_packet_queue_fd_write (GskPacketQueue    *queue,
		           GskPacket         *out,
		           GError           **error)
{
  GskPacketQueueFd *queue_fd = GSK_PACKET_QUEUE_FD (queue);
  guint native_size;
  gpointer native_addr;
  gssize rv;
  int fd = queue_fd->fd;
  if (out->dst_address != NULL)
    {
      native_size = gsk_socket_address_sizeof_native (out->dst_address);
      native_addr = alloca (native_size);
      if (!gsk_socket_address_to_native (out->dst_address, native_addr, error))
	return FALSE;
    }
  else
    {
      native_size = 0;
      native_addr = NULL;
    }
  rv = sendto (fd, out->data, out->len, 0, native_addr, native_size);
  if (rv < 0)
    {
      int e = errno;
      if (!gsk_errno_is_ignorable (e))
	g_set_error (error, GSK_G_ERROR_DOMAIN,
		     gsk_error_code_from_errno (e),
		     _("packet-queue-fd-write: %s"),
		     g_strerror (e));
      return FALSE;
    }
  if ((guint) rv < out->len)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
		   GSK_ERROR_UNEXPECTED_PARTIAL_WRITE,
		   _("sendto did not get all the bytes of the packet sent"));
      return FALSE;
    }
  return TRUE;
}

/* --- io methods --- */
/* The following functions are defined twice:
      add_poll()
      remove_poll()
      set_poll_read()
      set_poll_write()
   once for glib and once for gsk.
 */
     
#if USE_GLIB_MAIN_LOOP
typedef struct _GskPacketQueueFdSource GskPacketQueueFdSource;
struct _GskPacketQueueFdSource
{
  GSource base;
  GskPacketQueueFd *packet_queue_fd;
};

static gboolean
gsk_packet_queue_fd_source_prepare (GSource    *source,
			            gint       *timeout)
{
  return FALSE;
}

static gboolean
gsk_packet_queue_fd_source_check    (GSource    *source)
{
  GskPacketQueueFdSource *fd_source = (GskPacketQueueFdSource *) source;
  return fd_source->packet_queue_fd->poll_fd.revents != 0;
}

static gboolean
gsk_packet_queue_fd_source_dispatch (GSource    *source,
			             GSourceFunc callback,
			             gpointer    user_data)
{
  GskPacketQueueFdSource *fd_source = (GskPacketQueueFdSource *) source;
  GskPacketQueueFd *packet_queue_fd = fd_source->packet_queue_fd;
  guint events = fd_source->packet_queue_fd->poll_fd.revents;
  if ((events & (G_IO_IN|G_IO_HUP)) != 0)
    gsk_io_notify_ready_to_read (GSK_IO (packet_queue_fd));
  if ((events & G_IO_OUT) == G_IO_OUT)
    gsk_io_notify_ready_to_write (GSK_IO (packet_queue_fd));
  return TRUE;
}

static GSourceFuncs gsk_packet_queue_fd_source_funcs =
{
  gsk_packet_queue_fd_source_prepare,
  gsk_packet_queue_fd_source_check,
  gsk_packet_queue_fd_source_dispatch,
  NULL,					/* finalize */
  NULL,					/* closure-callback (reserved) */
  NULL					/* closure-marshal (reserved) */
};

static void
add_poll (GskPacketQueueFd *packet_queue_fd)
{
  GskPacketQueueFdSource *fd_source;
  packet_queue_fd->poll_fd.fd = packet_queue_fd->fd;
  packet_queue_fd->source = g_source_new (&gsk_packet_queue_fd_source_funcs,
				          sizeof (GskPacketQueueFdSource));
  fd_source = (GskPacketQueueFdSource *) packet_queue_fd->source;
  fd_source->packet_queue_fd = packet_queue_fd;
  g_source_add_poll (packet_queue_fd->source, &packet_queue_fd->poll_fd);
  g_source_attach (packet_queue_fd->source, g_main_context_default ());
  packet_queue_fd->poll_fd.events = 0;
}

static void
remove_poll (GskPacketQueueFd *packet_queue_fd)
{
  if (packet_queue_fd->source != NULL)
    {
      g_source_destroy (packet_queue_fd->source);
      g_source_unref (packet_queue_fd->source);
      packet_queue_fd->source = NULL;
    }
}

static void
gsk_packet_queue_fd_set_poll_read   (GskIO         *io,
			             gboolean       do_poll)
{
  GskPacketQueueFd *packet_queue_fd = GSK_PACKET_QUEUE_FD (io);
  if (do_poll)
    packet_queue_fd->poll_fd.events |= G_IO_IN;
  else
    packet_queue_fd->poll_fd.events &= ~G_IO_IN;
}

static void
gsk_packet_queue_fd_set_poll_write  (GskIO         *io,
			             gboolean       do_poll)
{
  GskPacketQueueFd *packet_queue_fd = GSK_PACKET_QUEUE_FD (io);
  if (do_poll)
    packet_queue_fd->poll_fd.events |= G_IO_OUT;
  else
    packet_queue_fd->poll_fd.events &= ~G_IO_OUT;
}
#else	/* !USE_GLIB_MAIN_LOOP */
static gboolean
handle_io_event (int fd, GIOCondition events, gpointer data)
{
  GskPacketQueueFd *packet_queue_fd = GSK_PACKET_QUEUE_FD (data);
  if ((events & (G_IO_IN|G_IO_HUP)) != 0)
    gsk_io_notify_ready_to_read (GSK_IO (packet_queue_fd));
  if ((events & G_IO_OUT) == G_IO_OUT)
    gsk_io_notify_ready_to_write (GSK_IO (packet_queue_fd));
  return TRUE;
}

static void
add_poll (GskPacketQueueFd *packet_queue_fd)
{
  packet_queue_fd->source = gsk_main_loop_add_io (gsk_main_loop_default (),
						  packet_queue_fd->fd,
						  0,	/* no initial events */
						  handle_io_event,
						  packet_queue_fd,
						  NULL);
}
static void
remove_poll (GskPacketQueueFd *packet_queue_fd)
{
  if (packet_queue_fd->source != NULL)
    {
      gsk_source_remove (packet_queue_fd->source);
      packet_queue_fd->source = NULL;
    }
}
static void
gsk_packet_queue_fd_set_poll_read   (GskIO         *io,
			             gboolean       do_poll)
{
  GskPacketQueueFd *packet_queue_fd = GSK_PACKET_QUEUE_FD (io);
  if (do_poll)
    gsk_source_add_io_events (packet_queue_fd->source, G_IO_IN);
  else
    gsk_source_remove_io_events (packet_queue_fd->source, G_IO_IN);
}
static void
gsk_packet_queue_fd_set_poll_write  (GskIO         *io,
			             gboolean       do_poll)
{
  GskPacketQueueFd *packet_queue_fd = GSK_PACKET_QUEUE_FD (io);
  if (do_poll)
    gsk_source_add_io_events (packet_queue_fd->source, G_IO_OUT);
  else
    gsk_source_remove_io_events (packet_queue_fd->source, G_IO_OUT);
}
#endif

static gboolean
gsk_packet_queue_fd_shutdown_read   (GskIO         *io,
				     GError       **error)
{
  GskPacketQueueFd *packet_queue_fd = GSK_PACKET_QUEUE_FD (io);
  if (shutdown (packet_queue_fd->fd, SHUT_RD) < 0)
    {
      int e = errno;
      g_set_error (error, GSK_G_ERROR_DOMAIN,
		   gsk_error_code_from_errno (e),
		   "error shutting down fd %d for reading: %s",
		   packet_queue_fd->fd,
		   g_strerror (e));
      return FALSE;
    }
  else
    {
      return TRUE;
    }
}

static gboolean
gsk_packet_queue_fd_shutdown_write  (GskIO         *io,
				     GError       **error)
{
  GskPacketQueueFd *packet_queue_fd = GSK_PACKET_QUEUE_FD (io);
  if (shutdown (packet_queue_fd->fd, SHUT_WR) < 0)
    {
      int e = errno;
      g_set_error (error, GSK_G_ERROR_DOMAIN,
		   gsk_error_code_from_errno (e),
		   "error shutting down fd %d for writing: %s",
		   packet_queue_fd->fd,
		   g_strerror (e));
      return FALSE;
    }
  else
    {
      return TRUE;
    }
}

static void
gsk_packet_queue_fd_close (GskIO         *io)
{
  GskPacketQueueFd *packet_queue_fd = GSK_PACKET_QUEUE_FD (io);
  remove_poll (packet_queue_fd);
  if (packet_queue_fd->fd >= 0)
    {
      close (packet_queue_fd->fd);
      gsk_fork_remove_cleanup_fd (packet_queue_fd->fd);
      packet_queue_fd->fd = -1;
    }
}

/* --- arguments --- */
static void
gsk_packet_queue_fd_get_property (GObject        *object,
			          guint           property_id,
			          GValue         *value,
			          GParamSpec     *pspec)
{
  switch (property_id)
    {
    case PROP_FILE_DESCRIPTOR:
      g_value_set_int (value, GSK_PACKET_QUEUE_FD (object)->fd);
      break;
    }
}

static void
gsk_packet_queue_fd_set_property (GObject        *object,
			          guint           property_id,
			          const GValue   *value,
			          GParamSpec     *pspec)
{
  switch (property_id)
    {
    case PROP_FILE_DESCRIPTOR:
      {
	int fd = g_value_get_int (value);
	GskPacketQueueFd *packet_queue_fd = GSK_PACKET_QUEUE_FD (object);
	if (packet_queue_fd->fd >= 0)
	  gsk_fork_remove_cleanup_fd (fd);
	if (fd >= 0)
	  gsk_fork_add_cleanup_fd (fd);
	packet_queue_fd->fd = fd;
	break;
      }
    }
}

static gboolean
gsk_packet_queue_fd_open (GskIO     *io,
		          GError   **error)
{
  GskPacketQueue *queue = GSK_PACKET_QUEUE (io);
  GskPacketQueueFd *packet_queue_fd = GSK_PACKET_QUEUE_FD (io);
  if (packet_queue_fd->fd < 0)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_OPEN_FAILED,
		   _("must specify valid file-descriptor"));
      return FALSE;
    }

  g_return_val_if_fail (packet_queue_fd->source == NULL, FALSE);
  add_poll (packet_queue_fd);
  GSK_HOOK_SET_FLAG (GSK_IO_WRITE_HOOK (queue), IS_AVAILABLE);
  GSK_HOOK_SET_FLAG (GSK_IO_READ_HOOK (queue), IS_AVAILABLE);
  return TRUE;
}

/* --- functions --- */
static void
gsk_packet_queue_fd_init (GskPacketQueueFd *packet_queue_fd)
{
  GskPacketQueue *queue = GSK_PACKET_QUEUE (packet_queue_fd);
  packet_queue_fd->fd = -1;
  gsk_packet_queue_mark_misses_packets (queue);
  gsk_packet_queue_mark_allow_address (queue);
}


static void
gsk_packet_queue_fd_class_init (GskPacketQueueFdClass *class)
{
  GskPacketQueueClass *queue_class = GSK_PACKET_QUEUE_CLASS (class);
  GskIOClass *io_class = GSK_IO_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GParamSpec *pspec;

  parent_class = g_type_class_peek_parent (class);

  queue_class->bind = gsk_packet_queue_fd_bind;
  queue_class->read = gsk_packet_queue_fd_read;
  queue_class->write = gsk_packet_queue_fd_write;
  io_class->set_poll_read = gsk_packet_queue_fd_set_poll_read;
  io_class->set_poll_write = gsk_packet_queue_fd_set_poll_write;
  io_class->shutdown_read = gsk_packet_queue_fd_shutdown_read;
  io_class->shutdown_write = gsk_packet_queue_fd_shutdown_write;
  io_class->open = gsk_packet_queue_fd_open;
  io_class->close = gsk_packet_queue_fd_close;
  object_class->get_property = gsk_packet_queue_fd_get_property;
  object_class->set_property = gsk_packet_queue_fd_set_property;
  pspec = gsk_param_spec_fd ("file-descriptor",
			     _("File Descriptor"),
			     _("for reading and/or writing"),
                             G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_FILE_DESCRIPTOR, pspec);
}

GType gsk_packet_queue_fd_get_type()
{
  static GType packet_queue_fd_type = 0;
  if (!packet_queue_fd_type)
    {
      static const GTypeInfo packet_queue_fd_info =
      {
	sizeof(GskPacketQueueFdClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_packet_queue_fd_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskPacketQueueFd),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_packet_queue_fd_init,
	NULL		/* value_table */
      };
      packet_queue_fd_type = g_type_register_static (GSK_TYPE_PACKET_QUEUE,
                                                     "GskPacketQueueFd",
						     &packet_queue_fd_info,
						     0);
    }
  return packet_queue_fd_type;
}

/* --- public constructors --- */
/**
 * gsk_packet_queue_fd_new:
 * @fd: the datagram socket file-descriptor.
 *
 * Create a new #GskPacketQueue from an already opened
 * file-descriptor.
 *
 * returns: the new packet-queue.
 */
GskPacketQueue *
gsk_packet_queue_fd_new           (int  fd)
{
  gsk_fd_set_nonblocking (fd);
  return g_object_new (GSK_TYPE_PACKET_QUEUE_FD, "file-descriptor", fd, NULL);
}

/**
 * gsk_packet_queue_fd_new_by_family:
 * @addr_family: the system-specific address family.
 * @error: optional pointer to an error to set if things go wrong.
 *
 * Create a new Packet Queue using a newly opened datagram
 * socket of a given address family.  The address family
 * is the sequence of AF_ defines in the header &lt;sys/socket.h&gt;
 * on most unices.
 *
 * The address family of a #GskSocketAddress may be found
 * using gsk_socket_address_protocol_family().
 *
 * returns: the new packet-queue, or NULL if there is a problem creating the socket.
 */
GskPacketQueue *
gsk_packet_queue_fd_new_by_family (int  addr_family,
				   GError **error)
{
  int fd;
retry:
  fd = socket (addr_family, SOCK_DGRAM, 0);
  if (fd < 0)
    {
      if (gsk_errno_is_ignorable (errno))
        goto retry;
      gsk_errno_fd_creation_failed ();
      g_set_error (error, GSK_G_ERROR_DOMAIN,
		   GSK_ERROR_OPEN_FAILED,
		   _("error creating socket: %s"),
		   g_strerror (errno));
      return NULL;
    }
  gsk_fd_set_close_on_exec (fd, TRUE);
  gsk_fd_set_nonblocking (fd);

  return gsk_packet_queue_fd_new (fd);
}

/**
 * gsk_packet_queue_fd_new_bound:
 * @address: the address to bind to.
 * @error: optional pointer to an error to set if things go wrong.
 * 
 * Create a new Packet Queue using a newly opened datagram
 * socket which is bound to a given address.
 *
 * Note that socket address space for TCP and UDP is
 * separate, so it's allowed (and sometimes encouraged)
 * to bind to the same port for both a packet queue,
 * and a stream-listener.
 *
 * returns: the new packet-queue, or NULL if there is a problem creating the socket or binding.
 */
GskPacketQueue *
gsk_packet_queue_fd_new_bound     (GskSocketAddress *address,
				   GError          **error)
{
  int family = gsk_socket_address_protocol_family (address);
  GskPacketQueue *queue = gsk_packet_queue_fd_new_by_family (family, error);
  if (queue == NULL)
    return NULL;
  if (! gsk_packet_queue_bind (queue, address, error))
    {
      g_object_unref (queue);
      return NULL;
    }
  return queue;
}

/**
 * gsk_packet_queue_fd_set_broadcast:
 * @packet_queue_fd: the packet-queue to affect.
 * @allow_broadcast: whether to allow (TRUE) or disallow broadcast sends
 * and receives.  The default for a new datagram socket is to
 * disallow broadcast packets.
 * @error: optional address of an error to set if things go wrong.
 *
 * Changes the operating-system-level flag of whether
 * sends and receives of broadcast packets are allowed
 * on datagram sockets.
 *
 * returns: whether the operation was successful.
 */
gboolean gsk_packet_queue_fd_set_broadcast (GskPacketQueueFd *packet_queue_fd,
					    gboolean          allow_broadcast,
					    GError          **error)
{
  int fd = packet_queue_fd->fd;
  int broadcast = allow_broadcast;
  if (setsockopt (fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof (int)) < 0)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, gsk_error_code_from_errno (errno),
		   "error setting file-descriptor %d to %s broadcast packets: %s",
		   fd, allow_broadcast ? "allow" : "disallow", g_strerror (errno));
      return FALSE;
    }
  return TRUE;
}
