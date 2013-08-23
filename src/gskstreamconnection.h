#ifndef __GSK_STREAM_CONNECTION_H_
#define __GSK_STREAM_CONNECTION_H_

#include "gskstream.h"

G_BEGIN_DECLS

typedef struct _GskStreamConnection GskStreamConnection;

GType gsk_stream_connection_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_STREAM_CONNECTION	(gsk_stream_connection_get_type ())
#define GSK_STREAM_CONNECTION(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_STREAM_CONNECTION, GskStreamConnection))
#define GSK_IS_STREAM_CONNECTION(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_STREAM_CONNECTION))

GskStreamConnection *
       gsk_stream_connection_new              (GskStream        *input_stream,
                                               GskStream        *output_stream,
              		                       GError          **error);
void   gsk_stream_connection_detach           (GskStreamConnection *connection);
void   gsk_stream_connection_shutdown         (GskStreamConnection *connection);
void   gsk_stream_connection_set_max_buffered (GskStreamConnection *connection,
              		                       guint                max_buffered);
guint  gsk_stream_connection_get_max_buffered (GskStreamConnection *connection);
void   gsk_stream_connection_set_atomic_read_size(GskStreamConnection *connection,
              		                       guint                atomic_read_size);
guint  gsk_stream_connection_get_atomic_read_size(GskStreamConnection *connection);

#define gsk_stream_connection_peek_read_side(conn) ((conn)->read_side)
#define gsk_stream_connection_peek_write_side(conn) ((conn)->write_side)

/* private, but useful for debugging */
struct _GskStreamConnection 
{
  GObject      object;

  /* The stream to read from. */
  GskStream *read_side;

  /* The stream to write to. */
  GskStream *write_side;

  /* Whether we are blocking the read-side because the buffer is 0 length. */
  guint blocking_write_side : 1;

  /* Whether we are blocking the write-side because the buffer is too long. */
  guint blocking_read_side : 1;

  /* Whether to use gsk_stream_read_buffer() on the read-side.
     This is TRUE by default, even though it can cause
     both max_buffered and atomic_read_size to be violated. */
  guint use_read_buffer : 1;

  /* Data which is to be transferred from read_side to write_side,
     which hasn't been processed on the write side. */
  GskBuffer buffer;

  /* The maximum number of bytes to store in buffer. */
  guint max_buffered;

  /* The maximum number of bytes to read atomically from the input stream. */
  guint atomic_read_size;
};

G_END_DECLS

#endif
