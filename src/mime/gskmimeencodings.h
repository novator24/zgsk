#ifndef __GSK_MIME_ENCODINGS_H_
#define __GSK_MIME_ENCODINGS_H_

#include "../gskstream.h"

G_BEGIN_DECLS

/* For a given transfer-encodining, make a chain of streams
   (possibly just a single stream will suffix) or
   give an error. */
gboolean
gsk_mime_make_transfer_encoding_decoders (const char *encoding,
		                          GskStream **write_end_out,
		                          GskStream **read_end_out,
		                          GError    **error);

gboolean
gsk_mime_make_transfer_encoding_encoders (const char *encoding,
		                          GskStream **write_end_out,
		                          GskStream **read_end_out,
					  const char *bdy_string_out,
		                          GError    **error);


GskStream *gsk_mime_base64_decoder_new (void);
GskStream *gsk_mime_base64_encoder_new (void);
GskStream *gsk_mime_quoted_printable_decoder_new (void);
GskStream *gsk_mime_quoted_printable_encoder_new (void);
GskStream *gsk_mime_identity_filter_new (void);

G_END_DECLS

#endif
