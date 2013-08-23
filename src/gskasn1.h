#ifndef __GSK_ASN1_H_
#define __GSK_ASN1_H_

/* used by ldap and snmp */
/* see: http://asn1.elibel.tm.fr/standards/#asn1 */

typedef enum
{
  GSK_ASN1_LENGTH_TYPE_DEFINITE,
  GSK_ASN1_LENGTH_TYPE_INDEFINITE,
  GSK_ASN1_LENGTH_TYPE_ERROR
} GskAsn1LengthType;

typedef enum
{
  GSK_ASN1_CLASS_UNIVERSAL,
  GSK_ASN1_CLASS_APPLICATION,
  GSK_ASN1_CLASS_CONTEXT_SPECIFIC,
  GSK_ASN1_CLASS_PRIVATE,
} GskAsn1Class;

typedef enum
{
  GSK_ASN1_ID_END_OF_CONTENTS    = 0x00,
  GSK_ASN1_ID_BOOLEAN            = 0x01,
  GSK_ASN1_ID_INTEGER            = 0x02,
  GSK_ASN1_ID_BIT_STRING         = 0x03,
  GSK_ASN1_ID_OCTET_STRING       = 0x04,
  GSK_ASN1_ID_NULL               = 0x05,
  GSK_ASN1_ID_OBJECT             = 0x06,
  GSK_ASN1_ID_OBJECT_DESCRIPTOR  = 0x07,
  GSK_ASN1_ID_EXTERNAL           = 0x08,
  GSK_ASN1_ID_REAL               = 0x09,
  GSK_ASN1_ID_ENUM               = 0x0a,
  GSK_ASN1_ID_UTF8_STRING        = 0x0c,
  GSK_ASN1_ID_UTF8_SEQUENCE      = 0x10,
  GSK_ASN1_ID_UTF8_SET           = 0x11,
  GSK_ASN1_ID_NUMERIC_STRING     = 0x12,
  GSK_ASN1_ID_PRINTABLE_STRING   = 0x13,
  GSK_ASN1_ID_TELETEX_STRING     = 0x14,
  GSK_ASN1_ID_VIDEOTEX_STRING    = 0x15,
  GSK_ASN1_ID_IA5_STRING         = 0x16,
  GSK_ASN1_ID_UTC_TIME           = 0x17,
  GSK_ASN1_ID_GENERALIZED_TIME   = 0x18,
  GSK_ASN1_ID_GRAPHIC_STRING     = 0x19,
  GSK_ASN1_ID_VISIBLE_STRING     = 0x1a,
  GSK_ASN1_ID_GENERAL_STRING     = 0x1b,
  GSK_ASN1_ID_UNIVERSAL_STRING   = 0x1c,
  GSK_ASN1_ID_BMP_STRING         = 0x1e

  _GSK_ASN1_ID_IS_4_BYTES    = 0x10000
} GskAsn1Id;

typedef struct _GskAsn1ObjectValue GskAsn1ObjectValue;
struct _GskAsn1ObjectValue
{
  GskAsn1Id id;
  union
  {
    gboolean v_boolean;
    gint64 v_integer;
    guint v_enum;
    struct {
      guint len;
      const guint8 *data;
    } v_octet_string;
    const char *v_string;
    struct {
      guint len;
      GskAsn1ObjectValue *data;
    } v_sequence;
  } info;
};


GskAsn1LengthType gsk_asn1_length_parse   (const guint8 *data,
                                           guint         data_len,
					   guint        *length_out,
					   guint        *n_bytes_used_out);
guint             gsk_asn1_length_get_size(guint         length);
guint             gsk_asn1_length_encode  (guint         length,
                                           guint8       *encoded_out);

gboolean          gsk_asn1_id_parse       (const guint8 *data,
                                           guint         data_len,
					   gboolean     *primitive_out,
					   GskAsn1Class *class_out,
					   GskAsn1Id    *id_out,
					   guint        *n_bytes_used_out);
guint             gsk_asn1_id_get_size    (GskAsn1Id     id);
guint             gsk_asn1_id_encode      (GskAsn1Id     id,
					   gboolean      primitive,
					   GskAsn1Class  class_,
                                           guint8       *encoded_out);


/* object names and values */
typedef struct
{
  guint n_parts;
  guint *parts;
} GskAsn1ObjectName;

gboolean gsk_asn1_object_name_parse    (const guint8 *data,
                                        guint         data_len,
                                        GskAsn1ObjectName *to_init,
                                        guint        *n_bytes_used_out);
guint    gsk_asn1_object_name_get_size (const GskAsn1ObjectName *name);
guint    gsk_asn1_object_name_encode   (const GskAsn1ObjectName *name,
                                        guint8       *encoded_out);
void     gsk_asn1_object_name_destruct (GskAsn1ObjectName *to_destruct);


gboolean gsk_asn1_object_value_parse   (const guint8 *data,
                                        guint         data_len,
                                        GskAsn1ObjectValue *to_init,
                                        guint        *n_bytes_used_out);
guint    gsk_asn1_object_value_get_size(const GskAsn1ObjectValue *value);
guint    gsk_asn1_object_value_encode  (const GskAsn1ObjectValue *value,
                                        guint8       *encoded_out);
void     gsk_asn1_object_value_destruct(GskAsn1ObjectValue *to_destruct);


typedef struct _GskAsn1Buffer GskAsn1Buffer;
struct _GskAsn1Buffer
{
  guint buf_size;
  guint8 *buf;

  guint start, len;
};

G_INLINE_FUNC void gsk_asn1_buffer_init     (GskAsn1Buffer *buffer);
G_INLINE_FUNC void gsk_asn1_buffer_clear    (GskAsn1Buffer *buffer);
G_INLINE_FUNC void gsk_asn1_buffer_prepend  (GskAsn1Buffer *buffer,
                                             const guint8  *data,
                                             guint          len);
G_INLINE_FUNC void gsk_asn1_buffer_append   (GskAsn1Buffer *buffer,
                                             const guint8  *data,
                                             guint          len);
G_INLINE_FUNC void gsk_asn1_buffer_append_id(GskAsn1Buffer *buffer,
                                             gboolean       is_primitive,
                                             GskAsn1Class   class_,
                                             GskAsn1Id      id);
void gsk_asn1_buffer_append_int_triple (GskAsn1Buffer *buffer,
                                        gint32         value);
void gsk_asn1_buffer_append_octet_string_triple (GskAsn1Buffer *buffer,
                                        const char *value,
                                        gssize      value_len);

void               gsk_asn1_buffer_pad_front(GskAsn1Buffer *buffer,
                                             guint          n_bytes);
void               gsk_asn1_buffer_pad_back (GskAsn1Buffer *buffer,
                                             guint          n_bytes);
void gsk_asn1_buffer_append_big_id(GskAsn1Buffer *buffer,
                                   gboolean       is_primitive,
                                   GskAsn1Class   class_,
                                   GskAsn1Id      id);



#if defined(G_CAN_INLINE) || defined(GSK_INTERNAL_IMPLEMENT_INLINES)
#include <string.h>             /* for memcpy() */
G_INLINE_FUNC void gsk_asn1_buffer_init (GskAsn1Buffer *slab)
{
  slab->buf_size = 1024;
  slab->buf = g_malloc (slab->buf_size);
  slab->start = slab->buf_size / 2;
  slab->len = 0;
}
G_INLINE_FUNC void gsk_asn1_buffer_clear (GskAsn1Buffer *slab)
{
  g_free (slab->buf);
}
G_INLINE_FUNC void
gsk_asn1_buffer_prepend (GskAsn1Buffer *slab,
                         const guint8 *data,
                         guint len)
{
  if (slab->start < len)
    gsk_asn1_buffer_pad_front (slab, len - slab->start);
  slab->start -= len;
  slab->len += len;
  memcpy (slab->buf + slab->start, data, len);
}
G_INLINE_FUNC void
gsk_asn1_buffer_append (GskAsn1Buffer *slab,
                        const guint8 *data,
                        guint len)
{
  guint space_at_back = slab->buf_size - slab->start - slab->len;
  if (G_UNLIKELY (space_at_back < len))
    gsk_asn1_buffer_pad_back (slab, len - space_at_back);
  memcpy (slab->buf + slab->start + slab->len, data, len);
  slab->len += len;
}
G_INLINE_FUNC void
gsk_asn1_buffer_append_id (GskAsn1Buffer *buffer,
                           gboolean       is_primitive,
                           GskAsn1Class   class_,
                           GskAsn1Id      id)
{
  if (id >= 32)
    gsk_asn1_buffer_append_big_id (buffer, is_primitive, class_, id);
  else
    {
      guint8 data = (is_primitive ? 0x00 : 0x20)
                  | (class_ << 6)
                  | id;
      gsk_asn1_buffer_append (buffer, &data, 1);
    }
}

#endif

#endif
