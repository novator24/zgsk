#ifndef __GSK_PERSISTENT_CONNECTION_H_
#define __GSK_PERSISTENT_CONNECTION_H_

#include "gskstream.h"
#include "gskmainloop.h"
#include "gsksocketaddress.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskPersistentConnection GskPersistentConnection;
typedef struct _GskPersistentConnectionClass GskPersistentConnectionClass;
/* --- type macros --- */
GType gsk_persistent_connection_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_PERSISTENT_CONNECTION			(gsk_persistent_connection_get_type ())
#define GSK_PERSISTENT_CONNECTION(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_PERSISTENT_CONNECTION, GskPersistentConnection))
#define GSK_PERSISTENT_CONNECTION_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_PERSISTENT_CONNECTION, GskPersistentConnectionClass))
#define GSK_PERSISTENT_CONNECTION_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_PERSISTENT_CONNECTION, GskPersistentConnectionClass))
#define GSK_IS_PERSISTENT_CONNECTION(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_PERSISTENT_CONNECTION))
#define GSK_IS_PERSISTENT_CONNECTION_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_PERSISTENT_CONNECTION))

/* --- structures --- */
/* NOTE: the "DOING_NAME_RESOLUTION" state is now obsolete,
   but it is retained for backward compatibility.
   Use the gsk_persistent_connection_is_doing_name_resolution()
   macro, and know that when doing name-resolution,
   state == CONNECTING. */
typedef enum
{
  GSK_PERSISTENT_CONNECTION_INIT,
  GSK_PERSISTENT_CONNECTION_DOING_NAME_RESOLUTION,
  GSK_PERSISTENT_CONNECTION_CONNECTING,
  GSK_PERSISTENT_CONNECTION_CONNECTED,
  GSK_PERSISTENT_CONNECTION_WAITING
} GskPersistentConnectionState;

struct _GskPersistentConnectionClass 
{
  GskStreamClass base_class;
  void (*handle_connected)    (GskPersistentConnection *);
  void (*handle_disconnected) (GskPersistentConnection *);
};

struct _GskPersistentConnection 
{
  GskStream      base_instance;

  GskPersistentConnectionState state;
  guint             retry_timeout_ms;

  /* debugging */
  guint             warn_on_transport_errors : 1;
  guint             debug_connection : 1;

  /* Alternate methods for specifying the address. */

  /* by socket address */
  GskSocketAddress *address;

  /*< private >*/
  GskStream        *transport;
  GskSource        *retry_timeout_source;
  gulong transport_on_connect_signal_handler;
  gulong transport_on_error_signal_handler;
};

/* note: you will have to #include streamfd.h for this to work. */
#define gsk_persistent_connection_is_doing_name_resolution(pc) \
  ((pc)->state == GSK_PERSISTENT_CONNECTION_CONNECTING         \
   && GSK_STREAM_FD ((pc)->transport)->is_resolving_name)

/* --- prototypes --- */
GskStream *gsk_persistent_connection_new (GskSocketAddress *address,
                                          guint             retry_timeout_ms);
GskStream *gsk_persistent_connection_new_lookup
                                     (const char *host,
                                      guint       port,
                                      guint       retry_timeout_ms);

void gsk_persistent_connection_restart (GskPersistentConnection *connection,
                                        guint                    retry_wait_ms);


G_END_DECLS

#endif
