#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>			/* ipv4 support */
#include <sys/un.h>			/* local (aka unix) support */
#include <string.h>

#include "config.h"
#include "gsksocketaddress.h"
#include "gskipv4.h"
#include "gskerror.h"
#include "gskghelpers.h"
#include "gskmacros.h"

G_DEFINE_ABSTRACT_TYPE(GskSocketAddress, gsk_socket_address, G_TYPE_OBJECT);
G_DEFINE_TYPE(GskSocketAddressIpv4, gsk_socket_address_ipv4, GSK_TYPE_SOCKET_ADDRESS);
G_DEFINE_TYPE(GskSocketAddressIpv6, gsk_socket_address_ipv6, GSK_TYPE_SOCKET_ADDRESS);
G_DEFINE_TYPE(GskSocketAddressEthernet, gsk_socket_address_ethernet, GSK_TYPE_SOCKET_ADDRESS);
G_DEFINE_TYPE(GskSocketAddressLocal, gsk_socket_address_local, GSK_TYPE_SOCKET_ADDRESS);

/* Macro to initialize the sa_len-like member of
   sockaddr, if it exists. */
#if HAS_SOCKADDR_SA_LEN
#define MAYBE_SET_LENGTH_MEMBER(addr_member, type)		\
	G_STMT_START{ addr_member = sizeof(type); }G_STMT_END
#else
#define MAYBE_SET_LENGTH_MEMBER(addr_member, type)
#endif

/* Define GSK_[AP]F_LOCAL as portible wrappers for [AP]F_LOCAL. */
#if HAS_PF_LOCAL
#define GSK_PF_LOCAL PF_LOCAL
#define GSK_AF_LOCAL AF_LOCAL
#elif HAS_PF_UNIX
#define GSK_PF_LOCAL PF_UNIX
#define GSK_AF_LOCAL AF_UNIX
#else
#warn "no PF_UNIX or PF_LOCAL macros (?)"
#endif

/* --- GskSocketAddress implementation --- */
static void
gsk_socket_address_init (GskSocketAddress *socket_address)
{
}

static void
gsk_socket_address_class_init (GskSocketAddressClass *class)
{
}

/* --- GskSocketAddressIpv4 implementation --- */

static gboolean
gsk_socket_address_ipv4_to_native   (GskSocketAddress *address,
			             gpointer          output)
{
  GskSocketAddressIpv4 *ipv4 = GSK_SOCKET_ADDRESS_IPV4 (address);
  struct sockaddr_in *addr = output;
  memset (addr, 0, sizeof (struct sockaddr_in));
  addr->sin_family = PF_INET;
  MAYBE_SET_LENGTH_MEMBER (addr->sin_len, struct sockaddr_in);
  addr->sin_port = GUINT16_TO_BE (ipv4->ip_port);
  memcpy (&addr->sin_addr, ipv4->ip_address, 4);
  return TRUE;
}

static gboolean
gsk_socket_address_ipv4_from_native (GskSocketAddress *address,
			             gconstpointer     sockaddr_data,
			             gsize             sockaddr_length)
{
  GskSocketAddressIpv4 *ipv4 = GSK_SOCKET_ADDRESS_IPV4 (address);
  const struct sockaddr_in *addr = sockaddr_data;
  ipv4->ip_port = GUINT16_FROM_BE (addr->sin_port);
  memcpy (ipv4->ip_address, &addr->sin_addr, 4);
  return TRUE;
}

static char *
gsk_socket_address_ipv4_to_string  (GskSocketAddress *address)
{
  GskSocketAddressIpv4 *ipv4 = GSK_SOCKET_ADDRESS_IPV4 (address);
  if (ipv4->ip_port != 0)
    return g_strdup_printf ("%d.%d.%d.%d:%d",
                            ipv4->ip_address[0],
                            ipv4->ip_address[1],
                            ipv4->ip_address[2],
                            ipv4->ip_address[3],
                            ipv4->ip_port);
  else
    return g_strdup_printf ("%d.%d.%d.%d",
                            ipv4->ip_address[0],
                            ipv4->ip_address[1],
                            ipv4->ip_address[2],
                            ipv4->ip_address[3]);
}

static gboolean
gsk_socket_address_ipv4_equals (GskSocketAddress *a,
				GskSocketAddress *b)
{
  GskSocketAddressIpv4 *ipv4_a = GSK_SOCKET_ADDRESS_IPV4 (a);
  GskSocketAddressIpv4 *ipv4_b = GSK_SOCKET_ADDRESS_IPV4 (b);
  return ipv4_a->ip_address[0] == ipv4_b->ip_address[0]
      && ipv4_a->ip_address[1] == ipv4_b->ip_address[1]
      && ipv4_a->ip_address[2] == ipv4_b->ip_address[2]
      && ipv4_a->ip_address[3] == ipv4_b->ip_address[3]
      && ipv4_a->ip_port == ipv4_b->ip_port;
}

static guint
gsk_socket_address_ipv4_hash (GskSocketAddress *a)
{
  GskSocketAddressIpv4 *ipv4 = GSK_SOCKET_ADDRESS_IPV4 (a);
  guint hash = ipv4->ip_address[0];
  hash *= 33;
  hash += ipv4->ip_address[1];
  hash *= 33;
  hash += ipv4->ip_address[2];
  hash *= 33;
  hash += ipv4->ip_address[3];
  hash *= 33;
  hash += ipv4->ip_port;
  return hash;
}

static void
gsk_socket_address_ipv4_init (GskSocketAddressIpv4 *socket_address_ipv4)
{
}

static void
gsk_socket_address_ipv4_class_init (GskSocketAddressIpv4Class *ipv4_class)
{
  GskSocketAddressClass *class = GSK_SOCKET_ADDRESS_CLASS (ipv4_class);
  class->address_family = AF_INET;
  class->protocol_family = PF_INET;
  class->sizeof_native_address = sizeof (struct sockaddr_in);
  class->from_native = gsk_socket_address_ipv4_from_native;
  class->to_native = gsk_socket_address_ipv4_to_native;
  class->to_string = gsk_socket_address_ipv4_to_string;
  class->hash = gsk_socket_address_ipv4_hash;
  class->equals = gsk_socket_address_ipv4_equals;
  gsk_socket_address_register_subclass (class);
}

/* --- ipv6 implementation --- */
#if SUPPORTS_IPV6
static gboolean
gsk_socket_address_ipv6_to_native   (GskSocketAddress *address,
			             gpointer          output)
{
  GskSocketAddressIpv6 *ipv6 = GSK_SOCKET_ADDRESS_IPV6 (address);
  struct sockaddr_in6 *addr = output;
  MAYBE_SET_LENGTH_MEMBER (addr->sin6_len, struct sockaddr_in6);
  addr->sin6_family = AF_INET6;
  addr->sin6_port = GUINT16_TO_BE (ipv6->port);
  addr->sin6_flowinfo = GUINT32_TO_BE (ipv6->flow_info);
  addr->sin6_scope_id = GUINT32_TO_BE (ipv6->scope_id);
  g_assert (sizeof (addr->sin6_addr) == 16);
  memcpy (&addr->sin6_addr, ipv6->address, 16);
  return TRUE;
}

static gboolean
gsk_socket_address_ipv6_from_native (GskSocketAddress *address,
			             gconstpointer     sockaddr_data,
			             gsize             sockaddr_length)
{
  GskSocketAddressIpv6 *ipv6 = GSK_SOCKET_ADDRESS_IPV6 (address);
  const struct sockaddr_in6 *addr = sockaddr_data;
  ipv6->port = GUINT16_FROM_BE (addr->sin6_port);
  ipv6->flow_info = GUINT32_FROM_BE (addr->sin6_flowinfo);
  ipv6->scope_id = GUINT32_FROM_BE (addr->sin6_scope_id);
  memcpy (ipv6->address, &addr->sin6_addr, 16);
  return TRUE;
}
#endif

static char *
gsk_socket_address_ipv6_to_string  (GskSocketAddress *address)
{
  GString *str = g_string_new ("");
  GskSocketAddressIpv6 *ipv6 = GSK_SOCKET_ADDRESS_IPV6 (address);
  guint8 *a = ipv6->address;
  guint i;
  g_string_printf (str, "%d@%x", ipv6->port, a[0]);
  for (i = 1; i < 16; i++)
    g_string_append_printf (str, ":%x", a[1]);
  return g_string_free (str, FALSE);
}

static gboolean
gsk_socket_address_ipv6_equals (GskSocketAddress *a,
				GskSocketAddress *b)
{
  GskSocketAddressIpv6 *ipv6_a = GSK_SOCKET_ADDRESS_IPV6 (a);
  GskSocketAddressIpv6 *ipv6_b = GSK_SOCKET_ADDRESS_IPV6 (b);
  return ipv6_a->port == ipv6_b->port
     &&  memcmp (ipv6_a->address, ipv6_b->address, 16) == 0;
}

static guint
gsk_socket_address_ipv6_hash (GskSocketAddress *a)
{
  GskSocketAddressIpv6 *ipv6 = GSK_SOCKET_ADDRESS_IPV6 (a);
  guint hash = ipv6->port;
  guint i;
  for (i = 0; i < 16; i++)
    {
      hash *= 33;
      hash += ipv6->address[i];
    }
  return hash;
}

static void
gsk_socket_address_ipv6_init (GskSocketAddressIpv6 *socket_address_ipv6)
{
}

static void
gsk_socket_address_ipv6_class_init (GskSocketAddressIpv6Class *ipv6_class)
{
  GskSocketAddressClass *class = GSK_SOCKET_ADDRESS_CLASS (ipv6_class);
#if SUPPORTS_IPV6
  class->address_family = AF_INET6;
  class->protocol_family = PF_INET6;
  class->sizeof_native_address = sizeof (struct sockaddr_in6);
  class->from_native = gsk_socket_address_ipv6_from_native;
  class->to_native = gsk_socket_address_ipv6_to_native;
#endif
  class->to_string = gsk_socket_address_ipv6_to_string;
  class->hash = gsk_socket_address_ipv6_hash;
  class->equals = gsk_socket_address_ipv6_equals;
  gsk_socket_address_register_subclass (class);
}

/* --- local (aka unix) socket address implementation --- */
static gboolean
gsk_socket_address_local_to_native   (GskSocketAddress *address,
			             gpointer          output)
{
  GskSocketAddressLocal *local = GSK_SOCKET_ADDRESS_LOCAL (address);
  struct sockaddr_un *addr = output;
  MAYBE_SET_LENGTH_MEMBER (addr->sun_len, struct sockaddr_un);
  addr->sun_family = GSK_PF_LOCAL;
  /* XXX: zero out other (a priori, unknown) arguments */
  strncpy (addr->sun_path, local->path, sizeof (addr->sun_path));
  return TRUE;
}


static gboolean
gsk_socket_address_local_from_native (GskSocketAddress *address,
			              gconstpointer     sockaddr_data,
			              gsize             sockaddr_length)
{
  GskSocketAddressLocal *local = GSK_SOCKET_ADDRESS_LOCAL (address);
  const struct sockaddr_un *native = sockaddr_data;
  gint max_len;
  guint len;
  if (GSK_STRUCT_IS_LAST_MEMBER (struct sockaddr_un, sun_path))
    max_len = sockaddr_length - G_STRUCT_OFFSET(struct sockaddr_un, sun_path);
  else
    max_len = sizeof (native->sun_path);
  if (max_len <= 0)
    return FALSE;

  g_free (local->path);
  len = gsk_strnlen (native->sun_path, max_len);
  local->path = g_new (char, len + 1);
  memcpy (local->path, native->sun_path, len);
  local->path[len] = '\0';
  return TRUE;
}

static char *
gsk_socket_address_local_to_string  (GskSocketAddress *address)
{
  GskSocketAddressLocal *local = GSK_SOCKET_ADDRESS_LOCAL (address);
  return g_strdup (local->path);
}

static gboolean
gsk_socket_address_local_equals (GskSocketAddress *a,
				GskSocketAddress *b)
{
  GskSocketAddressLocal *local_a = GSK_SOCKET_ADDRESS_LOCAL (a);
  GskSocketAddressLocal *local_b = GSK_SOCKET_ADDRESS_LOCAL (b);
  return strcmp (local_a->path, local_b->path) == 0;
}

static guint
gsk_socket_address_local_hash (GskSocketAddress *a)
{
  GskSocketAddressLocal *local = GSK_SOCKET_ADDRESS_LOCAL (a);
  return g_str_hash (local->path);
}

static void
gsk_socket_address_local_init (GskSocketAddressLocal *socket_address_local)
{
}

static void
gsk_socket_address_local_finalize (GObject *object)
{
  GskSocketAddressLocal *local = GSK_SOCKET_ADDRESS_LOCAL (object);
  g_free (local->path);
  G_OBJECT_CLASS (gsk_socket_address_local_parent_class)->finalize (object);
}

static void
gsk_socket_address_local_class_init (GskSocketAddressLocalClass *class)
{
  GskSocketAddressClass *address_class = GSK_SOCKET_ADDRESS_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  class->max_path_length = GSK_STRUCT_MEMBER_SIZE (struct sockaddr_un, sun_path);
  address_class->sizeof_native_address = sizeof (struct sockaddr_un);
  address_class->address_family = GSK_AF_LOCAL;
  address_class->protocol_family = GSK_PF_LOCAL;
  address_class->to_native = gsk_socket_address_local_to_native;
  address_class->from_native = gsk_socket_address_local_from_native;
  address_class->to_string = gsk_socket_address_local_to_string;
  address_class->equals = gsk_socket_address_local_equals;
  address_class->hash = gsk_socket_address_local_hash;
  gsk_socket_address_register_subclass (address_class);
  object_class->finalize = gsk_socket_address_local_finalize;
}

/**
 * gsk_socket_address_local_new:
 * @path: path in filesystem to hook this socket up.
 *
 * Create a socket-address which is associated with a path
 * in the local filesystem.  Such socket-addresses
 * are useful for fast communication between processes on the same
 * host.
 *
 * Sometimes, these types of addresses are called unix-domain addresses,
 * but it is better to avoid the term unix for a generic concept.
 *
 * returns: the newly allocated socket address.
 */
GskSocketAddress *
gsk_socket_address_local_new (const char *path)
{
  GskSocketAddressLocalClass *class = g_type_class_ref (GSK_TYPE_SOCKET_ADDRESS_LOCAL);
  guint path_len = strlen (path);
  GskSocketAddressLocal *rv;
  if (path_len > class->max_path_length)
    return NULL;
  rv = g_object_new (G_OBJECT_CLASS_TYPE (class), NULL);
  rv->path = g_strdup (path);
  g_type_class_unref (class);
  return GSK_SOCKET_ADDRESS (rv);
}

/* --- Ethernet (MAC) addresses --- */
#if 0	/* TODO: conversions to/from native */
static gboolean
gsk_socket_address_ethernet_to_native (GskSocketAddress *address,
                                       gpointer          output)
{
  GskSocketAddressEthernet *ethernet = GSK_SOCKET_ADDRESS_ETHERNET (address);
  ...
}

static gboolean
gsk_socket_address_ethernet_from_native (GskSocketAddress *address,
                                         gconstpointer     sockaddr_data,
                                         gsize             sockaddr_length)
{
  GskSocketAddressEthernet *ethernet = GSK_SOCKET_ADDRESS_ETHERNET (address);
  ...
}
#endif

static guint
gsk_socket_address_ethernet_hash (GskSocketAddress *addr)
{
  GskSocketAddressEthernet *ethernet = GSK_SOCKET_ADDRESS_ETHERNET (addr);
  guint i;
  guint rv = 0;
  for (i = 0; i < 6; i++)
    {
      rv *= 167;
      rv += ethernet->mac_address[i];
    }
  return rv;
}

static char    *
gsk_socket_address_ethernet_to_string (GskSocketAddress *address)
{
  GskSocketAddressEthernet *ethernet = GSK_SOCKET_ADDRESS_ETHERNET (address);
  return g_strdup_printf ("%02X:%02X:%02X:%02X:%02X:%02X",
			  ethernet->mac_address[0],
			  ethernet->mac_address[1],
			  ethernet->mac_address[2],
			  ethernet->mac_address[3],
			  ethernet->mac_address[4],
			  ethernet->mac_address[5]);
}

static gboolean
gsk_socket_address_ethernet_equals (GskSocketAddress *addr1,
                                    GskSocketAddress *addr2)
{
  GskSocketAddressEthernet *ethernet1 = GSK_SOCKET_ADDRESS_ETHERNET (addr1);
  GskSocketAddressEthernet *ethernet2 = GSK_SOCKET_ADDRESS_ETHERNET (addr2);
  return memcmp (ethernet1->mac_address, ethernet2->mac_address, 6) == 0;
}

/* --- functions --- */
static void
gsk_socket_address_ethernet_init (GskSocketAddressEthernet *socket_address_ethernet)
{
}

static void
gsk_socket_address_ethernet_class_init (GskSocketAddressEthernetClass *eth_class)
{
  GskSocketAddressClass *class = GSK_SOCKET_ADDRESS_CLASS (eth_class);
  class->hash = gsk_socket_address_ethernet_hash;
  class->to_string = gsk_socket_address_ethernet_to_string;
  class->equals = gsk_socket_address_ethernet_equals;
  /* TODO: convert to/from native */
#if 0
  class->to_native = gsk_socket_address_ethernet_to_native;
  class->from_native = gsk_socket_address_ethernet_from_native;
  class->address_family = AF_???;
  class->protocol_family = PF_???;
  class->sizeof_native_address = sizeof (struct sockaddr_??? );
  gsk_socket_address_register_subclass (class);
#endif
}

/**
 * gsk_socket_address_ethernet_new:
 * @mac_addr: the 6-byte unique address of this ethernet device.
 *
 * Allocate a new socket address corresponding to an
 * ethernet device.
 *
 * returns: the newly allocated socket-address.
 */
GskSocketAddress *
gsk_socket_address_ethernet_new (const guint8 *mac_addr)
{
  GskSocketAddressEthernet *eth = g_object_new (GSK_TYPE_SOCKET_ADDRESS_ETHERNET, NULL);
  memcpy (eth->mac_address, mac_addr, 6);
  return GSK_SOCKET_ADDRESS (eth);
}

/* --- public methods --- */
static GHashTable *native_to_gtype = NULL;
static GStaticRWLock native_to_gtype_lock = G_STATIC_RW_LOCK_INIT;

#define N2G_WRITE_LOCK()   g_static_rw_lock_writer_lock(&native_to_gtype_lock)
#define N2G_WRITE_UNLOCK() g_static_rw_lock_writer_unlock(&native_to_gtype_lock)
#define N2G_READ_LOCK()    g_static_rw_lock_reader_lock(&native_to_gtype_lock)
#define N2G_READ_UNLOCK()  g_static_rw_lock_reader_unlock(&native_to_gtype_lock)

/**
 * gsk_socket_address_from_native:
 * @native_data: a struct sockaddr_t*.
 * @native_size: length of native_data.
 *
 * Allocate a new GskSocketAddress based on
 * native_data, if we know how.
 *
 * returns: a new GskSocketAddress or NULL if we could not interpret the sockaddr.
 */
GskSocketAddress *
gsk_socket_address_from_native   (gconstpointer native_data,
				  gsize         native_size)
{
  const struct sockaddr *addr = native_data;
  GType type;
  GskSocketAddressClass *class;
  GObject *rv_object;
  GskSocketAddress *rv;
  guint family;
  N2G_READ_LOCK ();
  if (native_to_gtype == NULL)
    {
      N2G_READ_UNLOCK ();
      return NULL;
    }
  family = (guint) addr->sa_family;
  type = (GType) g_hash_table_lookup (native_to_gtype, GUINT_TO_POINTER (family));
  N2G_READ_UNLOCK ();
  if (type == 0)
    {
      return NULL;
    }
  rv_object = g_object_new (type, NULL);
  rv = GSK_SOCKET_ADDRESS (rv_object);
  class = GSK_SOCKET_ADDRESS_GET_CLASS (rv);
  if (!((*class->from_native) (rv, native_data, native_size)))
    {
      g_object_unref (rv);
      return NULL;
    }
  return GSK_SOCKET_ADDRESS (rv);
}

/**
 * gsk_socket_address_sizeof_native:
 * @address: a socket address.
 *
 * Determine how many bytes of storage the sockaddr_t
 * based on this object will require.
 *
 * returns: the size in bytes of the native sockaddr type.
 */
guint
gsk_socket_address_sizeof_native (GskSocketAddress *address)
{
  return GSK_SOCKET_ADDRESS_GET_CLASS (address)->sizeof_native_address;
}

/**
 * gsk_socket_address_protocol_family:
 * @address: a socket address.
 *
 * Get the PF_* macro value corresponding to this address.
 *
 * returns: the protocol family.
 */
gint
gsk_socket_address_protocol_family (GskSocketAddress *address)
{
  return GSK_SOCKET_ADDRESS_GET_CLASS (address)->protocol_family;
}

/**
 * gsk_socket_address_address_family:
 * @address: a socket address.
 *
 * Get the AF_* macro value corresponding to this address.
 *
 * returns: the address family.
 */
gint
gsk_socket_address_address_family (GskSocketAddress *address)
{
  return GSK_SOCKET_ADDRESS_GET_CLASS (address)->address_family;
}

/**
 * gsk_socket_address_to_native:
 * @address: a socket address.
 * @output: a struct sockaddr_t (at least conceptually)
 * @error: optional error return value.
 *
 * Convert a socket-address to its native form.
 *
 * returns: whether it was able to convert the address.
 */
gboolean
gsk_socket_address_to_native     (GskSocketAddress *address,
				  gpointer          output,
				  GError          **error)
{
  GskSocketAddressClass *class = GSK_SOCKET_ADDRESS_GET_CLASS (address);
  if (G_UNLIKELY (!class->to_native (address, output)))
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_FOREIGN_ADDRESS,
		   "error making a native socket-address for %s",
		   g_type_name (G_OBJECT_CLASS_TYPE (class)));
      return FALSE;
    }
  return TRUE;
}

/**
 * gsk_socket_address_to_string:
 * @address: a socket address.
 *
 * Convert a socket-address to a newly allocated string,
 * which the caller must free.
 *
 * returns: a string for the user to free.
 */
char *
gsk_socket_address_to_string     (GskSocketAddress *address)
{
  GskSocketAddressClass *class = GSK_SOCKET_ADDRESS_GET_CLASS (address);
  return class->to_string (address);
}

/**
 * gsk_socket_address_ipv4_new:
 * @ip_address: the 4-byte IP address
 * @port: the port number.
 *
 * Allocate a new IPv4 address given a numeric IP and port number.
 *
 * returns: a new GskSocketAddress
 */
GskSocketAddress *
gsk_socket_address_ipv4_new (const guint8 *ip_address,
			     guint16       port)
{
  GskSocketAddressIpv4 *ipv4;
  ipv4 = g_object_new (GSK_TYPE_SOCKET_ADDRESS_IPV4, NULL);
  ipv4->ip_port = port;
  memcpy (ipv4->ip_address, ip_address, 4);
  return GSK_SOCKET_ADDRESS (ipv4);
}

/**
 * gsk_socket_address_equals:
 * @address_a_ptr: a pointer to a #GskSocketAddress.
 * @address_b_ptr: a pointer to a #GskSocketAddress.
 *
 * This function is a GEqualFunc which can determine
 * if two socket address are the same.
 * This is principally used with #gsk_socket_address_hash
 * to make a hash-table mapping from socket-addresses.
 *
 * (This just uses the virtual method of GskSocketAddressClass)
 *
 * returns: whether the addresses are equal.
 */
gboolean
gsk_socket_address_equals        (gconstpointer     address_a_ptr,
				  gconstpointer     address_b_ptr)
{
  GskSocketAddress *address_a = (GskSocketAddress *) address_a_ptr;
  GskSocketAddress *address_b = (GskSocketAddress *) address_b_ptr;
  GskSocketAddressClass *class;
  GType type_a, type_b;
  g_return_val_if_fail (GSK_IS_SOCKET_ADDRESS (address_a)
		     && GSK_IS_SOCKET_ADDRESS (address_b), FALSE);
  type_a = G_OBJECT_TYPE (address_a);
  type_b = G_OBJECT_TYPE (address_b);
  if (type_a != type_b)
    return FALSE;
  class = GSK_SOCKET_ADDRESS_GET_CLASS (address_a);
  return (*class->equals) (address_a, address_b);
}

/**
 * gsk_socket_address_hash:
 * @address_ptr: a pointer to a #GskSocketAddress.
 *
 * This function is a GHashFunc which can determine
 * a hash value for a socket-address.
 *
 * This is principally used with #gsk_socket_address_equals
 * to make a hash-table mapping from socket-addresses.
 *
 * (This just uses the virtual method of GskSocketAddressClass)
 *
 * returns: the hash value for the socket-address.
 */
guint
gsk_socket_address_hash (gconstpointer  address_ptr)
{
  GskSocketAddress *address = (GskSocketAddress *) address_ptr;
  GskSocketAddressClass *class;
  g_return_val_if_fail (GSK_IS_SOCKET_ADDRESS (address), 0);
  class = GSK_SOCKET_ADDRESS_GET_CLASS (address);
  return (*class->hash) (address);
}

/* --- protected methods --- */
/**
 * gsk_socket_address_register_subclass:
 * @klass: a concrete derived class.
 *
 * Add the class to a per address-family hash table
 * for use converting from native.
 */
void
gsk_socket_address_register_subclass (GskSocketAddressClass *klass)
{
  GType type = G_OBJECT_CLASS_TYPE (klass);
  N2G_WRITE_LOCK ();
  if (native_to_gtype == NULL)
    native_to_gtype = g_hash_table_new (NULL, NULL);
  g_hash_table_insert (native_to_gtype,
		       GUINT_TO_POINTER (klass->address_family),
		       (gpointer) type);
  N2G_WRITE_UNLOCK ();
}

/* --- public: a few useful quarks, for use with g_object_set_qdata --- */
GQuark 
gsk_socket_address_get_remote_quark()
{
  static GQuark rv = 0;
  if (rv == 0)
    rv = g_quark_from_static_string ("gsk-socket-address-remote-quark");
  return rv;
}

GQuark 
gsk_socket_address_get_local_quark()
{
  static GQuark rv = 0;
  if (rv == 0)
    rv = g_quark_from_static_string ("gsk-socket-address-local-quark");
  return rv;
}

/* Deprecated.  included for binary compatibility */
#undef gsk_socket_address_new_local
GskSocketAddress *gsk_socket_address_new_local (const char       *path)
{
  return gsk_socket_address_local_new (path);
}
#undef gsk_socket_address_new_ethernet
GskSocketAddress *gsk_socket_address_new_ethernet  (const guint8     *mac_addr)
{
  return gsk_socket_address_ethernet_new (mac_addr);
}

