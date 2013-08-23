#include "gskdnsresolver.h"
#include <string.h>
#include "../gsknameresolver.h"
#include "../gskghelpers.h"
#include "../gskerror.h"
#include "../gskmacros.h"

/**
 * gsk_dns_resolver_resolve:
 * @resolver: the DNS resolver which should begin processing the request.
 * @recursive: whether to use recursive name resolution on the server.
 * @dns_questions: list of GskDnsQuestion's to resolve.
 * @func: function which will be called with answers
 * to the given questions.
 * @on_fail: function to call if the name cannot be resolved.
 * @func_data: data to pass to @func and @on_fail.
 * @destroy: function to call with the task is over.
 * @hints: flags to pass to the name resolver.
 *
 * Begin a DNS lookup task.
 *
 * returns: a running DNS lookup task.
 */
GskDnsResolverTask *
gsk_dns_resolver_resolve (GskDnsResolver        *resolver,
			  gboolean               recursive,
			  GSList                *dns_questions,
			  GskDnsResolverResponseFunc func,
			  GskDnsResolverFailFunc on_fail,
			  gpointer               func_data,
			  GDestroyNotify         destroy,
			  GskDnsResolverHints   *hints)
{
  GskDnsResolverIface *iface = GSK_DNS_RESOLVER_GET_IFACE (resolver);
  g_return_val_if_fail (iface != NULL, NULL);
  return (*iface->resolve)(resolver, recursive, dns_questions,
			   func, on_fail,
			   func_data, destroy, hints);
}

/**
 * gsk_dns_resolver_cancel:
 * @resolver: a resolver which is running a DNS lookup task.
 * @task: a DNS lookup task to cancel.
 *
 * Cancel a running DNS lookup task.
 */
void
gsk_dns_resolver_cancel (GskDnsResolver       *resolver,
			 GskDnsResolverTask   *task)
{
  GskDnsResolverIface *iface = GSK_DNS_RESOLVER_GET_IFACE (resolver);
  (*iface->cancel) (resolver, task);
}

/* --- forward lookup --- */
typedef struct _LookupData LookupData;
struct _LookupData
{
  char                    *host_name;
  gboolean                 is_ipv6;
  GskDnsResolverLookupFunc func;
  GskDnsResolverFailFunc   on_fail;
  gpointer                 func_data;
  GDestroyNotify           destroy;
};
#define LOOKUP_DATA_GET_RRTYPE(lookup_data)                             \
      ((lookup_data)->is_ipv6 ? GSK_DNS_RR_HOST_ADDRESS_IPV6            \
                              : GSK_DNS_RR_HOST_ADDRESS)

static GskDnsResourceRecord *
list_search (GSList *rr_list,
	     const char *owner,
	     GskDnsResourceRecordType type)
{
  while (rr_list != NULL)
    {
      GskDnsResourceRecord *record = rr_list->data;
      rr_list = rr_list->next;

      if (strcasecmp (record->owner, owner) == 0 && type == record->type)
	return record;
    }
  return NULL;
}

static gboolean
list_search_questions (GSList *questions, const char *host,
		       GskDnsResourceRecordType type)
{
  while (questions != NULL)
    {
      GskDnsQuestion *q = questions->data;
      questions = questions->next;
      if (strcmp (q->query_name, host) == 0
       && (q->query_type == GSK_DNS_RR_WILDCARD || q->query_type == type))
	return TRUE;
    }
  return FALSE;
}

static void
lookup_data_handle_result (GSList             *answers,
			   GSList             *authority,
			   GSList             *additional,
			   GSList             *negatives,
			   gpointer            handle_result_data)
{
  LookupData *data = handle_result_data;
  const char *host = data->host_name;
  GskDnsResourceRecordType rrtype = LOOKUP_DATA_GET_RRTYPE (data);

  for (;;)
    {
      GskDnsResourceRecord *answer;
      if (list_search_questions (negatives, host, rrtype)
       || list_search_questions (negatives, host, GSK_DNS_RR_CANONICAL_NAME))
	{
	  if (data->on_fail != NULL)
	    {
	      GError *error = g_error_new (GSK_G_ERROR_DOMAIN,
					   GSK_ERROR_RESOLVER_NOT_FOUND,
					   _("dns resolver: name not found: %s"), host);
	      (*data->on_fail) (error, data->func_data);
	      g_error_free (error);
	    }
	  return;
	}

      answer = list_search (answers, host, rrtype);
      if (answer == NULL)
	answer = list_search (authority, host, rrtype);
      if (answer == NULL)
	answer = list_search (additional, host, rrtype);
      if (answer != NULL)
	{
	  if (answer->type == GSK_DNS_RR_HOST_ADDRESS)
	    {
	      /* XXX: more error checking is really required!!! */
	      /* (basically there should be a trail of CNAME's) */
	      GskSocketAddress *addr;
	      addr = gsk_socket_address_ipv4_new (answer->rdata.a.ip_address, 0);
	      (*data->func) (addr, data->func_data);
	      g_object_unref (addr);
	      return;
	    }
	}
      answer = list_search (answers, host, GSK_DNS_RR_CANONICAL_NAME);
      if (answer == NULL)
	answer = list_search (authority, host, GSK_DNS_RR_CANONICAL_NAME);
      if (answer == NULL)
	answer = list_search (additional, host, GSK_DNS_RR_CANONICAL_NAME);
      if (answer == NULL)
	break;

      /* ok, try again with the cname */
      host = answer->rdata.domain_name;
    }

  if (data->on_fail)
    {
      GError *error = g_error_new (GSK_G_ERROR_DOMAIN,
                                   GSK_ERROR_RESOLVER_NO_DATA,
				   _("dns resolver: got answers, but nothing good"));
      (*data->on_fail) (error, data->func_data);
      g_error_free (error);
    }
}

static void
lookup_data_fail          (GError             *error,
			   gpointer            handle_result_data)
{
  LookupData *data = handle_result_data;
  if (data->on_fail)
    (*data->on_fail) (error, data->func_data);
}

static void
lookup_data_destroy      (gpointer             handle_result_data)
{
  LookupData *data = handle_result_data;

  if (data->destroy != NULL)
    (*data->destroy) (data->func_data);
  g_free (data);
}

/**
 * gsk_dns_resolver_lookup:
 * @resolver: DNS client to ask questions.
 * @name: name of host to look up.
 * @func: function to call on successful name lookup.
 * @on_fail: function to call on name lookup failure.
 * @func_data: data to pass to @func and @on_fail.
 * @destroy: function to call when the task is destroyed.
 *
 * Begin a simple DNS lookup, using the underlying general resolver.
 *
 * TODO. IPv6 support.
 *
 * returns: a running DNS lookup task.
 */
GskDnsResolverTask *
gsk_dns_resolver_lookup  (GskDnsResolver        *resolver,
			  const char            *name,
			  GskDnsResolverLookupFunc func,
			  GskDnsResolverFailFunc on_fail,
			  gpointer               func_data,
			  GDestroyNotify         destroy)
{
  GskDnsQuestion question
    = {
	(char *) name,
	GSK_DNS_RR_HOST_ADDRESS,
	GSK_DNS_CLASS_INTERNET,
	NULL
      };
  GSList question_list
    = {
	&question,
	NULL
      };

  LookupData *lookup_data;

  if (strspn(name, "0123456789. ") == strlen(name))
    {
      /* this isn't a name, it's a number.
	 probably an IP number. */
      const char *tmp = name;
      guint8 ip_address[4];
      if (gsk_dns_parse_ip_address (&tmp, ip_address))
	{
	  GskSocketAddress *socket_address;
	  socket_address = gsk_socket_address_ipv4_new(ip_address, 0);
	  (*func)(socket_address, func_data);
	  if (destroy != NULL)
	    (*destroy)(func_data);
	  g_object_unref (socket_address);
	  return NULL;
	}

      /* let it go on the fail through the normal code-path */
    }

  lookup_data = g_malloc (sizeof (LookupData) + strlen (name) + 1);
  lookup_data->func = func;
  lookup_data->is_ipv6 = FALSE;
  lookup_data->on_fail = on_fail;
  lookup_data->func_data = func_data;
  lookup_data->destroy = destroy;
  lookup_data->host_name = strcpy ((char*) (lookup_data + 1), name);

  return gsk_dns_resolver_resolve (resolver,
				   TRUE,
				   &question_list,
				   lookup_data_handle_result,
				   lookup_data_fail,
				   lookup_data,
				   lookup_data_destroy,
				   NULL);
}

/* --- type implementation --- */
GType
gsk_dns_resolver_get_type (void)
{
  static GType type = 0;
  if (type == 0)
    {
      static const GTypeInfo resolver_info =
      {
	sizeof (GskDnsResolverIface),
	NULL,			/* base_init */
	NULL,			/* base_finalize */
	NULL,
	NULL,			/* class_finalize */
	NULL,			/* class_data */
	0,
	0,
	NULL,
	NULL
      };
      type = g_type_register_static (G_TYPE_INTERFACE,
				     "GskDnsResolver",
				     &resolver_info,
				     G_TYPE_FLAG_ABSTRACT);
      g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);
    }
  return type;
}


/* --- name-resolver interface --- */
static gpointer 
name_start_resolve   (GskNameResolver           *resolver,
		      GskNameResolverFamily      family,
		      const char                *name,
		      GskNameResolverSuccessFunc success,
		      GskNameResolverFailureFunc failure,
		      gpointer                   func_data,
		      GDestroyNotify             destroy)
{
  GskDnsResolver *dns_resolver = GSK_DNS_RESOLVER (resolver);
  g_return_val_if_fail (family == GSK_NAME_RESOLVER_FAMILY_IPV4, NULL);
  return gsk_dns_resolver_lookup (dns_resolver, name,
				  success, failure, func_data, destroy);
}

static gboolean 
name_cancel_resolve (GskNameResolver           *resolver,
		     gpointer                   handle)
{
  gsk_dns_resolver_cancel (GSK_DNS_RESOLVER (resolver),
			   (GskDnsResolverTask *) handle);
  return TRUE;
}

static void
init_name_resolver_iface (GskNameResolverIface *iface)
{
  iface->start_resolve = name_start_resolve;
  iface->cancel_resolve = name_cancel_resolve;
}

void
gsk_dns_resolver_add_name_resolver_iface (GType type)
{
  static const GInterfaceInfo name_resolver_info =
  {
    (GInterfaceInitFunc) init_name_resolver_iface,
    NULL,
    NULL
  };
  g_type_add_interface_static (type,
			       GSK_TYPE_NAME_RESOLVER,
			       &name_resolver_info);
}
