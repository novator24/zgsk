#ifndef __GSK_SNMP_H_
#define __GSK_SNMP_H_

/* --- protocol --- */
typedef enum
{
  GSK_SNMP_MSG_GET      = 0,
  GSK_SNMP_MSG_GET_NEXT = 1,
  GSK_SNMP_MSG_RESPONSE = 2,
  GSK_SNMP_MSG_SET      = 3,
  GSK_SNMP_MSG_TRAP     = 4
} GskSnmpMsgType;

/* sample GET request: 0)\2\1\0\4\6public\240\34\2\4(\375\254\272\2\1\0\2\1\0000\0160\f\6\10+\6\1\2\1\1\1\0\5\0 */

/* 0       SEQUENCE
 * )       length of payload
 * \2      INTEGER
 * \1      length of version
 * \0      version
 * \4      OCTET_STRING
 * \6      length of "public"
 * public  community
 * \240    ID for CONTEXT_SPECIFIC/CONSTRUCTED type 0
 * \34     length of PDU content
 * \2      INTEGER (the request-id)
 * \4      length of request-id
 * (\375\254\262   the request-id
 * \2      INTEGER (the error-status)
 * \1      length of error-status
 * \0      error-status
 * \2      INTEGER (the error-index)
 * \1      length of error-index
 * \000    error-index
 * 0       SEQUENCE (the variable-bindings)
 * \016    length of sequence
 * 0       SEQUENCE (a VarBind)
 * \f      length of VarBind
 * \6      OBJECT (the name)
 * \10     length of object-name
 * +\6\1\2\1\1\1\0 (the object name)
 * \5      NULL
 * \0      length of NULL
 */

guint8 *gsk_snmp_pack_generic        (GskSnmpMsgType           msg_type,
                                      const char              *community,
                                      guint32                  request_id,
                                      GskSnmpErrorStatus       error_status,
                                      guint                    error_index,
                                      guint                    n_object_names,
                                      const GskAsn1ObjectName *object_names,
                                      const GskAsn1ObjectValue*object_values,
                                      guint                   *packed_len_out);
guint8 *gsk_snmp_pack_trap           (const char              *community,
                                      const GskAsn1ObjectName *enterprise,
                                      const GskAsn1NetworkAddr *addr,
                                      const GskSnmpTrapType    trap_type,
                                      guint                    trap_id,
                                      guint                    unix_time,
                                      guint                    n_object_names,
                                      const GskAsn1ObjectName *object_names);

#define gsk_snmp_pack_get(community, request_id, n_object_names, \
                          object_names, packed_len_out) \
  gsk_snmp_pack_generic(GSK_SNMP_MSG_GET, (community), (request_id), \
                        0, 0, (n_object_names), (object_names), NULL, \
                        (packed_len_out))
#define gsk_snmp_pack_get_next(community, request_id, n_object_names, \
                          object_names, packed_len_out) \
  gsk_snmp_pack_generic(GSK_SNMP_MSG_GET_NEXT, (community), (request_id), \
                        0, 0, (n_object_names), (object_names), NULL, \
                        (packed_len_out))
#define gsk_snmp_pack_response(community, request_id, n_object_names, \
                               object_names, object_values, packed_len_out) \
  gsk_snmp_pack_generic(GSK_SNMP_MSG_RESPONSE, (community), (request_id), \
                        0, 0, \
                        (n_object_names), (object_names), (object_values),\
                        (packed_len_out))
#define gsk_snmp_pack_error_response(community, request_id, n_object_names, \
                                     error_status, error_index, \
                                     object_names, packed_len_out) \
  gsk_snmp_pack_generic(GSK_SNMP_MSG_RESPONSE, (community), (request_id), \
                        (error_status), (error_index), \
                        (n_object_names), (object_names), NULL,\
                        (packed_len_out))
#define gsk_snmp_pack_set(community, request_id, n_object_names, \
                          object_names, object_values, packed_len_out) \
  gsk_snmp_pack_generic(GSK_SNMP_MSG_SET, (community), (request_id), \
                        0, 0, \
                        (n_object_names), (object_names), (object_values),\
                        (packed_len_out))

/* --- client api --- */
GskSnmpClient *gsk_snmp_client_new_udp (const char *hostname,
                                        guint       port);
void           gsk_snmp_client_send    (GskSnmpClient *client,
                                        const char    *community,
                                        guint          n_object_names,
                                        const GskAsn1ObjectName *names,
                                        const GskAsn1ObjectValue *values,
                                        gboolean       do_retry,
                                        GskSnmpClientHandler handler,
                                        gpointer       handler_data);

/* --- server api --- */
GskSnmpServer *gsk_snmp_server_new_udp (guint                    port);
void           gsk_snmp_server_set     (GskSnmpServer           *server,
                                        const char              *community,
                                        const GskAsn1ObjectName *name,
                                        const GskAsn1ObjectValue *value);
void           gsk_snmp_server_unset   (GskSnmpServer           *server,
                                        const char              *community,
                                        const GskAsn1ObjectName *name);

#endif

