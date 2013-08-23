#include <string.h>
#include "gsksocketaddresssymbolic.h"
#include "gsknameresolver.h"

G_DEFINE_ABSTRACT_TYPE(GskSocketAddressSymbolic,
                       gsk_socket_address_symbolic,
		       GSK_TYPE_SOCKET_ADDRESS);
G_DEFINE_TYPE         (GskSocketAddressSymbolicIpv4,
                       gsk_socket_address_symbolic_ipv4,
		       GSK_TYPE_SOCKET_ADDRESS_SYMBOLIC);

/* GskSocketAddressSymbolic */
enum
{
  SYMBOLIC_PROP_0,
  SYMBOLIC_PROP_NAME,
};
static gboolean
gsk_socket_address_symbolic_to_native(GskSocketAddress *address,
			              gpointer          output)
{
  return FALSE;
}

static gboolean
gsk_socket_address_symbolic_from_native (GskSocketAddress *address,
			                 gconstpointer     sockaddr_data,
			                 gsize             sockaddr_length)
{
  return FALSE;
}

static char *
gsk_socket_address_symbolic_to_string  (GskSocketAddress *address)
{
  return g_strdup (GSK_SOCKET_ADDRESS_SYMBOLIC (address)->name);
}

static gboolean
gsk_socket_address_symbolic_equals     (GskSocketAddress *addr1,
                                        GskSocketAddress *addr2)
{
  return strcmp (GSK_SOCKET_ADDRESS_SYMBOLIC (addr1)->name,
                 GSK_SOCKET_ADDRESS_SYMBOLIC (addr2)->name) == 0;
}
static guint
gsk_socket_address_symbolic_hash       (GskSocketAddress *addr)
{
  return g_str_hash (GSK_SOCKET_ADDRESS_SYMBOLIC (addr)->name) + 100000;
}

static void
gsk_socket_address_symbolic_set_property (GObject        *object,
                                          guint           property_id,
                                          const GValue   *value,
                                          GParamSpec     *pspec)
{
  GskSocketAddressSymbolic *symbolic = GSK_SOCKET_ADDRESS_SYMBOLIC (object);
  switch (property_id)
    {
    case SYMBOLIC_PROP_NAME:
      g_free (symbolic->name);
      symbolic->name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
gsk_socket_address_symbolic_get_property (GObject        *object,
                                          guint           property_id,
                                          GValue         *value,
                                          GParamSpec     *pspec)
{
  GskSocketAddressSymbolic *symbolic = GSK_SOCKET_ADDRESS_SYMBOLIC (object);
  switch (property_id)
    {
    case SYMBOLIC_PROP_NAME:
      g_value_set_string (value, symbolic->name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}
static void
gsk_socket_address_symbolic_finalize (GObject *object)
{
  GskSocketAddressSymbolic *symbolic = GSK_SOCKET_ADDRESS_SYMBOLIC (object);
  g_free (symbolic->name);
  G_OBJECT_CLASS (gsk_socket_address_symbolic_parent_class)->finalize (object);
}

static void
gsk_socket_address_symbolic_class_init (GskSocketAddressSymbolicClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GskSocketAddressClass *address_class = GSK_SOCKET_ADDRESS_CLASS (class);
  address_class->to_native = gsk_socket_address_symbolic_to_native;
  address_class->from_native = gsk_socket_address_symbolic_from_native;
  address_class->to_string = gsk_socket_address_symbolic_to_string;
  address_class->hash = gsk_socket_address_symbolic_hash;
  address_class->equals = gsk_socket_address_symbolic_equals;
  object_class->set_property = gsk_socket_address_symbolic_set_property;
  object_class->get_property = gsk_socket_address_symbolic_get_property;
  object_class->finalize = gsk_socket_address_symbolic_finalize;
  g_object_class_install_property (object_class, SYMBOLIC_PROP_NAME,
                                   g_param_spec_string ("name", "Name", "name (of host)",
                                                        NULL, G_PARAM_READWRITE));
}
static void
gsk_socket_address_symbolic_init (GskSocketAddressSymbolic *symbolic)
{
}

/* Public API */
gpointer
gsk_socket_address_symbolic_create_name_resolver (GskSocketAddressSymbolic *symbolic)
{
  GskSocketAddressSymbolicClass *class = GSK_SOCKET_ADDRESS_SYMBOLIC_GET_CLASS (symbolic);
  return class->create_name_resolver (symbolic);
}

void
gsk_socket_address_symbolic_start_resolution (GskSocketAddressSymbolic *symbolic,
                                              gpointer                  name_resolver,
                                              GskSocketAddressSymbolicResolveFunc r,
                                              GskSocketAddressSymbolicErrorFunc e,
                                              gpointer                  user_data,
                                              GDestroyNotify            destroy)
{
  GskSocketAddressSymbolicClass *class = GSK_SOCKET_ADDRESS_SYMBOLIC_GET_CLASS (symbolic);
  class->start_resolution (symbolic, name_resolver, r, e, user_data, destroy);
}
void
gsk_socket_address_symbolic_cancel_resolution (GskSocketAddressSymbolic *symbolic,
                                               gpointer                  name_resolver)
{
  GskSocketAddressSymbolicClass *class = GSK_SOCKET_ADDRESS_SYMBOLIC_GET_CLASS (symbolic);
  class->cancel_resolution (symbolic, name_resolver);
}

/* GskSocketAddressSymbolicIpv4 */
enum
{
  SYMBOLIC_IPV4_PROP_0,
  SYMBOLIC_IPV4_PROP_PORT,
};

typedef struct _Ipv4NameResolver Ipv4NameResolver;
struct _Ipv4NameResolver
{
  GskSocketAddressSymbolicIpv4 *ipv4;
  GskSocketAddressSymbolicResolveFunc resolve_func;
  GskSocketAddressSymbolicErrorFunc error_func;
  gpointer data;
  GDestroyNotify destroy;
  GskNameResolverTask *task;
  gboolean resolved;
};

static gpointer 
gsk_socket_address_symbolic_ipv4_create_name_resolver (GskSocketAddressSymbolic *symbolic)
{
  Ipv4NameResolver *resolver = g_new0 (Ipv4NameResolver, 1);
  resolver->ipv4 = GSK_SOCKET_ADDRESS_SYMBOLIC_IPV4 (g_object_ref (symbolic));
  return resolver;
}

static void
ipv4_handle_success (GskSocketAddress *address,
                     gpointer          func_data)
{
  Ipv4NameResolver *resolver = func_data;

  GskSocketAddressIpv4 *host_only = GSK_SOCKET_ADDRESS_IPV4 (address);
  GskSocketAddress *real;

  resolver->resolved = 1;

  /* prepare new socket address */
  real = gsk_socket_address_ipv4_new (host_only->ip_address,
                                      resolver->ipv4->port);

  /* invoke user's callback */
  (*resolver->resolve_func) (GSK_SOCKET_ADDRESS_SYMBOLIC (resolver->ipv4),
                             real,
                             resolver->data);

  /* cleanup */
  g_object_unref (real);
}

static void
ipv4_handle_failure (GError           *error,
                     gpointer          func_data)
{
  Ipv4NameResolver *resolver = func_data;
  resolver->resolved = 1;

  /* invoke user's callback */
  if (resolver->error_func != NULL)
    (*resolver->error_func) (GSK_SOCKET_ADDRESS_SYMBOLIC (resolver->ipv4),
                             error,
                             resolver->data);
}

static void
ipv4_handle_destroy (gpointer          func_data)
{
  Ipv4NameResolver *resolver = func_data;
  g_assert (resolver->resolved);
  if (resolver->destroy != NULL)
    resolver->destroy (resolver->data);
  g_object_unref (resolver->ipv4);
  g_free (resolver);
}

static void
gsk_socket_address_symbolic_ipv4_start_resolution (GskSocketAddressSymbolic *symbolic,
                                                   gpointer                  name_resolver,
                                                   GskSocketAddressSymbolicResolveFunc r,
                                                   GskSocketAddressSymbolicErrorFunc e,
                                                   gpointer                  user_data,
                                                   GDestroyNotify            destroy)
{
  Ipv4NameResolver *resolver = name_resolver;
  resolver->resolve_func = r;
  resolver->error_func = e;
  resolver->data = user_data;
  resolver->destroy = destroy;
  resolver->resolved = 0;
  resolver->task = gsk_name_resolver_task_new (GSK_NAME_RESOLVER_FAMILY_IPV4,
                                               symbolic->name,
                                               ipv4_handle_success,
                                               ipv4_handle_failure,
                                               resolver,
                                               ipv4_handle_destroy);

  /* note: may destroy resolver! */
  gsk_name_resolver_task_unref (resolver->task);
}

static void
gsk_socket_address_symbolic_ipv4_cancel_resolution (GskSocketAddressSymbolic *symbolic,
                                                    gpointer                  name_resolver)
{
  Ipv4NameResolver *resolver = name_resolver;

  /* if this triggers, i guess that we are in
     the start_resolution() callback,
     and name resolution hasn't blocked,
     so that either "success" or "failure" 
     callback is being invoked.  but then,
     how did the user get the pointer "resolver"?
     it's impossible. */
  g_return_if_fail (resolver->task != NULL);

  if (resolver->resolved)
    return;
  if (resolver->task != NULL)
    {
      GskNameResolverTask *task = resolver->task;
      resolver->task = NULL;
      gsk_name_resolver_task_cancel (task);
    }
}


static char *
gsk_socket_address_symbolic_ipv4_to_string  (GskSocketAddress *address)
{
  return g_strdup_printf ("%s:%u",
                          GSK_SOCKET_ADDRESS_SYMBOLIC (address)->name,
                          (guint) GSK_SOCKET_ADDRESS_SYMBOLIC_IPV4 (address)->port);
}

static gboolean
gsk_socket_address_symbolic_ipv4_equals(GskSocketAddress *addr1,
                                        GskSocketAddress *addr2)
{
  return strcmp (GSK_SOCKET_ADDRESS_SYMBOLIC (addr1)->name,
                 GSK_SOCKET_ADDRESS_SYMBOLIC (addr2)->name) == 0
     &&   GSK_SOCKET_ADDRESS_SYMBOLIC_IPV4 (addr1)->port
       == GSK_SOCKET_ADDRESS_SYMBOLIC_IPV4 (addr2)->port;
}
static guint
gsk_socket_address_symbolic_ipv4_hash   (GskSocketAddress *addr)
{
  return g_str_hash (GSK_SOCKET_ADDRESS_SYMBOLIC (addr)->name) + 200000
     + GSK_SOCKET_ADDRESS_SYMBOLIC_IPV4 (addr)->port;
}
static void
gsk_socket_address_symbolic_ipv4_set_property (GObject        *object,
                                               guint           property_id,
                                               const GValue   *value,
                                               GParamSpec     *pspec)
{
  GskSocketAddressSymbolicIpv4 *symbolic_ipv4 = GSK_SOCKET_ADDRESS_SYMBOLIC_IPV4 (object);
  switch (property_id)
    {
    case SYMBOLIC_IPV4_PROP_PORT:
      symbolic_ipv4->port = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
gsk_socket_address_symbolic_ipv4_get_property (GObject        *object,
                                               guint           property_id,
                                               GValue         *value,
                                               GParamSpec     *pspec)
{
  GskSocketAddressSymbolicIpv4 *symbolic_ipv4 = GSK_SOCKET_ADDRESS_SYMBOLIC_IPV4 (object);
  switch (property_id)
    {
    case SYMBOLIC_IPV4_PROP_PORT:
      g_value_set_uint (value, symbolic_ipv4->port);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
gsk_socket_address_symbolic_ipv4_finalize (GObject *object)
{
  //GskSocketAddressSymbolicIpv4 *symbolic_ipv4 = GSK_SOCKET_ADDRESS_SYMBOLIC_IPV4 (object);
  G_OBJECT_CLASS (gsk_socket_address_symbolic_ipv4_parent_class)->finalize (object);
}

static void
gsk_socket_address_symbolic_ipv4_class_init (GskSocketAddressSymbolicIpv4Class *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GskSocketAddressClass *address_class = GSK_SOCKET_ADDRESS_CLASS (class);
  GskSocketAddressSymbolicClass *symbolic_class = GSK_SOCKET_ADDRESS_SYMBOLIC_CLASS (class);
  object_class->set_property = gsk_socket_address_symbolic_ipv4_set_property;
  object_class->get_property = gsk_socket_address_symbolic_ipv4_get_property;
  object_class->finalize = gsk_socket_address_symbolic_ipv4_finalize;
  address_class->to_string = gsk_socket_address_symbolic_ipv4_to_string;
  address_class->hash = gsk_socket_address_symbolic_ipv4_hash;
  address_class->equals = gsk_socket_address_symbolic_ipv4_equals;
  symbolic_class->create_name_resolver = gsk_socket_address_symbolic_ipv4_create_name_resolver;
  symbolic_class->start_resolution = gsk_socket_address_symbolic_ipv4_start_resolution;
  symbolic_class->cancel_resolution = gsk_socket_address_symbolic_ipv4_cancel_resolution;
  g_object_class_install_property (object_class, SYMBOLIC_IPV4_PROP_PORT,
                                   g_param_spec_uint ("port", "Port", "TCP port",
                                                      0,65535,0, G_PARAM_READWRITE));
}
static void
gsk_socket_address_symbolic_ipv4_init (GskSocketAddressSymbolicIpv4 *symbolic_ipv4)
{
}
GskSocketAddress *
gsk_socket_address_symbolic_ipv4_new (const char *name,
                                      guint16     port)
{
  return g_object_new (GSK_TYPE_SOCKET_ADDRESS_SYMBOLIC_IPV4,
                       "name", name,
                       "port", port,
                       NULL);
}
