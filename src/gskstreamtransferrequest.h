#ifndef __GSK_STREAM_TRANSFER_REQUEST_H_
#define __GSK_STREAM_TRANSFER_REQUEST_H_

/*
 * GskStreamTransferRequest -- basically just like a GskStreamConnection,
 * except the requester gets notified when the read stream's data has been
 * fully transferred without error, or when an error occurs.
 */

/* TODO: might be useful to add a mode which only transfers n bytes,
 * and then doesn't shut down the streams.
 */

#include "gskstream.h"
#include "gskrequest.h"

G_BEGIN_DECLS

typedef GskRequestClass                  GskStreamTransferRequestClass;
typedef struct _GskStreamTransferRequest GskStreamTransferRequest;

GType gsk_stream_transfer_request_get_type (void) G_GNUC_CONST;

#define GSK_TYPE_STREAM_TRANSFER_REQUEST \
  (gsk_stream_transfer_request_get_type ())
#define GSK_STREAM_TRANSFER_REQUEST(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
			       GSK_TYPE_STREAM_TRANSFER_REQUEST, \
			       GskStreamTransferRequest))
#define GSK_IS_STREAM_TRANSFER_REQUEST(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_STREAM_TRANSFER_REQUEST))

struct _GskStreamTransferRequest
{
  GskRequest request;

  /* The stream to read from. */
  GskStream *read_side;

  /* The stream to write to. */
  GskStream *write_side;

  /* Data which is to be transferred from read_side to write_side,
   * which hasn't been processed on the write side.
   */
  GskBuffer buffer;

  /* The maximum number of bytes to store in buffer. */
  guint max_buffered;

  /* The maximum number of bytes to read atomically from the input stream. */
  guint atomic_read_size;

  /* Whether we are blocking the read-side because the buffer is 0 length. */
  guint blocking_write_side : 1;

  /* Whether we are blocking the write-side because the buffer is too long. */
  guint blocking_read_side : 1;
};

/* The request references both input_stream and output_stream
 * (i.e., you must also unref them at some point).
 */
GskStreamTransferRequest *
       gsk_stream_transfer_request_new
				  (GskStream                *input_stream,
				   GskStream                *output_stream);

void   gsk_stream_transfer_request_set_max_buffered
				  (GskStreamTransferRequest *request,
				   guint                     max_buffered);

guint  gsk_stream_transfer_request_get_max_buffered
				  (GskStreamTransferRequest *request);

void   gsk_stream_transfer_request_set_atomic_read_size
				  (GskStreamTransferRequest *request,
				   guint                     atomic_read_size);

guint  gsk_stream_transfer_request_get_atomic_read_size
				  (GskStreamTransferRequest *request);

G_END_DECLS

#endif
