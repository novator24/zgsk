/*
    GSK - a library to write servers
    Copyright (C) 2001 Dave Benson

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA

    Contact:
        daveb@ffem.org <Dave Benson>
*/

#ifndef __GSK_DNS_H_
#define __GSK_DNS_H_

/* XXX: read about Inverse queries (1034, 3.7.2), Status queries (1034, 3.8).  */


/* XXX: decide:           is a GskDnsZone structure a good idea?
 *                        cf 1034, 4.2.1, for a list of what's needed
 *                        to comprise a dns zone.
 */

/* XXX: GSK_DNS_RR_WELL_KNOWN_SERVICE */

/* GskDnsMessage & friends: basic DNS protocol; client & server */


/* Well-known port for name-servers.  */
#define GSK_DNS_PORT		53

/* The DNS specification is divided between RFC 1034, Concepts & Facilities,
 * and RFC 1035, Implementation and Specification.
 */

typedef struct _GskDnsMessage GskDnsMessage;
typedef struct _GskDnsResourceRecord GskDnsResourceRecord;
typedef struct _GskDnsQuestion GskDnsQuestion;

#include "../gskbuffer.h"
#include "../gskpacket.h"
#include <stdio.h>			/* for FILE, ugh */

/* RFC 1034, 3.6:  Each node has a set of resource information,
 *                 which may be empty.
 *
 * A GskDnsResourceRecord is one element of that set.
 *
 * All the terminology, and many comments, are from that section.
 */


/* GskDnsResourceRecordType: AKA RTYPE: 
 *       Types of `RR's or `ResourceRecord's (values match RFC 1035, 3.2.2)
 */
typedef enum
{
  /* An `A' record:  the ip address of a host. */
  GSK_DNS_RR_HOST_ADDRESS = 1,

  /* A `NS' record:  the authoritative name server for the domain */
  GSK_DNS_RR_NAME_SERVER = 2,

  /* A `CNAME' record:  indicate another name for an alias. */
  GSK_DNS_RR_CANONICAL_NAME = 5,

  /* A `HINFO' record: identifies the CPU and OS used by a host */
  GSK_DNS_RR_HOST_INFO = 13,

  /* A `MX' record */
  GSK_DNS_RR_MAIL_EXCHANGE = 15,

  /* A `PTR' record:  a pointer to another part of the domain name space */
  GSK_DNS_RR_POINTER = 12,

  /* A `SOA' record:  identifies the start of a zone of authority [???] */
  GSK_DNS_RR_START_OF_AUTHORITY = 6,

  /* A `TXT' record:  miscellaneous text */
  GSK_DNS_RR_TEXT = 16,

  /* A `WKS' record:  description of a well-known service */
  GSK_DNS_RR_WELL_KNOWN_SERVICE = 11,

  /* A `AAAA' record: for IPv6 (see RFC 1886) */
  GSK_DNS_RR_HOST_ADDRESS_IPV6 = 28,

  /* --- only allowed for queries --- */

  /* A `AXFR' record: `special zone transfer QTYPE' */
  GSK_DNS_RR_ZONE_TRANSFER = 252,

  /* A `MAILB' record: matches all mail box related RRs (e.g. MB and MG). */
  GSK_DNS_RR_ZONE_MAILB = 253,

  /* A `*' record:  matches anything. */
  GSK_DNS_RR_WILDCARD = 255

} GskDnsResourceRecordType;


/* GskDnsResourceRecordClass: AKA RCLASS:
 *       Types of networks the RR can apply to (values from 1035, 3.2.4)
 */
typedef enum
{
  /* `IN': the Internet system */
  GSK_DNS_CLASS_INTERNET = 1,

  /* `CH': the Chaos system (rare) */
  GSK_DNS_CLASS_CHAOS = 3,

  /* `HS': Hesiod (rare) */
  GSK_DNS_CLASS_HESIOD = 4,

  /* --- only for queries --- */

  /* `*': any system */
  GSK_DNS_CLASS_WILDCARD = 255
} GskDnsResourceClass;

struct _GskDnsResourceRecord
{
  GskDnsResourceRecordType  type;
  char                     *owner;     /* where the resource_record is found */
  guint32                   time_to_live;
  GskDnsResourceClass       record_class;

  /* rdata: record type specific data */
  union
  {
    /* For GSK_DNS_RR_HOST_ADDRESS and GSK_DNS_CLASS_INTERNET */
    struct
    {
      guint8 ip_address[4];
    } a;

		/* unsupported */
    /* For GSK_DNS_RR_HOST_ADDRESS and GSK_DNS_CLASS_CHAOS */
    struct
    {
      char *chaos_name;
      guint16 chaos_address;
    } a_chaos;

    /* For GSK_DNS_RR_CNAME, GSK_DNS_RR_POINTER, GSK_DNS_RR_NAME_SERVER */
    char *domain_name;

    /* For GSK_DNS_RR_MAIL_EXCHANGE */
    struct
    {
      guint preference_value; /* "lower is better" */

      char *mail_exchange_host_name;
    } mx;

    /* For GSK_DNS_RR_TEXT */
    char *txt;

    /* For GSK_DNS_RR_HOST_INFO */
    struct
    {
      char *cpu;
      char *os;
    } hinfo;


    /* SOA: Start Of a zone of Authority.
     *
     * Comments cut-n-pasted from RFC 1035, 3.3.13.
     */
    struct
    {
      /* The domain-name of the name server that was the
	 original or primary source of data for this zone. */
      char *mname;

      /* specifies the mailbox of the
	 person responsible for this zone. */
      char *rname;

      /* The unsigned 32 bit version number of the original copy
	 of the zone.  Zone transfers preserve this value.  This
	 value wraps and should be compared using sequence space
	 arithmetic. */
      guint32 serial;

      /* A 32 bit time interval before the zone should be
	 refreshed. (cf 1034, 4.3.5) [in seconds] */
      guint32 refresh_time;

      /* A 32 bit time interval that should elapse before a
	 failed refresh should be retried. [in seconds] */
      guint32 retry_time;

      /* A 32 bit time value that specifies the upper limit on
	 the time interval that can elapse before the zone is no
	 longer authoritative. [in seconds] */
      guint32 expire_time;

      /* The unsigned 32 bit minimum TTL field that should be
	 exported with any RR from this zone. [in seconds] */
      guint32 minimum_time;
    } soa;

    struct {
      guint8 address[16];
    } aaaa;
  } rdata;

  /* private */
  GskDnsMessage *allocator;
};

/* Test whether serial_1 is less that serial_2.  See RFC 1982.  */
#define GSK_DNS_SERIAL_LESS_THAN(serial_1, serial_2)		\
  (((gint32) ((guint32)(serial_1) - (guint32)(serial_2))) < 0)

/* --- constructing GskDnsResourceRecords --- */
GskDnsResourceRecord *gsk_dns_rr_new_generic (GskDnsMessage        *allocator,
					      const char           *owner,
					      guint32               ttl);
GskDnsResourceRecord *gsk_dns_rr_new_a       (const char           *owner,
					      guint32               ttl,
					      const guint8         *ip_address,
					      GskDnsMessage        *allocator);
GskDnsResourceRecord *gsk_dns_rr_new_aaaa    (const char           *owner,
                                              guint32               ttl,
                                              const guint8         *address,
                                              GskDnsMessage        *allocator);
GskDnsResourceRecord *gsk_dns_rr_new_ns      (const char           *owner,
					      guint32               ttl,
					      const char           *name_server,
					      GskDnsMessage        *allocator);
GskDnsResourceRecord *gsk_dns_rr_new_cname   (const char           *owner,
					      guint32               ttl,
					      const char           *canonical_name,
					      GskDnsMessage        *allocator);
GskDnsResourceRecord *gsk_dns_rr_new_ptr     (const char           *owner,
					      guint32               ttl,
					      const char           *ptr,
					      GskDnsMessage        *allocator);
GskDnsResourceRecord *gsk_dns_rr_new_mx      (const char           *owner,
					      guint32               ttl,
					      int                   preference,
					      const char           *mail_host,
					      GskDnsMessage        *allocator);
GskDnsResourceRecord *gsk_dns_rr_new_hinfo   (const char           *owner,
					      guint32               ttl,
					      const char           *cpu,
					      const char           *os,
					      GskDnsMessage        *allocator);
GskDnsResourceRecord *gsk_dns_rr_new_soa     (const char           *owner,
					      guint32               ttl,
					      const char           *mname,
					      const char           *rname,
					      guint32               serial,
					      guint32               refresh_time,
					      guint32               retry_time,
					      guint32               expire_time,
					      guint32               minimum_time,
					      GskDnsMessage        *allocator);
GskDnsResourceRecord *gsk_dns_rr_new_txt     (const char           *owner,
					      guint32               ttl,
					      const char           *text,
					      GskDnsMessage        *allocator);
void                  gsk_dns_rr_free        (GskDnsResourceRecord *record);

GskDnsResourceRecord *gsk_dns_rr_copy        (GskDnsResourceRecord *record,
					      GskDnsMessage        *allocator);

/* queries only */

GskDnsResourceRecord *gsk_dns_rr_new_wildcard(const char    *owner,
					      guint          ttl,
					      GskDnsMessage *allocator);

/* TODO: GSK_DNS_RR_ZONE_TRANSFER GSK_DNS_RR_ZONE_MAILB */

/* --- GskDnsQuestion --- */
struct _GskDnsQuestion
{
  /* The domain name for which information is being requested. */
  char                     *query_name;

  /* The type of query we are asking. */
  GskDnsResourceRecordType  query_type;

  /* The domain where the query applies. */
  GskDnsResourceClass       query_class;


  /*< private >*/
  GskDnsMessage            *allocator;
};

GskDnsQuestion *gsk_dns_question_new (const char               *query_name,
				      GskDnsResourceRecordType  query_type,
				      GskDnsResourceClass       query_class,
				      GskDnsMessage            *allocator);
GskDnsQuestion *gsk_dns_question_copy(GskDnsQuestion           *question,
				      GskDnsMessage            *allocator);
void            gsk_dns_question_free(GskDnsQuestion           *question);


/* --- Outputting and parsing text DNS resource records --- */
/* cf RFC 1034, Section 3.6.1. */

GskDnsResourceRecord *gsk_dns_rr_text_parse (const char           *record,
			                     const char           *last_owner,
					     const char           *origin,
			                     char                **err_msg,
					     GskDnsMessage        *allocator);
char                 *gsk_dns_rr_text_to_str(GskDnsResourceRecord *rr,
					     const char           *last_owner);
char           *gsk_dns_question_text_to_str(GskDnsQuestion       *question);
void                  gsk_dns_rr_text_write (GskDnsResourceRecord *rr,
					     GskBuffer            *out_buffer,
					     const char           *last_owner);


/* --- Response types --- */
/* Various error messages the server can send to a client. */
typedef enum
{
  GSK_DNS_RESPONSE_ERROR_NONE             =0,
  GSK_DNS_RESPONSE_ERROR_FORMAT           =1,
  GSK_DNS_RESPONSE_ERROR_SERVER_FAILURE   =2,
  GSK_DNS_RESPONSE_ERROR_NAME_ERROR       =3,
  GSK_DNS_RESPONSE_ERROR_NOT_IMPLEMENTED  =4,
  GSK_DNS_RESPONSE_ERROR_REFUSED          =5
} GskDnsResponseCode;

/* DNS messages, divided into queries & answers.
 *
 * cf. RFC 1034, Section 3.7.
 */
struct _GskDnsMessage
{
  /* Header: fixed data about all queries */
  guint16       id;       /* used by requestor to match queries and replies */

  /* Is this a query or a response? */
  guint16       is_query : 1;

  guint16       is_authoritative : 1;
  guint16       is_truncated : 1;

  /* [Responses only] the `RA bit': whether the server is willing to provide
   *                                recursive services. (cf 1034, 4.3.1)
   */
  guint16       recursion_available : 1;

  /* [Queries only] the `RD bit': whether the requester wants recursive
   *                              service for this queries. (cf 1034, 4.3.1)
   */
  guint16       recursion_desired : 1;

  /* Question: Carries the query name and other query parameters. */
  /* `qtype' (names are from RFC 1034, section 3.7.1): the query type */
  GSList       *questions;	/* of GskDnsQuestion */

  /* Answer: Carries RRs which directly answer the query. */
  GskDnsResponseCode error_code;
  GSList       *answers;	/* of GskDnsResourceRecord */

  /* Authority: Carries RRs which describe other authoritative servers.
   *            May optionally carry the SOA RR for the authoritative
   *            data in the answer section.
   */
  GSList       *authority;	/* of GskDnsResourceRecord */

  /* Additional: Carries RRs which may be helpful in using the RRs in the
   *             other sections.
   */
  GSList       *additional;	/* of GskDnsResourceRecord */

  /*< private >*/
  guint         ref_count;
  GMemChunk    *qr_pool;  /* for GskDnsQuestion and GskDnsResourceRecord */
  GStringChunk *str_pool; /* for all strings */
  GHashTable   *offset_to_str;   /* for decompressing only */
};

/* --- binary dns messages --- */
GskDnsMessage *gsk_dns_message_new           (guint16        id,
					      gboolean       is_request);
GskDnsMessage *gsk_dns_message_parse_buffer  (GskBuffer     *buffer);
GskDnsMessage *gsk_dns_message_parse_data    (const guint8  *data,
				              guint          length,
				              guint         *bytes_used_out);
void           gsk_dns_message_write_buffer  (GskDnsMessage *message,
				              GskBuffer     *buffer,
				              gboolean       compress);
GskPacket     *gsk_dns_message_to_packet     (GskDnsMessage *message);

/* --- adjusting DnsMessages --- */
/* (Just manipulate the lists directly, if there are a lot of changes to make.) */

/* the DnsMessage will free the added object */
void           gsk_dns_message_append_question (GskDnsMessage        *message,
						GskDnsQuestion       *question);
void           gsk_dns_message_append_answer   (GskDnsMessage        *message,
						GskDnsResourceRecord *answer);
void           gsk_dns_message_append_auth     (GskDnsMessage        *message,
						GskDnsResourceRecord *auth);
void           gsk_dns_message_append_addl     (GskDnsMessage        *message,
						GskDnsResourceRecord *addl);

/* calling these functions will free the second parameter */
void           gsk_dns_message_remove_question (GskDnsMessage        *message,
						GskDnsQuestion       *question);
void           gsk_dns_message_remove_answer   (GskDnsMessage        *message,
						GskDnsResourceRecord *answer);
void           gsk_dns_message_remove_auth     (GskDnsMessage        *message,
						GskDnsResourceRecord *auth);
void           gsk_dns_message_remove_addl     (GskDnsMessage        *message,
						GskDnsResourceRecord *addl);

/* refcounting */
void           gsk_dns_message_unref           (GskDnsMessage        *message);
void           gsk_dns_message_ref             (GskDnsMessage        *message);


/* for debugging: dump the message human-readably to a file pointer. */
void           gsk_dns_dump_message_fp         (GskDnsMessage        *message,
					        FILE                 *fp);
void           gsk_dns_dump_question_fp        (GskDnsQuestion       *question,
					        FILE                 *fp);

/*< private >*/
/* XXX: use gsk_ipv4_parse() instead */
gboolean gsk_dns_parse_ip_address (const char **pat,
		                   guint8      *ip_addr_out);

/* if you need to test whether a supplied name will be admitted
 * into the DNS domain. */
gboolean gsk_test_domain_name_validity (const char *domain_name);

#endif
