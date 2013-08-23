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

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "gskdns.h"
#include "../gskmacros.h"
#include "../gskghelpers.h"

/* SAFE_STRLEN: Determine the length of a string, or 0 for NULL. */
#define SAFE_STRLEN(str)     ((str) != NULL ? strlen(str) : 0)

/* SAFE_BUFFER_APPEND_STR0: Append a length-prefixed string,
 *                          up to 255 characters.
 */
#define SAFE_BUFFER_APPEND_STR0(buffer, str) 			\
      G_STMT_START{						\
	if ((str) != NULL)					\
	  {							\
	    guint len = strlen (str);				\
	    guint8 len8;					\
	    if (len > 255)					\
	      len = 255;					\
	    len8 = (guint8) len;				\
	    gsk_buffer_append (buffer, &len8, 1);		\
	    gsk_buffer_append (buffer, (str), len8);		\
	  }							\
	else							\
	  gsk_buffer_append_char (buffer, 0);			\
      }G_STMT_END

/* Maximum offset allowed by the compression scheme. */
#define MAX_OFFSET			((1<<14) - 1)

/* How many characters to make the OWNER column for textual representation. */
#define COLUMN_OWNER_WIDTH		32

/* Maximum number of dotted components in a domain name we can handle. */
#define MAX_COMPONENTS			128

/* Protocol-limited number of characters in a component of a domain name. */
#define MAX_COMPONENT_LENGTH		63

/* Maximum length for a TXT record. */
#define MAX_TEXT_STRING_SIZE           1024

#define GSK_SKIP_DIGITS(char_ptr)	GSK_SKIP_CHAR_TYPE(char_ptr, isdigit)

static char * strncpy_nul_internal (char *dest, const char *mem, int len)
{
  strncpy (dest, mem, len);
  dest[len] = 0;
  return dest;
}

static int compute_maybe_suffixed_length (const char *start,
					  const char *end,
					  const char *origin)
{
  if (start >= end)
    return 1;
  if (end[-1] == '.')
    return strlen (start) + 2;
  else
    return strlen (start) + strlen (origin) + 2;
}
static char *suffix_and_copy  (char           *rv,
			       const char     *start,
			       const char     *end,
			       const char     *origin)
{
  if (start >= end)
    {
      *rv = 0;
      return rv;
    }
  memcpy (rv, start, end - start);
  if (end[-1] == '.')
    {
      rv[end - start - 1] = 0;
      return rv;
    }
  if (strcmp (origin, ".") == 0)
    rv[end - start] = 0;
  else
    {
      rv[end - start] = '.';
      strcpy (rv + (end - start + 1), origin);
    }
  return rv;
}

#define CUT_ONTO_STACK(var,start,end) \
	G_STMT_START{ \
	  const char *_str = start; \
	  guint _len = end - start; \
	  char *_tmp; \
	  _tmp = alloca (_len + 1);	\
	  var = strncpy_nul_internal (_tmp, _str, _len); \
	}G_STMT_END

#define SUFFIXED_CUT_ONTO_STACK(var,start,end,suffix) \
  	G_STMT_START{ \
	  char *_tmp; \
	  _tmp = alloca (compute_maybe_suffixed_length (start, end, suffix)); \
	  var = suffix_and_copy (_tmp, start, end, suffix); \
  	}G_STMT_END

/* Support some of the extensions that BIND supports.  */
#define SUPPORT_BIND_ENHANCEMENTS	1

/* TEST ONLY */
#if 1
#define PARSE_FAIL(note)	gsk_g_debug ("NOTE: parse error: %s", note)
#else
#define PARSE_FAIL(note)	((void) (note))
#endif

/* Parse the time field for an SOA record. */
static int
parse_into_seconds (const char     *str,
		    char          **endp)
{
  int rv;
#if SUPPORT_BIND_ENHANCEMENTS
  char *tmp = (char *) str;
  rv = 0;
  GSK_SKIP_WHITESPACE (tmp);
  while (TRUE)
    {
      char *end;
      int v;
      int scale = 1;
      if (*tmp == 0)
	break;
      v = strtol (tmp, &end, 10);
      if (tmp == end)
	break;
      GSK_SKIP_DIGITS (tmp);
      switch (*tmp)
	{
	case 'm': case 'M': scale = 60; tmp++; break;
	case 'h': case 'H': scale = 60 * 60; tmp++; break;
	case 'd': case 'D': scale = 60 * 60 * 24; tmp++; break;
	case 'w': case 'W': scale = 60 * 60 * 24 * 7; tmp++; break;
	default: break;
	}
      rv += v * scale;
      if (*tmp == '\0')
	break;
      if (isspace (*tmp))
	break;
    }
  if (endp != NULL)
    *endp = tmp;
  return rv;
#else  /* !SUPPORT_BIND_ENHANCEMENTS */
  return (int) strtol (str, endp, 10);
#endif  /* !SUPPORT_BIND_ENHANCEMENTS */
}

/* --- pooling GskDnsMessage's --- */
static GMemChunk *gsk_dns_message_chunk = NULL;
G_LOCK_DEFINE_STATIC (gsk_dns_message_chunk);

static inline GskDnsMessage  *
gsk_dns_message_alloc   ()
{
  GskDnsMessage *rv;
  G_LOCK (gsk_dns_message_chunk);
  if (gsk_dns_message_chunk == NULL)
    gsk_dns_message_chunk = g_mem_chunk_create (GskDnsMessage,
						16,
						G_ALLOC_AND_FREE);
  rv = g_mem_chunk_alloc (gsk_dns_message_chunk);
  G_UNLOCK (gsk_dns_message_chunk);

  memset (rv, 0, sizeof (GskDnsMessage));

  rv->qr_pool = g_mem_chunk_new ("DNS (Resource and Question) Pool",
				 MAX (sizeof (GskDnsResourceRecord),
				      sizeof (GskDnsQuestion)),
				 2048,
				 G_ALLOC_ONLY);
  rv->str_pool = g_string_chunk_new (2048);
  rv->ref_count = 1;

  return rv;
}
static inline void
gsk_dns_message_free (GskDnsMessage *gsk_dns_message)
{
  g_string_chunk_free (gsk_dns_message->str_pool);
  g_mem_chunk_destroy (gsk_dns_message->qr_pool);

  G_LOCK (gsk_dns_message_chunk);
  g_mem_chunk_free (gsk_dns_message_chunk, gsk_dns_message);
  G_UNLOCK (gsk_dns_message_chunk);
}

/* --- Dns Message parsing.  (See RFC 1035, 4.1.1.) --- */

/* The Compression Algorithm
 * 
 * Each component 
 *     (length-prefixed string)+ (0 | pointer-to-end-of-another string)
 * for example:
 *     a.hello.com.
 * might be stored:
 *     \001a\005hello\003com\0
 * the compression stunt is that since the length of each
 * component is limited to 63 characters, the upper
 * two bits of the length are used as a flag:
 *
 *    - if the top two bits are both one, then the next 14 bits
 *      are a pointer to a byte offset of a terminating string
 *
 *      eg if "hello.com" had already been stored at offset 64 (==\100),
 *      then a.hello.com could be stored:
 *    \001a\300\100
 */


/* --- decompressing (RFC 1035, 4.1.4) --- */
static char *
parse_domain_name (GskBufferIterator *iterator,
		   GskDnsMessage     *message)
{
  char tmp_buf[63 + 1];
  char rv_buf[1024];
  int len = 0;
  guint i;
  GString *string = NULL;

  /* We need to store other offset pairs,
   * which will be entered into the compression table
   * after the string has been formed in all its dotted glory...
   */
  int str_offsets[MAX_COMPONENTS];
  int buffer_offsets[MAX_COMPONENTS];
  guint num_offsets = 0;

  char *rv;
  gboolean stop = FALSE;

  rv_buf[0] = 0;

  while (!stop)
    {
      guint8 tmp_len;
      guint piece_len;
      gchar *str;
      guint component_offset = gsk_buffer_iterator_offset (iterator);
      if (gsk_buffer_iterator_read (iterator, &tmp_len, 1) != 1)
	return NULL;
      piece_len = tmp_len;

      if ((piece_len >> 6) == 3)
	{
	  guint8 ptr_byte_2;
	  guint offset;
	  if (gsk_buffer_iterator_read (iterator, &ptr_byte_2, 1) != 1)
	    return NULL;
	  offset = (((guint)piece_len & 0x3f) << 8) | ((guint)ptr_byte_2);
	  str = g_hash_table_lookup (message->offset_to_str,
				     GUINT_TO_POINTER (offset));
	  if (str == NULL)
	    {
	      PARSE_FAIL ("offset not found (for compression)");
	      return NULL;
	    }

	  /* XXX: SECURITY: this needs to enforce MAX_COMPONENTS!!! */
	  piece_len = strlen (str);
	  stop = TRUE;
	}
      else if ((piece_len >> 6) != 0)
	{
	  /* Not allowed ``reserved for future use'' (rfc1035) */
	  /* XXX: parse error? */
	  PARSE_FAIL ("bad bit sequence at start of string");
	  return NULL;
	}
      else if (piece_len == 0)
	{
	  break;
	}
      else
	{
	  str = tmp_buf;
	  g_assert (piece_len < 64);
	  if (gsk_buffer_iterator_read (iterator,
					tmp_buf,
					piece_len) != piece_len)
	    {
	      /* XXX: parse error? */
	      PARSE_FAIL ("data shorter than header byte indicated");
	      return NULL;
	    }
	  tmp_buf[piece_len] = 0;
	}

      /* Add this offset for this compression table... */
      if (num_offsets == MAX_COMPONENTS)
	{
	  g_warning ("too many dotted components for compile time limit (%d)?",
		     MAX_COMPONENTS);
	  return NULL;
	}
      str_offsets[num_offsets] = (len == 0 ? 0 : len + 1);
      buffer_offsets[num_offsets] = component_offset;
      num_offsets++;

      if (string == NULL && piece_len + len >= (sizeof (rv_buf) - 2))
	{
	  rv_buf[len] = 0;
	  string = g_string_new (rv_buf);
	}
      if (string != NULL)
	{
	  if (len > 0)
	    g_string_append_c (string, '.');
	  g_string_append (string, str);
	}
      else
	{
	  if (len > 0)
	    rv_buf[len++] = '.';
	  memcpy (rv_buf + len, str, piece_len);
	  rv_buf[len + piece_len] = 0;
	}
      len += piece_len;
    }

  if (string == NULL)
    {
      rv = g_string_chunk_insert (message->str_pool, rv_buf);
    }
  else
    {
      rv = g_string_chunk_insert (message->str_pool, string->str);
      g_string_free (string, TRUE);
    }

  /* Register the returned string, for future decompressions. */
  for (i = 0; i < num_offsets; i++)
    {
      g_hash_table_insert (message->offset_to_str,
			   GUINT_TO_POINTER (buffer_offsets[i]),
			   rv + str_offsets[i]);
    }

  return rv;
}

static char *
parse_char_single_string (GskBufferIterator *iterator,
		          GskDnsMessage     *message,
		          int                max_iterate)
{
  guint8 len;
  char *buf = alloca (max_iterate + 1);
  if (gsk_buffer_iterator_read (iterator, &len, 1) != 1)
    return NULL;
  max_iterate--;
  if (len == 0)
    return NULL;
  if (len > max_iterate)
    return NULL;
  if (gsk_buffer_iterator_read (iterator, buf, len) != len)
    return NULL;
  buf[len] = 0;
  return g_string_chunk_insert (message->str_pool, buf);
}

static char *
parse_char_string (GskBufferIterator *iterator,
		   GskDnsMessage     *message,
		   int                max_iterate)
{
  char *buf;
  int out_len = 0;
  g_return_val_if_fail (max_iterate > 0, NULL);
  buf = alloca (max_iterate + 1);
  while (max_iterate > 0)
    {
      guint8 len;
      if (gsk_buffer_iterator_read (iterator, &len, 1) != 1)
	break;
      max_iterate--;
      if (len == 0)
	break;
      if (len > max_iterate)
	break;
      if (gsk_buffer_iterator_read (iterator, buf + out_len, len) != len)
	return NULL;
      out_len += len;
      max_iterate -= len;
    }
  buf[out_len] = 0;
  return g_string_chunk_insert (message->str_pool, buf);
}

/* Duplicate a string, either from the stringpool of a message,
   or off the heap. */
#define ALLOCATOR_STRDUP(dns_message, str)				\
    ( ((str) == NULL)							\
       ? (NULL)								\
       : ((dns_message == NULL) 					\
	 ? (g_strdup (str))						\
	 : (g_string_chunk_insert ((dns_message)->str_pool, (str)))) )

/**
 * gsk_test_domain_name_validity:
 * @domain_name: a name which is supposed to be a domain name to test for validity.
 *
 * Verify that the domain_name meets certain required to be a hostname
 * on the internet.  In particular, all domain names MUST have <= 128 parts
 * each with <= 63 characters.
 *
 * returns: whether the domain name was valid.
 */
gboolean
gsk_test_domain_name_validity (const char *domain_name)
{
  /* Test that domain_name consists of MAX_COMPONENTS components
   * each with less than MAX_COMPONENT_LENGTH (==63 by very hardcoded limits)
   * characters.
   */
  int max_remaining = MAX_COMPONENTS;
  while (max_remaining > 0)
    {
      guint max_char = MAX_COMPONENT_LENGTH;
      while (*domain_name != '\0' && *domain_name != '.' && max_char > 0)
	{
	  domain_name++;
	  max_char--;
	}
      if (*domain_name != '\0' && *domain_name != '.')
	{
	  /* component was too long. */
	  return FALSE;
	}
      if (*domain_name == '\0')
	return TRUE;

      /* skip past the period. */
      g_assert (*domain_name == '.');
      domain_name++;
      max_remaining--;
    }

  /* too many components. */
  return FALSE;
}

/* --- building/allocating resource-records --- */
/**
 * gsk_dns_rr_new_generic:
 * @allocator: a message from which to draw the resource-records memory.
 * @owner: the owner field, which is common to all types of resource-records.
 * @ttl: the time-to-live for this record.
 *
 * Allocate a new blank record of the given type.
 *
 * The returned resource-record will probably not be valid,
 * since most records have non-optional type-specific fields
 * that the caller must initialize.
 *
 * returns: the newly allocated Resource Record.
 */ 
GskDnsResourceRecord *
gsk_dns_rr_new_generic (GskDnsMessage *allocator,
			const char    *owner,
			guint32        ttl)
{
  GskDnsResourceRecord *rv;
  if (allocator != NULL)
    rv = g_mem_chunk_alloc0 (allocator->qr_pool);
  else
    rv = g_new0 (GskDnsResourceRecord, 1);
  rv->record_class = GSK_DNS_CLASS_INTERNET;
  if (owner != NULL)
    rv->owner = ALLOCATOR_STRDUP (allocator, owner);
  rv->time_to_live = ttl;
  rv->allocator = allocator;
  return rv;
}

/**
 * gsk_dns_rr_new_a:
 * @owner: hostname whose ip address is given.
 * @ttl: the time-to-live for this record.
 *     This is maximum time for this record to be stored on a remote host,
 *     in seconds.
 * @ip_address: 4-byte IP address to contact @owner.
 * @allocator: an optional message to get memory from; this can prevent you
 *    from having to destroy the record: it will automatically get freed as
 *    part of the #GskDnsMessage.
 *
 * Allocate a new A record.  It is a mapping from domain name
 * to IP address.
 *
 * returns: the newly allocated Resource Record.
 */
GskDnsResourceRecord *
gsk_dns_rr_new_a       (const char    *owner,
			guint32        ttl,
			const guint8  *ip_address,
			GskDnsMessage *allocator)
{
  GskDnsResourceRecord *rv;
  if (!gsk_test_domain_name_validity (owner))
    return NULL;
  rv = gsk_dns_rr_new_generic (allocator, owner, ttl);
  rv->type = GSK_DNS_RR_HOST_ADDRESS;
  memcpy (rv->rdata.a.ip_address, ip_address, 4);
  return rv;
}

/**
 * gsk_dns_rr_new_aaaa:
 * @owner: hostname whose IPv6 address is given.
 * @ttl: the time-to-live for this record.
 *     This is maximum time for this record to be stored on a remote host,
 *     in seconds.
 * @address: 16-byte IP address to contact @owner.
 * @allocator: an optional message to get memory from; this can prevent you
 *    from having to destroy the record: it will automatically get freed as
 *    part of the #GskDnsMessage.
 *
 * Allocate a new AAAA record.  It is a mapping from domain name
 * to IP address.
 *
 * returns: the newly allocated Resource Record.
 */
GskDnsResourceRecord *
gsk_dns_rr_new_aaaa    (const char    *owner,
			guint32        ttl,
			const guint8  *address,
			GskDnsMessage *allocator)
{
  GskDnsResourceRecord *rv;
  if (!gsk_test_domain_name_validity (owner))
    return NULL;
  rv = gsk_dns_rr_new_generic (allocator, owner, ttl);
  rv->type = GSK_DNS_RR_HOST_ADDRESS_IPV6;
  memcpy (rv->rdata.aaaa.address, address, 16);
  return rv;
}

/**
 * gsk_dns_rr_new_ns:
 * @owner: hostname or domain name whose nameserver is given by this record.
 * @ttl: the time-to-live for this record.
 *    This is maximum time for this record to be stored on a remote host, in seconds.
 * @name_server: the name of a nameserver responsible for this hostname or domainname.
 * @allocator: an optional message to get memory from; this can prevent you
 *    from having to destroy the record: it will automatically get freed as
 *    part of the #GskDnsMessage.
 *
 * Allocate a new NS record.  It gives the name of a nameserver that can provide information about
 * @owner.
 *
 * If you are encountering this in response to a query,
 * you typically also get the address of a nameserver is given in an Additional
 * record of the message as an A record; otherwise, you'd probably have to look up
 * the nameserver in a separate query.
 *
 * returns: the newly allocated Resource Record.
 */
GskDnsResourceRecord *
gsk_dns_rr_new_ns      (const char    *owner,
			guint32        ttl,
			const char    *name_server,
			GskDnsMessage *allocator)
{
  GskDnsResourceRecord *rv;
  if (!gsk_test_domain_name_validity (owner)
   || ! gsk_test_domain_name_validity (name_server))
    return NULL;
  rv = gsk_dns_rr_new_generic (allocator, owner, ttl);
  rv->type = GSK_DNS_RR_NAME_SERVER;
  rv->rdata.domain_name = ALLOCATOR_STRDUP (allocator, name_server);
  return rv;
}

/**
 * gsk_dns_rr_new_cname:
 * @owner: hostname or domain name whose canonical name is given by this record.
 * @ttl: the time-to-live for this record.
 *     This is maximum time for this record to be stored on a remote host, in seconds.
 * @canonical_name: the canonical name of @owner.  The canonical name in some sense should
 * be the most preferred name for this host, but in practice it's no different than
 * a way to alias one name for another.
 * @allocator: an optional message to get memory from; this can prevent you
 *    from having to destroy the record: it will automatically get freed as
 *    part of the #GskDnsMessage.
 *
 * Allocate a new CNAME record.  It gives the canonical name of a record.
 * @owner.
 *
 * Information about the canonical host may be given in the Additional section.
 *
 * returns: the newly allocated Resource Record.
 */
GskDnsResourceRecord *
gsk_dns_rr_new_cname   (const char    *owner,
			guint32        ttl,
			const char    *canonical_name,
			GskDnsMessage *allocator)
{
  GskDnsResourceRecord *rv;
  if (!gsk_test_domain_name_validity (owner)
   || ! gsk_test_domain_name_validity (canonical_name))
    return NULL;
  rv = gsk_dns_rr_new_generic (allocator, owner, ttl);
  rv->type = GSK_DNS_RR_CANONICAL_NAME;
  rv->rdata.domain_name = ALLOCATOR_STRDUP (allocator, canonical_name);
  return rv;
}

/**
 * gsk_dns_rr_new_ptr:
 * @owner: hostname or domain name whose PTR record is being looked up.
 *    In practice, this name is almost always in the .arpa domain.
 * @ttl: the time-to-live for this record.
 *    This is maximum time for this record to be stored on a remote host,
 *    in seconds.
 * @ptr: a hostname which @owner points to.
 * @allocator: an optional message to get memory from; this can prevent you
 *    from having to destroy the record: it will automatically get freed as
 *    part of the #GskDnsMessage.
 *
 * Allocate a new PTR record.  It gives ``a pointer to another part of the domain name space''
 * according to [RFC 1034, Section 3.6]
 *
 * In practice, it is almost always used to do reverse-DNS.
 * That is because, by RFC 1034 and 1035, if you have an IP address aa.bb.cc.dd,
 * then looking up a PTR record for dd.cc.bb.aa.IN-ADDR.ARPA
 * should give the name for the host.
 *
 * Of course, reverse DNS is a bit flaky, and is principally for debugging information.
 *
 * returns: the newly allocated Resource Record.
 */
GskDnsResourceRecord *
gsk_dns_rr_new_ptr     (const char    *owner,
			guint32        ttl,
			const char    *ptr,
			GskDnsMessage *allocator)
{
  GskDnsResourceRecord *rv;
  if (!gsk_test_domain_name_validity (owner)
   || !gsk_test_domain_name_validity (ptr))
    return NULL;
  rv = gsk_dns_rr_new_generic (allocator, owner, ttl);
  rv->type = GSK_DNS_RR_POINTER;
  rv->rdata.domain_name = ALLOCATOR_STRDUP (allocator, ptr);
  return rv;
}

/**
 * gsk_dns_rr_new_mx:
 * @owner: hostname or domain name whose PTR record is being looked up.
 * @ttl: the time-to-live for this record.
 * @preference: ???
 * @mail_host: host responsible for mail for this domain.
 * @allocator: an optional message to get memory from; this can prevent you
 *
 * Allocate a mail-exchange record.
 * This gives a hostname which is responsible for main for the @owner
 * of this record.
 *
 * returns: the newly allocated Resource Record.
 */
GskDnsResourceRecord *
gsk_dns_rr_new_mx      (const char    *owner,
			guint32        ttl,
			int            preference,
			const char    *mail_host,
			GskDnsMessage *allocator)
{
  GskDnsResourceRecord *rv;
  if (!gsk_test_domain_name_validity (owner)
   || !gsk_test_domain_name_validity (mail_host))
    return NULL;
  rv = gsk_dns_rr_new_generic (allocator, owner, ttl);
  rv->type = GSK_DNS_RR_MAIL_EXCHANGE;
  rv->rdata.mx.preference_value = preference;
  rv->rdata.mx.mail_exchange_host_name = ALLOCATOR_STRDUP (allocator,
							   mail_host);
  return rv;
}

/**
 * gsk_dns_rr_new_hinfo:
 * @owner: hostname or domain name whose PTR record is being looked up.
 *    In practice, this name is almost always in the .arpa domain.
 * @ttl: the time-to-live for this record.
 *    This is maximum time for this record to be stored on a remote host,
 *    in seconds.
 * @cpu: CPU name for the host.
 * @os: OS for the host.
 * @allocator: an optional message to get memory from; this can prevent you
 *    from having to destroy the record: it will automatically get freed as
 *    part of the #GskDnsMessage.
 *
 * Allocate a new HINFO record.  It ``identifies the CPU and OS used by a host'',
 * see [RFC 1034, Section 3.6]
 *
 * In practice, it is never used.
 * It is provided for completeness, and also for experimental use.
 *
 * returns: the newly allocated Resource Record.
 */
GskDnsResourceRecord *
gsk_dns_rr_new_hinfo   (const char    *owner,
			guint32        ttl,
			const char    *cpu,
			const char    *os,
			GskDnsMessage *allocator)
{
  GskDnsResourceRecord *rv;
  if (!gsk_test_domain_name_validity (owner)
   || !gsk_test_domain_name_validity (cpu)
   || !gsk_test_domain_name_validity (os))
    return NULL;
  rv = gsk_dns_rr_new_generic (allocator, owner, ttl);
  rv->type = GSK_DNS_RR_HOST_INFO;
  rv->rdata.hinfo.cpu = ALLOCATOR_STRDUP (allocator, cpu);
  rv->rdata.hinfo.os = ALLOCATOR_STRDUP (allocator, os);
  return rv;
}

/**
 * gsk_dns_rr_new_soa:
 * @owner: hostname or domain name whose authority is being stated.
 * @ttl: the time-to-live for this record.
 *    This is maximum time for this record to be stored on a remote host, in seconds.
 * @mname:
 * The domain-name of the name server that was the
 * original or primary source of data for this zone.
 * @rname:
 * A domain-name which specifies the mailbox of the
 * person responsible for this zone.
 * @serial:
 * The unsigned 32 bit version number of the original copy
 * of the zone.  Zone transfers preserve this value.  This
 * value wraps and should be compared using sequence space
 * arithmetic.
 * @refresh_time:
 * A 32 bit time interval before the zone should be
 * refreshed.
 * @retry_time:
 * A 32 bit time interval that should elapse before a
 * failed refresh should be retried.
 * @expire_time:
 * A 32 bit time value that specifies the upper limit on
 * the time interval that can elapse before the zone is no
 * longer authoritative.
 * @minimum_time:
 * The unsigned 32 bit minimum TTL field that should be
 * exported with any RR from this zone.
 * @allocator: an optional message to get memory from; this can prevent you
 *    from having to destroy the record: it will automatically get freed as
 *    part of the #GskDnsMessage.
 *
 * Allocate a new SOA record.  It ``identifies the start of a zone of authority'',
 * see [RFC 1034, Section 3.6]

 * [The field descriptions come from RFC 1035, section 3.3.13]
 *
 * returns: the newly allocated Resource Record.
 */
GskDnsResourceRecord *
gsk_dns_rr_new_soa     (const char    *owner,
			guint32        ttl,
			const char    *mname,
			const char    *rname,
			guint32        serial,
			guint32        refresh_time,
			guint32        retry_time,
			guint32        expire_time,
			guint32        minimum_time,
			GskDnsMessage *allocator)
{
  GskDnsResourceRecord *rv;
  if (!gsk_test_domain_name_validity (owner)
   || !gsk_test_domain_name_validity (mname)
   || !gsk_test_domain_name_validity (rname))
    return NULL;
  rv = gsk_dns_rr_new_generic (allocator, owner, ttl);
  rv->type = GSK_DNS_RR_START_OF_AUTHORITY;
  rv->rdata.soa.mname = ALLOCATOR_STRDUP (allocator, mname);
  rv->rdata.soa.rname = ALLOCATOR_STRDUP (allocator, rname);
  rv->rdata.soa.serial = serial;
  rv->rdata.soa.refresh_time = refresh_time;
  rv->rdata.soa.retry_time = retry_time;
  rv->rdata.soa.expire_time = expire_time;
  rv->rdata.soa.minimum_time = minimum_time;
  return rv;
}

/**
 * gsk_dns_rr_new_txt:
 * @owner: hostname or domain name whose authority is being stated.
 * @ttl: the time-to-live for this record.
 *    This is maximum time for this record to be stored on a remote host, in seconds.
 * @text: text about the owner.
 * @allocator: an optional message to get memory from; this can prevent you
 *    from having to destroy the record: it will automatically get freed as
 *    part of the #GskDnsMessage.
 *
 * TXT RRs are used to hold descriptive text.  The semantics of the text
 * depends on the domain where it is found.
 *
 * returns: the newly allocated Resource Record.
 */
GskDnsResourceRecord *
gsk_dns_rr_new_txt     (const char    *owner,
			guint32        ttl,
			const char    *text,
			GskDnsMessage *allocator)
{
  GskDnsResourceRecord *rv;
  if (!gsk_test_domain_name_validity (owner)
   || strlen (text) > MAX_TEXT_STRING_SIZE)
    return NULL;
  rv = gsk_dns_rr_new_generic (allocator, owner, ttl);
  rv->type = GSK_DNS_RR_START_OF_AUTHORITY;
  rv->rdata.txt = ALLOCATOR_STRDUP (allocator, text);
  return rv;
}

/* queries only */

/**
 * gsk_dns_rr_new_wildcard:
 * @owner: hostname or domain name whose information is given by this record.
 * @ttl: the time-to-live for this record.
 * @allocator: an optional message to get memory from; this can prevent you
 *    from having to destroy the record: it will automatically get freed as
 *    part of the #GskDnsMessage.
 *
 * Create a new wildcard ("*") record.  This should never need to be done, I guess.
 *
 * TODO: delete this function
 *
 * returns: the newly allocated Resource Record.
 */
GskDnsResourceRecord *
gsk_dns_rr_new_wildcard(const char    *owner,
			guint          ttl,
			GskDnsMessage *allocator)
{
  GskDnsResourceRecord *rv;
  if (!gsk_test_domain_name_validity (owner))
    return NULL;
  rv = gsk_dns_rr_new_generic (allocator, owner, ttl);
  rv->type = GSK_DNS_RR_WILDCARD;
  return rv;
}

/* freeing */

/**
 * gsk_dns_rr_free:
 * @record: the record to free.
 *
 * De-allocate memory associated with a resource record.
 */
void
gsk_dns_rr_free        (GskDnsResourceRecord *record)
{
  if (record->allocator != NULL)
    {
      /* XXX: maybe deleting is viable, but more importantly
	      deal with things which *need* dynamic allocation */
      /* XXX: g_mem_chunk_free only needed for --disable-mem-pool support (see glib) */
      g_mem_chunk_free (record->allocator->qr_pool, record);
      return;
    }

  switch (record->type)
    {
    case GSK_DNS_RR_HOST_ADDRESS:
    case GSK_DNS_RR_HOST_ADDRESS_IPV6:
      break;
    case GSK_DNS_RR_NAME_SERVER:
    case GSK_DNS_RR_CANONICAL_NAME:
    case GSK_DNS_RR_POINTER:
      g_free (record->rdata.domain_name);
      break;
    case GSK_DNS_RR_HOST_INFO:
      g_free (record->rdata.hinfo.os);
      g_free (record->rdata.hinfo.cpu);
      break;
    case GSK_DNS_RR_MAIL_EXCHANGE:
      g_free (record->rdata.mx.mail_exchange_host_name);
      break;
    case GSK_DNS_RR_START_OF_AUTHORITY:
      g_free (record->rdata.soa.mname);
      g_free (record->rdata.soa.rname);
      break;
    case GSK_DNS_RR_TEXT:
      g_free (record->rdata.txt);
      break;
    case GSK_DNS_RR_WELL_KNOWN_SERVICE:
      g_warning ("XXX: unimplemented");
      break;
    case GSK_DNS_RR_ZONE_TRANSFER:
      g_warning ("XXX: unimplemented");
      break;
    case GSK_DNS_RR_ZONE_MAILB:
      g_warning ("XXX: unimplemented");
      break;
    case GSK_DNS_RR_WILDCARD:
      break;
    default:
      g_warning ("unknown DNS record type: %d", record->type);
      break;
    }
  g_free (record->owner);
  g_free (record);
}

/* copying */
/**
 * gsk_dns_rr_copy:
 * @record: the record to copy.
 * @allocator: an optional message to get memory from; this can prevent you
 *    from having to destroy the record: it will automatically get freed as
 *    part of the #GskDnsMessage.
 *
 * Make a copy of a resource record, possibly coming from a given #GskDnsMessage's
 * allocator.
 *
 * returns: the new copy of the resource record.
 */
GskDnsResourceRecord *
gsk_dns_rr_copy        (GskDnsResourceRecord *record,
			GskDnsMessage        *allocator)
{
  switch (record->type)
    {
    case GSK_DNS_RR_HOST_ADDRESS:
      return gsk_dns_rr_new_a (record->owner,
			       record->time_to_live,
			       record->rdata.a.ip_address,
			       allocator);
    case GSK_DNS_RR_HOST_ADDRESS_IPV6:
      return gsk_dns_rr_new_aaaa (record->owner,
			          record->time_to_live,
			          record->rdata.aaaa.address,
			          allocator);
    case GSK_DNS_RR_NAME_SERVER:
      return gsk_dns_rr_new_ns (record->owner,
				record->time_to_live,
				record->rdata.domain_name,
				allocator);
    case GSK_DNS_RR_CANONICAL_NAME:
      return gsk_dns_rr_new_cname (record->owner,
				   record->time_to_live,
				   record->rdata.domain_name,
				   allocator);
    case GSK_DNS_RR_POINTER:
      return gsk_dns_rr_new_ptr (record->owner,
				 record->time_to_live,
				 record->rdata.domain_name,
				 allocator);
    case GSK_DNS_RR_HOST_INFO:
      return gsk_dns_rr_new_hinfo (record->owner,
				   record->time_to_live,
				   record->rdata.hinfo.cpu,
				   record->rdata.hinfo.os,
				   allocator);
    case GSK_DNS_RR_MAIL_EXCHANGE:
      return gsk_dns_rr_new_mx (record->owner,
				record->time_to_live,
				record->rdata.mx.preference_value,
				record->rdata.mx.mail_exchange_host_name,
				allocator);
    case GSK_DNS_RR_START_OF_AUTHORITY:
      return gsk_dns_rr_new_soa (record->owner,
				 record->time_to_live,
				 record->rdata.soa.mname,
				 record->rdata.soa.rname,
				 record->rdata.soa.serial,
				 record->rdata.soa.refresh_time,
				 record->rdata.soa.retry_time,
				 record->rdata.soa.expire_time,
				 record->rdata.soa.minimum_time,
				 allocator);
    case GSK_DNS_RR_TEXT:
      return gsk_dns_rr_new_txt (record->owner,
				 record->time_to_live,
				 record->rdata.txt,
				 allocator);
    case GSK_DNS_RR_WELL_KNOWN_SERVICE:
      g_warning ("XXX: unimplemented");
      return NULL;
    case GSK_DNS_RR_ZONE_TRANSFER:
      g_warning ("XXX: unimplemented");
      return NULL;
    case GSK_DNS_RR_ZONE_MAILB:
      g_warning ("XXX: unimplemented");
      return NULL;
    case GSK_DNS_RR_WILDCARD:
      return gsk_dns_rr_new_wildcard (record->owner,
				      record->time_to_live,
				      allocator);
    default:
      g_warning ("dns rr copy: unknown DNS record type: %d", record->type);
      return NULL;
    }
  return NULL;
}


/* --- questions --- */
/**
 * gsk_dns_question_new:
 * @query_name: the name for which information is being saught.
 * @query_type: the type of resource-record that is saught.
 * @query_class: the address namespace for the query.
 * @allocator: an optional message to get memory from; this can prevent you
 *    from having to destroy the question: it will automatically get freed as
 *    part of the #GskDnsMessage.
 *
 * Allocate a new question.
 *
 * returns: the new question.
 */
GskDnsQuestion *
gsk_dns_question_new (const char               *query_name,
		      GskDnsResourceRecordType  query_type,
		      GskDnsResourceClass       query_class,
		      GskDnsMessage            *allocator)
{
  GskDnsQuestion *question;
  if (allocator == NULL)
    question = g_new (GskDnsQuestion, 1);
  else
    question = g_mem_chunk_alloc (allocator->qr_pool);
  question->query_name = ALLOCATOR_STRDUP (allocator, query_name);
  question->query_type = query_type;
  question->query_class = query_class;
  question->allocator = allocator;
  return question;
}

/**
 * gsk_dns_question_copy:
 * @question: the question to make a copy of.
 * @allocator: an optional message to get memory from.
 *
 * Make a copy of a question, optionally drawing its memory
 * from the given message's pool.
 *
 * returns: the new question.
 */
GskDnsQuestion *
gsk_dns_question_copy(GskDnsQuestion           *question,
		      GskDnsMessage            *allocator)
{
  return gsk_dns_question_new (question->query_name,
			       question->query_type,
			       question->query_class,
			       allocator);
}

/**
 * gsk_dns_question_free:
 * @question: the question to deallocate.
 *
 * Deallocate a question (unless it was drawn from a GskDnsMessage,
 * in which case it will be destroyed automatically when
 * the message is destroyed.
 */
void
gsk_dns_question_free(GskDnsQuestion           *question)
{
  if (question->allocator != NULL)
    {
      /* XXX: g_mem_chunk_free only needed for --disable-mem-pool support (see glib) */
      g_mem_chunk_free (question->allocator->qr_pool, question);
    }
  else
    {
      g_free (question->query_name);
      g_free (question);
    }
}

/* --- parsing binary messages --- */

/* questions (RFC 1035, 4.1.2) */
static GskDnsQuestion *
parse_question (GskBufferIterator *iterator,
		GskDnsMessage     *message)
{
  GskDnsQuestion *question;
  guint16 qarray[2];
  char *name;

  name = parse_domain_name (iterator, message);

  if (gsk_buffer_iterator_read (iterator, qarray, 4) != 4)
    return NULL;
  qarray[0] = GUINT16_FROM_BE (qarray[0]);
  qarray[1] = GUINT16_FROM_BE (qarray[1]);

  question = gsk_dns_question_new (NULL, qarray[0], qarray[1], message);
  question->query_name = name;
  return question;
}

/* resource records (RR's) (RFC 1035, 4.1.3) */
static GskDnsResourceRecord *
parse_resource_record (GskBufferIterator *iterator,
		       GskDnsMessage     *message)
{
  char *owner;
  guint16 type;
  guint16 class;
  guint32 ttl;
  guint16 rdlength;
  guint8 header[10];
  GskDnsResourceRecord *rr;

  owner = parse_domain_name (iterator, message);
  if (owner == NULL)
    return NULL;

  if (gsk_buffer_iterator_read (iterator, header, 10) != 10)
    return NULL;

  type     = ((guint16)header[0] << 8)  | ((guint16)header[1] << 0);
  class    = ((guint16)header[2] << 8)  | ((guint16)header[3] << 0);
  ttl      = ((guint32)header[4] << 24) | ((guint32)header[5] << 16)
           | ((guint32)header[6] << 8)  | ((guint32)header[7] << 0);
  rdlength = ((guint16)header[8] << 8)  | ((guint16)header[9] << 0);

  rr = gsk_dns_rr_new_generic (message, owner, ttl);
  rr->type = type;
  rr->record_class = class;
  /* TODO: verify class=INTERNET */

  switch (rr->type)
    {
    case GSK_DNS_RR_HOST_ADDRESS:
      if (class == GSK_DNS_CLASS_INTERNET)
	{
	  if (rdlength != 4)
	    {
	      g_warning ("only 4 byte internet addresses are supported");
              gsk_dns_rr_free (rr);
	      return NULL;
	    }
	  if (gsk_buffer_iterator_read (iterator,
					&rr->rdata.a.ip_address, 4) != 4)
            {
              gsk_dns_rr_free (rr);
	      return NULL;
            }
	}
      else
	{
	  g_warning ("class != INTERNET not supported yet, sorry");
          gsk_dns_rr_free (rr);
	  return NULL;
	}
      break;
    case GSK_DNS_RR_HOST_ADDRESS_IPV6:
      if (class == GSK_DNS_CLASS_INTERNET)
	{
	  if (rdlength != 16)
	    {
	      g_warning ("only 16 byte internet addresses are supported");
              gsk_dns_rr_free (rr);
	      return NULL;
	    }
	  if (gsk_buffer_iterator_read (iterator,
					&rr->rdata.aaaa.address, 16) != 16)
            {
              gsk_dns_rr_free (rr);
	      return NULL;
            }
	}
      else
	{
	  g_warning ("class != INTERNET not supported yet, sorry");
          gsk_dns_rr_free (rr);
	  return NULL;
	}
      break;


    case GSK_DNS_RR_NAME_SERVER:
    case GSK_DNS_RR_CANONICAL_NAME:
    case GSK_DNS_RR_POINTER:
      /* XXX: what to do if this reads too much or too little compared
       *      to rdlen???
       */
      rr->rdata.domain_name = parse_domain_name (iterator, message);
      break;

    case GSK_DNS_RR_MAIL_EXCHANGE:
      {
	guint preference_value;
	if (gsk_buffer_iterator_read (iterator, &preference_value, 2) != 2)
	  return NULL;
	rr->rdata.mx.preference_value = GUINT16_FROM_BE (preference_value);
	rr->rdata.mx.mail_exchange_host_name
	  = parse_domain_name (iterator, message);
	if (rr->rdata.mx.mail_exchange_host_name == NULL)
          {
            gsk_dns_rr_free (rr);
	    return NULL;
          }
	break;
      }

    case GSK_DNS_RR_HOST_INFO:
      {
	guint init_offset = gsk_buffer_iterator_offset (iterator);
	guint used_len;
	rr->rdata.hinfo.cpu
	  = parse_char_single_string (iterator, message, rdlength);
	used_len
	  = gsk_buffer_iterator_offset (iterator) - init_offset;
	rr->rdata.hinfo.os
	  = parse_char_single_string (iterator, message, rdlength - used_len);
	if (rr->rdata.hinfo.cpu == NULL || rr->rdata.hinfo.os == NULL)
          {
            gsk_dns_rr_free (rr);
	    return NULL;
          }
	break;
      }

    case GSK_DNS_RR_START_OF_AUTHORITY:
      {
	guint32 intervals[5];
	guint init_offset = gsk_buffer_iterator_offset (iterator);
	guint used_len;
	/* XXX: either of these domain names could go past rdlength!!! */
	rr->rdata.soa.mname = parse_domain_name (iterator, message);
        if (rr->rdata.soa.mname == NULL)
          return NULL;
	rr->rdata.soa.rname = parse_domain_name (iterator, message);
        if (rr->rdata.soa.rname == NULL)
          return NULL;
	used_len = gsk_buffer_iterator_offset (iterator) - init_offset;
	if (rdlength < used_len + 20)
	  return NULL;
	if (gsk_buffer_iterator_read (iterator, intervals, 20) != 20)
	  return NULL;
	rr->rdata.soa.serial = GUINT32_FROM_BE (intervals[0]);
	rr->rdata.soa.refresh_time = GUINT32_FROM_BE (intervals[1]);
	rr->rdata.soa.retry_time = GUINT32_FROM_BE (intervals[2]);
	rr->rdata.soa.expire_time = GUINT32_FROM_BE (intervals[3]);
	rr->rdata.soa.minimum_time = GUINT32_FROM_BE (intervals[4]);

	g_assert(gsk_buffer_iterator_offset (iterator) - init_offset == rdlength);
      }
      break;

    case GSK_DNS_RR_TEXT:
      rr->rdata.txt = parse_char_string (iterator, message, rdlength);
      break;

    case GSK_DNS_RR_WELL_KNOWN_SERVICE:
      g_warning ("XXX: unimplemented");
      gsk_dns_rr_free (rr);
      return NULL;

    case GSK_DNS_RR_ZONE_TRANSFER:
      g_warning ("XXX: unimplemented");
      gsk_dns_rr_free (rr);
      return NULL;

    case GSK_DNS_RR_ZONE_MAILB:
      g_warning ("XXX: unimplemented");
      gsk_dns_rr_free (rr);
      return NULL;

    case GSK_DNS_RR_WILDCARD:
      break;

    default:
      g_warning ("HMM.  Don't know how to deal with RTYPE==%d", rr->type);
      gsk_dns_rr_free (rr);
      return NULL;
    }
  return rr;
}

/* parse_resource_record_list:
 *   fetch a list of GskDnsResourceRecord's into *(LIST-OUT)
 *   by scanning through ITERATOR.  COUNT
 *   resource-records are expected.
 *
 *   SECTION is used for diagnostic messages;
 *   MESSAGE is used to allocation.
 */

static gboolean
parse_resource_record_list (GskBufferIterator *iterator,
			    guint              count,
			    GSList           **list_out,
			    const char        *section,
			    GskDnsMessage     *message)
{
  g_return_val_if_fail (*list_out == NULL, FALSE);
  while (count-- != 0)
    {
      GskDnsResourceRecord *rr = parse_resource_record (iterator, message);
      if (rr == NULL)
	{
	  PARSE_FAIL (section);
	  return FALSE;
	}
      *list_out = g_slist_prepend (*list_out, rr);
    }
  *list_out = g_slist_reverse (*list_out);
  return TRUE;
}

/**
 * gsk_dns_message_new:
 * @id: the identifier used to match client requests to server responses.
 * @is_request: whether the message is a client request.
 * If FALSE, then the message is a server response.
 *
 * Allocate a new blank GskDnsMessage.
 *
 * returns: the new message.
 */
GskDnsMessage *
gsk_dns_message_new (guint16            id,
		     gboolean           is_request)
{
  GskDnsMessage *message = gsk_dns_message_alloc ();
  message->id = id;
  message->is_query = is_request ? 1 : 0;
  return message;
}

static GskDnsMessage *
gsk_dns_parse_buffer_internal (GskBuffer    *buffer,
			       guint        *num_bytes_parsed)
{
  guint16 header[6];
  guint i;
  guint question_count;
  guint answer_count;
  guint auth_count;
  guint addl_count;
  GskDnsMessage *message = NULL;
  GskBufferIterator iterator;

  gsk_buffer_iterator_construct (&iterator, buffer);
  if (gsk_buffer_iterator_read (&iterator, header, 12) != 12)
    return NULL;

  for (i = 0; i < 6; i++)
    header[i] = GUINT16_FROM_BE (header[i]);

  message = gsk_dns_message_alloc ();


  message->id                  =  header[0];
  message->is_query            = (header[1] & (1<<15)) ? 0 : 1;
  message->is_authoritative    = (header[1] & (1<<10)) ? 1 : 0;
  message->is_truncated        = (header[1] & (1<<9))  ? 1 : 0;
  message->recursion_desired   = (header[1] & (1<<8))  ? 1 : 0;
  message->recursion_available = (header[1] & (1<<7))  ? 1 : 0;
  message->error_code          = (header[1] & 0x000f) >> 0;
  question_count               =  header[2];
  answer_count                 =  header[3];
  auth_count                   =  header[4];
  addl_count                   =  header[5];
  message->offset_to_str       = g_hash_table_new (NULL, NULL);

  /* question section */
  for (i = 0; i < question_count; i++)
    {
      GskDnsQuestion *question = parse_question (&iterator, message);
      if (question == NULL)
	{
	  PARSE_FAIL ("question section");
	  goto fail;
	}
      message->questions = g_slist_prepend (message->questions, question);
    }
  message->questions = g_slist_reverse (message->questions);

  /* the other three sections are the same: a list of resource-records */
  if (!parse_resource_record_list (&iterator, answer_count,
				   &message->answers, "answer", message))
    goto fail;
  if (!parse_resource_record_list (&iterator, auth_count,
				   &message->authority, "authority", message))
    goto fail;
  if (!parse_resource_record_list (&iterator, addl_count,
				   &message->additional, "additional", message))
    goto fail;

  g_assert (g_slist_length (message->questions) == question_count);
  g_assert (g_slist_length (message->answers) == answer_count);
  g_assert (g_slist_length (message->authority) == auth_count);
  g_assert (g_slist_length (message->additional) == addl_count);

  if (num_bytes_parsed != NULL)
    *num_bytes_parsed = gsk_buffer_iterator_offset (&iterator);
  return message;

fail:
  if (message != NULL)
    gsk_dns_message_unref (message);
  return NULL;
}

/**
 * gsk_dns_message_parse_buffer:
 * @buffer: the buffer to parse to get a binary DNS message.
 *
 * Parse a GskDnsMessage from a buffer, removing the binary data
 * from the buffer.
 *
 * returns: the new DNS message, or NULL if a parse error occurs.
 */
GskDnsMessage *gsk_dns_message_parse_buffer  (GskBuffer    *buffer)
{
  guint len;
  GskDnsMessage *rv = gsk_dns_parse_buffer_internal (buffer, &len);
  if (rv == NULL)
    return rv;
  gsk_buffer_discard (buffer, len);
  return rv;
}

/**
 * gsk_dns_message_parse_data:
 * @data: binary data to parse into a DNS message.
 * @length: length of @data in bytes.
 * @bytes_used_out: number of bytes of @data actually used to make the
 *   returned message, or NULL if you don't care.
 *
 * Parse a GskDnsMessage from a buffer.
 *
 * returns: the new DNS message, or NULL if a parse error occurs.
 */
GskDnsMessage *gsk_dns_message_parse_data    (const guint8 *data,
				              guint         length,
				              guint        *bytes_used_out)
{
  GskBuffer buffer;
  GskDnsMessage *message;
  guint len;
  gsk_buffer_construct (&buffer);
  gsk_buffer_append_foreign (&buffer, data, length, NULL, NULL);
  message = gsk_dns_parse_buffer_internal (&buffer, &len);
  gsk_buffer_destruct (&buffer);
  if (message == NULL)
    return NULL;
  if (bytes_used_out != NULL)
    *bytes_used_out = len;
  return message;
}

/* --- writing binary messages --- */
typedef struct _SerializeInfo SerializeInfo;
struct _SerializeInfo
{
  gboolean    compress;
  GHashTable *str_to_offset;
  GskBuffer  *buffer;
  gint        init_buffer_size;
};

static void compress_string (SerializeInfo *ser_info,
			     const char    *str)
{
  GHashTable *offsets = ser_info->str_to_offset;
  const char *at = str;
  const char *component = at;
  guint offset = 0;

  while (*component != 0)
    {
      const char *end_component;
      guint len;
      guint buf_offset;
      offset = GPOINTER_TO_UINT (g_hash_table_lookup (offsets, component));
      if (offset != 0)
	break;

      buf_offset = ser_info->buffer->size - ser_info->init_buffer_size;
      if (buf_offset <= MAX_OFFSET)
	g_hash_table_insert (ser_info->str_to_offset,
			     (gpointer) component,
			     GUINT_TO_POINTER (buf_offset));

      end_component = strchr (component, '.');
      if (end_component != NULL)
	len = end_component - component;
      else
	len = strlen (component);

      /* Skip extraneous `.'s */
      if (len == 0)
	{
	  component++;
	  continue;
	}

      /* Hmm, warning??? */
      if (len > MAX_COMPONENT_LENGTH)
	len = MAX_COMPONENT_LENGTH;
      gsk_buffer_append_char (ser_info->buffer, len);
      gsk_buffer_append (ser_info->buffer, component, len);
      
      if (end_component == NULL)
	{
	  component = NULL;
	  break;
	}
      else
	component = end_component + 1;
    }
  if (offset == 0)
    gsk_buffer_append_char (ser_info->buffer, 0);
  else
    {
      gsk_buffer_append_char (ser_info->buffer, (offset >> 8) | 0xc0);
      gsk_buffer_append_char (ser_info->buffer, offset & 0xff);
    }
}

static void
write_question_to_buffer (gpointer    list_data,
			  gpointer    ser_data)
{
  SerializeInfo *ser_info = ser_data;
  GskDnsQuestion *question = list_data;
  guint16 data[2];

  compress_string (ser_info, question->query_name);
  data[0] = GUINT16_TO_BE (question->query_type);
  data[1] = GUINT16_TO_BE (question->query_class);
  gsk_buffer_append (ser_info->buffer, data, 4);
}

static void
append_char_string (GskBuffer  *buffer,
		    const char *str)
{
  int len = strlen (str);
  if (len > 63)
    len = 63;
  gsk_buffer_append_char (buffer, len);
  gsk_buffer_append (buffer, str, len);
}

static void
write_rr_to_buffer (gpointer    list_data,
		    gpointer    ser_data)
{
  SerializeInfo *ser_info = ser_data;
  GskDnsResourceRecord *rr = list_data;
  GskBuffer *buffer = ser_info->buffer;
  guint16 data[5];
  GskBuffer tmp_buffer;
  gsk_buffer_construct (&tmp_buffer);
  data[0] = GUINT16_TO_BE (rr->type);
  data[1] = GUINT16_TO_BE (rr->record_class);
  data[2] = GUINT16_TO_BE (rr->time_to_live >> 16);
  data[3] = GUINT16_TO_BE ((guint16) rr->time_to_live);

  compress_string (ser_info, rr->owner);

  switch (rr->type)
    {
    case GSK_DNS_RR_HOST_ADDRESS:
      if (rr->record_class == GSK_DNS_CLASS_INTERNET)
	{
	  data[4] = GUINT16_TO_BE (4);
	  gsk_buffer_append (buffer, data, 10);
	  gsk_buffer_append (buffer, rr->rdata.a.ip_address, 4);
	}
      else
	{
	  g_warning ("cannot serialize DnsClasses beside `INTERNET'");
	  return;
	}
      break;

    case GSK_DNS_RR_HOST_ADDRESS_IPV6:
      if (rr->record_class == GSK_DNS_CLASS_INTERNET)
	{
	  data[4] = GUINT16_TO_BE (16);
	  gsk_buffer_append (buffer, data, 10);
	  gsk_buffer_append (buffer, rr->rdata.aaaa.address, 16);
	}
      else
	{
	  g_warning ("cannot serialize DnsClasses beside `INTERNET'");
	  return;
	}
      break;

    case GSK_DNS_RR_NAME_SERVER:
    case GSK_DNS_RR_CANONICAL_NAME:
    case GSK_DNS_RR_POINTER:
      {
	GskBuffer tmp_buffer;
	gint old_init_buf_size = ser_info->init_buffer_size;
	gsk_buffer_construct (&tmp_buffer);
	ser_info->buffer = &tmp_buffer;
	ser_info->init_buffer_size = old_init_buf_size
				   - buffer->size
				   - 10;
	compress_string (ser_info, rr->rdata.domain_name);
	data[4] = tmp_buffer.size;
	data[4] = GUINT16_TO_BE (data[4]);
	gsk_buffer_append (buffer, data, 10);
	gsk_buffer_drain (buffer, &tmp_buffer);
	ser_info->buffer = buffer;
	ser_info->init_buffer_size = old_init_buf_size;
	break;
      }

    case GSK_DNS_RR_HOST_INFO:
      data[4] = SAFE_STRLEN (rr->rdata.hinfo.cpu)
	      + SAFE_STRLEN (rr->rdata.hinfo.os) + 2;
      gsk_buffer_append (buffer, data, 10);
      append_char_string (buffer, rr->rdata.hinfo.cpu);
      append_char_string (buffer, rr->rdata.hinfo.os);
      break;

    case GSK_DNS_RR_MAIL_EXCHANGE:
      {
	guint16 pref = GUINT16_TO_BE (rr->rdata.mx.preference_value);
	gint old_init_buf_size = ser_info->init_buffer_size;
	ser_info->buffer = &tmp_buffer;
	ser_info->init_buffer_size = old_init_buf_size
				   - buffer->size
				   - 10;
	gsk_buffer_append (&tmp_buffer, &pref, 2);
	compress_string (ser_info, rr->rdata.mx.mail_exchange_host_name);
	data[4] = tmp_buffer.size;
	data[4] = GUINT16_TO_BE (data[4]);
	gsk_buffer_append (buffer, data, 10);
	gsk_buffer_drain (buffer, &tmp_buffer);
	ser_info->buffer = buffer;
	ser_info->init_buffer_size = old_init_buf_size;
      }
      break;

    case GSK_DNS_RR_START_OF_AUTHORITY:
      {
	gint old_init_buf_size = ser_info->init_buffer_size;
	ser_info->buffer = &tmp_buffer;
	ser_info->init_buffer_size = old_init_buf_size
				   - buffer->size
				   - 10;

	compress_string (ser_info, rr->rdata.soa.mname);
	compress_string (ser_info, rr->rdata.soa.rname);
	{
	  guint32 int_buf[5];
	  int_buf[0] = GUINT32_TO_BE (rr->rdata.soa.serial);
	  int_buf[1] = GUINT32_TO_BE (rr->rdata.soa.refresh_time);
	  int_buf[2] = GUINT32_TO_BE (rr->rdata.soa.retry_time);
	  int_buf[3] = GUINT32_TO_BE (rr->rdata.soa.expire_time);
	  int_buf[4] = GUINT32_TO_BE (rr->rdata.soa.minimum_time);
	  gsk_buffer_append (buffer, int_buf, sizeof (int_buf));
	}

	data[4] = tmp_buffer.size;
	data[4] = GUINT16_TO_BE (data[4]);

	gsk_buffer_append (buffer, data, 10);
	gsk_buffer_drain (buffer, &tmp_buffer);
	ser_info->buffer = buffer;
	ser_info->init_buffer_size = old_init_buf_size;
	break;
      }

    case GSK_DNS_RR_TEXT:
      {
	char *text = rr->rdata.txt;
	int remaining = strlen (text);
	while (remaining > 0)
	  {
	    int to_write = MIN (remaining, 255);
	    gsk_buffer_append_char (buffer, to_write);
	    gsk_buffer_append (buffer, text, to_write);
	    remaining -= to_write;
	    text += to_write;
	  }
	break;
      }

    case GSK_DNS_RR_WELL_KNOWN_SERVICE:
      g_warning ("XXX: writing DNS WKS RR's not supported");
      data[4] = 0;
      gsk_buffer_append (buffer, data, 10);
      break;

    case GSK_DNS_RR_ZONE_TRANSFER:
      g_warning ("XXX: writing DNS AXFR RR's not supported");
      break;

    case GSK_DNS_RR_ZONE_MAILB:
      g_warning ("XXX: writing DNS MAILB RR's not supported");
      break;

    default:
      data[4] = 0;
      gsk_buffer_append (buffer, data, 10);
      break;
    }
}

/**
 * gsk_dns_message_write_buffer:
 * @message: the DNS message to serialize.
 * @buffer: where to write the message.
 * @compress: whether to use compression.  always specify TRUE!
 *
 * Create a serialized message to send in a packet, or
 * whatever transport.
 *
 * XXX:  technically, DNS is supposed to support really crappy
 * transport media, which only allow a very short message.
 * We have no real control over how long the message will be,
 * a priori, and we just ignore the problem...
 * We casually try to send whatever packet a caller wants,
 * even if it probably won't work in the transport layer.
 */
void
gsk_dns_message_write_buffer  (GskDnsMessage *message,
		               GskBuffer     *buffer,
		               gboolean       compress)
{
  SerializeInfo ser_info;

  ser_info.compress = compress;
  ser_info.buffer = buffer;
  if (compress)
    ser_info.str_to_offset = g_hash_table_new (g_str_hash, g_str_equal);
  else
    ser_info.str_to_offset = NULL;
  ser_info.init_buffer_size = buffer->size;

  {
    guint16 dns_header[6];
    guint i;
    dns_header[0] = message->id;
    dns_header[1] = (message->is_query            ? 0 : (1 << 15))
                  | (message->is_authoritative    ? (1 << 10) : 0)
		  | (message->is_truncated        ? (1 << 9)  : 0)
		  | (message->recursion_desired   ? (1 << 8)  : 0)
		  | (message->recursion_available ? (1 << 7)  : 0)
		  | (message->error_code & 0x000f);
    dns_header[2] = g_slist_length (message->questions);
    dns_header[3] = g_slist_length (message->answers);
    dns_header[4] = g_slist_length (message->authority);
    dns_header[5] = g_slist_length (message->additional);
    for (i = 0; i < 6; i++)
      dns_header[i] = GUINT16_FROM_BE (dns_header[i]);
    gsk_buffer_append (buffer, dns_header, 12);
  }
  g_slist_foreach (message->questions, write_question_to_buffer, &ser_info);
  g_slist_foreach (message->answers, write_rr_to_buffer, &ser_info);
  g_slist_foreach (message->authority, write_rr_to_buffer, &ser_info);
  g_slist_foreach (message->additional, write_rr_to_buffer, &ser_info);

  if (ser_info.str_to_offset != NULL)
    g_hash_table_destroy (ser_info.str_to_offset);
}

/**
 * gsk_dns_message_to_packet:
 * @message: a DNS message which is going to be sent out.
 *
 * Convert a DNS message into a packet.
 * Typically then the packet will be added to a packet queue.
 *
 * returns: the new packet containing the binary data.
 */
GskPacket *
gsk_dns_message_to_packet (GskDnsMessage *message)
{
  GskBuffer buffer;
  gsize size;
  gpointer slab;
  gsk_buffer_construct (&buffer);
  gsk_dns_message_write_buffer (message, &buffer, TRUE);
  size = buffer.size;
  slab = g_malloc (size);
  gsk_buffer_read (&buffer, slab, size);
  return gsk_packet_new (slab, size, (GskPacketDestroyFunc) g_free, slab);
}

/* --- adjusting GskDnsMessage's --- */
/**
 * gsk_dns_message_append_question:
 * @message: the message to affect.
 * @question: a question to add to the message.
 *
 * Add a question to the QUESTION section of a GskDnsMessage.
 * For client requests, the message usually consists of
 * just questions.  For server responses, the message will
 * have copies the questions that the message deals with.
 *
 * The question will be free'd by the message.
 */
void
gsk_dns_message_append_question (GskDnsMessage        *message,
				 GskDnsQuestion       *question)
{
  message->questions = g_slist_append (message->questions,
				       question);
}

/**
 * gsk_dns_message_append_answer:
 * @message: the message to affect.
 * @answer: a resource-record to add to the answer section of the message.
 *
 * Add an answer to the DNS message.
 * This is done by servers when they have a direct
 * answer to a question (either a real answer or a reference to
 * a nameserver which can answer it).
 *
 * @answer will be freed when @message is freed.
 */
void
gsk_dns_message_append_answer   (GskDnsMessage        *message,
				 GskDnsResourceRecord *answer)
{
  message->answers = g_slist_append (message->answers, answer);
}

/**
 * gsk_dns_message_append_auth:
 * @message: the message to affect.
 * @auth: a resource-record to add to the authority section of the message.
 *
 * Add an authority-record to the DNS message.
 * This is done by servers to indicate who
 * is in charge of certain names and caching information.
 * 
 * @auth will be freed when @message is freed.
 */
void
gsk_dns_message_append_auth     (GskDnsMessage        *message,
				 GskDnsResourceRecord *auth)
{
  message->authority = g_slist_append (message->authority, auth);
}

/**
 * gsk_dns_message_append_addl:
 * @message: the message to affect.
 * @addl: a resource-record to add to the additional section of the message.
 *
 * Add an additional record to the DNS message.
 * This may be done by servers as a courtesy and optimization.
 * The most common use is to give the numeric IP address 
 * when a nameserver referenced in the answers section.
 * 
 * @auth will be freed when @message is freed.
 */
void
gsk_dns_message_append_addl     (GskDnsMessage        *message,
				 GskDnsResourceRecord *addl)
{
  message->additional = g_slist_append (message->additional, addl);
}

/**
 * gsk_dns_message_remove_question:
 * @message: a DNS message.
 * @question: a question in the message.
 *
 * Remove the question from the message's list,
 * and delete the question.
 */
void
gsk_dns_message_remove_question (GskDnsMessage        *message,
				 GskDnsQuestion       *question)
{
  g_return_if_fail (g_slist_find (message->questions, question) != NULL);
  message->questions = g_slist_remove (message->questions, question);
  gsk_dns_question_free (question);
}

/**
 * gsk_dns_message_remove_answer:
 * @message: a DNS message.
 * @answer: an answer in the message.
 *
 * Remove the record from the message's answer list.
 * This frees the record.
 */
void
gsk_dns_message_remove_answer   (GskDnsMessage        *message,
				 GskDnsResourceRecord *answer)
{
  g_return_if_fail (g_slist_find (message->answers, answer) != NULL);
  message->answers = g_slist_remove (message->answers, answer);
  gsk_dns_rr_free (answer);
}

/**
 * gsk_dns_message_remove_auth:
 * @message: a DNS message.
 * @auth: an authority record in the message.
 *
 * Remove the record from the message's authority list.
 * This frees the record.
 */
void
gsk_dns_message_remove_auth     (GskDnsMessage        *message,
				 GskDnsResourceRecord *auth)
{
  g_return_if_fail (g_slist_find (message->authority, auth) != NULL);
  message->authority = g_slist_remove (message->authority, auth);
  gsk_dns_rr_free (auth);
}

/**
 * gsk_dns_message_remove_addl:
 * @message: a DNS message.
 * @addl: an additional record in the message.
 *
 * Remove the record from the message's additional list.
 * This frees the record.
 */
void
gsk_dns_message_remove_addl     (GskDnsMessage        *message,
				 GskDnsResourceRecord *addl)
{
  g_return_if_fail (g_slist_find (message->additional, addl) != NULL);
  message->additional = g_slist_remove (message->additional, addl);
  gsk_dns_rr_free (addl);
}

/* --- refcounting messages --- */
/**
 * gsk_dns_message_unref:
 * @message: the message to stop referencing.
 *
 * Decrease the reference-count on the message.
 * The message will be destroyed once its ref-count
 * gets to 0.
 */
void
gsk_dns_message_unref           (GskDnsMessage        *message)
{
  g_return_if_fail (message->ref_count > 0);
  --(message->ref_count);
  if (message->ref_count == 0)
    {
      g_slist_foreach (message->questions, (GFunc) gsk_dns_question_free, NULL);
      g_slist_free (message->questions);
      g_slist_foreach (message->answers, (GFunc) gsk_dns_rr_free, NULL);
      g_slist_free (message->answers);
      g_slist_foreach (message->authority, (GFunc) gsk_dns_rr_free, NULL);
      g_slist_free (message->authority);
      g_slist_foreach (message->additional, (GFunc) gsk_dns_rr_free, NULL);
      g_slist_free (message->additional);
      if (message->offset_to_str != NULL)
	g_hash_table_destroy (message->offset_to_str);
      gsk_dns_message_free (message);
    }
}

/**
 * gsk_dns_message_ref:
 * @message: the message to add a reference to.
 *
 * Increase the reference count on the message by one.
 * The message will not be destroyed until its ref-count
 * gets to 0.
 */
void
gsk_dns_message_ref             (GskDnsMessage        *message)
{
  g_return_if_fail (message->ref_count > 0);
  ++(message->ref_count);
}

/* --- parsing text resource-records (RFC 1034, 3.6.1) --- */
/**
 * gsk_dns_parse_ip_address:
 * @pat: pointer which starts at a numeric IP address.  *@pat
 * will be updated to past the IP address.
 * @ip_addr_out: the 4-byte IP address.
 *
 * Parse a numeric IP address, in the standard fashion (RFC 1034, 3.6.1).
 *
 * returns: whether the address was parsed successfully.
 */
gboolean
gsk_dns_parse_ip_address (const char **pat,
		          guint8      *ip_addr_out)
{
  /* XXX: clean this one  up */
  const char *at = *pat;
  char *endp;
  ip_addr_out[0] = (guint) strtoul (at, &endp, 10);
  if (at == endp || *endp != '.')
    return FALSE;
  at = endp + 1;
  ip_addr_out[1] = (guint) strtoul (at, &endp, 10);
  if (at == endp || *endp != '.')
    return FALSE;
  at = endp + 1;
  ip_addr_out[2] = (guint) strtoul (at, &endp, 10);
  if (at == endp || *endp != '.')
    return FALSE;
  at = endp + 1;
  ip_addr_out[3] = (guint) strtoul (at, &endp, 10);
  if (at == endp)
    return FALSE;
  GSK_SKIP_WHITESPACE (endp);
  *pat = endp;
  return TRUE;
}

/**
 * gsk_dns_parse_ipv6_address:
 * @pat: pointer which starts at a numeric IP address.  *@pat
 * will be updated to past the IP address.
 * @ip_addr_out: the 4-byte IP address.
 *
 * Parse a numeric IP address, in the standard fashion (RFC 1034, 3.6.1).
 *
 * returns: whether the address was parsed successfully.
 */
gboolean
gsk_dns_parse_ipv6_address (const char **pat,
		            guint8      *ip_addr_out)
{
  guint i;
  const char *at = *pat;
  char *endp;
  for (i = 0; i < 7; i++)
    {
      guint16 word = (guint16) strtoul (at, &endp, 16);
      ip_addr_out[2 * i + 0] = word >> 8;
      ip_addr_out[2 * i + 1] = word & 0xff;
      if (at == endp || *endp != ':')
        return FALSE;
      at = endp + 1;
    }
  {
    guint16 word = (guint16) strtoul (at, &endp, 16);
    ip_addr_out[2 * i + 0] = word >> 8;
    ip_addr_out[2 * i + 1] = word & 0xff;
    if (at == endp)
      return FALSE;
  }
  GSK_SKIP_WHITESPACE (endp);
  *pat = endp;
  return TRUE;
}


static gboolean
parse_rr_type (const char               *type_str,
               GskDnsResourceRecordType *type_out)
{
#define TEST_STRING(str, rv)			\
  G_STMT_START{					\
    if (strcasecmp (type_str, str) == 0)	\
      {						\
	*type_out = rv;				\
	return TRUE;				\
      }						\
  }G_STMT_END
  switch (*type_str)
    {
    case 'a': case 'A':
      TEST_STRING("a", GSK_DNS_RR_HOST_ADDRESS);
      TEST_STRING("aaaa", GSK_DNS_RR_HOST_ADDRESS_IPV6);
      TEST_STRING("axfr", GSK_DNS_RR_ZONE_TRANSFER);
      break;

    case 'n': case 'N':
      TEST_STRING ("ns", GSK_DNS_RR_NAME_SERVER);
      break;

    case 'h': case 'H':
      TEST_STRING ("hinfo", GSK_DNS_RR_HOST_INFO);
      break;

    case 'c': case 'C':
      TEST_STRING ("cname", GSK_DNS_RR_CANONICAL_NAME);
      break;

    case 'm': case 'M':
      TEST_STRING ("mx", GSK_DNS_RR_MAIL_EXCHANGE);
      break;

    case 'p': case 'P':
      TEST_STRING ("ptr", GSK_DNS_RR_POINTER);
      break;

    case 's': case 'S':
      TEST_STRING ("soa", GSK_DNS_RR_START_OF_AUTHORITY);
      break;

    case 'w': case 'W':
      TEST_STRING ("wks", GSK_DNS_RR_WELL_KNOWN_SERVICE);
      break;

    case '*':
      TEST_STRING ("*", GSK_DNS_RR_WILDCARD);
      break;
    }
#undef TEST_STRING
  /*gsk_g_debug ("Unknown RRTYPE parsed from text: %s", type_str);*/
  return FALSE;
}

static gboolean
parse_rr_class (const char          *class_str,
                GskDnsResourceClass *class_out)
{
#define TEST_STRING(second_chars, rv)		\
  G_STMT_START{					\
    if (class_str[1] == second_chars[0] 	\
     || class_str[1] == second_chars[1])	\
      {						\
	*class_out = rv;			\
	return TRUE;				\
      }						\
  }G_STMT_END
  switch (*class_str)
    {
    case 'i': case 'I':
      TEST_STRING ("nN", GSK_DNS_CLASS_INTERNET);
      break;
    case 'c': case 'C':
      TEST_STRING ("hH", GSK_DNS_CLASS_CHAOS);
      break;
    case 'h': case 'H':
      TEST_STRING ("sS", GSK_DNS_CLASS_HESIOD);
      break;
    }
#undef TEST_STRING
  /*gsk_g_debug ("Unknown RRCLASS parsed from text: %s", class_str);*/
  return FALSE;
}

/**
 * gsk_dns_rr_text_parse:
 * @record: the record as a string.
 * @last_owner: the last parsed resource-record's "owner"
 * field.  Spaces at the beginning of the line are
 * taken to represent the same owner.
 * @origin: the origin server for this record.
 * @err_msg: optional place to put an allocated error message.
 * @allocator: a message from which to draw the resource-records memory.
 *
 * Parse a text representation of a message,
 * as from a zone file or 'dig'.
 *
 * returns: the new resource-record, or NULL if an error occurred.
 */
GskDnsResourceRecord *
gsk_dns_rr_text_parse     (const char       *record,
			   const char       *last_owner,
			   const char       *origin,
			   char            **err_msg,
			   GskDnsMessage    *allocator)
{
  gboolean start_with_space;
  const char *at = record;
  const char *endp;
  const char *owner = NULL;
  GskDnsResourceRecord *rr = NULL;
  guint ttl;
  char *rrtype_str;
  char *rclass_str;
  GskDnsResourceRecordType rtype;
  GskDnsResourceClass rclass;

  g_return_val_if_fail (origin != NULL, NULL);

  if (err_msg)
    *err_msg = NULL;

  if (*at == 0)
    return NULL;

  start_with_space = isspace (*at);
  GSK_SKIP_WHITESPACE (at);

  /* XXX: is this a good comment character? */
  if (*at == ';')
    return NULL;

  if (!start_with_space)
    {
      endp = at;
      GSK_SKIP_NONWHITESPACE (endp);
      if (endp == at + 1 && *at == '.')
	{
	  owner = origin;
	  if (origin == NULL)
	    {
	      if (err_msg != NULL)
		*err_msg = g_strdup ("@ specified as RR-owner, but no origin");
	      return NULL;
	    }
	}
      else
	{
	  SUFFIXED_CUT_ONTO_STACK (owner, at, endp, origin);
	}
      at = endp;
      GSK_SKIP_WHITESPACE (at);
    }
  else if (last_owner == NULL)
    {
      if (err_msg != NULL)
	*err_msg = g_strdup ("line began with SPACE but last-owner was NULL");
      goto fail;
    }
  else
    {
      owner = last_owner;
    }

  /* TTL */
  if (!isdigit (*at))
    {
      if (err_msg != NULL)
	*err_msg = g_strdup ("TTL was not a number");
      goto fail;
    }
  ttl = parse_into_seconds (at, (char **) &endp);
  if (at == endp)
    {
      if (err_msg != NULL)
	*err_msg = g_strdup ("TTL was not a number");
      goto fail;
    }
  at = endp;
  GSK_SKIP_WHITESPACE (at);

  /* Type (MX, A, etc.) */
  endp = at;
  GSK_SKIP_NONWHITESPACE (endp);
  if (endp == at || *endp == 0)
    {
      if (err_msg != NULL)
        *err_msg = g_strdup ("end-of-record before Class/Type");
      goto fail;
    }
  CUT_ONTO_STACK (rrtype_str, at, endp);

  /* Class */
  at = endp;
  GSK_SKIP_WHITESPACE (at);
  endp = at;
  GSK_SKIP_NONWHITESPACE (endp);
  if (endp == at)
    {
      if (err_msg != NULL)
	*err_msg = g_strdup ("end-of-record before Class/Type");
      goto fail;
    }
  CUT_ONTO_STACK (rclass_str, at, endp);

  {
    gboolean rrtype_succeeded, rclass_succeded;

    rrtype_succeeded = parse_rr_type (rrtype_str, &rtype);
    rclass_succeded = parse_rr_class (rclass_str, &rclass);
#if SUPPORT_BIND_ENHANCEMENTS
    /* BIND allows RRCLASS AND RRTYPE to be backward. */
    if (!rrtype_succeeded && !rclass_succeded)
      {
	char *tmp = rrtype_str;
	rrtype_str = rclass_str;
	rclass_str = tmp;
	rrtype_succeeded = parse_rr_type (rrtype_str, &rtype);
	rclass_succeded = parse_rr_class (rclass_str, &rclass);
      }
#endif
    if (!rrtype_succeeded)
      {
	if (err_msg != NULL)
	  *err_msg = g_strdup ("unknown RTYPE");
	goto fail;
      }
    if (!rclass_succeded)
      {
	if (err_msg != NULL)
	  *err_msg = g_strdup ("unknown RCLASS");
	goto fail;
      }
  }
  at = endp;
  GSK_SKIP_WHITESPACE (at);

  /* Rdata */
  switch (rtype)
    {
    case GSK_DNS_RR_HOST_ADDRESS:
      if (rclass == GSK_DNS_CLASS_INTERNET)
	{
	  guint8 ip_address[4];
	  if (!gsk_dns_parse_ip_address (&at, ip_address))
	    {
	      if (err_msg != NULL)
		*err_msg = g_strdup ("error parsing IP address for A record");
	      goto fail;
	    }
	  rr = gsk_dns_rr_new_a (owner, ttl, ip_address, allocator);
	}
      else
	{
	  /* TODO: maybe add a way to register more classes externally? */
	  if (err_msg != NULL)
	    *err_msg = g_strdup ("Only `IN' class `A' records can be parsed");
	  goto fail;
	}
      break;

    case GSK_DNS_RR_HOST_ADDRESS_IPV6:
      if (rclass == GSK_DNS_CLASS_INTERNET)
	{
	  guint8 address[16];
	  if (!gsk_dns_parse_ipv6_address (&at, address))
	    {
	      if (err_msg != NULL)
		*err_msg = g_strdup ("error parsing IP address for AAAA record");
	      goto fail;
	    }
	  rr = gsk_dns_rr_new_aaaa (owner, ttl, address, allocator);
	}
      else
	{
	  /* TODO: maybe add a way to register more classes externally? */
	  if (err_msg != NULL)
	    *err_msg = g_strdup ("Only `IN' class `AAAA' records can be parsed");
	  goto fail;
	}
      break;

    case GSK_DNS_RR_NAME_SERVER:
    case GSK_DNS_RR_CANONICAL_NAME:
    case GSK_DNS_RR_POINTER:
      endp = at;
      GSK_SKIP_NONWHITESPACE (endp);
      {
	char *name;

	SUFFIXED_CUT_ONTO_STACK (name, at, endp, origin);
	if (rtype == GSK_DNS_RR_NAME_SERVER)
	  rr = gsk_dns_rr_new_ns (owner, ttl, name, allocator);
	else if (rtype == GSK_DNS_RR_CANONICAL_NAME)
	  rr = gsk_dns_rr_new_cname (owner, ttl, name, allocator);
	else if (rtype == GSK_DNS_RR_POINTER)
	  rr = gsk_dns_rr_new_ptr (owner, ttl, name, allocator);
      }
      break;

    /* A `HINFO' record: identifies the CPU and OS used by a host */
    case GSK_DNS_RR_HOST_INFO:
      /* TODO: fix this */
      if (err_msg != NULL)
	*err_msg = g_strdup ("HINFO text messages not supported");
      goto fail;

    /* A `MX' record */
    case GSK_DNS_RR_MAIL_EXCHANGE:
      {
	/* parse a preference, then a host */
	int preference = (int) strtol (at, (char **) &endp, 10);
	char *name;
	if (at == endp)
	  {
	    if (err_msg != NULL)
	      *err_msg = g_strdup ("first field of MX wasn't an int `pref'");
	    goto fail;
	  }
	GSK_SKIP_WHITESPACE (endp);
	at = endp;
	GSK_SKIP_NONWHITESPACE (endp);
	if (at == endp)
	  {
	    if (err_msg != NULL)
	      *err_msg = g_strdup ("no name (last field) in MX record");
	    goto fail;
	  }
	SUFFIXED_CUT_ONTO_STACK (name, at, endp, origin);
	rr = gsk_dns_rr_new_mx (owner, ttl, preference, name, allocator);
	break;
      }

    /* A `SOA' record:  identifies the start of a zone of authority [???] */
    case GSK_DNS_RR_START_OF_AUTHORITY:
      {
	/* The fields in an SOA entry */
	guint32 int_array[5];
	char *mname;
	char *rname;
	guint i;
	endp = at;
	GSK_SKIP_NONWHITESPACE (endp);
	SUFFIXED_CUT_ONTO_STACK (mname, at, endp, origin);
	at = endp;
	GSK_SKIP_WHITESPACE (at);
	endp = at;
	GSK_SKIP_NONWHITESPACE (endp);
	SUFFIXED_CUT_ONTO_STACK (rname, at, endp, origin);
	at = endp;
	GSK_SKIP_WHITESPACE (at);

	/* we ignore a left-paren */
	if (*at == '(')
	  {
	    at++;
	    GSK_SKIP_WHITESPACE (at);
	  }

	/* parse the 5 int32s into int_array */
	int_array[0] = (int) strtol (at, (char **) &endp, 10);
	if (at == endp)
	  {
	    if (err_msg != NULL)
	      *err_msg = g_strdup_printf ("error parsing int from SOA record");
	    goto fail;
	  }
	GSK_SKIP_WHITESPACE (endp);
	at = endp;
	for (i = 1; i < 5; i++)
	  {
	    int_array[i] = (int) parse_into_seconds (at, (char **) &endp);
	    if (at == endp)
	      {
		if (err_msg != NULL)
		  *err_msg
		    = g_strdup_printf ("error parsing time %d from SOA at %s",
				       i, endp);
		goto fail;
	      }
	    GSK_SKIP_WHITESPACE (endp);
	    at = endp;
	  }

	/* map int_array to the soa member */
	rr = gsk_dns_rr_new_soa (owner, ttl, mname, rname,
				 int_array[0], int_array[1],
				 int_array[2], int_array[3],
				 int_array[4], allocator);
      }
      break;

    /* A `TXT' record:  miscellaneous text */
    case GSK_DNS_RR_TEXT:
      rr = gsk_dns_rr_new_txt (owner, ttl, at, allocator);
      break;

    /* A `WKS' record:  description of a well-known service */
    case GSK_DNS_RR_WELL_KNOWN_SERVICE:
      if (err_msg != NULL)
	*err_msg = g_strdup ("TODO: can't deal with parsing WKS text fields yet");
      break;

    default:
      if (err_msg != NULL)
	*err_msg = g_strdup_printf ("could not convert RTYPE %d to text", rtype);
      goto fail;
    }

  return rr;
fail:
  if (rr != NULL)
    gsk_dns_rr_free (rr);
  return NULL;
}

static void
append_spaces (GString *str, int n)
{
  char *spaces = alloca (n + 1);
  memset (spaces, ' ', n);
  spaces[n] = 0;
  g_string_append (str, spaces);
}

static const char *
gsk_resource_record_type_to_string (GskDnsResourceRecordType type)
{
  switch (type)
    {
    case GSK_DNS_RR_HOST_ADDRESS:       return "A";
    case GSK_DNS_RR_HOST_ADDRESS_IPV6:  return "AAAA";
    case GSK_DNS_RR_NAME_SERVER:        return "NS";
    case GSK_DNS_RR_CANONICAL_NAME:     return "CNAME";
    case GSK_DNS_RR_HOST_INFO:          return "HINFO";
    case GSK_DNS_RR_MAIL_EXCHANGE:      return "MX";
    case GSK_DNS_RR_POINTER:            return "PTR";
    case GSK_DNS_RR_START_OF_AUTHORITY: return "SOA";
    case GSK_DNS_RR_TEXT:               return "TXT";
    case GSK_DNS_RR_WELL_KNOWN_SERVICE: return "WKS";
    case GSK_DNS_RR_ZONE_TRANSFER:      return "AXFR";
    case GSK_DNS_RR_ZONE_MAILB:         return "MAILB";
    case GSK_DNS_RR_WILDCARD:           return "*";
    default:                            return "UNKNOWN-RTYPE";
    }
}

static const char *
gsk_resource_record_class_to_string (GskDnsResourceClass class)
{
  switch (class)
    {
    case GSK_DNS_CLASS_INTERNET:        return "IN";
    case GSK_DNS_CLASS_CHAOS:           return "CH";
    case GSK_DNS_CLASS_HESIOD:          return "HS";
    case GSK_DNS_CLASS_WILDCARD:        return "*";
    default:                            return "UNKNOWN-RCLASS";
    }
}

/**
 * gsk_dns_rr_text_to_str:
 * @rr: the resource record to convert to a text string.
 * @last_owner: if non-NULL, then this should be the owner
 * of the last resource record printed out.
 * For human-readability, zone files merely indent subsequent
 * lines that refer to the same owner.
 *
 * Convert the resource record into a format which would be appropriate
 * for a zone file.
 *
 * returns: a newly allocated string.  The text of the resource-record.
 */
char *
gsk_dns_rr_text_to_str(GskDnsResourceRecord *rr,
		       const char           *last_owner)
{
  GString *rv = g_string_new ("");
  if (last_owner == NULL
   || strcmp (last_owner, rr->owner) != 0)
    {
      int owner_len = strlen (rr->owner);
      g_string_append (rv, rr->owner);
      if (owner_len <= COLUMN_OWNER_WIDTH - 1)
	append_spaces (rv, COLUMN_OWNER_WIDTH - owner_len);
      else
	g_string_append_c (rv, ' ');
    }
  else
    {
      /* Append COLUMN_OWNER_WIDTH spaces. */
      append_spaces (rv, COLUMN_OWNER_WIDTH);
    }

  /* append ttl */
  g_string_sprintfa (rv, "%-7d ", rr->time_to_live);

  /* append type */
  g_string_append (rv, gsk_resource_record_type_to_string (rr->type));
  g_string_append_c (rv, ' ');

  /* append class */
  g_string_append (rv, gsk_resource_record_class_to_string (rr->record_class));
  g_string_append_c (rv, ' ');

  /* append rdata */
  switch (rr->type)
    {
    case GSK_DNS_RR_HOST_ADDRESS:
      if (rr->record_class == GSK_DNS_CLASS_INTERNET)
	{
	  g_string_sprintfa (rv, "%d.%d.%d.%d",
			     rr->rdata.a.ip_address[0],
			     rr->rdata.a.ip_address[1],
			     rr->rdata.a.ip_address[2],
			     rr->rdata.a.ip_address[3]);
	}
      else
	{
	  g_string_sprintfa (rv,
		     "ERROR: cannot print non-internet (IN) class address");
	  goto fail;
	}
      break;

    case GSK_DNS_RR_HOST_ADDRESS_IPV6:
      if (rr->record_class == GSK_DNS_CLASS_INTERNET)
	{
	  g_string_sprintfa (rv, "%x:%x:%x:%x:%x:%x:%x:%x",
                 GUINT16_FROM_BE (((guint16*)rr->rdata.aaaa.address)[0]),
                 GUINT16_FROM_BE (((guint16*)rr->rdata.aaaa.address)[1]),
                 GUINT16_FROM_BE (((guint16*)rr->rdata.aaaa.address)[2]),
                 GUINT16_FROM_BE (((guint16*)rr->rdata.aaaa.address)[3]),
                 GUINT16_FROM_BE (((guint16*)rr->rdata.aaaa.address)[4]),
                 GUINT16_FROM_BE (((guint16*)rr->rdata.aaaa.address)[5]),
                 GUINT16_FROM_BE (((guint16*)rr->rdata.aaaa.address)[6]),
                 GUINT16_FROM_BE (((guint16*)rr->rdata.aaaa.address)[7]));
	}
      else
	{
	  g_string_sprintfa (rv,
		     "ERROR: cannot print non-internet (IN) class address");
	  goto fail;
	}
      break;

    case GSK_DNS_RR_NAME_SERVER:
    case GSK_DNS_RR_CANONICAL_NAME:
    case GSK_DNS_RR_POINTER:
      g_string_append (rv, rr->rdata.domain_name);
      break;

    case GSK_DNS_RR_HOST_INFO:
      g_string_append (rv, rr->rdata.hinfo.cpu);
      g_string_append_c (rv, ' ');
      g_string_append (rv, rr->rdata.hinfo.os);
      goto fail;

    case GSK_DNS_RR_MAIL_EXCHANGE:
      g_string_sprintfa (rv, "%d %s",
			 rr->rdata.mx.preference_value,
			 rr->rdata.mx.mail_exchange_host_name);
      break;

    /* A `SOA' record:  identifies the start of a zone of authority [???] */
    case GSK_DNS_RR_START_OF_AUTHORITY:
      g_string_sprintfa (rv, "%s %s %u %u %u %u %u",
			 rr->rdata.soa.mname,
			 rr->rdata.soa.rname,
			 rr->rdata.soa.serial,
			 rr->rdata.soa.refresh_time,
			 rr->rdata.soa.retry_time,
			 rr->rdata.soa.expire_time,
			 rr->rdata.soa.minimum_time);
      break;

    /* A `TXT' record:  miscellaneous text */
    case GSK_DNS_RR_TEXT:
      g_string_append (rv, rr->rdata.txt);
      break;

    case GSK_DNS_RR_WELL_KNOWN_SERVICE:
      g_warning ("WKS not printable yet");
      g_string_append (rv, "ERROR: cannot print WKS records yet");
      break;

    default:
      g_string_sprintfa (rv, "Unknown RTYPE %d", rr->type);
      break;
    }

  return g_string_free (rv, FALSE);

fail:
  g_string_free (rv, TRUE);
  g_warning ("error converting DNS record to ASCII");
  return NULL;
}

/**
 * gsk_dns_question_text_to_str:
 * @question: question to convert to text.
 *
 * Convert a question to a newly allocated string
 * containing the question roughly as it would be
 * printed by the unix program 'dig'.
 *
 * returns: the newly allocated string.
 */
char *
gsk_dns_question_text_to_str(GskDnsQuestion       *question)
{
  GString *rv;
  char *rv_str;
  int owner_len = strlen (question->query_name);
  const char *enum_str;

  rv = g_string_new ("");
  g_string_append (rv, question->query_name);
  if (owner_len <= COLUMN_OWNER_WIDTH - 1)
    append_spaces (rv, COLUMN_OWNER_WIDTH - owner_len);
  else
    g_string_append_c (rv, ' ');

  enum_str = gsk_resource_record_class_to_string (question->query_class);
  g_string_append (rv, enum_str);
  g_string_append_c (rv, ' ');
  enum_str = gsk_resource_record_type_to_string (question->query_type);
  g_string_append (rv, enum_str);

  rv_str = rv->str;
  g_string_free (rv, FALSE);
  return rv_str;
}

/**
 * gsk_dns_rr_text_write:
 * @rr: the resource record to write as text into the buffer.
 * @out_buffer: the buffer to which the text resource-record should be appended.
 * @last_owner: if non-NULL, then spaces will be printed instead
 * of @rr->owner, if it is the same as last_owner.
 *
 * Print a resource-record to a buffer as text.
 * This will look similar to a zone file.
 */
void
gsk_dns_rr_text_write     (GskDnsResourceRecord *rr,
			   GskBuffer            *out_buffer,
			   const char           *last_owner)
{
  char *txt = gsk_dns_rr_text_to_str (rr, last_owner);
  gsk_buffer_append_string (out_buffer, txt);
  gsk_buffer_append_char (out_buffer, '\n');
  g_free (txt);
}


/* --- debugging dump --- */
void
gsk_dns_dump_question_fp (GskDnsQuestion *question,
			  FILE           *fp)
{
  char *str = gsk_dns_question_text_to_str (question);
  fprintf (fp, "%s [%p]\n", str, question);
  g_free (str);
}


static void print_rr_to_fp (gpointer list_data, gpointer data)
{
  FILE *fp = data;
  GskDnsResourceRecord *rr = list_data;
  char *str = gsk_dns_rr_text_to_str (rr, NULL);
  fprintf (fp, "%s\n", str);
  g_free (str);
}

/**
 * gsk_dns_dump_message_fp:
 * @message: the message to print out.
 * @fp: the file to write the message out to.
 *
 * Dump a message to a FILE*, for debugging.
 * This output format is modelled after the somewhat
 * obscure unix utility 'dig'.
 */
void
gsk_dns_dump_message_fp (GskDnsMessage *message,
			 FILE          *fp)
{
  const char *error_str = "UNKNOWN ERROR";
  switch (message->error_code)
    {
    case GSK_DNS_RESPONSE_ERROR_NONE:
      error_str = "NO ERROR";
      break;
    case GSK_DNS_RESPONSE_ERROR_FORMAT:
      error_str = "FORMAT ERROR";
      break;
    case GSK_DNS_RESPONSE_ERROR_SERVER_FAILURE:
      error_str = "SERVER FAILURE";
      break;
    case GSK_DNS_RESPONSE_ERROR_NAME_ERROR:
      error_str = "NAME ERROR";
      break;
    case GSK_DNS_RESPONSE_ERROR_NOT_IMPLEMENTED:
      error_str = "NOT IMPLEMENTED ERROR";
      break;
    case GSK_DNS_RESPONSE_ERROR_REFUSED:
      error_str = "REFUSED";
      break;
    }

  fprintf (fp, "%s.  ID=%d. %s%s%s%s (%s)\n",
	   message->is_query ? "QUERY" : "RESPONSE",
	   (int) message->id,
	   message->is_authoritative ? " (AA)" : "",
	   message->is_truncated ? " (TRUNCATED)" : "",
	   message->recursion_available ? " (RECURSION AVAIL)" : "",
	   message->recursion_desired ? " (RECURSION DESIRED)" : "",
	   error_str);

  switch (message->error_code)
    {
    case GSK_DNS_RESPONSE_ERROR_REFUSED:
      fprintf (fp, "Response: ERROR: REFUSED\n");
      break;
    case GSK_DNS_RESPONSE_ERROR_FORMAT:
      fprintf (fp, "Response: ERROR: FORMAT\n");
      break;
    case GSK_DNS_RESPONSE_ERROR_SERVER_FAILURE:
      fprintf (fp, "Response: ERROR: SERVER FAILURE\n");
      break;
    case GSK_DNS_RESPONSE_ERROR_NAME_ERROR:
      fprintf (fp, "Response: ERROR: NAME ERROR\n");
      break;
    case GSK_DNS_RESPONSE_ERROR_NOT_IMPLEMENTED:
      fprintf (fp, "Response: ERROR: NOT IMPLEMENTED\n");
      break;
    case GSK_DNS_RESPONSE_ERROR_NONE:
      break;
    }

  if (message->questions != NULL)
    fprintf (fp, "\nQuestions:\n");
  g_slist_foreach (message->questions, (GFunc) gsk_dns_dump_question_fp, fp);

  if (message->answers != NULL)
    fprintf (fp, "\nAnswers:\n");
  g_slist_foreach (message->answers, print_rr_to_fp, fp);

  if (message->authority != NULL)
    fprintf (fp, "\nAuthority:\n");
  g_slist_foreach (message->authority, print_rr_to_fp, fp);

  if (message->additional != NULL)
    fprintf (fp, "\nAdditional:\n");
  g_slist_foreach (message->additional, print_rr_to_fp, fp);
}

