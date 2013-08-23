#include "gsknameresolver.h"
#include "gskerror.h"
#include "gskmacros.h"

/* for dns support */
#include "dns/gskdnsclient.h"
#include "gskpacketqueuefd.h"

GType gsk_name_resolver_get_type(void)
{
  static GType name_resolver_type = 0;
  if (!name_resolver_type)
    {
      static const GTypeInfo name_resolver_info =
      {
	sizeof (GskNameResolverIface),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) NULL,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	0,
	0,		/* n_preallocs */
	NULL,		/* instance_init */
	NULL		/* value_table */
      };
      name_resolver_type = g_type_register_static (G_TYPE_INTERFACE,
                                                   "GskNameResolver",
						   &name_resolver_info,
						   G_TYPE_FLAG_ABSTRACT);
    }
  return name_resolver_type;
}

/* --- resolver-tasks --- */
struct _GskNameResolverTask
{
  guint16                     ref_count;

  guint16		      is_running : 1;
  guint16                     was_cancelled : 1;
  guint16                     cancel_succeeded : 1;
  guint16                     task_succeeded : 1;

  gpointer                    task_data;

  GskNameResolver            *resolver;
  GskNameResolverIface       *iface;

  GskNameResolverSuccessFunc  success;
  GskNameResolverFailureFunc  failure;
  gpointer                    func_data;
  GDestroyNotify              destroy;
};

GSK_DECLARE_POOL_ALLOCATORS(GskNameResolverTask, gsk_name_resolver_task, 8)

typedef struct _Handler Handler;
struct _Handler
{
  GskNameResolverFamilyHandler handler;
  gpointer data;
  GDestroyNotify destroy;
  GskNameResolver *resolver;
};

static void
handler_destroy (gpointer data)
{
  Handler *handler = data;
  if (handler->destroy)
    handler->destroy (handler->data);
  if (handler->resolver)
    g_object_unref (handler->resolver);
  g_free (handler);
}

/* global tables */
static GHashTable *family_to_handler = NULL;
static GHashTable *family_to_name = NULL;
static GHashTable *name_to_family = NULL;
static guint       last_family = 0;

G_LOCK_DEFINE_STATIC (family_registry);
#define LOCK()		G_LOCK(family_registry)
#define UNLOCK()	G_UNLOCK(family_registry)


static void
handle_resolver_success (GskSocketAddress *address,
			 gpointer          task_ptr)
{
  GskNameResolverTask *task = task_ptr;
  if (task->success)
    (*task->success) (address, task->func_data);
  task->is_running = 0;
  task->task_succeeded = 1;
}

static void
handle_resolver_failure (GError           *error,
			 gpointer          task_ptr)
{
  GskNameResolverTask *task = task_ptr;
  if (task->failure)
    (*task->failure) (error, task->func_data);
  task->is_running = 0;
  task->task_succeeded = 0;
}

/**
 * gsk_name_resolver_task_new:
 * @family: name space to look the address up in.
 * @name: name within @family's namespace.
 * @success: function to be called with an appropriate #GskSocketAddress
 *    once the name is successfully resolved.
 * @failure: function to call if the name lookup failed.
 * @func_data: data to pass to @success or @failure.
 * @destroy: optionally called after @success or @failure, to deallocate
 * func_data usually.
 *
 * Begin a name lookup.  This may succeed before the function returns.
 * It you wish to cancel a name resolution task, call
 * gsk_name_resolver_task_cancel().  In any event,
 * you must gsk_name_resolver_task_unref() once you are done
 * with the handle.  (This will NOT cause a running task to be cancelled.)
 *
 * returns: a reference to a #GskNameResolverTask which can
 * be used to cancel or query the task.
 */
GskNameResolverTask *
gsk_name_resolver_task_new (GskNameResolverFamily       family,
		            const char                 *name,
		            GskNameResolverSuccessFunc  success,
		            GskNameResolverFailureFunc  failure,
		            gpointer                    func_data,
		            GDestroyNotify              destroy)
{
  GskNameResolverTask *task;
  Handler *handler;

  task = gsk_name_resolver_task_alloc ();
  task->ref_count = 2;
  task->success = success;
  task->failure = failure;
  task->func_data = func_data;
  task->destroy = destroy;
  task->task_data = NULL;
  task->is_running = 1;
  task->was_cancelled = 0;
  task->cancel_succeeded = 0;
  task->task_succeeded = 0;

  LOCK ();
  handler = g_hash_table_lookup (family_to_handler, GUINT_TO_POINTER (family));
  UNLOCK ();
  if (handler != NULL && handler->resolver == NULL)
    handler->resolver = (*handler->handler) (handler->data);
  if (handler)
    task->resolver = handler->resolver;
  else
    task->resolver = NULL;

  if (task->resolver == NULL)
    {
      const char *name = gsk_name_resolver_family_get_name (family);
      GError *error = g_error_new (GSK_G_ERROR_DOMAIN,
				   GSK_ERROR_BAD_ADDRESS,
				   _("no handler for address family %d (%s)"),
				   family,
				   name ? name : "*unknown*");
      handle_resolver_failure (error, task);
      gsk_name_resolver_task_unref (task);
    }
  else
    {
      task->iface = GSK_NAME_RESOLVER_GET_IFACE (task->resolver);
      task->task_data = (*task->iface->start_resolve) (task->resolver,
						       family, name,
						       handle_resolver_success,
						       handle_resolver_failure,
						       task,
						       (GDestroyNotify) gsk_name_resolver_task_unref);
    }
  return task;
}

/**
 * gsk_name_resolver_task_cancel:
 * @task: a running name resolution task to cancel.
 *
 * Stops the name lookup from continuing.
 * Neither the success or failure functions will be invoked subsequently,
 * but the destroy method will be.
 */
void
gsk_name_resolver_task_cancel (GskNameResolverTask *task)
{
  g_return_if_fail (task->is_running);
  g_return_if_fail (!task->was_cancelled);
  task->was_cancelled = 1;
  if (!task->iface->cancel_resolve (task->resolver, task->task_data))
    {
      task->cancel_succeeded = 0;
    }
  else
    {
      task->cancel_succeeded = 1;
      g_return_if_fail (!task->is_running);
    }
}

/**
 * gsk_name_resolver_task_ref:
 * @task: task whose reference count should be increased.
 *
 * Increase the reference count on a name-resolver task.
 * This is mostly useless outside the resolver code.
 */
void
gsk_name_resolver_task_ref (GskNameResolverTask *task)
{
  g_return_if_fail (task->ref_count > 0);
  ++(task->ref_count);
  g_return_if_fail (task->ref_count != 0);
}

/**
 * gsk_name_resolver_task_unref:
 * @task: task whose reference count should be decreased.
 *
 * Decrease the reference count on a name-resolver task, freeing it if needed.
 * This does NOT cancel the task.  You MUST unref the task returned by
 * gsk_name_resolve().
 */
void
gsk_name_resolver_task_unref (GskNameResolverTask *task)
{
  g_return_if_fail (task->ref_count > 0);
  if (--(task->ref_count) == 0)
    {
      g_return_if_fail (!task->is_running);
      if (task->destroy)
	(*task->destroy) (task->func_data);
      gsk_name_resolver_task_free (task);
    }
}

/* --- family handling --- */
/**
 * gsk_name_resolver_family_get_by_name:
 * @name: the name of the namespace, as a c string.
 *
 * Get the #GskNameResolverFamily of a resolver namespace
 * by ascii string.
 *
 * returns: the family, or 0 on error.
 */
GskNameResolverFamily
gsk_name_resolver_family_get_by_name (const char *name)
{
  GskNameResolverFamily family;
  LOCK ();
  family = GPOINTER_TO_UINT (g_hash_table_lookup (name_to_family, name));
  UNLOCK ();
  return family;
}


/**
 * gsk_name_resolver_family_get_name:
 * @family: the namespace family to enquire about.
 *
 * Get the resolver-namespace as a printable c string.
 *
 * returns: the namespace's name as a c string.
 */
const char *
gsk_name_resolver_family_get_name(GskNameResolverFamily family)
{
  const char *result;
  LOCK ();
  result = g_hash_table_lookup (family_to_name, GUINT_TO_POINTER (family));
  UNLOCK ();
  return result;
}

/**
 * gsk_name_resolver_family_unique:
 * @name: name of a new namespace to register.
 *
 * Allocate a unique GskNameResolverFamily
 * given a new name, or return the old GskNameResolverFamily
 * if one already exists.
 *
 * returns: the family corresponding to @name.
 */
GskNameResolverFamily
gsk_name_resolver_family_unique (const char *name)
{
  GskNameResolverFamily family = gsk_name_resolver_family_get_by_name (name);
  if (!family)
    {
      LOCK ();
      family = ++last_family;
      UNLOCK ();
      gsk_name_resolver_add_family_name (family, name);
    }
  return family;
}

/**
 * gsk_name_resolver_add_family_name:
 * @family: registered family to give a new name for.
 * @name: alias name for @family.
 *
 * Add a new nickname for the name resolver family.
 *
 * The family is the name of the namespace.
 */
void
gsk_name_resolver_add_family_name    (GskNameResolverFamily   family,
				      const char             *name)
{
  char *copy;
  LOCK ();
  g_return_if_fail (g_hash_table_lookup (name_to_family, name) == NULL);
  copy = g_strdup (name);
  if (g_hash_table_lookup (family_to_name, GUINT_TO_POINTER (family)) == NULL)
    g_hash_table_insert (family_to_name, GUINT_TO_POINTER (family), copy);
  g_hash_table_insert (name_to_family, copy, GUINT_TO_POINTER (family));
  if (family > last_family)
    last_family = family;
  UNLOCK ();
}

/**
 * gsk_name_resolver_add_family_resolver:
 * @family: registered family to provide an alias for.
 * @resolver: name resolver to use for addresses in @family.
 *
 * Add a name-resolver that will handle a request
 * of a given family.
 */
void
gsk_name_resolver_add_family_resolver (GskNameResolverFamily   family,
				       GskNameResolver        *resolver)
{
  Handler *handler;
  g_return_if_fail (GSK_IS_NAME_RESOLVER (resolver));
  handler = g_new (Handler, 1);
  handler->resolver = g_object_ref (resolver);
  handler->destroy = NULL;
  LOCK ();
  g_hash_table_insert (family_to_handler, GUINT_TO_POINTER (family), handler);
  UNLOCK ();
}

/**
 * gsk_name_resolver_add_family_handler:
 * @family: registered family to provide a resolver implementation for.
 * @handler: ...
 * @data: data to pass to @handler
 * @destroy: function to call when the handler has deregistered.
 *
 * Add a name-resolver that will handle a request
 * of a given family.
 */
void
gsk_name_resolver_add_family_handler (GskNameResolverFamily family,
				      GskNameResolverFamilyHandler handler,
				      gpointer data,
				      GDestroyNotify destroy)
{
  Handler *h;
  h = g_new (Handler, 1);
  h->resolver = NULL;
  h->handler = handler;
  h->data = data;
  h->destroy = destroy;
  LOCK ();
  g_hash_table_insert (family_to_handler, GUINT_TO_POINTER (family), h);
  UNLOCK ();
}

/**
 * gsk_name_resolver_lookup:
 * @family: name family to perform the lookup in.
 * @name: name to lookup.
 * @success: callback for successful name-lookup: this will
 * be called with the #GskSocketAddress that was found.
 * @failure: callback for failure.  This is invoked with the
 * #GError object.
 * @func_data: data to call to the callbacks.
 * @destroy: function to be called after the callbacks are done.
 *
 * Begin a non-cancellable name-lookup.
 */

/* --- global initialization --- */
static gboolean made_dns_name_resolver = FALSE;
static GskDnsRRCache *dns_rr_cache = NULL;

static GskNameResolver *
make_dns_client (gpointer unused)
{
  GskPacketQueue *queue;
  GskSocketAddressClass *class;
  GskDnsClient *client;
  GError *error = NULL;

  g_assert (unused == NULL);

  class = g_type_class_ref (GSK_TYPE_SOCKET_ADDRESS_IPV4);
  queue = gsk_packet_queue_fd_new_by_family (class->protocol_family, &error);
  if (queue == NULL)
    {
      g_warning ("error creating dns client file-descriptor: %s",
		 error->message);
      g_free (error);
      return NULL;
    }
  g_type_class_unref (class);
  client = gsk_dns_client_new (queue, dns_rr_cache, 0);
  g_object_unref (queue);
  if (!gsk_dns_client_parse_system_files (client))
    g_warning ("error initializing dns client");
  made_dns_name_resolver = TRUE;

  return GSK_NAME_RESOLVER (client);
}

void
_gsk_name_resolver_init (void)
{
  /* initialize the various hash-tables */
  family_to_name = g_hash_table_new (NULL, NULL);
  family_to_handler = g_hash_table_new_full (NULL, NULL, NULL, handler_destroy);
  name_to_family = g_hash_table_new (g_str_hash, g_str_equal);

  /* add dns handling by default */
  {
    GskNameResolverFamily family = gsk_name_resolver_family_unique ("ipv4");
    g_assert (family == GSK_NAME_RESOLVER_FAMILY_IPV4);
    gsk_name_resolver_add_family_handler (GSK_NAME_RESOLVER_FAMILY_IPV4,
					  make_dns_client, NULL, NULL);
  }
}

/**
 * gsk_name_resolver_set_dns_cache_size:
 * @max_bytes: maximum number of bytes of cached DNS data to keep around.
 * @max_records: maximum number of cached DNS records to keep around.
 *
 * Set the DNS cache size.
 *
 * Currently, the defaults are 128*1024 and 1024.
 *
 * This must be called before any name-resolving activities,
 * and may only be called once.
 */
void
gsk_name_resolver_set_dns_cache_size (guint64 max_bytes,
                                      guint   max_records)
{
  g_return_if_fail (!made_dns_name_resolver);
  g_return_if_fail (dns_rr_cache == NULL);
  dns_rr_cache = gsk_dns_rr_cache_new (max_bytes, max_records);
}


/**
 * gsk_name_resolver_set_dns_roundrobin:
 * @do_roundrobin: whether to support round-robin DNS.
 *
 * Set the DNS support for round-robin.
 *
 * You must call gsk_name_resolver_set_dns_cache_size()
 * before calling this function.
 *
 * Default is TRUE.
 */
void
gsk_name_resolver_set_dns_roundrobin (gboolean do_roundrobin)
{
  g_return_if_fail (dns_rr_cache != NULL);
  gsk_dns_rr_cache_roundrobin (dns_rr_cache, do_roundrobin);
}
