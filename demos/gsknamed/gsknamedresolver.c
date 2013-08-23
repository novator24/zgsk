#include "gsknamedresolver.h"
#include <gsk/protocols/gskdnsimplementations.h>
#include <gsk/gskmain.h>
#include "gsksimpleacl.h"
#include "gsksyslog.h"
#include "textnode.h"

/* --- implement the NamedResolver, which uses a NamedConfig
       and a bunch of other resolvers to service requests
       in various ways (the ways indicated by GskNamedZoneType) --- */
static GtkObjectClass *named_resolver_parent_class = NULL;

/* --- GskDnsResolver functions --- */
typedef struct _ResponseMergeInfo ResponseMergeInfo;
struct _ResponseMergeInfo
{
  GskDnsMessage *results;
  gpointer func_data;
  GskDnsResolverResponseFunc func;
  GskDnsResolverFailFunc on_fail;
  GDestroyNotify destroy;
  GskNamedConfig *config;
};

static void
named_resolve_response_func  (GSList       *answers,
			      GSList       *authority,
			      GSList       *additional,
			      gpointer      data)
{
  ResponseMergeInfo *merge_info = data;
  GskDnsMessage *message = merge_info->results;

  /* pointers to the last-link of the lists in the response,
     which is the head of the list if empty; used to quickly
     graft/ungraft `answers', `authority' and `additional' */
#define GET_LAST_LINK(list) ((list) ? &(g_slist_last (list)->next) : &(list))
  GSList **pold_answer = GET_LAST_LINK (message->answers);
  GSList **pold_auth   = GET_LAST_LINK (message->authority);
  GSList **pold_addl   = GET_LAST_LINK (message->additional);
  g_assert (*pold_answer == NULL && *pold_auth == NULL && *pold_addl == NULL);

  /* splice the two lists together */
  *pold_answer = answers;
  *pold_auth = authority;
  *pold_addl = additional;

  (*merge_info->func) (message->answers,
		       message->authority,
		       message->additional,
		       merge_info->func_data);
  *pold_answer = NULL;
  *pold_auth = NULL;
  *pold_addl = NULL;
}

static void
named_on_fail_func           (GskDnsResolverError err_code,
			      gpointer            data)
{
  ResponseMergeInfo *merge_info = data;
  /* XXX: it'd be nice to be able to include error codes and responses
          simultaneously */
  (*merge_info->on_fail) (err_code, data);
}

static void
response_merge_info_destroy (gpointer             data)
{
  ResponseMergeInfo *merge_info = data;
  if (merge_info->destroy != NULL)
    (*merge_info->destroy) (merge_info->func_data);
  gsk_dns_message_unref (merge_info->results);
  g_free (merge_info);
}

static GskDnsResolverTask *
gsk_named_resolver_resolve (GskDnsResolver               *resolver,
			    gboolean                      recursive,
			    GSList                       *dns_questions,
			    GskDnsResolverResponseFunc    func,
			    GskDnsResolverFailFunc        on_fail,
			    gpointer                      func_data,
			    GDestroyNotify                destroy,
			    GskDnsResolverHints          *hints)
{
  GskNamedResolver *named = GSK_NAMED_RESOLVER (resolver);

  /* list of questions we cannot answer */
  GSList *unanswered = NULL;

  /* For storing partial answers. */
  GskDnsMessage *allocator = gsk_dns_message_new (0, FALSE);

  /* Whether we got any results. */
  gboolean got_something = FALSE;

  /* Find out if we have an IP address. */
  gboolean has_ip_address = hints ? (hints->address != NULL) : FALSE;
  guint8 ip_address[4];

  GSList *cur_question;

  /* Find out if the request has a client-ip associated with it. */
  if (hints != NULL
   && hints->address != NULL
   && hints->address->address_family == GSK_SOCKET_ADDRESS_IPv4)
    {
      has_ip_address = TRUE;
      memcpy (ip_address, hints->address->ipv4.ip_address, 4);
    }
  else
    {
      has_ip_address = TRUE;
    }

  /* check the ip address to see if it is allowed to make a query at all */
  if (has_ip_address && named->config->may_query != NULL
   && !gsk_simple_acl_check (named->config->may_query, ip_address))
    {
      if (on_fail != NULL)
	(*on_fail) (GSK_DNS_RESOLVER_ERR_ACCESS, func_data);
      if (destroy != NULL)
	(*destroy) (func_data);
      return NULL;
    }

  for (cur_question = dns_questions;
       cur_question != NULL;
       cur_question = cur_question->next)
    {
      GskDnsQuestion *question = cur_question->data;

      /* First, find the pertinent zone. */
      GskNamedZone *zone;
      zone = gsk_named_config_find_zone (named->config, question->query_name);

      if (has_ip_address && zone->may_query != NULL
       && !gsk_simple_acl_check (zone->may_query, named->tmp_ip_info))
	{
	  gsk_named_debug_log (named->config, 5,
	     "ACL check for query on %s (zone %s) failed for %d.%d.%d.%d",
			   question->query_name, zone->zone_name,
			   ip_address[0],
			   ip_address[1],
			   ip_address[2],
			   ip_address[3]);
	  continue;
	}

      /* Try to resolve the request out of local data. */
      if (named->rr_cache != NULL)
	{
	  GskDnsLocalResult result;
	  result = gsk_dns_local_resolver_answer (named->rr_cache,
						  question,
						  allocator);
	  if (result == GSK_DNS_LOCAL_NO_DATA
	   ||  (result == GSK_DNS_LOCAL_PARTIAL_DATA && recursive))
	    unanswered = g_slist_prepend (unanswered, question);
	  if (result == GSK_DNS_LOCAL_PARTIAL_DATA
	   || result == GSK_DNS_LOCAL_SUCCESS)
	    got_something = TRUE;
	}
    }

  if (unanswered != NULL)
    {
      GskDnsResolverTask *rv;
      ResponseMergeInfo *merge_info;
      if (named->resolver == NULL)
	{
	  /* Cannot do recursive lookup. */

	  /* Do we have enough data to succeed anyway? */
	  if (allocator->answers != NULL
	   || allocator->authority  != NULL
	   || allocator->additional != NULL)
	    {
	      (*func) (allocator->answers, allocator->authority,
		       allocator->additional, func_data);
	    }
	  else
	    {
	      if (on_fail != NULL)
		(*on_fail) (GSK_DNS_RESOLVER_ERR_NO_DATA, func_data);
	    }
	  gsk_dns_message_unref (allocator);
	  if (destroy != NULL)
	    (*destroy) (func_data);
	  return NULL;
	}

      /* Set up a callback to merge the current
       * answers with the answers obtained from
       * the resolver.
       */
      merge_info = g_new (ResponseMergeInfo, 1);
      merge_info->func_data = func_data;
      merge_info->func = func;
      merge_info->on_fail = on_fail;
      merge_info->destroy = destroy;
      merge_info->results = allocator;
      merge_info->config = named->config;
      rv = gsk_dns_resolver_resolve (named->resolver,
				     recursive,
				     unanswered,
				     named_resolve_response_func,
				     named_on_fail_func,
				     merge_info,
				     response_merge_info_destroy,
				     hints);
      g_slist_free (unanswered);
      return rv;
    }
  else
    {
      (*func) (allocator->answers, allocator->authority,
	       allocator->additional, func_data);
      if (on_fail != NULL)
	(*on_fail) (GSK_DNS_RESOLVER_ERR_NO_DATA, func_data);
      gsk_dns_message_unref (allocator);
      if (destroy != NULL)
	(*destroy) (func_data);
      return NULL;
    }
}

static void
gsk_named_resolver_cancel  (GskDnsResolver               *resolver,
			    GskDnsResolverTask           *task)
{
  GskNamedResolver *named = GSK_NAMED_RESOLVER (resolver);
  gsk_dns_resolver_cancel (named->resolver, task);
}

/* --- GtkObject functions --- */
static void
gsk_named_resolver_finalize (GtkObject *object)
{
  GskNamedResolver *named = GSK_NAMED_RESOLVER (object);
  if (named->config != NULL)
    gsk_named_config_destroy (named->config);
  (*named_resolver_parent_class->finalize) (object);
}

static void
gsk_named_resolver_init (GskNamedResolver *named_resolver)
{
  (void) named_resolver;
}

static void
gsk_named_resolver_class_init (GtkObjectClass* object_class)
{
  static GskDnsResolverIface resolver_iface =
    {
      gsk_named_resolver_resolve,
      gsk_named_resolver_cancel
    };
  object_class->finalize = gsk_named_resolver_finalize;
  gsk_interface_implement (GSK_TYPE_DNS_RESOLVER_IFACE,
			   object_class,
			   &resolver_iface);
}

GtkType gsk_named_resolver_get_type()
{
  static GtkType named_resolver_type = 0;
  if (!named_resolver_type) {
    static const GtkTypeInfo named_resolver_info =
    {
      "GskNamedResolver",
      sizeof(GskNamedResolver),
      sizeof(GskNamedResolverClass),
      (GtkClassInitFunc) gsk_named_resolver_class_init,
      (GtkObjectInitFunc) gsk_named_resolver_init,
      /* reserved_1 */ NULL,
      /* reserved_2 */ NULL,
      (GtkClassInitFunc) NULL
    };
    GtkType parent = GTK_TYPE_OBJECT;
    named_resolver_type = gtk_type_unique (parent, &named_resolver_info);
    named_resolver_parent_class = gtk_type_class (parent);
  }
  return named_resolver_type;
}

GskNamedResolver *
gsk_dns_named_resolver_new (GskNamedConfig       *named_config,
			    GskDnsRRCache        *rr_cache,
			    GskDnsResolver       *resolver)
{
  GskNamedResolver *named;
  named = GSK_NAMED_RESOLVER (gsk_gtk_object_new (GSK_TYPE_NAMED_RESOLVER));
  named->resolver = resolver;
  named->rr_cache = rr_cache;
  named->config = named_config;
  if (resolver != NULL)
    gtk_object_ref (GTK_OBJECT (resolver));
  if (rr_cache != NULL)
    gsk_dns_rr_cache_ref (rr_cache);
  return named;
}

