#ifndef __GSK_STREAM_LISTENER_SOCKET_H_
#define __GSK_STREAM_LISTENER_SOCKET_H_

#include "gskstreamlistener.h"
#include "gsksocketaddress.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskStreamListenerSocket GskStreamListenerSocket;
typedef struct _GskStreamListenerSocketClass GskStreamListenerSocketClass;
/* --- type macros --- */
GType gsk_stream_listener_socket_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_STREAM_LISTENER_SOCKET			(gsk_stream_listener_socket_get_type ())
#define GSK_STREAM_LISTENER_SOCKET(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_STREAM_LISTENER_SOCKET, GskStreamListenerSocket))
#define GSK_STREAM_LISTENER_SOCKET_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_STREAM_LISTENER_SOCKET, GskStreamListenerSocketClass))
#define GSK_STREAM_LISTENER_SOCKET_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_STREAM_LISTENER_SOCKET, GskStreamListenerSocketClass))
#define GSK_IS_STREAM_LISTENER_SOCKET(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_STREAM_LISTENER_SOCKET))
#define GSK_IS_STREAM_LISTENER_SOCKET_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_STREAM_LISTENER_SOCKET))

#define GSK_STREAM_LISTENER_SOCKET_USE_GLIB_MAIN_LOOP	0

#if !GSK_STREAM_LISTENER_SOCKET_USE_GLIB_MAIN_LOOP
#include "gskmainloop.h"
#endif

/* --- structures --- */
struct _GskStreamListenerSocketClass 
{
  GskStreamListenerClass stream_listener_class;
};
struct _GskStreamListenerSocket 
{
  GskStreamListener      stream_listener;
  gint                   fd;
#if GSK_STREAM_LISTENER_SOCKET_USE_GLIB_MAIN_LOOP
  GPollFD                poll_fd;
  GSource               *source;
#else
  GskSource             *source;
#endif 
  GskSocketAddress      *listening_address;
  gboolean               may_reuse_address;
  gboolean               unlink_when_done;      /* only available if listening_address is 'local' */
};

/* --- prototypes --- */
GskStreamListener *
gsk_stream_listener_socket_new_bind     (GskSocketAddress *address,
					 GError          **error);

GskStreamListener *
gsk_stream_listener_socket_new_from_fd (int      fd,
                                        GError **error);


/* --- tenative (not recommended at this point) --- */
typedef enum
{
  GSK_STREAM_LISTENER_SOCKET_DONT_REUSE_ADDRESS = (1<<0),
  GSK_STREAM_LISTENER_SOCKET_UNLINK_WHEN_DONE = (1<<1)
} GskStreamListenerSocketFlags;
GType gsk_stream_listener_socket_flags_get_type (void) G_GNUC_CONST;

GskStreamListener *
gsk_stream_listener_socket_new_bind_full(GskSocketAddress *address,
					 GskStreamListenerSocketFlags flags,
					 GError          **error);
void    gsk_stream_listener_socket_set_backlog (GskStreamListenerSocket *lis,
						guint             backlog);


/*< private >*/
void _gsk_socket_address_local_maybe_delete_stale_socket (GskSocketAddress *local_socket);
G_END_DECLS

#endif
