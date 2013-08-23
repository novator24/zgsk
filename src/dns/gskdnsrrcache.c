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

#include <string.h>
#include <ctype.h>
#include <errno.h>
#define G_LOG_DOMAIN    "Gsk-Dns"
#include "gskdnsrrcache.h"
#include "../gskghelpers.h"
#include "../gskmacros.h"
#include "../gskerror.h"
#include "../gskdebug.h"
#include "../debug.h"
#include "../gskmainloop.h"

/* unimportant constant.
 *
 * The minimum to even try and read, used for
 * linebuffer-full detection only.
 */
#define MIN_LINE_LENGTH		32

/* XXX: lowercase_string and LOWER_CASE_COPY_ON_STACK are
        copied from gskdnsimplementations.c, hmm. */
static char * lowercase_string (char *out, const char *in)
{
  char *rv = out;
  while (*in != 0)
    {
      if ('A' <= *in && *in <= 'Z')
	*out = *in + ('a' - 'A');
      else
	*out = *in;
      out++;
      in++;
    }
  *out = 0;
  return rv;
}

/* Make VAR a lower-cased copy of STR, on the stack. */
#define LOWER_CASE_COPY_ON_STACK(var,str) \
	G_STMT_START{ \
	  const char *_str = str; \
	  char *_tmp = alloca (strlen (_str) + 1); \
	  var = lowercase_string (_tmp, _str); \
	}G_STMT_END

typedef enum
{
  RR_LIST_MAGIC = 0x322611de,
  RR_LIST_EXPIRED = 0x143331fe,
  RR_LIST_REPLACED = 0x15ddeabc
} RRListMagic;

/* A single entry (a ResourceRecord) in the cache.
   
   This is in two lists:
     - a per-owner list of resource-records
     - a LRU list to discard old entries. */
typedef struct _RRList RRList;
struct _RRList
{
  GskDnsResourceRecord rr;
  RRListMagic magic;
  guint expire_time;
  guint byte_size;
  guint lock_count;

  /* Whether the record comes from an authoritative DNS server. */
  guint is_authoritative : 1;

  /* Whether the record comes from the user.
     Such records are treated as completely authoritative. */
  guint is_from_user : 1;

  /* Whether the entry is a negative entry.
     That means that it was returned with the
     GSK_DNS_RESPONSE_ERROR_NAME_ERROR error code. */
  guint is_negative : 1;

  /* Indicates that this record has been abandoned since it was locked.
     It has been removed from all the lists and trees,
     but it still affects the estimated size of the cache.
     It will be freed as soon as its lock_count reaches 0. */
  guint is_deprecated : 1;

  RRList *owner_next;
  RRList *owner_prev;
  RRList *lru_next;
  RRList *lru_prev;
};

/* predicate to determine if the given rr_list is in the lru_list.
   This is equivalent to whether the rr_list
   is in the by_expire_time tree. */
#define RR_LIST_IS_IN_LRU_LIST(rr_list)   ((rr_list)->lock_count == 0 \
                                        && !(rr_list)->is_from_user \
                                        && !(rr_list)->is_deprecated)

struct _GskDnsRRCache
{
  GHashTable         *owner_to_rr_list;

  /* ordered by expiry */
  GTree              *rr_list_by_expire_time;

  guint               ref_count;
  gboolean            is_roundrobin;

  /* current use of all objects in this cache */
  guint64             num_bytes_used;
  guint               num_records;

  /* maximum use, except when too much is locked in. */
  guint64             max_bytes_used;
  guint               max_records;

  /* list of discardable rrlists (those with lock_count==0) */
  RRList             *lru_first;		/* the most recently used */
  RRList             *lru_last;			/* the least recently used */
};

static gint
compare_rr_list_by_expire_time (const RRList *a,
				const RRList *b)
{
  if (a->expire_time < b->expire_time)
    return -1;
  if (a->expire_time > b->expire_time)
    return +1;
  if (a < b)
    return -1;
  if (a > b)
    return +1;
  return 0;
}

/* Set the expiration time on an rr_list,
   possibly updating the by-expire-time tree. */
static inline void
set_expire_time (GskDnsRRCache *rr_cache,
                 RRList        *rr_list,
                 guint          expire_time)
{
  gboolean expirable = RR_LIST_IS_IN_LRU_LIST (rr_list);
  if (expirable)
    {
      g_assert (g_tree_lookup (rr_cache->rr_list_by_expire_time, rr_list) != NULL);
      g_tree_remove (rr_cache->rr_list_by_expire_time, rr_list);
    }
  rr_list->expire_time = expire_time;
  if (expirable)
    {
      g_tree_insert (rr_cache->rr_list_by_expire_time, rr_list, rr_list);
    }
}


/* For debugging, implement ASSERT_INVARIANTS(rr_cache),
   which does nothing if dns debugging is disabled,
   or asserts every condition possible if dns debugging enabled.

   NOTE: this is unused unless you specify --gsk-debug=dns
   as an argument to your program.
 */
#ifdef GSK_DEBUG
typedef struct
{
  guint n_lru;
  guint n_rr;
  guint n_user_rr;
  guint n_locked_nonuser;
  GskDnsRRCache *rr_cache;
} CheckInvariantForeachInfo;

static void
check_invariant_owner_to_rr_list_foreach (gpointer key, gpointer value, gpointer data)
{
  CheckInvariantForeachInfo *info = data;
  GskDnsRRCache *rr_cache = info->rr_cache;
  RRList *at = value;
  g_assert (key); g_assert (value);
  g_assert (at->owner_prev == NULL);
  for (at = value; at; at = at->owner_next)
    {
      g_assert (at->magic == RR_LIST_MAGIC);
      if (at->owner_prev)
        g_assert (at->owner_prev->owner_next == at);
      if (at->owner_next)
        g_assert (at->owner_next->owner_prev == at);
      if (RR_LIST_IS_IN_LRU_LIST (at))
        info->n_lru += 1;
      g_assert (g_ascii_strcasecmp (at->rr.owner, (char*) key) == 0);
      info->n_rr += 1;
      if (at->is_from_user)
        info->n_user_rr += 1;
      else if (at->lock_count == 0)
        g_assert (g_tree_lookup (rr_cache->rr_list_by_expire_time, at) != NULL);
      else
        info->n_locked_nonuser += 1;
    }
}
  
static void
assert_invariants (GskDnsRRCache *rr_cache)
{
  RRList *at;
  CheckInvariantForeachInfo info = { 0, 0, 0, 0, rr_cache };
  guint lru_count = 0;
  for (at = rr_cache->lru_first; at; at = at->lru_next)
    {
      g_assert (at->magic == RR_LIST_MAGIC);
      g_assert (RR_LIST_IS_IN_LRU_LIST (at));
      if (at->lru_prev)
        {
          g_assert (at->lru_prev->lru_next == at);
          g_assert (at != rr_cache->lru_first);
        }
      else
        g_assert (at == rr_cache->lru_first);
      if (at->lru_next)
        {
          g_assert (at->lru_next->lru_prev == at);
          g_assert (at != rr_cache->lru_last);
        }
      else
        g_assert (at == rr_cache->lru_last);
      lru_count++;
    }
  g_hash_table_foreach (rr_cache->owner_to_rr_list,
                        check_invariant_owner_to_rr_list_foreach,
                        &info);
  g_assert (lru_count == info.n_lru);
  g_assert (g_tree_nnodes (rr_cache->rr_list_by_expire_time) == (int)info.n_rr - (int)info.n_locked_nonuser - (int)info.n_user_rr);
}
#define ASSERT_INVARIANTS(rr_cache)				\
  G_STMT_START{							\
    if (GSK_IS_DEBUGGING (DNS))					\
      assert_invariants (rr_cache);				\
  }G_STMT_END
#else
#define ASSERT_INVARIANTS(rr_cache)
#endif

/**
 * gsk_dns_rr_cache_new:
 * @max_bytes: the maximum number of bytes to use for all the resource
 * records and negative information in this pool.  (Note that there is other
 * overhead, like hash-tables, which is not considered in this number.)
 * @max_records: the maximum number of records in this cache.
 *
 * Create a new, empty DNS cache.
 *
 * returns: the new GskDnsRRCache.
 */
GskDnsRRCache *
gsk_dns_rr_cache_new        (guint64                  max_bytes,
			     guint                    max_records)
{
  GskDnsRRCache *rv = g_new (GskDnsRRCache, 1);
  rv->owner_to_rr_list = g_hash_table_new (g_str_hash, g_str_equal);
  rv->rr_list_by_expire_time
    = g_tree_new ((GCompareFunc) compare_rr_list_by_expire_time);
  rv->ref_count = 1;
  rv->num_bytes_used = 0;
  rv->num_records = 0;
  rv->max_bytes_used = max_bytes;
  rv->max_records = max_records;
  rv->lru_first = NULL;
  rv->lru_last = NULL;
  rv->is_roundrobin = TRUE;
  ASSERT_INVARIANTS (rv);
  return rv;
}

/**
 * gsk_dns_rr_cache_roundrobin:
 * @rr_cache: a resource-record cache.
 * @do_roundrobin: whether to randomize requests to support round-round DNS.
 *
 * Set whether to randomize returns if more than one record exists.
 */
void
gsk_dns_rr_cache_roundrobin (GskDnsRRCache *rr_cache,
                             gboolean       do_roundrobin)
{
  rr_cache->is_roundrobin = do_roundrobin;
}


static inline void
remove_from_lru_list (GskDnsRRCache *rr_cache, RRList *at)
{
  if (at->lru_prev != NULL)
    at->lru_prev->lru_next = at->lru_next;
  else
    {
      g_assert (at == rr_cache->lru_first);
      rr_cache->lru_first = at->lru_next;
    }
  if (at->lru_next != NULL)
    at->lru_next->lru_prev = at->lru_prev;
  else
    {
      g_assert (at == rr_cache->lru_last);
      rr_cache->lru_last = at->lru_prev;
    }
  at->lru_prev = at->lru_next = NULL;
}

static inline void
prepend_to_lru_list (GskDnsRRCache *rr_cache, RRList *at)
{
  at->lru_prev = NULL;
  at->lru_next = rr_cache->lru_first;
  if (rr_cache->lru_first != NULL)
    rr_cache->lru_first->lru_prev = at;
  else
    rr_cache->lru_last = at;
  rr_cache->lru_first = at;
}

static void
remove_owner_to_rr_list_entry (GskDnsRRCache *rr_cache,
                               const char    *owner)
{
  char *lc_owner;
  gpointer name;
  gpointer list;
  LOWER_CASE_COPY_ON_STACK (lc_owner, owner);
  /* delete/remove the key */
  if (!g_hash_table_lookup_extended (rr_cache->owner_to_rr_list,
				     lc_owner,
				     &name,
				     &list))
    g_assert_not_reached ();
  g_hash_table_remove (rr_cache->owner_to_rr_list, lc_owner);
  g_free (name);
}

static void
change_owner_to_rr_list_entry (GskDnsRRCache *rr_cache,
                               RRList        *new_head)
{
  char *lc_owner;
  LOWER_CASE_COPY_ON_STACK (lc_owner, new_head->rr.owner);
  g_assert (g_hash_table_lookup (rr_cache->owner_to_rr_list, lc_owner) != NULL);
  g_assert (new_head->magic == RR_LIST_MAGIC);
  g_hash_table_insert (rr_cache->owner_to_rr_list, lc_owner, new_head);
}

static RRList *
lookup_owner_to_rr_list_entry (GskDnsRRCache *rr_cache, 
                               const char *owner)
{
  char *lc_owner;
  RRList *rv;
  LOWER_CASE_COPY_ON_STACK (lc_owner, owner);
  rv = g_hash_table_lookup (rr_cache->owner_to_rr_list, lc_owner);
  if (rv != NULL)
    g_assert (rv->magic == RR_LIST_MAGIC);
  return rv;
}

/* Verify that enough space is free before adding/allocating
 * a new rrcache, if possible.
 */
static void
ensure_space (GskDnsRRCache   *rr_cache,
	      guint            num_records,
	      guint            byte_size)
{
  while (rr_cache->lru_last != NULL
      && (rr_cache->num_bytes_used + byte_size > rr_cache->max_bytes_used
       || rr_cache->num_records + num_records > rr_cache->max_records))
    {
      RRList *to_discard = rr_cache->lru_last;
      remove_from_lru_list (rr_cache, to_discard);
      if (to_discard->owner_prev == NULL)
	{
	  if (to_discard->owner_next == NULL)
	    {
              remove_owner_to_rr_list_entry (rr_cache, to_discard->rr.owner);
	    }
	  else
	    {
              change_owner_to_rr_list_entry (rr_cache, to_discard->owner_next);
	      to_discard->owner_next->owner_prev = NULL;
	    }
	}
      else
	{
	  /* adjust pointers: the hash-table can stay the same */
	  to_discard->owner_prev->owner_next = to_discard->owner_next;
	  if (to_discard->owner_next != NULL)
	    to_discard->owner_next->owner_prev = to_discard->owner_prev;
	}

      rr_cache->num_records--;
      rr_cache->num_bytes_used -= to_discard->byte_size;
      g_tree_remove (rr_cache->rr_list_by_expire_time, to_discard);

      to_discard->magic = RR_LIST_EXPIRED;

      g_free (to_discard);
    }
}

/* Return TRUE if OLD_STR is shorter than NEW_STR,
 * in which case we will copy NEW_STR over OLD_STR.
 */
static gboolean
try_update_string (char       *old_str,
		   const char *new_str)
{
  if (strlen (old_str) >= strlen (new_str))
    {
      strcpy (old_str, new_str);
      return TRUE;
    }
  return FALSE;
}

/* Update a RRList to match a new ResourceRecord,
 * returning if it succeeded.
 *
 * If it fails, remove RRList and add record.
 *
 * You may assume `owner', `type' and `class' already match.
 */
typedef enum
{
  UPDATE_SUCCESS,
  UPDATE_ADD_NEW,
  UPDATE_REPLACE
} UpdateResult;

static UpdateResult
update_record (GskDnsRRCache        *rr_cache,
	       RRList               *list,
	       const GskDnsResourceRecord *record,
	       gboolean              is_authoritative,
	       gulong                cur_time)
{
  /* First check if the information matches. */
  gboolean matches = TRUE;
  switch (record->type)
    {
    case GSK_DNS_RR_HOST_ADDRESS:
      matches = (memcmp (list->rr.rdata.a.ip_address,
			 record->rdata.a.ip_address,
			 4) == 0);
      break;
    case GSK_DNS_RR_HOST_ADDRESS_IPV6:
      matches = (memcmp (list->rr.rdata.aaaa.address,
			 record->rdata.aaaa.address,
			 16) == 0);
      break;

    case GSK_DNS_RR_NAME_SERVER:
    case GSK_DNS_RR_CANONICAL_NAME:
    case GSK_DNS_RR_POINTER:
      matches = (strcmp (list->rr.rdata.domain_name,
			 record->rdata.domain_name) == 0);
      break;

    case GSK_DNS_RR_MAIL_EXCHANGE:
      matches = (strcmp (list->rr.rdata.mx.mail_exchange_host_name,
			 record->rdata.mx.mail_exchange_host_name) == 0)
            &&  (list->rr.rdata.mx.preference_value
	      == record->rdata.mx.preference_value);
      break;

    case GSK_DNS_RR_HOST_INFO:
      matches = (strcmp (list->rr.rdata.hinfo.cpu,
			 record->rdata.hinfo.cpu) == 0)
           &&   (strcmp (list->rr.rdata.hinfo.os,
			 record->rdata.hinfo.os) == 0);
      break;

      /* XXX: hmm, i don't know if we should be caching soa's */
    case GSK_DNS_RR_START_OF_AUTHORITY:
      matches = (strcmp (list->rr.rdata.soa.mname,
			 record->rdata.soa.mname) == 0)
           &&   (strcmp (list->rr.rdata.soa.rname,
			 record->rdata.soa.rname) == 0);
      break;

    case GSK_DNS_RR_TEXT:
      matches = (strcmp (list->rr.rdata.txt,
			 record->rdata.txt) == 0);
      break;

    case GSK_DNS_RR_WILDCARD:
      break;

    case GSK_DNS_RR_WELL_KNOWN_SERVICE:
    case GSK_DNS_RR_ZONE_TRANSFER:
    case GSK_DNS_RR_ZONE_MAILB:
      g_warning ("rr_cache: update_record: UNIMPLEMENTED");
      /*TODO*/
      break;
    }

  /* XXX: error checking, maybe?
     For example, it's weird if a non-authoritative positive
     result exists when we have an authoritative negative response.
     (All negative responses with non-zero timeout have authority records) */
  if (list->is_negative)
    {
      return UPDATE_REPLACE;
    }

  /* if matches, just prolong the authority data */
  if (matches)
    {
      gulong new_expire_time = cur_time + record->time_to_live;
      if (list->is_from_user)
	return UPDATE_SUCCESS;

      /* XXX: technically, need to separate non-authoritative longer
	 timeouts and authoritative shorter timeouts, maybe? */
      if (is_authoritative)
	list->is_authoritative = 1;

      /* Update the expire_time to be longer, if needed. */
      if (list->expire_time < new_expire_time)
	set_expire_time (rr_cache, list, new_expire_time);
      return UPDATE_SUCCESS;
    }

  /* Ok, try to update the data in-place. */
  switch (record->type)
    {
    case GSK_DNS_RR_HOST_ADDRESS:
    case GSK_DNS_RR_HOST_ADDRESS_IPV6:
      return UPDATE_ADD_NEW;

    case GSK_DNS_RR_NAME_SERVER:
      return UPDATE_ADD_NEW;

    case GSK_DNS_RR_CANONICAL_NAME:
    case GSK_DNS_RR_POINTER:
      if (list->is_from_user)
	return UPDATE_SUCCESS;
      if (try_update_string (list->rr.rdata.domain_name,
			     record->rdata.domain_name))
	return UPDATE_SUCCESS;
      break;

      /* XXX: is this really a merging situation? */
    case GSK_DNS_RR_MAIL_EXCHANGE:
      if (list->is_from_user)
	return UPDATE_SUCCESS;
      if (try_update_string (list->rr.rdata.mx.mail_exchange_host_name,
			     record->rdata.mx.mail_exchange_host_name))
	{
	  list->rr.rdata.mx.preference_value = record->rdata.mx.preference_value;
	  return UPDATE_SUCCESS;
	}
      break;

      /* XXX: a `try_update_2strings might allow different overlappings, etc. */
	      
    case GSK_DNS_RR_HOST_INFO:
      if (list->is_from_user)
	return UPDATE_SUCCESS;
      if (try_update_string (list->rr.rdata.hinfo.cpu,
			     record->rdata.hinfo.cpu)
       && try_update_string (list->rr.rdata.hinfo.os,
			     record->rdata.hinfo.os))
	{
	  return UPDATE_SUCCESS;
	}
      break;

    /* XXX: i doubt that merging SOA records like this is correct. */
    case GSK_DNS_RR_START_OF_AUTHORITY:
      if (list->is_from_user)
	return UPDATE_SUCCESS;
      if (try_update_string (list->rr.rdata.soa.mname,
			     record->rdata.soa.mname)
       && try_update_string (list->rr.rdata.soa.rname,
			     record->rdata.soa.rname))
	{
	  list->rr.rdata.soa.serial = record->rdata.soa.serial;
	  list->rr.rdata.soa.refresh_time = record->rdata.soa.refresh_time;
	  list->rr.rdata.soa.retry_time = record->rdata.soa.retry_time;
	  list->rr.rdata.soa.expire_time = record->rdata.soa.expire_time;
	  list->rr.rdata.soa.minimum_time = record->rdata.soa.minimum_time;
	  return UPDATE_SUCCESS;
	}
      break;

    case GSK_DNS_RR_TEXT:
      if (list->is_from_user)
	return UPDATE_SUCCESS;
      if (try_update_string (list->rr.rdata.txt, record->rdata.txt))
	return UPDATE_SUCCESS;
      break;

    case GSK_DNS_RR_WILDCARD:
      return UPDATE_SUCCESS;

    case GSK_DNS_RR_WELL_KNOWN_SERVICE:
    case GSK_DNS_RR_ZONE_TRANSFER:
    case GSK_DNS_RR_ZONE_MAILB:
      g_warning ("rr_cache: update_record: UNIMPLEMENTED");
      /*TODO*/
      break;
    }

  return UPDATE_REPLACE;
}

/* Compute the number of bytes that flatten_rr will
 * need in order to make a copy of this record.
 */
static guint
compute_byte_size (const GskDnsResourceRecord *record)
{
  int str_length = strlen (record->owner) + 1;
  switch (record->type)
    {
    case GSK_DNS_RR_NAME_SERVER:
    case GSK_DNS_RR_CANONICAL_NAME:
    case GSK_DNS_RR_POINTER:
      str_length += strlen (record->rdata.domain_name) + 1;
      break;
    case GSK_DNS_RR_MAIL_EXCHANGE:
      str_length += strlen (record->rdata.mx.mail_exchange_host_name) + 1;
      break;
    case GSK_DNS_RR_HOST_INFO:
      str_length += strlen (record->rdata.hinfo.cpu) + 1
	          + strlen (record->rdata.hinfo.os) + 1;
      break;
    case GSK_DNS_RR_START_OF_AUTHORITY:
      str_length += strlen (record->rdata.soa.mname) + 1
                  + strlen (record->rdata.soa.rname) + 1;
      break;
    case GSK_DNS_RR_TEXT:
      str_length += strlen (record->rdata.txt) + 1;
      break;
    case GSK_DNS_RR_HOST_ADDRESS:
    case GSK_DNS_RR_HOST_ADDRESS_IPV6:
    case GSK_DNS_RR_WILDCARD:
      break;

    case GSK_DNS_RR_WELL_KNOWN_SERVICE:
    case GSK_DNS_RR_ZONE_TRANSFER:
    case GSK_DNS_RR_ZONE_MAILB:
      /*TODO*/
      break;

  }
  return str_length + sizeof (RRList);
}

static guint
compute_byte_size_for_negative_record (const char *owner)
{
  return strlen (owner) + 1 + sizeof (RRList);
}

/* Take a RRList (which must have been allocated acc. to compute_byte_size)
 * and a record/cur_time and store a complete copy in rr_list (strings
 * will be stored in the memory right after the structure).
 */
static void
flatten_rr (RRList               *out,
	    const GskDnsResourceRecord *record,
	    gulong                cur_time)
{
  char *str_slab = (char *) (out + 1);
  out->rr = *record;
#define MAKE_INTO_SLAB_COPY(ptr)			\
  G_STMT_START{						\
    ptr = strcpy (str_slab, ptr);			\
    str_slab = strchr (str_slab, 0) + 1;		\
  }G_STMT_END
    
  MAKE_INTO_SLAB_COPY (out->rr.owner);

  switch (record->type)
    {
    case GSK_DNS_RR_NAME_SERVER:
    case GSK_DNS_RR_CANONICAL_NAME:
    case GSK_DNS_RR_POINTER:
      MAKE_INTO_SLAB_COPY (out->rr.rdata.domain_name);
      break;
    case GSK_DNS_RR_MAIL_EXCHANGE:
      MAKE_INTO_SLAB_COPY (out->rr.rdata.mx.mail_exchange_host_name);
      break;
    case GSK_DNS_RR_HOST_INFO:
      MAKE_INTO_SLAB_COPY (out->rr.rdata.hinfo.cpu);
      MAKE_INTO_SLAB_COPY (out->rr.rdata.hinfo.os);
      break;
    case GSK_DNS_RR_START_OF_AUTHORITY:
      MAKE_INTO_SLAB_COPY (out->rr.rdata.soa.mname);
      MAKE_INTO_SLAB_COPY (out->rr.rdata.soa.rname);
      break;
    case GSK_DNS_RR_TEXT:
      MAKE_INTO_SLAB_COPY (out->rr.rdata.txt);
      break;

    case GSK_DNS_RR_HOST_ADDRESS:
    case GSK_DNS_RR_HOST_ADDRESS_IPV6:
    case GSK_DNS_RR_WILDCARD:
      break;

    case GSK_DNS_RR_WELL_KNOWN_SERVICE:
    case GSK_DNS_RR_ZONE_TRANSFER:
    case GSK_DNS_RR_ZONE_MAILB:
      /*TODO*/
      break;
  }

  out->magic = RR_LIST_MAGIC;
  out->expire_time = cur_time + record->time_to_live;
  out->byte_size = str_slab - ((char *) out);
  out->lock_count = 0;
  out->rr.allocator = NULL;
  out->owner_next = out->owner_prev = NULL;
  out->lru_next = out->lru_prev = NULL;
  out->is_negative = 0;
  out->is_deprecated = 0;
}

static void
flatten_negative_rr (RRList               *out,
		     const char           *owner,
		     GskDnsResourceRecordType type,
		     GskDnsResourceClass   class,
		     gboolean              is_authoritative,
		     glong                 expire_time)
{
  char *str_slab = (char *) (out + 1);
  out->magic = RR_LIST_MAGIC;
  out->is_authoritative = is_authoritative;
  out->expire_time = expire_time;
  out->rr.type = type;
  out->rr.record_class = class;
  out->rr.time_to_live = -1;
  out->rr.allocator = NULL;
  out->lock_count = 0;
  out->is_from_user = 0;
  out->is_negative = 1;
  out->is_deprecated = 0;
  out->owner_next = out->owner_prev = out->lru_next = out->lru_prev = NULL;

  out->rr.owner = str_slab;
  strcpy (str_slab, owner);
  str_slab = strchr (str_slab, 0) + 1;

  out->byte_size = str_slab - ((char*)(out));
}

/**
 * gsk_dns_rr_cache_insert:
 * @rr_cache: a resource-record cache.
 * @record: the record to store in the cache.
 * (The cache will maintain its own copy of the data)
 * @is_authoritative: whether the entry comes from an authoritative
 * source, as defined by RFC 1034.
 * @cur_time: the current time, used for figuring out the absolute expiration time.
 *
 * Insert a new finding into the resource-record cache.
 *
 * returns:
 * A new copy of the record is returned; if you wish to guarantee that
 * the record is not deleted, you should call gsk_dns_rr_cache_lock() on it.
 */
GskDnsResourceRecord *
gsk_dns_rr_cache_insert     (GskDnsRRCache     *rr_cache,
			     const GskDnsResourceRecord    *record,
			     gboolean                 is_authoritative,
			     gulong                   cur_time)
{
  guint byte_size;
  RRList *owner_list;
  RRList *new_owner_list;
  RRList *at;
  char *lc_owner;

  LOWER_CASE_COPY_ON_STACK (lc_owner, record->owner);

  byte_size = compute_byte_size (record);
  ensure_space (rr_cache, 1, byte_size);

  ASSERT_INVARIANTS (rr_cache);

  g_return_val_if_fail (record->type != GSK_DNS_RR_WILDCARD, NULL);
  owner_list = g_hash_table_lookup (rr_cache->owner_to_rr_list, lc_owner);
  new_owner_list = owner_list;

  /* search for collisions */
  for (at = owner_list; at != NULL; )
    {
      if (record->type == at->rr.type
       && record->record_class == at->rr.record_class)
	{

	  /* Update the current record, possibly
	   * deleting the old record, or scrapping
	   * the new record, or keeping them both.
	   */
	  UpdateResult result;
	  result = update_record (rr_cache, at,
				  record, is_authoritative,
				  cur_time);
	  switch (result)
	    {
	    case UPDATE_SUCCESS:
	      return &at->rr;
	    case UPDATE_ADD_NEW:
	      at = at->owner_next;
	      break;
	    case UPDATE_REPLACE:
	      /* otherwise, remove `at', it is obsolete. */
	      if (at->owner_prev == NULL)
	        new_owner_list = at->owner_next;
	      else
		at->owner_prev->owner_next = at->owner_next;
	      if (at->owner_next != NULL)
		at->owner_next->owner_prev = at->owner_prev;
	      if (RR_LIST_IS_IN_LRU_LIST (at))
		remove_from_lru_list (rr_cache, at);
              if (at->lock_count > 0)
                {
		  at->is_deprecated = 1;
                  at = at->owner_next;
                }
              else
		{
		  RRList *next;
                  g_assert (g_tree_lookup (rr_cache->rr_list_by_expire_time, at) != NULL);
                  g_tree_remove (rr_cache->rr_list_by_expire_time, at);
                  next = at->owner_next;
		  at->magic = RR_LIST_REPLACED;
		  rr_cache->num_bytes_used -= at->byte_size;
		  rr_cache->num_records--;
		  g_free (at);
		  at = next;
		}
	      break;
	    }
	}
      else
	{
	  at = at->owner_next;
	}
    }

  if (owner_list != NULL && new_owner_list == NULL)
    {
      gpointer key, value;
      if (!g_hash_table_lookup_extended (rr_cache->owner_to_rr_list,
					 lc_owner, &key, &value))
	g_assert_not_reached ();
      g_hash_table_remove (rr_cache->owner_to_rr_list, key);
      g_free (key);
    }
  else if (owner_list != new_owner_list)
    {
      g_assert (new_owner_list->magic == RR_LIST_MAGIC);
      g_hash_table_insert (rr_cache->owner_to_rr_list,
			   lc_owner,
			   new_owner_list);
    }
  g_assert (at == NULL);

  at = g_malloc (byte_size);
  flatten_rr (at, record, cur_time);
  at->is_authoritative = is_authoritative ? 1 : 0;
  at->is_from_user = 0;
  at->byte_size = byte_size;
  rr_cache->num_bytes_used += byte_size;
  rr_cache->num_records += 1;

  /* add it to owner_list */
  if (new_owner_list == NULL)
    {
      g_hash_table_insert (rr_cache->owner_to_rr_list,
			   g_strdup (lc_owner),
			   at);
      at->owner_next = at->owner_prev = NULL;
    }
  else
    {
      /* insert this element as the second in the list,
       * to avoid disturbing the hashtable.
       */
      at->owner_prev = new_owner_list;
      at->owner_next = new_owner_list->owner_next;
      if (at->owner_next != NULL)
	at->owner_next->owner_prev = at;
      new_owner_list->owner_next = at;
      g_assert (new_owner_list->owner_prev == NULL);
    }

  /* add it to expire tree */
  g_tree_insert (rr_cache->rr_list_by_expire_time, at, at);

  /* add it to lru list */
  prepend_to_lru_list (rr_cache, at);

  ASSERT_INVARIANTS (rr_cache);

  return &at->rr;
}

/* NOTE: do not match cnames here */
static gboolean
record_matches_query (GskDnsResourceRecord    *record,
		      GskDnsResourceRecordType query_type,
		      GskDnsResourceClass      query_class)
{
  if (query_class != GSK_DNS_CLASS_WILDCARD
   && query_class != record->record_class)
    return FALSE;

  if (query_type != GSK_DNS_CLASS_WILDCARD
   && query_type != record->type)
    return FALSE;

  return TRUE;
}

/**
 * gsk_dns_rr_cache_lookup_list:
 * @rr_cache: a resource-record cache to query.
 * @owner: the domain name to lookup resource-records for.
 * @query_type: the type of resource-record to look up.
 * @query_class: the address namespace in which to look for the information.
 *
 * Lookup some or all information about a given name in
 * @rr_cache's memory.
 *
 * If you receive NULL, it may be because we have gotten
 * negative information registered with gsk_dns_rr_cache_add_negative()
 * or because we don't have a relevant record on file.
 * You may call gsk_dns_rr_cache_is_negative() to distinguish these cases.
 *
 * returns: a list of #GskDnsResourceRecord which you must
 * call gsk_dns_rr_cache_lock() on if you want to keep around.
 * However, you must call g_slist_free() on the list itself.
 */
GSList *
gsk_dns_rr_cache_lookup_list(GskDnsRRCache           *rr_cache,
			     const char              *owner,
			     GskDnsResourceRecordType query_type,
			     GskDnsResourceClass      query_class)
{
  GSList *rv = NULL;
  RRList *at;
  char *lc_owner;
  gsk_dns_rr_cache_flush (rr_cache, gsk_main_loop_default ()->current_time.tv_sec + 1);

  LOWER_CASE_COPY_ON_STACK (lc_owner, owner);
  for (at = g_hash_table_lookup (rr_cache->owner_to_rr_list, lc_owner);
       at != NULL;
       at = at->owner_next)
    {
      if (!at->is_negative)
	{
          if (record_matches_query (&at->rr, query_type, query_class))
	    rv = g_slist_prepend (rv, &at->rr);
	  /*
	  else if (at->rr.type == GSK_DNS_RR_CANONICAL_NAME)
	    cname = g_slist_prepend (rv, &at->rr);
	   */
	}
    }
  return g_slist_reverse (rv);
}

/**
 * gsk_dns_rr_cache_lookup_one:
 * @rr_cache: a resource-record cache to query.
 * @owner: the domain name to lookup resource-records for.
 * @query_type: the type of resource-record to look up.
 * @query_class: the address namespace in which to look for the information.
 *
 * Find the first appropriate resource record of a given
 * specification.
 *
 * If you receive NULL, it may be because we have gotten
 * negative information registered with gsk_dns_rr_cache_add_negative()
 * or because we don't have a relevant record on file.
 * You may call gsk_dns_rr_cache_is_negative() to distinguish these cases.
 *
 * returns: a pointer to a #GskDnsResourceRecord.
 * You must call gsk_dns_rr_cache_lock() on it if you want to keep it around.
 */
GskDnsResourceRecord *
gsk_dns_rr_cache_lookup_one (GskDnsRRCache           *rr_cache,
			     const char              *owner,
			     GskDnsResourceRecordType query_type,
			     GskDnsResourceClass      query_class,
                             GskDnsRRCacheLookupFlags flags)

{
  RRList *at;
  char *lc_owner;
  GHashTable *cname_table = NULL;
  GSList *pending_names_to_lookup;
  RRList *rv = NULL;
  GSList *rv_list = NULL;
  gboolean roundrobin = rr_cache->is_roundrobin;

  gsk_dns_rr_cache_flush (rr_cache, gsk_main_loop_default ()->current_time.tv_sec + 1);

  LOWER_CASE_COPY_ON_STACK (lc_owner, owner);
  pending_names_to_lookup = g_slist_prepend (NULL, lc_owner);

  while (pending_names_to_lookup != NULL)
    {
      const char *this_owner = pending_names_to_lookup->data;
      pending_names_to_lookup = g_slist_remove (pending_names_to_lookup, this_owner);
      for (at = lookup_owner_to_rr_list_entry (rr_cache, this_owner);
	   at != NULL;
	   at = at->owner_next)
	{
	  if (!at->is_negative)
	    {
	      if (record_matches_query (&at->rr, query_type, query_class))
		{
                  if (!roundrobin)
                    {
                      g_slist_free (pending_names_to_lookup);
                      if (cname_table != NULL)
                        g_hash_table_destroy (cname_table);
                      return &at->rr;
                    }
                  if (rv)
                    rv_list = g_slist_prepend (rv_list, at);
                  else
                    rv = at;
		}
	      else if ((flags & GSK_DNS_RR_CACHE_LOOKUP_DEREF_CNAMES) != 0
                    && at->rr.type == GSK_DNS_RR_CANONICAL_NAME)
		{
		  if (cname_table == NULL)
		    {
		      cname_table = g_hash_table_new (g_str_hash, g_str_equal);
		      g_hash_table_insert (cname_table, lc_owner, GUINT_TO_POINTER (1));
		    }
		  if (g_hash_table_lookup (cname_table, at->rr.rdata.domain_name) == NULL)
		    {
		      g_hash_table_insert (cname_table, at->rr.rdata.domain_name, GUINT_TO_POINTER (1));
		      pending_names_to_lookup = g_slist_append (pending_names_to_lookup, at->rr.rdata.domain_name);
		    }
		}
	    }
	}
    }
  g_slist_free (pending_names_to_lookup);
  if (cname_table != NULL)
    g_hash_table_destroy (cname_table);

  ASSERT_INVARIANTS (rr_cache);

  /* If there was more than one thing found,
     choose randomly. */
  if (rv_list != NULL)
    {
      guint which = g_random_int_range (0, 1 + g_slist_length (rv_list));
      if (which > 0)
        rv = g_slist_nth_data (rv_list, which - 1);
      g_slist_free (rv_list);
    }
  return &rv->rr;
}

/**
 * gsk_dns_rr_cache_unlock:
 * @rr_cache: a resource-record cache.
 * @record: a locked record belonging to that cache.
 *
 * Reduce the lock-count of this record within the cache.
 * When a record is locked, it will not be deleted, even
 * if memory requirements are violated as a result.
 *
 * Once the lock-count is zero, the resource record
 * will be deleted whenever it grows sufficiently stale,
 * or resources grow sufficiently scarce.
 */
void
gsk_dns_rr_cache_unlock     (GskDnsRRCache           *rr_cache,
			     GskDnsResourceRecord    *record)
{
  RRList *rr_list = (RRList *) record;
  char *lc_owner;
  g_return_if_fail (rr_list->magic == RR_LIST_MAGIC);
  g_return_if_fail (rr_list->lock_count > 0);

  LOWER_CASE_COPY_ON_STACK (lc_owner, record->owner);

  g_assert (rr_list->is_deprecated
        ||  g_hash_table_lookup (rr_cache->owner_to_rr_list, lc_owner) != NULL);

  --(rr_list->lock_count);

  if (rr_list->lock_count > 0)
    return;

  /* should we destroy it? */
  if (rr_list->is_deprecated
   || rr_cache->max_records < rr_cache->num_records
   || rr_cache->max_bytes_used < rr_cache->num_bytes_used)
    {
      if (!rr_list->is_deprecated)
        {
          /* should have already flushed anything in the LRU list */
          g_return_if_fail (rr_cache->lru_first == NULL);
          g_return_if_fail (!rr_list->is_from_user);
        }

      /* yes, destroy it */

      /* remove from owner list */
      if (!rr_list->is_deprecated)
	{
	  if (rr_list->owner_next != NULL)
	    rr_list->owner_next->owner_prev = rr_list->owner_prev;
	  if (rr_list->owner_prev != NULL)
	    rr_list->owner_prev->owner_next = rr_list->owner_next;
	  else
	    {
	      if (rr_list->owner_next == NULL)
		{
		  gpointer key, value;
		  if (!g_hash_table_lookup_extended (rr_cache->owner_to_rr_list,
						     lc_owner,
						     &key, &value))
		    g_assert_not_reached ();
		  g_hash_table_remove (rr_cache->owner_to_rr_list, key);
		  g_free (key);
		}
	      else
		g_hash_table_insert (rr_cache->owner_to_rr_list,
				     lc_owner,
				     rr_list->owner_next);
	    }
	}
      rr_cache->num_bytes_used -= rr_list->byte_size;
      rr_cache->num_records--;
      g_free (record);

      return;
    }

  g_tree_insert (rr_cache->rr_list_by_expire_time, rr_list, rr_list);

  /* move rr_list to lru cache */
  prepend_to_lru_list (rr_cache, rr_list);

  ASSERT_INVARIANTS (rr_cache);
}

/**
 * gsk_dns_rr_cache_lock:
 * @rr_cache: a resource-record cache.
 * @record: a record obtained from gsk_dns_rr_cache_lookup_one()
 * or gsk_dns_rr_cache_lookup_list().
 *
 * Lock this record in the cache.
 * When a record is locked, it will not be deleted, even
 * if memory requirements are violated as a result.
 */
void
gsk_dns_rr_cache_lock       (GskDnsRRCache           *rr_cache,
			     GskDnsResourceRecord    *record)
{
  RRList *rr_list = (RRList *) record;
  gboolean was_in_lru = RR_LIST_IS_IN_LRU_LIST (rr_list);
  g_return_if_fail (rr_list->magic == RR_LIST_MAGIC);
  
  ++(rr_list->lock_count);

  if (was_in_lru)
    {
      remove_from_lru_list (rr_cache, rr_list);
      g_tree_remove (rr_cache->rr_list_by_expire_time, rr_list);
    }

#ifndef G_DISABLE_ASSERT
  {
    char *lc_owner;
    LOWER_CASE_COPY_ON_STACK (lc_owner, record->owner);
    g_assert (g_hash_table_lookup (rr_cache->owner_to_rr_list, lc_owner) != NULL);
  }
#endif
  ASSERT_INVARIANTS (rr_cache);
}

/**
 * gsk_dns_rr_cache_mark_user:
 * @rr_cache: a resource-record cache.
 * @record: a record obtained from gsk_dns_rr_cache_lookup_one()
 * or gsk_dns_rr_cache_lookup_list().
 *
 * Indicate that a record should be regarded as completely
 * authoritative.  This is like locking the record, but
 * additionally, the record will be used no matter what other
 * supposed authorities say.
 *
 * This is currently just used for handling /etc/hosts,
 * whose entries precede any other name-server checking.
 *
 * You should not call this function if the is_from_user
 * flag is already set.
 */
void
gsk_dns_rr_cache_mark_user  (GskDnsRRCache           *rr_cache,
			     GskDnsResourceRecord    *record)
{
  RRList *rr_list = (RRList *) record;
  g_return_if_fail (rr_list->magic == RR_LIST_MAGIC);
  if (rr_list->is_from_user)
    return;
  gsk_dns_rr_cache_lock (rr_cache, record);
  rr_list->is_from_user = 1;
  ASSERT_INVARIANTS (rr_cache);
}

/**
 * gsk_dns_rr_cache_unmark_user:
 * @rr_cache: a resource-record cache.
 * @record: a record which had previously been passed to
 * gsk_dns_rr_cache_mark_user().
 *
 * Undo the affect of gsk_dns_rr_cache_mark_user().
 */
void
gsk_dns_rr_cache_unmark_user  (GskDnsRRCache           *rr_cache,
			       GskDnsResourceRecord    *record)
{
  RRList *rr_list = (RRList *) record;
  g_return_if_fail (rr_list->magic == RR_LIST_MAGIC);
  if (!rr_list->is_from_user)
    return;
  g_assert (rr_list->lock_count > 0);
  rr_list->is_from_user = 0;
  gsk_dns_rr_cache_unlock (rr_cache, record);
}

/**
 * gsk_dns_rr_cache_get_addr:
 * @rr_cache: a resource-record cache.
 * @host: a name to obtain an IP address for.
 * @address: pointer to where the returned address should be put.
 *
 * Look up the IP address of a name.
 * This will follow CNAME records.
 *
 * BUGS: 
 * - OBSOLETE BUG: This will allow a loop of CNAMEs to cause a hang!
 *   (i think i fixed this bug...)
 *
 * returns: whether the cache has sufficient information to resolve the name.
 */
gboolean
gsk_dns_rr_cache_get_addr   (GskDnsRRCache           *rr_cache,
			     const char              *host,
			     GskSocketAddressIpv4   **address)
{
  GskDnsResourceRecord *record;
  GskSocketAddress *addr;
  record = gsk_dns_rr_cache_lookup_one (rr_cache,
					host,
					GSK_DNS_RR_HOST_ADDRESS,
					GSK_DNS_CLASS_INTERNET,
					GSK_DNS_RR_CACHE_LOOKUP_DEREF_CNAMES);
  if (record == NULL)
    return FALSE;

  addr = gsk_socket_address_ipv4_new (record->rdata.a.ip_address, GSK_DNS_PORT);
  *address = GSK_SOCKET_ADDRESS_IPV4 (addr);
  return TRUE;
}


/**
 * gsk_dns_rr_cache_get_ns_addr:
 * @rr_cache: a resource-record cache.
 * @host: the name to find a nameserver for.
 * @ns_name_out: a string, maintained by the RRCache, for the nameserver record,
 * if there is any present.
 * The string should not be freed by the caller.
 * @address_out: pointer to where the returned address should be put.
 * The address should be g_object_unref'd by the caller.
 *
 * Look up an address for a nameserver.
 *
 * returns: whether an address could be found.
 */
/* TODO: could probably be made faster */
gboolean
gsk_dns_rr_cache_get_ns_addr(GskDnsRRCache           *rr_cache,
			     const char              *host,
			     const char             **ns_name_out,
			     GskSocketAddressIpv4   **address_out)
{
  GSList *record_list;
  GSList *at;
  GHashTable *circ_ref_guard = NULL;

  LOWER_CASE_COPY_ON_STACK (host, host);

retry:
  record_list = gsk_dns_rr_cache_lookup_list (rr_cache,
					      host,
					      GSK_DNS_RR_NAME_SERVER,
					      GSK_DNS_CLASS_INTERNET);
  if (record_list == NULL)
    {
      GskDnsResourceRecord *record;
      record = gsk_dns_rr_cache_lookup_one (rr_cache,
					    host,
					    GSK_DNS_RR_CANONICAL_NAME,
					    GSK_DNS_CLASS_INTERNET,
                                            0);
      if (record != NULL)
	{
          if (circ_ref_guard == NULL)
            circ_ref_guard = g_hash_table_new (g_str_hash, g_str_equal);
          g_hash_table_insert (circ_ref_guard,
                               (gpointer) host,
                               (gpointer) host);
	  host = record->rdata.domain_name;
          if (g_hash_table_lookup (circ_ref_guard, host) == NULL)
            goto retry;

          /* found circular reference... fallthrough */
	}
      if (circ_ref_guard)
        g_hash_table_destroy (circ_ref_guard);
      return FALSE;
    }
  if (circ_ref_guard)
    g_hash_table_destroy (circ_ref_guard);

  for (at = record_list; at != NULL; at = at->next)
    {
      GskDnsResourceRecord *record = at->data;
      if (ns_name_out != NULL)
	*ns_name_out = record->owner;

      /* CAREFUL: record->owner will be deleted (maybe)
	 if anything is added to the cache */
      if (gsk_dns_rr_cache_get_addr (rr_cache, record->rdata.domain_name, address_out))
	{
	  g_slist_free (record_list);
	  return TRUE;
	}
    }
  g_slist_free (record_list);
  return FALSE;
}

/**
 * gsk_dns_rr_cache_add_negative:
 * @rr_cache: a resource-record cache.
 * @owner: the name which the negative response belongs to.
 * @query_type: type of negative response.
 * @query_class: address namespace of negative response.
 * @expire_time: when this negative record should be deleted.
 * @is_authoritative: whether this negative record came from an authority.
 *
 * Add an entry into the cache indicating
 * that there is no entry of the given type.
 *
 * The DNS specification refers to this as 'negative caching'.
 */
void
gsk_dns_rr_cache_add_negative(GskDnsRRCache           *rr_cache,
			      const char              *owner,
			      GskDnsResourceRecordType query_type,
			      GskDnsResourceClass      query_class,
			      gulong                   expire_time,
			      gboolean                 is_authoritative)
{
  guint byte_size;
  RRList *owner_list;
  RRList *new_owner_list;
  RRList *at;
  char *lc_owner;

  LOWER_CASE_COPY_ON_STACK (lc_owner, owner);

  owner_list = g_hash_table_lookup (rr_cache->owner_to_rr_list, lc_owner);
  new_owner_list = owner_list;

  /* Scan for records that conflict with a negative record. */
  /* If there are any, figure out if they are more authoritative than us.
     If so, do nothing (leave the old record and scrap this one).
     If not, replace the old record with the negative one in-place. */
  for (at = owner_list; at != NULL; at = at->owner_next)
    {
      gboolean conflict = FALSE;
      g_assert (at->magic == RR_LIST_MAGIC);
      if (at->is_negative)
	{
	  if (at->rr.type == query_type)
	    {
	      /* extend record lifetime, if applicable */
	      if (expire_time > at->expire_time)
                {
                  gboolean expirable = (at->lock_count == 0
                                     && !at->is_deprecated);
                  if (expirable)
                    {
                      g_assert (g_tree_lookup (rr_cache->rr_list_by_expire_time, at) != NULL);
                      g_tree_remove (rr_cache->rr_list_by_expire_time, at);
                    }
                  at->expire_time = expire_time;
                  if (expirable)
                    g_tree_insert (rr_cache->rr_list_by_expire_time, at, at);
                }
	      /* TODO: touch entry in LRU list */
	      return;
	    }
	}
      else if (at->rr.type == query_type)
	{
	  conflict = TRUE;
	}
      else if (at->rr.type == GSK_DNS_RR_CANONICAL_NAME)
	{
	  /* a CNAME record redirects us; that's another conflict */
	  conflict = TRUE;
	}

      if (conflict)
	{
	  /* The records conflict.  Update. */
	  if (!is_authoritative || at->is_authoritative)
	    {
	      /* If the records are of the same authority, tie goes to positive record. */
	      /* XXX: we are not as authoritative as the record we conflict with.
		 Trigger an error someday?  */
	      return;
	    }

	  /* update 'at' to be negative */
	  at->is_negative = 1;
	  at->expire_time = expire_time;
	  at->is_authoritative = is_authoritative ? 1 : 0;
	  /* TODO: touch entry in LRU list */

	  return;
	}
    }

  /* Allocate an RRList. */
  byte_size = compute_byte_size_for_negative_record (owner);
  ensure_space (rr_cache, 1, byte_size);
  at = g_malloc (byte_size);
  flatten_negative_rr (at, owner, query_type, query_class, is_authoritative, expire_time);
  at->is_from_user = 0;

  /* add it to owner_list */
  if (owner_list == NULL)
    {
      g_hash_table_insert (rr_cache->owner_to_rr_list,
			   g_strdup (lc_owner),
			   at);
      at->owner_prev = at->owner_next = NULL;
    }
  else
    {
      /* insert this element as the second in the list,
       * to avoid disturbing the hashtable.
       */
      at->owner_prev = owner_list;
      at->owner_next = owner_list->owner_next;
      if (at->owner_next != NULL)
	at->owner_next->owner_prev = at;
      owner_list->owner_next = at;
    }

  /* add it to expire tree */
  g_tree_insert (rr_cache->rr_list_by_expire_time, at, at);

  /* add it to lru list */
  prepend_to_lru_list (rr_cache, at);

  ASSERT_INVARIANTS (rr_cache);
  //return &at->rr;
}

/**
 * gsk_dns_rr_cache_is_negative:
 * @rr_cache: a resource-record cache.
 * @owner: name to lookup.
 * @query_type: type of query to answer.
 * @query_class: class of query to answer.
 *
 * Look to see if we have an explicit negative
 * entry for this resource-record.
 *
 * Note that if @query_type or @query_class
 * is WILDCARD then there must be a negative wildcard
 * resource-record in the cache.
 *
 * A negative WILDCARD resource-record in the cache will
 * satisfy any type or class.
 *
 * returns: TRUE if an explicit negative entry exists in the cache.
 *   FALSE means either a positive entry or no entry exists in the cache.
 */
gboolean
gsk_dns_rr_cache_is_negative (GskDnsRRCache           *rr_cache,
			      const char              *owner,
			      GskDnsResourceRecordType query_type,
			      GskDnsResourceClass      query_class)
{
  RRList *owner_list;
  RRList *at;
  char *lc_owner;

  LOWER_CASE_COPY_ON_STACK (lc_owner, owner);
  owner_list = g_hash_table_lookup (rr_cache->owner_to_rr_list, lc_owner);
  for (at = owner_list; at != NULL; at = at->owner_next)
    {
      if ( (at->rr.type == GSK_DNS_RR_WILDCARD
         || at->rr.type == query_type)
       &&  (at->rr.record_class == GSK_DNS_CLASS_WILDCARD
         || at->rr.record_class == query_class)
       &&  at->is_negative)
	return TRUE;
    }
  return FALSE;
}

/**
 * gsk_dns_rr_cache_ref:
 * @rr_cache: a resource-record cache.
 *
 * Increase the reference count on the resource record cache.
 * The cache will not be destroyed until the reference count gets to 0.
 *
 * returns: @rr_cache, for convenience.
 */ 
GskDnsRRCache *
gsk_dns_rr_cache_ref        (GskDnsRRCache           *rr_cache)
{
  g_return_val_if_fail (rr_cache->ref_count > 0, rr_cache);
  ++(rr_cache->ref_count);
  return rr_cache;
}

static void
free_name_and_rr_list (char *name, RRList *owner_list)
{
  g_free (name);
  while (owner_list != NULL)
    {
      RRList *next = owner_list->owner_next;
      g_free (owner_list);
      owner_list = next;
    }
}

/**
 * gsk_dns_rr_cache_unref:
 * @rr_cache: a resource-record cache.
 *
 * Decrease the reference count on the resource record cache.
 * The cache will be destroyed when the reference count gets to 0.
 */
void
gsk_dns_rr_cache_unref      (GskDnsRRCache           *rr_cache)
{
  g_return_if_fail (rr_cache->ref_count > 0);
  --(rr_cache->ref_count);
  if (rr_cache->ref_count == 0)
    {
      g_hash_table_foreach (rr_cache->owner_to_rr_list,
			    (GHFunc) free_name_and_rr_list,
			    NULL);
      g_hash_table_destroy (rr_cache->owner_to_rr_list);
      g_tree_destroy (rr_cache->rr_list_by_expire_time);
      g_free (rr_cache);
    }
}

/**
 * gsk_dns_rr_cache_flush:
 * @rr_cache: a resource-record cache.
 * @cur_time: the current unix time.
 *
 * Flush out the expired records from the cache, and
 * try to ensure that we are using an acceptable amount of memory.
 */
void
gsk_dns_rr_cache_flush      (GskDnsRRCache           *rr_cache,
			     gulong                   cur_time)
{
  for (;;)
    {
      RRList *next_to_expire;
      next_to_expire = gsk_g_tree_min (rr_cache->rr_list_by_expire_time);
      if (next_to_expire == NULL)
        break;
      if (next_to_expire->expire_time > cur_time)
        break;

      if (next_to_expire->owner_next != NULL)
	next_to_expire->owner_next->owner_prev = next_to_expire->owner_prev;
      if (next_to_expire->owner_prev != NULL)
	next_to_expire->owner_prev->owner_next = next_to_expire->owner_next;
      else
	{
          char *lc_owner;
          LOWER_CASE_COPY_ON_STACK (lc_owner, next_to_expire->rr.owner);
	  if (next_to_expire->owner_next != NULL)
            {
	      g_hash_table_insert (rr_cache->owner_to_rr_list,
				   lc_owner,
				   next_to_expire->owner_next);
              g_assert (next_to_expire->owner_next->magic == RR_LIST_MAGIC);
            }
	  else
	    {
	      gpointer key, value;
	      if (!g_hash_table_lookup_extended (rr_cache->owner_to_rr_list,
						 lc_owner,
						 &key, &value))
		g_assert_not_reached ();
	      g_hash_table_remove (rr_cache->owner_to_rr_list, lc_owner);
	      g_free (key);
	    }
	}
      g_tree_remove (rr_cache->rr_list_by_expire_time, next_to_expire);

      /* Remove from LRU list */
      if (next_to_expire->lru_prev == NULL)
        {
          g_assert (rr_cache->lru_first == next_to_expire);
          rr_cache->lru_first = next_to_expire->lru_next;
        }
      else
        next_to_expire->lru_prev->lru_next = next_to_expire->lru_next;
      if (next_to_expire->lru_next == NULL)
        {
          g_assert (rr_cache->lru_last == next_to_expire);
          rr_cache->lru_last = next_to_expire->lru_prev;
        }
      else
        next_to_expire->lru_next->lru_prev = next_to_expire->lru_prev;

      rr_cache->num_records--;
      rr_cache->num_bytes_used -= next_to_expire->byte_size;
      g_free (next_to_expire);
    }
  ensure_space (rr_cache, 0, 0);
  ASSERT_INVARIANTS (rr_cache);
}

/* --- parsing a Master File --- */
/* See RFC 1035, Section 5. */

static gboolean
process_zone_file_command (GskDnsRRCache   *rr_cache,
			   const char      *command,
			   const char      *default_origin,
			   char           **cur_alt_origin_in_out,
			   const char     **last_owner_in_out,
			   gulong           cur_time,
			   char           **include_fname_out,
			   const char      *filename,
			   int              lineno)
{
  GskDnsResourceRecord *rr;
  char *err_message = NULL;
  const char *origin;
  if (*cur_alt_origin_in_out == NULL)
    origin = default_origin;
  else
    origin = *cur_alt_origin_in_out;

  if (command[0] == '$')
    {
      /* ``control entries'', the two allowed types are:
       *     $ORIGIN           Specify a relative domain for this section.
       *     $INCLUDE fname    Pull the contents of `fname' into this file.
       */
      if (g_strncasecmp (command, "$origin", 7) == 0)
	{
	  char *name;
	  command += 7;
	  GSK_SKIP_WHITESPACE (command);
	  name = g_strdup (command);
	  g_strchomp (name);
	  if (name[0] == 0)
	    {
	      g_warning ("error parsing $ORIGIN command, %s:%d",
			 filename, lineno);
	      return FALSE;
	    }
	  g_free (*cur_alt_origin_in_out);
	  *cur_alt_origin_in_out = name;
	}
      else if (g_strncasecmp (command, "$include", 8) == 0)
	{
	  char *name;
	  command += 8;
	  GSK_SKIP_WHITESPACE (command);
	  name = g_strdup (command);
	  g_strchomp (name);
	  if (name[0] == 0)
	    {
	      g_warning ("error parsing $INCLUDE command, %s:%d",
			 filename, lineno);
	      return FALSE;
	    }
	  *include_fname_out = name;
	  return TRUE;
	}
      else
	{
	  g_warning ("unknown `.' command: %s:%d", filename, lineno);
	  return FALSE;
	}
    }

  /* XXX: we need some kind of funny allocator for these
          temporary cases! */
  rr = gsk_dns_rr_text_parse (command,
			      *last_owner_in_out,
			      origin,
			      &err_message,
			      NULL);
  if (rr == NULL)
    {
      if (err_message != NULL)
	{
	  g_warning ("Error parsing zone file: file %s, line %d: %s",
		     filename, lineno, err_message);
	  g_free (err_message);
	  return FALSE;
	}
      return TRUE;
    }
  {
    GskDnsResourceRecord *cache_rr;
    /* XXX: is setting is_authoritative really correct here??? */
    cache_rr = gsk_dns_rr_cache_insert (rr_cache, rr, TRUE, cur_time);
    gsk_dns_rr_cache_mark_user (rr_cache, cache_rr);
  }
  gsk_dns_rr_free (rr);
  return TRUE;
}

typedef struct _IncludeStack IncludeStack;
struct _IncludeStack
{
  char *filename;
  int lineno;
  FILE *fp;
  IncludeStack *next;
};

static IncludeStack *pop_include_stack (IncludeStack *stack)
{
  IncludeStack *next_top = stack->next;
  g_free (stack->filename);
  fclose (stack->fp);
  g_free (stack);
  return next_top;
}

/* ???: it'd be nice to make public functions for this, probably:
      gsk_path_get_file_relative
      gsk_path_get_dir_relative

   (this is gsk_path_get_file_relative) */
static char *
make_relative_path (const char *base_file,
		    const char *rel_file)
{
  const char *last_slash;
  char *rv;
  if (*rel_file == '/')
    return g_strdup (rel_file);
  last_slash = strrchr (base_file, '/');
  if (last_slash == NULL)
    return g_strdup (rel_file);
  rv = g_new (char, strlen (rel_file) + last_slash + 1 - base_file + 1);
  memcpy (rv, base_file, last_slash - base_file + 1);
  strcpy (rv + (last_slash - base_file) + 1, rel_file);
  return rv;
}

/**
 * gsk_dns_rr_cache_load_zone:
 * @rr_cache: a resource-record cache.
 * @filename: a zone file to parse.
 * @default_origin: the initial origin.
 *
 * Parse a zone file.
 * This file format is defined in RFC 1035, Section 5.
 *
 * returns: whether the zone file was parsed successfully.
 */
gboolean
gsk_dns_rr_cache_load_zone(GskDnsRRCache           *rr_cache,
			   const char              *filename,
			   const char              *default_origin,
			   GError                 **error)
{
  char buf[4096];
  char *buf_at = buf;
  int paren_count = 0;
  char *origin_record = NULL;
  int line_no = 0;
  const char *last_owner = NULL;
  GTimeVal tv;
  IncludeStack *stack;
  char *err_msg = NULL;
  stack = g_new (IncludeStack, 1);
  stack->fp = fopen (filename, "r");
  if (stack->fp == NULL)
    {
      g_warning ("Master zone file `%s' not found: %s", filename,
		 strerror (errno));
      g_free (stack);
      return FALSE;
    }
  stack->next = NULL;
  stack->filename = g_strdup (filename);
  stack->lineno = 0;

  g_get_current_time (&tv);

  while (stack != NULL)
    {
      while (fgets (buf_at, buf + sizeof (buf) - buf_at, stack->fp) != NULL)
	{
	  stack->lineno++;
	  /* Remove any comment. */
	  {
	    char *semicolon = strchr (buf_at, ';');
	    if (semicolon != NULL)
	      *semicolon = 0;
	  }
	  g_strchomp (buf_at);

	  while (*buf_at != 0)
	    {
	      if (*buf_at == '(')
		paren_count++;
	      else if (*buf_at == ')')
		paren_count--;
	      buf_at++;
	    }
	  buf_at++;
	  if (paren_count < 0)
	    {
	      g_warning ("Zone file contained mismatched right-paren: %s: %d",
			 filename, line_no);
	      goto fail;
	    }
	  if (paren_count == 0)
	    {
	      char *buf_end = buf_at - 1;
	      char *include_fname;
	      for (buf_at = buf; buf_at < buf_end; buf_at++)
		if (*buf_at == 0)
		  *buf_at = ' ';
	      if (!process_zone_file_command (rr_cache, buf, 
					      default_origin,
					      &origin_record, &last_owner,
					      tv.tv_sec,
					      &include_fname,
					      filename,
					      line_no))
		{
                  err_msg = "error parsing zone file command";
		  goto fail;
		}
	      buf_at = buf;
	      if (include_fname != NULL)
		{
		  FILE *fp;
		  const char *last_filename = filename;
		  char *new_fname;
		  IncludeStack *new_top;
		  new_fname = make_relative_path (filename, include_fname);
		  if (new_fname == NULL)
		    {
		      err_msg = g_strdup_printf (
		                 "couldn't combine %s and %s into a path",
				 last_filename, include_fname);
		      goto fail;
		    }
		  fp = fopen (new_fname, "r");
		  if (fp == NULL)
		    {
		      err_msg = g_strdup_printf (
		                 "error opening included file %s", filename);
		      g_free (new_fname);
		      goto fail;
		    }
		  new_top = g_new (IncludeStack, 1);
		  new_top->next = stack;
		  new_top->filename = new_fname;
		  new_top->lineno = 1;
		  new_top->fp = fp;
		  stack = new_top;
		}
	    }

	  if (buf + sizeof (buf) - buf_at < MIN_LINE_LENGTH)
	    {
	      err_msg = g_strdup_printf (
			 "dns-master-parser: line too long at %s, line %d",
			 filename, line_no);
	      goto fail;
	    }
	}
      stack = pop_include_stack (stack);
    }

  g_free (origin_record);
  return TRUE;

fail:
  g_free (origin_record);
  while (stack != NULL)
    stack = pop_include_stack (stack);
  g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_RESOLVER_FORMAT,
	       "parsing zone file %s: %s", filename, err_msg);
  g_free (err_msg);
  return FALSE;
}

/* --- parsing an /etc/hosts file --- */
/**
 * gsk_dns_rr_cache_parse_etc_hosts_line:
 * @rr_cache: a resource-record cache.
 * @text: a line from an /etc/hosts file.
 *
 * Process a single line of text from an /etc/hosts file.
 *
 * This file format is defined in RFC 952.
 *
 * TODO: this does not handle IPv6 correctly!!!
 *
 * returns: whether the line was parsed successfully.
 */
gboolean
gsk_dns_rr_cache_parse_etc_hosts_line(GskDnsRRCache      *rr_cache,
				      const char         *text)
{
  guint8 addr[4];
  const char *end;
  GskDnsResourceRecord *rr;
  GskDnsResourceRecord *in_cache;
  char *canon_name;
  GTimeVal tv;
  g_get_current_time (&tv);

  GSK_SKIP_WHITESPACE (text);

  if (*text == '#' || *text == '\0')
    return TRUE;

  /* TODO: ipv6 */
  if (strstr (text, "::") != NULL)
    return TRUE;

  if (!gsk_dns_parse_ip_address (&text, addr))
    return FALSE;
  GSK_SKIP_WHITESPACE (text);

  /* cut out the canonical name and make an A record. */
  end = text;
  GSK_SKIP_NONWHITESPACE (end);
  if (end == text)
    return FALSE;
  canon_name = g_new (char, end - text + 1);
  memcpy (canon_name, text, end - text);
  canon_name[end - text] = '\0';
  rr = gsk_dns_rr_new_a (canon_name, 1000, addr, NULL);
  in_cache = gsk_dns_rr_cache_insert (rr_cache, rr, FALSE, tv.tv_sec);
  gsk_dns_rr_cache_mark_user (rr_cache, in_cache);
  gsk_dns_rr_free (rr);

  /* for each of the remaining names, make a CNAME record
     pointing from the alias the canon_name. */
  text = end;
  GSK_SKIP_WHITESPACE (text);
  while (*text != 0)
    {
      char *tmp;
      end = text;
      GSK_SKIP_NONWHITESPACE (end);
      tmp = g_new (char, end - text + 1);
      memcpy (tmp, text, end - text);
      tmp[end - text] = '\0';

      rr = gsk_dns_rr_new_cname (tmp, 1000, canon_name, NULL);
      in_cache = gsk_dns_rr_cache_insert (rr_cache, rr, FALSE, tv.tv_sec);
      gsk_dns_rr_cache_mark_user (rr_cache, in_cache);
      gsk_dns_rr_free (rr);
      g_free (tmp);

      text = end;
      GSK_SKIP_WHITESPACE (text);
    }

  g_free (canon_name);
  return TRUE;
}

/**
 * gsk_dns_rr_cache_parse_etc_hosts:
 * @rr_cache: a resource-record cache.
 * @filename: an /etc/hosts file.
 * @may_be_missing: whether to return an error if the file does not exist.
 *
 * Process an /etc/hosts file.
 *
 * This file format is defined in RFC 952.
 *
 * TODO: this does not handle IPv6 correctly!!!
 *
 * returns: whether the file was parsed successfully.
 */
/* Format of /etc/hosts:
 *
 * IP-ADDRESS CANONICAL-NAME ALIAS-1 ....
 *
 * We could do a lot more (comply with RFC 952),
 * but all that other stuff is obsolete anyway ;)
 */
gboolean
gsk_dns_rr_cache_parse_etc_hosts   (GskDnsRRCache     *rr_cache,
				    const char        *filename,
				    gboolean           may_be_missing)
{
  FILE *fp;
  char buf[8192];
  int line = 1;
  fp = fopen (filename, "r");
  if (fp == NULL)
    return may_be_missing;
  while (fgets (buf, sizeof (buf), fp) != NULL)
    {
      char *nl = strchr (buf, '\n');
      if (nl == NULL)
	{
	  g_warning ("%s: line too long or truncated file?", filename);
	  fclose (fp);
	  return FALSE;
	}
      *nl = '\0';
      if (!gsk_dns_rr_cache_parse_etc_hosts_line (rr_cache, buf))
	{
	  g_warning ("hosts-file-parser: %s: error parsing line %d",
		     filename, line);
	  fclose (fp);
	  return FALSE;
	}
      line++;
    }
  fclose (fp);
  return TRUE;
}


GType
gsk_dns_rr_cache_get_type ()
{
  static GType type = 0;
  if (type == 0)
    type = g_boxed_type_register_static ("GskDnsRRCache",
					 (GBoxedCopyFunc) gsk_dns_rr_cache_ref,
					 (GBoxedFreeFunc) gsk_dns_rr_cache_unref);
  return type;
}
