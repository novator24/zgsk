#include "gskstreamqueue.h"
#include "gskstreamconcat.h"

GskStream *
gsk_streams_concat_and_unref (GskStream *stream0,
			      ...)
{
  GskStreamQueue *queue = gsk_stream_queue_new (TRUE, FALSE);
  GskStream *stream;
  va_list args;
  va_start (args, stream0);
  for (stream = stream0; stream != NULL; stream = va_arg (args, GskStream *))
    {
      gsk_stream_queue_append_read_stream (queue, stream);
      g_object_unref (stream);
    }
  va_end (args);
  gsk_stream_queue_no_more_read_streams (queue);
  return GSK_STREAM (queue);
}

GskStream *
gsk_streams_concat_v       (unsigned    n_streams,
                            GskStream **streams)
{
  GskStreamQueue *queue = gsk_stream_queue_new (TRUE, FALSE);
  unsigned i;
  for (i = 0; i < n_streams; i++)
    gsk_stream_queue_append_read_stream (queue, streams[i]);
  gsk_stream_queue_no_more_read_streams (queue);
  return GSK_STREAM (queue);
}
