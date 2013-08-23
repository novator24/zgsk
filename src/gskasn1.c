
/* --- GskAsn1Buffer --- */

#define GSK_ASN1_BUFFER_ROUND_CHUNK_LOG2 9  /* 512 byte increments */
#define GSK_ASN1_BUFFER_ROUND_PADDING(value) \
  ((((value) + (1<<GSK_ASN1_BUFFER_ROUND_CHUNK_LOG2) - 1) \
     >> GSK_ASN1_BUFFER_ROUND_CHUNK_LOG2) << GSK_ASN1_BUFFER_ROUND_CHUNK_LOG2)
void
gsk_asn1_buffer_pad_front (GskAsn1Buffer *slab,
                           guint          raw_add)
{
  guint add = GSK_ASN1_BUFFER_ROUND_PADDING (raw_add);
  guint8 *new_buf = g_malloc (buf->buf_size + add);
  memcpy (new_buf + add + slab->start, slab->buf + slab->start, slab->len);
  slab->start += add;
  slab->buf_size += add;
  g_free (slab->buf);
  slab->buf = new_buf;
}
void
gsk_asn1_buffer_pad_back  (GskAsn1Buffer *slab,
                           guint          raw_add)
{
  guint add = GSK_ASN1_BUFFER_ROUND_PADDING (raw_add);
  guint8 *new_buf = g_malloc (buf->buf_size + add);
  memcpy (new_buf + slab->start, slab->buf + slab->start, slab->len);
  slab->buf_size += add;
  g_free (slab->buf);
  slab->buf = new_buf;
}
void gsk_asn1_buffer_append_int_triple (GskAsn1Buffer *buffer,
                                        gint32         value)
{
  guint8 tmp_buf[16];
  guint tmp_len;
  gsk_asn1_buffer_append_id (buffer,
                             TRUE,
                             GSK_ASN1_CLASS_UNIVERSAL,
                             GSK_ASN1_ID_INTEGER);
  tmp_len = gsk_asn1_int_encode (value, tmp_buf + 1);
  tmp_buf[0] = tmp_len;
  gsk_asn1_buffer_append (buffer, tmp_buf, tmp_len + 1);
}

void gsk_asn1_buffer_append_octet_string_triple (GskAsn1Buffer *buffer,
                                        const char *value,
                                        gssize      value_len)
{
  if (value_len < 0)
    value_len = strlen (value);
  gsk_asn1_buffer_append_id (buffer,
                             TRUE,
                             GSK_ASN1_CLASS_UNIVERSAL,
                             GSK_ASN1_ID_OCTET_STRING);
  gsk_asn1_buffer_append_length (buffer, value_len);
  gsk_asn1_buffer_append (buffer, (guint8 *) value, value_len);
}
