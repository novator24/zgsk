#ifndef __GSK_BUFFER_STREAM_H_
#define __GSK_BUFFER_STREAM_H_

#include "gskstream.h"

/* A class to allow quick-and-dirty stream implementations.

   Instead of deriving from a GskStream and doing a full
   implementation, you merely trap the buffer-read/buffer-write
   hooks and fill the buffers directly.

   Because the extensibility is attained through hooks
   you should NOT derive from this class, instead
   just add to the GskBuffers directly.

   For implementing a stream using a buffer-stream,
   you should understand that the read_buffer
   is for putting data that will be gsk_stream_read() out,
   and the write_buffer is for grabbing data
   that will had been gsk_stream_write() in. */

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskBufferStream GskBufferStream;
typedef struct _GskBufferStreamClass GskBufferStreamClass;
/* --- type macros --- */
GType gsk_buffer_stream_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_BUFFER_STREAM			(gsk_buffer_stream_get_type ())
#define GSK_BUFFER_STREAM(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_BUFFER_STREAM, GskBufferStream))
#define GSK_BUFFER_STREAM_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_BUFFER_STREAM, GskBufferStreamClass))
#define GSK_BUFFER_STREAM_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_BUFFER_STREAM, GskBufferStreamClass))
#define GSK_IS_BUFFER_STREAM(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_BUFFER_STREAM))
#define GSK_IS_BUFFER_STREAM_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_BUFFER_STREAM))

/* --- structures --- */
struct _GskBufferStreamClass 		/* final */
{
  GskStreamClass stream_class;

  void (*buffered_read_set_poll) (GskBufferStream *bs, gboolean);
  void (*buffered_write_set_poll) (GskBufferStream *bs, gboolean);
  void (*buffered_read_shutdown) (GskBufferStream *bs);
  void (*buffered_write_shutdown) (GskBufferStream *bs);
};
struct _GskBufferStream 		/* final */
{
  GskStream      stream; /*< private >*/

  /* after modifying any of these you
     must call gsk_buffer_stream_changed()
     EXCEPT that
     you may call gsk_buffer_stream_read_buffer_changed() instead
     if just the read_buffer was modified,
     and likewise 
     you may call gsk_buffer_stream_write_buffer_changed() instead
     if just the write_buffer was modified, */

  GskBuffer      read_buffer;
  GskBuffer      write_buffer;

  /*< private >*/
  guint          max_write_buffer;

  /* Run when the read_buffer has been drained. */
  GskHook        buffered_read_hook;

  /* Run when the write_buffer is non-empty. */
  GskHook        buffered_write_hook;
};

/* --- prototypes --- */
GskBufferStream *gsk_buffer_stream_new (void);

void gsk_buffer_stream_read_buffer_changed  (GskBufferStream *stream);
void gsk_buffer_stream_write_buffer_changed (GskBufferStream *stream);
void gsk_buffer_stream_changed              (GskBufferStream *stream);

#define gsk_buffer_stream_read_hook(stream)		\
	&(GSK_BUFFER_STREAM (stream)->buffered_read_hook)
#define gsk_buffer_stream_write_hook(stream)		\
	&(GSK_BUFFER_STREAM (stream)->buffered_write_hook)

#define gsk_buffer_stream_peek_read_buffer(stream)	\
	(&GSK_BUFFER_STREAM (stream)->read_buffer)
#define gsk_buffer_stream_peek_write_buffer(stream)	\
	(&GSK_BUFFER_STREAM (stream)->write_buffer)
#define gsk_buffer_stream_get_max_write_buffer(stream)	\
	(GSK_BUFFER_STREAM (stream)->max_write_buffer + 0)

/* whether to strictly enforce the max-write-buffer parameter */
#define gsk_buffer_stream_has_strict_max_write(stream)	\
  GSK_HOOK_TEST_USER_FLAG (gsk_buffer_stream_write_hook(stream), 1)
#define gsk_buffer_stream_mark_strict_max_write(stream)	\
  GSK_HOOK_MARK_USER_FLAG (gsk_buffer_stream_write_hook(stream), 1)
#define gsk_buffer_stream_clear_strict_max_write(stream)	\
  GSK_HOOK_CLEAR_USER_FLAG (gsk_buffer_stream_write_hook(stream), 1)

/* Shut the readable end of the stream down immediately
   if the buffer is empty, or shut it down when the buffer empties
   otherwise. */
void gsk_buffer_stream_read_shutdown (GskBufferStream *stream);

G_END_DECLS

#endif
