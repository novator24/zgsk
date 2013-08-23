#include "gskdnslocalresolver.h"
#include "../gskerror.h"
#include "../gskmacros.h"

enum
{
  PROP_0,
  PROP_RR_CACHE
};

/* DB: Mar 4, 2005.  I don't know why this has been
   commented out, and i don't think it's needed by
   anyone either.  Investigate, maybe. */
#define IMPLEMENT_DNS_RESOLVER_IFACE    0

/*
 * A GskDnsResolver which never defers to the network,
 * or blocks. 
 */
static GObjectClass *parent_class = NULL;

struct _GskDnsLocalResolverClass
{
  GObjectClass		object_class;
};

struct _GskDnsLocalResolver
{
  GObject		object;
  GskDnsRRCache        *rr_cache;
};

static void
gsk_dns_local_resolver_finalize (GObject *object)
{
  GskDnsLocalResolver *resolver = GSK_DNS_LOCAL_RESOLVER (object);
  if (resolver->rr_cache != NULL)
    gsk_dns_rr_cache_unref (resolver->rr_cache);
  (*parent_class->finalize) (object);
}

#if IMPLEMENT_DNS_RESOLVER_IFACE
/* Note: this function should never be called... */
static void
gsk_dns_local_resolver_resolve_cancel  (GskDnsResolver            *resolver,
			                gpointer                   task)
{
  g_return_if_fail (GSK_IS_DNS_LOCAL_RESOLVER (resolver));
  g_return_if_fail (task != NULL);

  /* however, gsk_dns_local_resolver_resolve always returns NULL. */
  g_return_if_fail (task == NULL);
}
#endif

/**
 * gsk_dns_local_resolver_answer:
 * @rr_cache: resource-record cache to use to derive the answer to the question.
 * @question: question to answer.
 * @results: message to store results in.
 *
 * Attempt to compute an answer to a DNS question,
 * using only information locally available in
 * the resource-record cache.
 *
 * returns: the result of the query.
 */
GskDnsLocalResult
gsk_dns_local_resolver_answer        (GskDnsRRCache      *rr_cache,
				      GskDnsQuestion     *question,
				      GskDnsMessage      *results)
{
  const char *name = question->query_name;
  GSList *list;
  GSList *at;
  const char *cname = NULL;
  gboolean got_something = FALSE;

  g_return_val_if_fail (results != NULL, GSK_DNS_LOCAL_NO_DATA);
  if (rr_cache == NULL)
    {
      g_warning ("gsk_dns_local_resolver_resolve called without a cache");
      return GSK_DNS_LOCAL_NO_DATA;
    }

retry_lookup_name:
  list = gsk_dns_rr_cache_lookup_list (rr_cache,
				       name,
				       GSK_DNS_RR_WILDCARD,
				       question->query_class);
  if (list == NULL)
    {
      if (gsk_dns_rr_cache_is_negative (rr_cache, name,
					question->query_type,
					question->query_class))
	return GSK_DNS_LOCAL_NEGATIVE;

      for (;;)
	{
	  /* Ok, still go on a little hunt for additional
	     NS records up the hierarchy from name;
	     shovel those into the answer section. */
	  while (*name != '.' && *name != '\0')
	    name++;
	  while (*name == '.')
	    name++;
	  list = gsk_dns_rr_cache_lookup_list (rr_cache,
					       name,
					       GSK_DNS_RR_NAME_SERVER,
					       question->query_class);
	  if (list != NULL)
	    {
	      for (at = list; at != NULL; at = at->next)
		gsk_dns_rr_cache_lock (rr_cache, at->data);
	      results->answers = g_slist_concat (results->answers, list);
	      return GSK_DNS_LOCAL_PARTIAL_DATA;
	    }
	}
      return got_something ? GSK_DNS_LOCAL_PARTIAL_DATA : GSK_DNS_LOCAL_NO_DATA;
    }

  for (at = list; at != NULL; at = at->next)
    {
      GskDnsResourceRecord *record = at->data;
      if (record->type == question->query_type
       || record->type == GSK_DNS_RR_CANONICAL_NAME
       || question->query_type == GSK_DNS_RR_WILDCARD)
	{
	  gsk_dns_rr_cache_lock (rr_cache, record);
	  results->answers = g_slist_prepend (results->answers, record);
	  got_something = TRUE;
	}
      if (record->type == GSK_DNS_RR_CANONICAL_NAME)
	cname = record->rdata.domain_name;
    }

  /* and if we got a cname, and we weren't explicitly
     searching for CNAMEs (which by convention indicates you
     are not interested in CNAME's resolution),
     retry the whole query with name set to CNAME. */
  if (cname != NULL)
    {
      name = cname;
      cname = NULL;
      goto retry_lookup_name;
    }
  return got_something ? GSK_DNS_LOCAL_PARTIAL_DATA : GSK_DNS_LOCAL_NO_DATA;
}

#if IMPLEMENT_DNS_RESOLVER_IFACE
static gpointer
gsk_dns_local_resolver_real_resolve (GskDnsResolver               *resolver,
				     gboolean                      recursive,
				     GSList                       *questions,
				     GskDnsResolverResponseFunc    func,
				     GskDnsResolverFailFunc        on_fail,
				     gpointer                      func_data,
				     GDestroyNotify                destroy,
				     GskDnsResolverHints          *hints)
{
  GskDnsLocalResolver *local_resolver = GSK_DNS_LOCAL_RESOLVER (resolver);
  GskDnsMessage *allocator = gsk_dns_message_new (0, FALSE);
  GSList *negatives = NULL;
  gboolean got_something = FALSE;

  /* We always operate non-recursively. */
  (void) recursive;

  /* we don't do any ip-based filtering */
  (void) hints;

  while (questions != NULL)
    {
      GskDnsQuestion *question = (GskDnsQuestion*)(questions->data);
      switch (gsk_dns_local_resolver_answer (local_resolver->rr_cache,
					     question, allocator))
	{
	case GSK_DNS_LOCAL_NO_DATA:
	  break;
	case GSK_DNS_LOCAL_PARTIAL_DATA:
	case GSK_DNS_LOCAL_SUCCESS:
	  got_something = TRUE;
	  break;
	case GSK_DNS_LOCAL_NEGATIVE:
	  negatives = g_slist_prepend (negatives, question);
	  got_something = TRUE;
	  break;
	}
      questions = questions->next;
    }
  if (got_something)
    {
      negatives = g_slist_reverse (negatives);
      (*func) (allocator->answers,
	       allocator->authority,
	       allocator->additional,
	       negatives,
	       func_data);
      g_slist_free (negatives);
    }
  else
    {
      if (on_fail != NULL)
	{
	  GError *error = g_error_new (GSK_G_ERROR_DOMAIN,
				       GSK_ERROR_RESOLVER_NO_DATA,
				       _("no valid resources were found"));
	  (*on_fail) (error, func_data);
	  g_error_free (error);
	}
    }
  if (destroy != NULL)
    (*destroy) (func_data);
  return NULL;
}
#endif

static void
gsk_dns_local_resolver_get_property (GObject        *object,
			             guint           property_id,
			             GValue         *value,
			             GParamSpec     *pspec)
{
  switch (property_id)
    {
    case PROP_RR_CACHE:
      g_value_set_boxed (value, GSK_DNS_LOCAL_RESOLVER (object)->rr_cache);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gsk_dns_local_resolver_set_property (GObject        *object,
			             guint           property_id,
			             const GValue   *value,
			             GParamSpec     *pspec)
{
  GskDnsLocalResolver *local_resolver = GSK_DNS_LOCAL_RESOLVER (object);
  switch (property_id)
    {
    case PROP_RR_CACHE:
      {
	GskDnsRRCache *rr_cache = g_value_get_boxed (value);
	if (rr_cache)
	  gsk_dns_rr_cache_ref (rr_cache);
	if (local_resolver->rr_cache)
	  gsk_dns_rr_cache_unref (local_resolver->rr_cache);
	local_resolver->rr_cache = rr_cache;
	break;
      }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gsk_dns_local_resolver_init (GskDnsLocalResolver *resolver)
{
  resolver->rr_cache = NULL;
}

#if IMPLEMENT_DNS_RESOLVER_IFACE
static void
gsk_dns_local_resolver_resolver_init (GskDnsResolverIface *iface)
{
  iface->resolve = gsk_dns_local_resolver_real_resolve;
  iface->cancel = gsk_dns_local_resolver_resolve_cancel;
}
#endif

static void
gsk_dns_local_resolver_class_init (GObjectClass *object_class)
{
  GParamSpec *pspec;
#if 0
  static GInterfaceInfo client_resolver_info =
  {
    (GInterfaceInitFunc) gsk_dns_local_resolver_resolver_init,
    NULL,			/* interface_finalize */
    NULL			/* interface_data */
  };
#endif
  parent_class = g_type_class_peek_parent (object_class);
  object_class->finalize = gsk_dns_local_resolver_finalize;
  object_class->get_property = gsk_dns_local_resolver_get_property;
  object_class->set_property = gsk_dns_local_resolver_set_property;

#if IMPLEMENT_DNS_RESOLVER_IFACE
  g_type_add_interface_static (G_OBJECT_CLASS_TYPE (object_class),
			       GSK_TYPE_DNS_RESOLVER,
			       &client_resolver_info);
#endif

  pspec = g_param_spec_boxed ("resource-cache",
			      _("Resource Record Cache"),
			      _("cache of resource data used to answer queries"),
			      GSK_TYPE_DNS_RR_CACHE,
			      G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_RR_CACHE, pspec);
}

GType
gsk_dns_local_resolver_get_type()
{
  static GType dns_local_resolver_type = 0;
  if (!dns_local_resolver_type)
    {
      static const GTypeInfo dns_local_resolver_info =
      {
	sizeof(GskDnsLocalResolverClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_dns_local_resolver_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskDnsLocalResolver),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_dns_local_resolver_init,
	NULL		/* value_table */
      };
      dns_local_resolver_type = g_type_register_static (G_TYPE_OBJECT,
                                                  "GskDnsLocalResolver",
						  &dns_local_resolver_info, 0);
    }
  return dns_local_resolver_type;
}

/**
 * gsk_dns_local_resolver_new:
 * @rr_cache: resource-record cache to use.
 *
 * Create a new local resolver based on an optional resource-record cache.
 *
 * returns: the newly allocated resolver.
 */
GskDnsResolver *
gsk_dns_local_resolver_new   (GskDnsRRCache *rr_cache)
{
  GskDnsLocalResolver *resolver;
  if (rr_cache)
    resolver = g_object_new (GSK_TYPE_DNS_LOCAL_RESOLVER,
			     "resource-cache", rr_cache,
			     NULL);
  else
    resolver = g_object_new (GSK_TYPE_DNS_LOCAL_RESOLVER, NULL);
  return GSK_DNS_RESOLVER (resolver);
}

