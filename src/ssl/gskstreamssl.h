#ifndef __GSK_STREAM_SSL_H_
#define __GSK_STREAM_SSL_H_

#include "../gskstream.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskStreamSsl GskStreamSsl;
typedef struct _GskStreamSslClass GskStreamSslClass;
/* --- type macros --- */
GType gsk_stream_ssl_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_STREAM_SSL			(gsk_stream_ssl_get_type ())
#define GSK_STREAM_SSL(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_STREAM_SSL, GskStreamSsl))
#define GSK_STREAM_SSL_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_STREAM_SSL, GskStreamSslClass))
#define GSK_STREAM_SSL_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_STREAM_SSL, GskStreamSslClass))
#define GSK_IS_STREAM_SSL(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_STREAM_SSL))
#define GSK_IS_STREAM_SSL_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_STREAM_SSL))

/* --- structures --- */
struct _GskStreamSslClass 
{
  GskStreamClass stream_class;
};

typedef enum
{
  GSK_STREAM_SSL_STATE_CONSTRUCTING,
  GSK_STREAM_SSL_STATE_NORMAL,
  GSK_STREAM_SSL_STATE_SHUTTING_DOWN,
  GSK_STREAM_SSL_STATE_SHUT_DOWN,

  GSK_STREAM_SSL_STATE_ERROR
} GskStreamSslState;

struct _GskStreamSsl 
{
  GskStream      stream;

  gpointer       ctx;		/* an SSL_CTX *        */
  gpointer       ssl;           /* an SSL *            */

  guint          is_client : 1;
  guint          doing_handshake : 1;
  guint          got_remote_shutdown : 1;

  /* Is this stream supposed to readable/writable? */
  guint          this_readable : 1;
  guint          this_writable : 1;

  /* What events are we waiting for on the backend? */
  guint          backend_poll_read : 1;
  guint          backend_poll_write : 1;

  /* Sometimes the opposite event is required to accomplish
     the read or write.
     
     If these are not set, then this_readable==transport_poll_read
     and this_writable==transport_poll_write. */
  guint          read_needed_to_write : 1;
  guint          write_needed_to_read : 1;
  guint          reread_length;
  guint          rewrite_length;

  guint          read_buffer_alloc;
  guint          write_buffer_alloc;
  guint          read_buffer_length;
  guint          write_buffer_length;
  guint8        *read_buffer;
  guint8        *write_buffer;


  GskStreamSslState state;

  char    *password;
  char    *ca_file;
  char    *ca_dir;
  char    *cert_file;
  char    *key_file;

  GskStream     *backend;     /* buffered transport layer */
  GskStream     *transport;   /* raw transport layer */
};

/* --- prototypes --- */
GskStream   *gsk_stream_ssl_new_server   (const char   *cert_file,
					  const char   *key_file,
					  const char   *password,
					  GskStream    *transport,
					  GError      **error);
GskStream   *gsk_stream_ssl_new_client   (const char   *cert_file,
					  const char   *key_file,
					  const char   *password,
					  GskStream    *transport,
					  GError      **error);
GskStream   *gsk_stream_ssl_peek_backend (GskStreamSsl *ssl);


G_END_DECLS

#endif
