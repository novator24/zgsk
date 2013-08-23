#ifndef __GSK_MIME_MULTIPART_ENCODER_H_
#define __GSK_MIME_MULTIPART_ENCODER_H_

#include "gskmimemultipartpiece.h"
#include "../gskstream.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskMimeMultipartEncoder GskMimeMultipartEncoder;
typedef struct _GskMimeMultipartEncoderClass GskMimeMultipartEncoderClass;
/* --- type macros --- */
GType gsk_mime_multipart_encoder_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MIME_MULTIPART_ENCODER			(gsk_mime_multipart_encoder_get_type ())
#define GSK_MIME_MULTIPART_ENCODER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_MIME_MULTIPART_ENCODER, GskMimeMultipartEncoder))
#define GSK_MIME_MULTIPART_ENCODER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_MIME_MULTIPART_ENCODER, GskMimeMultipartEncoderClass))
#define GSK_MIME_MULTIPART_ENCODER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_MIME_MULTIPART_ENCODER, GskMimeMultipartEncoderClass))
#define GSK_IS_MIME_MULTIPART_ENCODER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_MIME_MULTIPART_ENCODER))
#define GSK_IS_MIME_MULTIPART_ENCODER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_MIME_MULTIPART_ENCODER))

/* --- structures --- */
struct _GskMimeMultipartEncoderClass 
{
  GskStreamClass stream_class;
  void (*new_part_needed_set_poll) (GskMimeMultipartEncoder  *encoder,
				    gboolean                  do_poll);
  void (*new_part_needed_shutdown) (GskMimeMultipartEncoder  *encoder);
};

struct _GskMimeMultipartEncoder 
{
  GskStream      stream;
  GskHook        new_part_needed;
  GQueue        *outgoing_pieces;
  GskStream     *active_stream;
  GskBuffer      outgoing_data;

  char          *boundary_str;

  guint          max_buffered;
  guint          blocked_active_stream : 1;
  guint          shutdown : 1;
  guint          wrote_terminator : 1;
};

/* --- prototypes --- */
#define GSK_MIME_MULTIPART_ENCODER_GOOD_BOUNDARY	"_=_"

GskMimeMultipartEncoder *gsk_mime_multipart_encoder_new (const char *boundary);
gboolean gsk_mime_multipart_encoder_add_part (GskMimeMultipartEncoder *encoder,
					      GskMimeMultipartPiece   *piece,
					      GError                 **error);

#define gsk_mime_multipart_encoder_new_defaults()	                 \
  gsk_mime_multipart_encoder_new(GSK_MIME_MULTIPART_ENCODER_GOOD_BOUNDARY)
#define gsk_mime_multipart_encoder_trap_part_needed(encoder, func, data) \
  gsk_hook_trap(_GSK_MIME_MULTIPART_ENCODER_HOOK(encoder),               \
		(GskHookFunc) (func), (data))
#define  gsk_mime_multipart_encoder_terminate(encoder)			 \
  gsk_hook_shutdown(_GSK_MIME_MULTIPART_ENCODER_HOOK(encoder), NULL)

/*< private >*/
#define _GSK_MIME_MULTIPART_ENCODER_HOOK(encoder)			 \
  (&((GSK_MIME_MULTIPART_ENCODER(encoder))->new_part_needed))

G_END_DECLS

#endif
