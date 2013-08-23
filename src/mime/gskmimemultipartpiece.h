#ifndef __GSK_MIME_MULTIPART_PIECE_H_
#define __GSK_MIME_MULTIPART_PIECE_H_

#include "../gskstream.h"

G_BEGIN_DECLS

typedef struct _GskMimeMultipartPiece GskMimeMultipartPiece;
struct _GskMimeMultipartPiece
{
  char *type;
  char *subtype;
  char *id;
  char *description;
  char *charset;
  char *location;
  char *transfer_encoding;
  char *disposition;
  char **other_fields;

  /* if is_memory */
  guint content_length;
  gconstpointer content_data;
  GDestroyNotify destroy;	/*< private >*/
  gpointer destroy_data;	/*< private >*/

  /* if !is_memory */
  GskStream *content;

  guint16 is_memory : 1;

  /*< private >*/
  guint16 ref_count;
};

GskMimeMultipartPiece *gsk_mime_multipart_piece_alloc (void);
GskMimeMultipartPiece *gsk_mime_multipart_piece_ref   (GskMimeMultipartPiece *piece);
void                   gsk_mime_multipart_piece_unref (GskMimeMultipartPiece *piece);

void  gsk_mime_multipart_piece_set_data (GskMimeMultipartPiece *piece,
					 gconstpointer          data,
					 guint                  len,
					 GDestroyNotify         destroy,
					 gpointer               destroy_data);
void
gsk_mime_multipart_piece_set_stream     (GskMimeMultipartPiece *piece,
				         GskStream             *stream);
void 
gsk_mime_multipart_piece_set_description(GskMimeMultipartPiece *piece,
					 const char            *description);
void gsk_mime_multipart_piece_set_id    (GskMimeMultipartPiece *piece,
				         const char            *id);
void
gsk_mime_multipart_piece_set_location   (GskMimeMultipartPiece *piece,
				         const char            *location);
void gsk_mime_multipart_piece_set_transfer_encoding
                                        (GskMimeMultipartPiece *piece,
					 const char            *encoding);
void gsk_mime_multipart_piece_set_type  (GskMimeMultipartPiece *piece,
				         const char            *type,
					 const char            *subtype,
					 const char            *charset,
					 const char * const    *kv_pairs);




G_END_DECLS

#endif
