/* GskStreamQueue: a container class for streams.
   you can add input or output streams and they will be read/written to
   until they shutdown.
   
   This class can also be used as a proxy. */

#ifndef __GSK_STREAM_QUEUE_H_
#define __GSK_STREAM_QUEUE_H_

#include "gskstream.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskStreamQueue GskStreamQueue;
typedef struct _GskStreamQueueClass GskStreamQueueClass;
/* --- type macros --- */
GType gsk_stream_queue_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_STREAM_QUEUE			(gsk_stream_queue_get_type ())
#define GSK_STREAM_QUEUE(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_STREAM_QUEUE, GskStreamQueue))
#define GSK_STREAM_QUEUE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_STREAM_QUEUE, GskStreamQueueClass))
#define GSK_STREAM_QUEUE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_STREAM_QUEUE, GskStreamQueueClass))
#define GSK_IS_STREAM_QUEUE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_STREAM_QUEUE))
#define GSK_IS_STREAM_QUEUE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_STREAM_QUEUE))

/* --- structures --- */
struct _GskStreamQueueClass 
{
  GskStreamClass base_class;
  void (*set_read_empty_poll) (GskStreamQueue *, gboolean);
  void (*set_write_empty_poll) (GskStreamQueue *, gboolean);
};
struct _GskStreamQueue 
{
  GskStream      base_instance;

  /* a list of readable streams; the head of the queue will
     be removed once they reached end-of-file. */
  GQueue        *read_streams;
  GQueue        *write_streams;
  gboolean       no_more_read_streams;
  gboolean       no_more_write_streams;
  gboolean       is_reading, is_writing;

  /* These hooks are tripped whenever there are no more
     streams (the read_empty_hook is triggered
     when there are no more streams to read from;
     the write_empty_hook is triggered
     when there are no more streams to write to) */
  GskHook        read_empty_hook;
  GskHook        write_empty_hook;
};

/* --- prototypes --- */
GskStreamQueue * gsk_stream_queue_new       (gboolean is_readable,
                                             gboolean is_writable);

void gsk_stream_queue_append_read_stream    (GskStreamQueue *queue,
				             GskStream      *substream);
void gsk_stream_queue_append_write_stream   (GskStreamQueue *queue,
				             GskStream      *substream);
void gsk_stream_queue_no_more_read_streams  (GskStreamQueue *queue);
void gsk_stream_queue_no_more_write_streams (GskStreamQueue *queue);

/* --- trapping the 'is_empty' event --- */
#define GSK_STREAM_QUEUE_READ_EMPTY_HOOK(queue)                                \
    (&(GSK_STREAM_QUEUE (queue)->read_empty_hook))
#define gsk_stream_queue_trap_read_empty(queue, func, shutdown, data, destroy) \
  gsk_hook_trap(GSK_STREAM_QUEUE_READ_EMPTY_HOOK(queue),                       \
		(GskHookFunc) func, (GskHookFunc) shutdown, data, destroy)
#define gsk_stream_queue_untrap_read_empty(queue)                              \
  gsk_hook_untrap(GSK_STREAM_QUEUE_READ_EMPTY_HOOK(queue))
#define GSK_STREAM_QUEUE_WRITE_EMPTY_HOOK(queue)                               \
    (&(GSK_STREAM_QUEUE (queue)->write_empty_hook))
#define gsk_stream_queue_trap_write_empty(queue, func, shutdown, data, destroy)\
  gsk_hook_trap(GSK_STREAM_QUEUE_WRITE_EMPTY_HOOK(queue), \
		(GskHookFunc) func, (GskHookFunc) shutdown, data, destroy)
#define gsk_stream_queue_untrap_write_empty(queue) \
  gsk_hook_untrap(GSK_STREAM_QUEUE_WRITE_EMPTY_HOOK(queue))


G_END_DECLS

#endif
