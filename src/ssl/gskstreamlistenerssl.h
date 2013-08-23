#ifndef __GSK_STREAM_LISTENER_SSL_H_
#define __GSK_STREAM_LISTENER_SSL_H_

#include "../gskstreamlistener.h"
#include "gskstreamssl.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskStreamListenerSsl GskStreamListenerSsl;
typedef struct _GskStreamListenerSslClass GskStreamListenerSslClass;
/* --- type macros --- */
GType gsk_stream_listener_ssl_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_STREAM_LISTENER_SSL			(gsk_stream_listener_ssl_get_type ())
#define GSK_STREAM_LISTENER_SSL(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_STREAM_LISTENER_SSL, GskStreamListenerSsl))
#define GSK_STREAM_LISTENER_SSL_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_STREAM_LISTENER_SSL, GskStreamListenerSslClass))
#define GSK_STREAM_LISTENER_SSL_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_STREAM_LISTENER_SSL, GskStreamListenerSslClass))
#define GSK_IS_STREAM_LISTENER_SSL(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_STREAM_LISTENER_SSL))
#define GSK_IS_STREAM_LISTENER_SSL_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_STREAM_LISTENER_SSL))

/* --- structures --- */
struct _GskStreamListenerSslClass 
{
  GskStreamListenerClass stream_listener_class;
};
struct _GskStreamListenerSsl 
{
  GskStreamListener      stream_listener;
  char *cert_file;
  char *key_file;
  char *password;
  GskStreamListener *underlying;
};

/* --- prototypes --- */
GskStreamListener *gsk_stream_listener_ssl_new (GskStreamListener *underlying,
						const char        *cert_file,
						const char        *key_file);

G_END_DECLS

#endif
