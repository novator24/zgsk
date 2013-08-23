/* See RFC 2045, Section 6.7 */
#include "gskmimeencodings.h"
#include "../gsksimplefilter.h"
#include "../gskmacros.h"
#include <string.h>
#include <ctype.h>


/* --- common --- */
static GObjectClass *parent_class = NULL;

/* --- typedefs --- */
typedef struct _GskMimeQuotedPrintableDecoder GskMimeQuotedPrintableDecoder;
typedef struct _GskMimeQuotedPrintableDecoderClass GskMimeQuotedPrintableDecoderClass;
/* --- type macros --- */
GType gsk_mime_quoted_printable_decoder_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MIME_QUOTED_PRINTABLE_DECODER			(gsk_mime_quoted_printable_decoder_get_type ())
#define GSK_MIME_QUOTED_PRINTABLE_DECODER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_MIME_QUOTED_PRINTABLE_DECODER, GskMimeQuotedPrintableDecoder))
#define GSK_MIME_QUOTED_PRINTABLE_DECODER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_MIME_QUOTED_PRINTABLE_DECODER, GskMimeQuotedPrintableDecoderClass))
#define GSK_MIME_QUOTED_PRINTABLE_DECODER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_MIME_QUOTED_PRINTABLE_DECODER, GskMimeQuotedPrintableDecoderClass))
#define GSK_IS_MIME_QUOTED_PRINTABLE_DECODER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_MIME_QUOTED_PRINTABLE_DECODER))
#define GSK_IS_MIME_QUOTED_PRINTABLE_DECODER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_MIME_QUOTED_PRINTABLE_DECODER))

/* --- structures --- */
struct _GskMimeQuotedPrintableDecoderClass 
{
  GskSimpleFilterClass simple_filter_class;
};
struct _GskMimeQuotedPrintableDecoder 
{
  GskSimpleFilter      simple_filter;
};

/* --- GskSimpleFilter methods --- */

static gboolean
quoteprintable_char_to_hexval (char c, guint8 *val_out, GError **error)
{
  if ('0' <= c && c <= '9')
    *val_out = c - '0';
  else if ('A' <= c && c <= 'F')
    *val_out = c - 'A' + 10;
  else
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		   _("quoted-printable: error parsing hex value '%c'"), c);
      return FALSE;
    }
  return TRUE;
}
static gboolean
gsk_mime_quoted_printable_decoder_process (GskSimpleFilter *filter,
                                           GskBuffer       *dst,
                                           GskBuffer       *src,
                                           GError         **error)
{
  for (;;)
    {
      char buf[3];
      guint p = gsk_buffer_peek (src, buf, 3);
      guint ne = 0;
      guint8 hexvalues[2];
      while (ne < p)
	{
	  if (buf[ne] == '=')
	    break;
	  ne++;
	}
      if (ne > 0)
	{
	  gsk_buffer_append (dst, buf, ne);
	  gsk_buffer_discard (src, ne);
	}
      else if (p <= 1)
	{
	  break;
	}
      else if (p == 2 && buf[1] == '\n')
	{
	  gsk_buffer_discard (src, 2);
	}
      else
	{
	  if (buf[1] == '\r' && buf[2] == '\n')
	    gsk_buffer_discard (src, 3);
	  else if (buf[1] == '\n')
	    gsk_buffer_discard (src, 2);
	  else
	    {
	      if (!quoteprintable_char_to_hexval (buf[1], &hexvalues[0], error)
	       || !quoteprintable_char_to_hexval (buf[2], &hexvalues[1], error))
		return FALSE;
	      gsk_buffer_append_char (dst, (hexvalues[0] << 4) | hexvalues[1]);
	      gsk_buffer_discard (src, 3);
	    }
	}
    }
  return TRUE;
}

static gboolean
gsk_mime_quoted_printable_decoder_flush (GskSimpleFilter *filter,
                                         GskBuffer       *dst,
                                         GskBuffer       *src,
                                         GError         **error)
{
  if (src->size > 0)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		   _("unprocessable junk in quoted-printable (%u bytes)"),
		   src->size);
      return FALSE;
    }
  return TRUE;
}

/* --- functions --- */
static void
gsk_mime_quoted_printable_decoder_init (GskMimeQuotedPrintableDecoder *mime_quoted_printable_decoder)
{
}

static void
gsk_mime_quoted_printable_decoder_class_init (GskMimeQuotedPrintableDecoderClass *class)
{
  GskSimpleFilterClass *simple_filter_class = GSK_SIMPLE_FILTER_CLASS (class);
  parent_class = g_type_class_peek_parent (class);
  simple_filter_class->process = gsk_mime_quoted_printable_decoder_process;
  simple_filter_class->flush = gsk_mime_quoted_printable_decoder_flush;
}

GType gsk_mime_quoted_printable_decoder_get_type()
{
  static GType mime_quoted_printable_decoder_type = 0;
  if (!mime_quoted_printable_decoder_type)
    {
      static const GTypeInfo mime_quoted_printable_decoder_info =
      {
	sizeof(GskMimeQuotedPrintableDecoderClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_mime_quoted_printable_decoder_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskMimeQuotedPrintableDecoder),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_mime_quoted_printable_decoder_init,
	NULL		/* value_table */
      };
      mime_quoted_printable_decoder_type = g_type_register_static (GSK_TYPE_SIMPLE_FILTER,
                                                  "GskMimeQuotedPrintableDecoder",
						  &mime_quoted_printable_decoder_info, 0);
    }
  return mime_quoted_printable_decoder_type;
}

/**
 * gsk_mime_quoted_printable_decoder_new:
 *
 * Allocate a new MIME encoder which
 * takes quoted-printable encoded data in and gives
 * raw binary data out.
 * (See RFC 2045, Section 6.7).
 *
 * returns: the newly allocated decoder.
 */
GskStream *
gsk_mime_quoted_printable_decoder_new (void)
{
  return g_object_new (GSK_TYPE_MIME_QUOTED_PRINTABLE_DECODER, NULL);
}

/* ================================= Encoder ================================ */
/* --- typedefs --- */
typedef struct _GskMimeQuotedPrintableEncoder GskMimeQuotedPrintableEncoder;
typedef struct _GskMimeQuotedPrintableEncoderClass GskMimeQuotedPrintableEncoderClass;
/* --- type macros --- */
GType gsk_mime_quoted_printable_encoder_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MIME_QUOTED_PRINTABLE_ENCODER			(gsk_mime_quoted_printable_encoder_get_type ())
#define GSK_MIME_QUOTED_PRINTABLE_ENCODER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_MIME_QUOTED_PRINTABLE_ENCODER, GskMimeQuotedPrintableEncoder))
#define GSK_MIME_QUOTED_PRINTABLE_ENCODER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_MIME_QUOTED_PRINTABLE_ENCODER, GskMimeQuotedPrintableEncoderClass))
#define GSK_MIME_QUOTED_PRINTABLE_ENCODER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_MIME_QUOTED_PRINTABLE_ENCODER, GskMimeQuotedPrintableEncoderClass))
#define GSK_IS_MIME_QUOTED_PRINTABLE_ENCODER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_MIME_QUOTED_PRINTABLE_ENCODER))
#define GSK_IS_MIME_QUOTED_PRINTABLE_ENCODER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_MIME_QUOTED_PRINTABLE_ENCODER))

/* --- structures --- */
struct _GskMimeQuotedPrintableEncoderClass 
{
  GskSimpleFilterClass simple_filter_class;
};
struct _GskMimeQuotedPrintableEncoder 
{
  GskSimpleFilter      simple_filter;
  guint n_chars_in_line;
};
/* --- prototypes --- */

#define IS_ENCODABLE_AS_ITSELF(c)	\
	((33 <= (c) && (c) <= 60) || (62 <= (c) && (c) <= 126))

/* --- GskSimpleFilter methods --- */
static gboolean
gsk_mime_quoted_printable_encoder_process (GskSimpleFilter *filter,
                                           GskBuffer       *dst,
                                           GskBuffer       *src,
                                           GError         **error)
{
  GskMimeQuotedPrintableEncoder *encoder = GSK_MIME_QUOTED_PRINTABLE_ENCODER (filter);
  guint n_chars_in_line = encoder->n_chars_in_line;
  guint n_peeked;
  char buf[256];
  while ((n_peeked = gsk_buffer_peek (src, buf, sizeof(buf))) > 0)
    {
      char *at = buf;
      guint rem = n_peeked;
      while (rem)
	{
	  if (n_chars_in_line > 68)
	    {
	      gsk_buffer_append (dst, "=\r\n", 3);
	      n_chars_in_line = 0;
	    }
	  if (IS_ENCODABLE_AS_ITSELF (*at))
	    {
	      gsk_buffer_append_char (dst, *at);
	      rem--;
	      at++;
	      n_chars_in_line++;
	      continue;
	    }
	  else if (*at == '\r')
	    {
	      if (rem >= 2)
		{
		  if (at[1] == '\n')
		    {
		      gsk_buffer_append (dst, at, 2);
		      rem -= 2;
		      at += 2;
		      n_chars_in_line = 0;
		      continue;
		    }
		}
	      else
		break;
	    }

	  /* encode all other chars as hex */
	  {
	    char tmp[4];
	    g_snprintf (tmp, 4, "=%02X", (guint8) *at);
	    gsk_buffer_append (dst, tmp, 3);
	    rem--;
	    at++;
	    n_chars_in_line += 3;
	  }
	}
      gsk_buffer_discard (src, n_peeked - rem);
      if (n_peeked < sizeof (buf))
	break;
    }
  encoder->n_chars_in_line = n_chars_in_line;
  return TRUE;
}

static gboolean
gsk_mime_quoted_printable_encoder_flush (GskSimpleFilter *filter,
                                         GskBuffer       *dst,
                                         GskBuffer       *src,
                                         GError         **error)
{
  GskMimeQuotedPrintableEncoder *encoder = GSK_MIME_QUOTED_PRINTABLE_ENCODER (filter);
  g_return_val_if_fail (src->size <= 1, FALSE);
  if (src->size == 1)
    {
      gsk_buffer_printf (dst, "=%02X", gsk_buffer_read_char (src));
      encoder->n_chars_in_line += 3;
    }
  if (encoder->n_chars_in_line > 0)
    gsk_buffer_append (dst, "=\r\n", 3);
  return TRUE;
}

/* --- functions --- */
static void
gsk_mime_quoted_printable_encoder_init (GskMimeQuotedPrintableEncoder *mime_quoted_printable_encoder)
{
}
static void
gsk_mime_quoted_printable_encoder_class_init (GskMimeQuotedPrintableEncoderClass *class)
{
  GskSimpleFilterClass *simple_filter_class = GSK_SIMPLE_FILTER_CLASS (class);
  parent_class = g_type_class_peek_parent (class);
  simple_filter_class->process = gsk_mime_quoted_printable_encoder_process;
  simple_filter_class->flush = gsk_mime_quoted_printable_encoder_flush;
}

GType gsk_mime_quoted_printable_encoder_get_type()
{
  static GType mime_quoted_printable_encoder_type = 0;
  if (!mime_quoted_printable_encoder_type)
    {
      static const GTypeInfo mime_quoted_printable_encoder_info =
      {
	sizeof(GskMimeQuotedPrintableEncoderClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_mime_quoted_printable_encoder_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskMimeQuotedPrintableEncoder),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_mime_quoted_printable_encoder_init,
	NULL		/* value_table */
      };
      mime_quoted_printable_encoder_type = g_type_register_static (GSK_TYPE_SIMPLE_FILTER,
                                                  "GskMimeQuotedPrintableEncoder",
						  &mime_quoted_printable_encoder_info, 0);
    }
  return mime_quoted_printable_encoder_type;
}

/**
 * gsk_mime_quoted_printable_encoder_new:
 *
 * Allocate a new MIME encoder which
 * takes raw binary data in and gives
 * quoted-printable encoded data out.
 * (See RFC 2045, Section 6.7).
 *
 * returns: the newly allocated encoder.
 */
GskStream *
gsk_mime_quoted_printable_encoder_new (void)
{
  return g_object_new (GSK_TYPE_MIME_QUOTED_PRINTABLE_ENCODER, NULL);
}
