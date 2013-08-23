#ifndef __GSK_STREAM_H_
#define __GSK_STREAM_H_

#include "gskbuffer.h"
#include "gskio.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskStream GskStream;
typedef struct _GskStreamClass GskStreamClass;

/* --- type macros --- */
GType gsk_stream_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_STREAM			(gsk_stream_get_type ())
#define GSK_STREAM(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_STREAM, GskStream))
#define GSK_STREAM_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_STREAM, GskStreamClass))
#define GSK_STREAM_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_STREAM, GskStreamClass))
#define GSK_IS_STREAM(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_STREAM))
#define GSK_IS_STREAM_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_STREAM))


/* Convention:
 *
 *
 *      The ends of a stream are the 'read-end' and the 'write-end'.
 *      The read-end is drawn-from using gsk_stream_read()
 *      and the write-end is affected by gsk_stream_write().
 *      
 *      Note that it might be tempting to call the 'read-end' the "output end",
 *      since that is where data is "output" -- that's confusing though --
 *      just avoid reference to output if you want to understand what's
 *      going on.  Likewise thinking of the 'write-end' as "for input" is more
 *      likely to confuse than clarify.
 */

/* --- structures --- */
struct _GskStreamClass 
{
  GskIOClass base_class;

  /* --- virtuals --- */
  guint      (*raw_read)        (GskStream     *stream,
			 	 gpointer       data,
			 	 guint          length,
			 	 GError       **error);
  guint      (*raw_write)       (GskStream     *stream,
			 	 gconstpointer  data,
			 	 guint          length,
			 	 GError       **error);
  guint      (*raw_read_buffer) (GskStream     *stream,
				 GskBuffer     *buffer,
				 GError       **error);
  guint      (*raw_write_buffer)(GskStream    *stream,
				 GskBuffer     *buffer,
				 GError       **error);
};

struct _GskStream 
{
  GskIO        base_io;

  /*< protected >*/
  guint        never_partial_reads : 1;
  guint        never_partial_writes : 1;
};


/* --- prototypes --- */

/* read-from/write-to a stream */
gsize    gsk_stream_read              (GskStream        *stream,
		                       gpointer          buffer,
		                       gsize             buffer_length,
		                       GError          **error);
gsize    gsk_stream_write             (GskStream        *stream,
		                       gconstpointer     buffer,
		                       gsize             buffer_length,
		                       GError          **error);
/* read into buffer from stream */
gsize    gsk_stream_read_buffer       (GskStream        *stream,
		                       GskBuffer        *buffer,
		                       GError          **error);
/* write out of buffer to stream */
gsize    gsk_stream_write_buffer      (GskStream        *stream,
		                       GskBuffer        *buffer,
		                       GError          **error);

/* connections from the output of one stream to the input of another. */
gboolean gsk_stream_attach            (GskStream        *input_stream,
                                       GskStream        *output_stream,
				       GError          **error);

gboolean gsk_stream_attach_pair       (GskStream        *stream_a,
                                       GskStream        *stream_b,
				       GError          **error);

/* public */
#define gsk_stream_get_is_connecting(stream)                gsk_io_get_is_connecting(stream)
#define gsk_stream_get_is_readable(stream)                  gsk_io_get_is_readable(stream)
#define gsk_stream_get_is_writable(stream)                  gsk_io_get_is_writable(stream)
#define gsk_stream_get_never_blocks_write(stream)           gsk_io_get_never_blocks_write(stream)
#define gsk_stream_get_never_blocks_read(stream)            gsk_io_get_never_blocks_read(stream)
#define gsk_stream_get_idle_notify_write(stream)            gsk_io_get_idle_notify_write(stream)
#define gsk_stream_get_idle_notify_read(stream)             gsk_io_get_idle_notify_read(stream)
#define gsk_stream_get_is_open(stream)                      gsk_io_get_is_open (stream)
#define gsk_stream_get_never_partial_reads(stream)          (GSK_STREAM (stream)->never_partial_reads != 0)
#define gsk_stream_get_never_partial_writes(stream)         (GSK_STREAM (stream)->never_partial_writes != 0)
#define gsk_stream_trap_readable                            gsk_io_trap_readable
#define gsk_stream_trap_writable                            gsk_io_trap_writable
#define gsk_stream_untrap_readable                          gsk_io_untrap_readable
#define gsk_stream_untrap_writable                          gsk_io_untrap_writable

/* protected */
#define gsk_stream_mark_is_connecting(stream)               gsk_io_mark_is_connecting(stream)
#define gsk_stream_mark_is_readable(stream)                 gsk_io_mark_is_readable(stream)
#define gsk_stream_mark_is_writable(stream)                 gsk_io_mark_is_writable(stream)
#define gsk_stream_mark_never_blocks_write(stream)          gsk_io_mark_never_blocks_write(stream)
#define gsk_stream_mark_never_blocks_read(stream)           gsk_io_mark_never_blocks_read(stream)
#define gsk_stream_mark_idle_notify_write(stream)           gsk_io_mark_idle_notify_write(stream)
#define gsk_stream_mark_idle_notify_read(stream)            gsk_io_mark_idle_notify_read(stream)
#define gsk_stream_mark_is_open(stream)                     gsk_io_mark_is_open (stream)
#define gsk_stream_mark_never_partial_reads(stream)         G_STMT_START{ GSK_STREAM (stream)->never_partial_reads = 1; }G_STMT_END
#define gsk_stream_mark_never_partial_writes(stream)        G_STMT_START{ GSK_STREAM (stream)->never_partial_writes = 1; }G_STMT_END
#define gsk_stream_clear_is_readable(stream)                gsk_io_clear_is_readable(stream)
#define gsk_stream_clear_is_writable(stream)                gsk_io_clear_is_writable(stream)
#define gsk_stream_clear_is_open(stream)                    gsk_io_clear_is_open (stream)
#define gsk_stream_clear_idle_notify_write(stream)          gsk_io_clear_idle_notify_write(stream)
#define gsk_stream_clear_idle_notify_read(stream)           gsk_io_clear_idle_notify_read(stream)
#define gsk_stream_clear_never_partial_reads(stream)        G_STMT_START{ GSK_STREAM (stream)->never_partial_reads = 0; }G_STMT_END
#define gsk_stream_clear_never_partial_writes(stream)       G_STMT_START{ GSK_STREAM (stream)->never_partial_writes = 0; }G_STMT_END

G_END_DECLS

#endif
