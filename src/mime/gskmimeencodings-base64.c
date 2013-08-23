#include "gskmimeencodings.h"
#include "../gsksimplefilter.h"
#include "../gskmacros.h"
#include <string.h>
#include <ctype.h>

/* --- common to base64 encoder and decoder --- */
static GObjectClass *parent_class = NULL;

#define            base64_terminal_char	'='
static const char *base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                  "abcdefghijklmnopqrstuvwxyz"
			          "0123456789"
			          "+/";
static guint8      ascii_to_base64[256];

#define BASE64_VALUE_WHITESPACE		255
#define BASE64_VALUE_TERMINAL		254
#define BASE64_VALUE_BAD_CHAR		253

/* ==================== GskMimeBase64Decoder =========================== */
/* --- typedefs --- */
typedef struct _GskMimeBase64Decoder GskMimeBase64Decoder;
typedef struct _GskMimeBase64DecoderClass GskMimeBase64DecoderClass;
/* --- type macros --- */
GType gsk_mime_base64_decoder_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MIME_BASE64_DECODER			(gsk_mime_base64_decoder_get_type ())
#define GSK_MIME_BASE64_DECODER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_MIME_BASE64_DECODER, GskMimeBase64Decoder))
#define GSK_MIME_BASE64_DECODER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_MIME_BASE64_DECODER, GskMimeBase64DecoderClass))
#define GSK_MIME_BASE64_DECODER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_MIME_BASE64_DECODER, GskMimeBase64DecoderClass))
#define GSK_IS_MIME_BASE64_DECODER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_MIME_BASE64_DECODER))
#define GSK_IS_MIME_BASE64_DECODER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_MIME_BASE64_DECODER))

/* --- structures --- */
struct _GskMimeBase64DecoderClass 
{
  GskSimpleFilterClass simple_filter_class;
};
struct _GskMimeBase64Decoder 
{
  GskSimpleFilter      simple_filter;

  guint8 cur_bits_in_byte;
  guint8 byte;
  guint8 got_terminal : 1;
};

/* --- GskSimpleFilter methods --- */
static inline gboolean
decoder_process_one (GskMimeBase64Decoder *decoder,
                     GskBuffer            *dst,
		     int                   input,
		     GError              **error)
{
  guint cur_bits = decoder->cur_bits_in_byte;
  guint8 cur_value = decoder->byte;
  guint8 b64 = ascii_to_base64[input];
  if (b64 == BASE64_VALUE_WHITESPACE)
    return TRUE;
  if (b64 == BASE64_VALUE_TERMINAL)
    {
      decoder->got_terminal = 1;
      return TRUE;
    }
  if (b64 == BASE64_VALUE_BAD_CHAR)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
		   GSK_ERROR_BAD_FORMAT,
		   _("did not expect character 0x%02x in base64 stream"),
		   input);
      return FALSE;
    }
  switch (cur_bits)
    {
    case 0:
      cur_value |= (b64 << 2);
      cur_bits = 6;
      break;
    case 2:
      gsk_buffer_append_char (dst, cur_value | b64);
      cur_bits = 0;
      cur_value = 0;
      break;
    case 4:
      gsk_buffer_append_char (dst, cur_value | (b64 >> 2));
      cur_bits = 2;
      cur_value = b64 << 6;
      break;
    case 6:
      gsk_buffer_append_char (dst, cur_value | (b64 >> 4));
      cur_bits = 4;
      cur_value = b64 << 4;
      break;
    }
  decoder->cur_bits_in_byte = cur_bits;
  decoder->byte = cur_value;
  return TRUE;
}


static gboolean
gsk_mime_base64_decoder_process (GskSimpleFilter *filter,
                                 GskBuffer       *dst,
                                 GskBuffer       *src,
                                 GError         **error)
{
  GskMimeBase64Decoder *decoder = GSK_MIME_BASE64_DECODER (filter);
  /* Decode to binary data one character at a time. */
  int c;
  while ((c = gsk_buffer_read_char (src)) != -1)
    {
      if (!decoder_process_one (decoder, dst, c, error))
	return FALSE;
    }
  return TRUE;
}

static gboolean
gsk_mime_base64_decoder_flush (GskSimpleFilter *filter,
                               GskBuffer       *dst,
                               GskBuffer       *src,
                               GError         **error)
{
  gsk_mime_base64_decoder_process (filter, dst, src, error);
  if (!GSK_MIME_BASE64_DECODER (filter)->got_terminal)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
		   GSK_ERROR_BAD_FORMAT,
		   _("missing terminal '%c' in base64 encoded stream"),
		   base64_terminal_char);
      return FALSE;
    }
  return TRUE;
}

/* --- functions --- */
static void
gsk_mime_base64_decoder_init (GskMimeBase64Decoder *mime_base64_decoder)
{
}

static void
gsk_mime_base64_decoder_class_init (GskMimeBase64DecoderClass *class)
{
  GskSimpleFilterClass *simple_filter_class = GSK_SIMPLE_FILTER_CLASS (class);
  guint i;
  parent_class = g_type_class_peek_parent (class);
  simple_filter_class->process = gsk_mime_base64_decoder_process;
  simple_filter_class->flush = gsk_mime_base64_decoder_flush;

  memset (ascii_to_base64, BASE64_VALUE_BAD_CHAR, 256);
  for (i = 1; i < 128; i++)
    if (isspace (i))
      ascii_to_base64[i] = BASE64_VALUE_WHITESPACE;
  ascii_to_base64[base64_terminal_char] = BASE64_VALUE_TERMINAL;
  for (i = 0; i < 64; i++)
    ascii_to_base64[(guint8)base64_chars[i]] = i;
}

GType gsk_mime_base64_decoder_get_type()
{
  static GType mime_base64_decoder_type = 0;
  if (!mime_base64_decoder_type)
    {
      static const GTypeInfo mime_base64_decoder_info =
      {
	sizeof(GskMimeBase64DecoderClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_mime_base64_decoder_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskMimeBase64Decoder),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_mime_base64_decoder_init,
	NULL		/* value_table */
      };
      mime_base64_decoder_type = g_type_register_static (GSK_TYPE_SIMPLE_FILTER,
                                                  "GskMimeBase64Decoder",
						  &mime_base64_decoder_info, 0);
    }
  return mime_base64_decoder_type;
}

/**
 * gsk_mime_base64_decoder_new:
 *
 * Allocate a new MIME encoder which
 * takes base64 encoded data in and gives
 * raw binary data out.
 *
 * returns: the newly allocated decoder.
 */
GskStream *
gsk_mime_base64_decoder_new (void)
{
  return g_object_new (GSK_TYPE_MIME_BASE64_DECODER, NULL);
}

/* ==================== GskMimeBase64Encoder =========================== */

/* --- typedefs --- */
typedef struct _GskMimeBase64Encoder GskMimeBase64Encoder;
typedef struct _GskMimeBase64EncoderClass GskMimeBase64EncoderClass;
/* --- type macros --- */
GType gsk_mime_base64_encoder_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MIME_BASE64_ENCODER			(gsk_mime_base64_encoder_get_type ())
#define GSK_MIME_BASE64_ENCODER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_MIME_BASE64_ENCODER, GskMimeBase64Encoder))
#define GSK_MIME_BASE64_ENCODER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_MIME_BASE64_ENCODER, GskMimeBase64EncoderClass))
#define GSK_MIME_BASE64_ENCODER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_MIME_BASE64_ENCODER, GskMimeBase64EncoderClass))
#define GSK_IS_MIME_BASE64_ENCODER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_MIME_BASE64_ENCODER))
#define GSK_IS_MIME_BASE64_ENCODER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_MIME_BASE64_ENCODER))

/* --- structures --- */
struct _GskMimeBase64EncoderClass 
{
  GskSimpleFilterClass simple_filter_class;
};
struct _GskMimeBase64Encoder 
{
  GskSimpleFilter      simple_filter;
  guint chars_per_line;
  guint chars_in_this_line;
  guint8 n_bits;	/* 0,2,4 */
  guint8 partial_data;	/* the first n_bits on 32,16,8,4,2,1 used */
};
/* --- prototypes --- */

/* --- GskSimpleFilter methods --- */
static gboolean
gsk_mime_base64_encoder_process (GskSimpleFilter *filter,
                                 GskBuffer       *dst,
                                 GskBuffer       *src,
                                 GError         **error)
{
  GskMimeBase64Encoder *encoder = GSK_MIME_BASE64_ENCODER (filter);
  guint8 n_bits = encoder->n_bits;
  guint8 partial_data = encoder->partial_data;
  guint chars_in_this_line = encoder->chars_in_this_line;
  guint chars_per_line = encoder->chars_per_line;
  int c;
  while ((c=gsk_buffer_read_char (src)) != -1)
    {
      /* Append 6 bits of data to the stream (encoding to the
	 base64 character set).  Uses chars_in_this_line,chars_per_line.
       */
#define APPEND_6BITS_AND_MAYBE_ADD_NEWLINE(value)		\
      G_STMT_START{						\
	gsk_buffer_append_char (dst, base64_chars[(value)]);	\
	if (++chars_in_this_line == chars_per_line)		\
	  {							\
	    gsk_buffer_append (dst, "\r\n", 2);			\
	    chars_in_this_line = 0;				\
	  }							\
      }G_STMT_END
      switch (n_bits)
	{
	case 0:
	  APPEND_6BITS_AND_MAYBE_ADD_NEWLINE (c>>2);
	  n_bits = 2;
	  partial_data = (c & 3) << 4;
	  break;
	case 2:
	  APPEND_6BITS_AND_MAYBE_ADD_NEWLINE (partial_data | (c>>4));
	  n_bits = 4;
	  partial_data = (c & 15) << 2;
	  break;
	case 4:
	  APPEND_6BITS_AND_MAYBE_ADD_NEWLINE (partial_data | (c>>6));
	  APPEND_6BITS_AND_MAYBE_ADD_NEWLINE (c % (1<<6));
	  n_bits = 0;
	  partial_data = 0;
	  break;
	}
    }
  encoder->n_bits = n_bits;
  encoder->partial_data = partial_data;
  encoder->chars_in_this_line = chars_in_this_line;
  encoder->chars_per_line = chars_per_line;
  return TRUE;
}

static gboolean
gsk_mime_base64_encoder_flush (GskSimpleFilter *filter,
                               GskBuffer       *dst,
                               GskBuffer       *src,
                               GError         **error)
{
  GskMimeBase64Encoder *encoder = GSK_MIME_BASE64_ENCODER (filter);

  /* These are use by APPEND_CHARS_AND_MAYBE_ADD_NEWLINE. */
  guint chars_in_this_line = encoder->chars_in_this_line;
  guint chars_per_line = encoder->chars_per_line;

  g_return_val_if_fail (src->size == 0, FALSE);
  if (encoder->n_bits)
    APPEND_6BITS_AND_MAYBE_ADD_NEWLINE (encoder->partial_data);
  gsk_buffer_append (dst, "=\r\n", 3);
  return TRUE;
}

/* --- functions --- */
static void
gsk_mime_base64_encoder_init (GskMimeBase64Encoder *mime_base64_encoder)
{
}

static void
gsk_mime_base64_encoder_class_init (GskMimeBase64EncoderClass *class)
{
  GskSimpleFilterClass *simple_filter_class = GSK_SIMPLE_FILTER_CLASS (class);
  parent_class = g_type_class_peek_parent (class);
  simple_filter_class->process = gsk_mime_base64_encoder_process;
  simple_filter_class->flush = gsk_mime_base64_encoder_flush;
}

GType gsk_mime_base64_encoder_get_type()
{
  static GType mime_base64_encoder_type = 0;
  if (!mime_base64_encoder_type)
    {
      static const GTypeInfo mime_base64_encoder_info =
      {
	sizeof(GskMimeBase64EncoderClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_mime_base64_encoder_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskMimeBase64Encoder),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_mime_base64_encoder_init,
	NULL		/* value_table */
      };
      mime_base64_encoder_type = g_type_register_static (GSK_TYPE_SIMPLE_FILTER,
                                                  "GskMimeBase64Encoder",
						  &mime_base64_encoder_info, 0);
    }
  return mime_base64_encoder_type;
}

/**
 * gsk_mime_base64_encoder_new:
 *
 * Allocate a new MIME encoder which
 * takes raw binary data in and gives
 * base64 encoded data out.
 *
 * returns: the newly allocated encoder.
 */
GskStream *
gsk_mime_base64_encoder_new (void)
{
  return g_object_new (GSK_TYPE_MIME_BASE64_ENCODER, NULL);
}
