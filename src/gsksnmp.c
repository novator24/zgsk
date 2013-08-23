/* packing messages */

guint8 *
gsk_snmp_pack_generic        (GskSnmpMsgType           msg_type,
                              const char              *community,
                              guint32                  request_id,
                              GskSnmpErrorStatus       error_status,
                              guint                    error_index,
                              guint                    n_object_names,
                              const GskAsn1ObjectName *object_names,
                              const GskAsn1ObjectValue*object_values,
                              guint                   *packed_len_out)
{
  GskAsn1Buffer buffer;
  guint8 tmp_buf[256];
  guint community_len = strlen (community);
  gsk_asn1_buffer_init (&buffer);
  gsk_asn1_buffer_append_int_triple (&buffer, 0);  /* version */
  gsk_asn1_buffer_append_octet_string_triple (&buffer, community, -1);

  gsk_asn1_buffer_append (&buffer, (guint8 *) "\2\1\0", 3);
  gsk_asn1_buffer_append (&buffer, (guint8 *) "\4", 1);
  tmp_len = gsk_asn1_length_encode (community_len, tmp_buf);
  gsk_asn1_buffer_append (&buffer, tmp_buf, tmp_len);
  gsk_asn1_buffer_append (&buffer, (guint8 *) community, community_len);

guint8 *gsk_snmp_pack_trap           (const char              *community,
                                      const GskAsn1ObjectName *enterprise,
                                      const GskAsn1NetworkAddr *addr,
                                      const GskSnmpTrapType    trap_type,
                                      guint                    trap_id,
                                      guint                    unix_time,
                                      guint                    n_object_names,
                                      const GskAsn1ObjectName *object_names);
