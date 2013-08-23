#ifndef __GSK_MIME_MULTIPART_DECODER_H_
#define __GSK_MIME_MULTIPART_DECODER_H_

/* Implements RFC 2046, Section 5: MIME MultipartDecoder Media Types */
#include "../gskstream.h"
#include "gskmimemultipartpiece.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskMimeMultipartDecoder GskMimeMultipartDecoder;
typedef struct _GskMimeMultipartDecoderClass GskMimeMultipartDecoderClass;

typedef gboolean (*GskMimeMultipartDecoderHook) (GskMimeMultipartDecoder *multipart_decoder);

/* --- type macros --- */
GType gsk_mime_multipart_decoder_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MIME_MULTIPART_DECODER			(gsk_mime_multipart_decoder_get_type ())
#define GSK_MIME_MULTIPART_DECODER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_MIME_MULTIPART_DECODER, GskMimeMultipartDecoder))
#define GSK_MIME_MULTIPART_DECODER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_MIME_MULTIPART_DECODER, GskMimeMultipartDecoderClass))
#define GSK_MIME_MULTIPART_DECODER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_MIME_MULTIPART_DECODER, GskMimeMultipartDecoderClass))
#define GSK_IS_MIME_MULTIPART_DECODER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_MIME_MULTIPART_DECODER))
#define GSK_IS_MIME_MULTIPART_DECODER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_MIME_MULTIPART_DECODER))

/* --- structures --- */

/* This controls whether large data is streamed out from the
   multipart_decoder.  The default is to put small data in memory
   and large data as streams. */
typedef enum
{
  GSK_MIME_MULTIPART_DECODER_MODE_DEFAULT,
  GSK_MIME_MULTIPART_DECODER_MODE_ALWAYS_MEMORY,
  GSK_MIME_MULTIPART_DECODER_MODE_ALWAYS_STREAM
} GskMimeMultipartDecoderMode;

struct _GskMimeMultipartDecoderClass 
{
  GskStreamClass base_class;

  gboolean (*set_poll_new_part) (GskMimeMultipartDecoder *decoder,
				 gboolean do_polling);
  void     (*shutdown_new_part) (GskMimeMultipartDecoder *decoder);
};
struct _GskMimeMultipartDecoder 
{
  GskStream      base_instance;

  /* these are parsed out of the initial key-value pairs. */
  char *type;		/* 3.1: type of the body */
  char *start;		/* 3.2: content-id of root part */
  char *start_info;	/* 3.3: random application information */


  /*< private >*/
  GskBuffer buffer;
  GskHook new_part_available;
  GSList *first_piece;
  GSList *last_piece;

  guint is_shutdown : 1;
  guint swallowed_crlf : 1;

  /* The piece which is currently
     being parsed.
     This is non-NULL only if reading this piece. */
  GskMimeMultipartPiece *current_piece;

  /* This GskBufferStream is given encoded data:
     it is will be decoded then either given to the user directly,
     or if the data is being force into an in-memory buffer,
     it is handled by a gsk_memory_buffer_sink, which eventually
     exports it into the buffer in current_piece.

     (Since it is a GskBufferStream we can always write to it
     without blocking). */
  GskStream *feed_stream;

  GskMimeMultipartDecoderMode mode;
  guint8 feed_stream_encoding;
  char *boundary_str;
  unsigned boundary_str_len;

  guint n_pieces_alloced;
  guint n_pieces_obtained;
  guint next_piece_index_to_append;
  GHashTable *piece_index_to_piece;

  guint8 state;

  gboolean got_terminal_boundary;
};


/* --- prototypes --- */
GskMimeMultipartDecoder *gsk_mime_multipart_decoder_new       (char                       **kv_pairs);
void                   gsk_mime_multipart_decoder_set_mode    (GskMimeMultipartDecoder     *decoder,
						               GskMimeMultipartDecoderMode  mode);
GskMimeMultipartPiece *gsk_mime_multipart_decoder_get_piece   (GskMimeMultipartDecoder     *decoder);

#define gsk_mime_multipart_decoder_trap(multipart_decoder, func, shutdown, data, destroy)	\
  gsk_hook_trap (_GSK_MIME_MULTIPART_DECODER_HOOK(multipart_decoder),				\
		 (GskHookFunc) func, (GskHookFunc) shutdown, 			\
		 (data), (destroy))
#define gsk_mime_multipart_decoder_untrap(multipart_decoder)  gsk_hook_untrap (_GSK_MIME_MULTIPART_DECODER_HOOK(multipart_decoder))
#define gsk_mime_multipart_decoder_block(multipart_decoder)   gsk_hook_block (_GSK_MIME_MULTIPART_DECODER_HOOK(multipart_decoder))
#define gsk_mime_multipart_decoder_unblock(multipart_decoder) gsk_hook_unblock (_GSK_MIME_MULTIPART_DECODER_HOOK(multipart_decoder))

/* implementation bits */
/*< private >*/
#define _GSK_MIME_MULTIPART_DECODER_HOOK(multipart_decoder) (&(GSK_MIME_MULTIPART_DECODER (multipart_decoder)->new_part_available))

G_END_DECLS

#endif
