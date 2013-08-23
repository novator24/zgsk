#ifndef __GSK_SOCKET_ADDRESS_H_
#define __GSK_SOCKET_ADDRESS_H_

#include <glib-object.h>
#include "gskipv4.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskSocketAddress GskSocketAddress;
typedef struct _GskSocketAddressClass GskSocketAddressClass;
typedef struct _GskSocketAddressIpv4 GskSocketAddressIpv4;
typedef struct _GskSocketAddressIpv4Class GskSocketAddressIpv4Class;
typedef struct _GskSocketAddressIpv6 GskSocketAddressIpv6;
typedef struct _GskSocketAddressIpv6Class GskSocketAddressIpv6Class;
typedef struct _GskSocketAddressLocal GskSocketAddressLocal;
typedef struct _GskSocketAddressLocalClass GskSocketAddressLocalClass;
typedef struct _GskSocketAddressEthernet GskSocketAddressEthernet;
typedef struct _GskSocketAddressEthernetClass GskSocketAddressEthernetClass;

/* --- type macros --- */
#define GSK_TYPE_SOCKET_ADDRESS			  (gsk_socket_address_get_type ())
#define GSK_SOCKET_ADDRESS(obj)                   (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_SOCKET_ADDRESS, GskSocketAddress))
#define GSK_SOCKET_ADDRESS_CLASS(klass)           (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_SOCKET_ADDRESS, GskSocketAddressClass))
#define GSK_SOCKET_ADDRESS_GET_CLASS(obj)         (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_SOCKET_ADDRESS, GskSocketAddressClass))
#define GSK_IS_SOCKET_ADDRESS(obj)                (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_SOCKET_ADDRESS))
#define GSK_IS_SOCKET_ADDRESS_CLASS(klass)        (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_SOCKET_ADDRESS))
#define GSK_TYPE_SOCKET_ADDRESS_IPV4              (gsk_socket_address_ipv4_get_type ())
#define GSK_SOCKET_ADDRESS_IPV4(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_SOCKET_ADDRESS_IPV4, GskSocketAddressIpv4))
#define GSK_SOCKET_ADDRESS_IPV4_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_SOCKET_ADDRESS_IPV4, GskSocketAddressIpv4Class))
#define GSK_SOCKET_ADDRESS_IPV4_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_SOCKET_ADDRESS_IPV4, GskSocketAddressIpv4Class))
#define GSK_IS_SOCKET_ADDRESS_IPV4(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_SOCKET_ADDRESS_IPV4))
#define GSK_IS_SOCKET_ADDRESS_IPV4_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_SOCKET_ADDRESS_IPV4))
#define GSK_TYPE_SOCKET_ADDRESS_IPV6              (gsk_socket_address_ipv6_get_type ())
#define GSK_SOCKET_ADDRESS_IPV6(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_SOCKET_ADDRESS_IPV6, GskSocketAddressIpv6))
#define GSK_SOCKET_ADDRESS_IPV6_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_SOCKET_ADDRESS_IPV6, GskSocketAddressIpv6Class))
#define GSK_SOCKET_ADDRESS_IPV6_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_SOCKET_ADDRESS_IPV6, GskSocketAddressIpv6Class))
#define GSK_IS_SOCKET_ADDRESS_IPV6(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_SOCKET_ADDRESS_IPV6))
#define GSK_IS_SOCKET_ADDRESS_IPV6_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_SOCKET_ADDRESS_IPV6))
#define GSK_TYPE_SOCKET_ADDRESS_LOCAL		  (gsk_socket_address_local_get_type ())
#define GSK_SOCKET_ADDRESS_LOCAL(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_SOCKET_ADDRESS_LOCAL, GskSocketAddressLocal))
#define GSK_SOCKET_ADDRESS_LOCAL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_SOCKET_ADDRESS_LOCAL, GskSocketAddressLocalClass))
#define GSK_SOCKET_ADDRESS_LOCAL_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_SOCKET_ADDRESS_LOCAL, GskSocketAddressLocalClass))
#define GSK_IS_SOCKET_ADDRESS_LOCAL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_SOCKET_ADDRESS_LOCAL))
#define GSK_IS_SOCKET_ADDRESS_LOCAL_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_SOCKET_ADDRESS_LOCAL))
#define GSK_TYPE_SOCKET_ADDRESS_ETHERNET			(gsk_socket_address_ethernet_get_type ())
#define GSK_SOCKET_ADDRESS_ETHERNET(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_SOCKET_ADDRESS_ETHERNET, GskSocketAddressEthernet))
#define GSK_SOCKET_ADDRESS_ETHERNET_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_SOCKET_ADDRESS_ETHERNET, GskSocketAddressEthernetClass))
#define GSK_SOCKET_ADDRESS_ETHERNET_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_SOCKET_ADDRESS_ETHERNET, GskSocketAddressEthernetClass))
#define GSK_IS_SOCKET_ADDRESS_ETHERNET(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_SOCKET_ADDRESS_ETHERNET))
#define GSK_IS_SOCKET_ADDRESS_ETHERNET_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_SOCKET_ADDRESS_ETHERNET))


/* Useful quarks (for g_object_get_qdata) */
#define GSK_SOCKET_ADDRESS_REMOTE_QUARK	(gsk_socket_address_get_remote_quark())
#define GSK_SOCKET_ADDRESS_LOCAL_QUARK	(gsk_socket_address_get_local_quark())

/* --- structures --- */
struct _GskSocketAddressClass 
{
  GObjectClass object_class;
  gsize sizeof_native_address;
  gint protocol_family;		/* eg PF_INET, PF_LOCAL etc */
  gint address_family;		/* eg AF_INET, AF_LOCAL etc */
  gboolean (*to_native)  (GskSocketAddress *address,
			  gpointer          output);
  gboolean (*from_native)(GskSocketAddress *address,
			  gconstpointer     sockaddr_data,
			  gsize             sockaddr_length);
  char    *(*to_string)  (GskSocketAddress *address);
  /* note: both addresses will be of exactly the same type */
  gboolean (*equals)     (GskSocketAddress *addr1,
			  GskSocketAddress *addr2);
  guint    (*hash)       (GskSocketAddress *addr);
};
struct _GskSocketAddress 
{
  GObject      object;
};

struct _GskSocketAddressIpv4Class 
{
  GskSocketAddressClass socket_address_class;
};
struct _GskSocketAddressIpv4 
{
  GskSocketAddress      socket_address;
  guint8 ip_address[4];
  guint16 ip_port;
};

/* aka 'unix-domain addresses' */
struct _GskSocketAddressLocalClass 
{
  GskSocketAddressClass socket_address_class;
  guint max_path_length;
};
struct _GskSocketAddressLocal 
{
  GskSocketAddress      socket_address;
  char *path;
};

/* ipv6 socket addresses, where available */
struct _GskSocketAddressIpv6Class 
{
  GskSocketAddressClass socket_address_class;
};
struct _GskSocketAddressIpv6
{
  GskSocketAddress      socket_address;
  guint16 port;
  guint8 address[16];

  guint32 flow_info;	/* TODO figure this out */
  guint scope_id;	/* TODO figure this out */
};

struct _GskSocketAddressEthernetClass 
{
  GskSocketAddressClass socket_address_class;
};
struct _GskSocketAddressEthernet 
{
  GskSocketAddress      socket_address;
  guint8                mac_address[6];
};


/* --- prototypes --- */
GskSocketAddress *gsk_socket_address_from_native   (gconstpointer native_data,
						    gsize    native_size);
gint              gsk_socket_address_protocol_family(GskSocketAddress *address);
gint              gsk_socket_address_address_family(GskSocketAddress *address);
guint             gsk_socket_address_sizeof_native (GskSocketAddress *address);
gboolean          gsk_socket_address_to_native     (GskSocketAddress *address,
						    gpointer          output,
						    GError          **error);
char             *gsk_socket_address_to_string     (GskSocketAddress *address);


/* ipv4 specific */

#define gsk_socket_address_ipv4_localhost(port) \
	gsk_socket_address_ipv4_new (gsk_ipv4_ip_address_loopback, (port))

/* (ip_addr is 4-bytes long) */
GskSocketAddress *gsk_socket_address_ipv4_new      (const guint8     *ip_address,
						    guint16           port);

/* ipv6 specific */
gboolean gsk_socket_address_system_supports_ipv6 (void) G_GNUC_CONST;

/* (addr is 16-bytes long) */
GskSocketAddress *gsk_socket_address_ipv6_new      (const guint8     *address,
						    guint16           port);

/* local (unix) specific */
GskSocketAddress *gsk_socket_address_local_new     (const char       *path);

/* ethernet specific */
GskSocketAddress *gsk_socket_address_ethernet_new  (const guint8     *mac_addr);

#ifndef GSK_DISABLE_DEPRECATED
#define gsk_socket_address_new_ethernet gsk_socket_address_ethernet_new
#define gsk_socket_address_new_local    gsk_socket_address_local_new
#endif

/* --- implementing new sock-addr types --- */
/* call from the relevant class's class_init */
void  gsk_socket_address_register_subclass (GskSocketAddressClass *klass);


/* --- connecting to a socket-address --- */
/* gsk_socket_address_connect_fd() returns a file descriptor.
 * if it returns >= 0:
 *   if *is_connected, the fd is ready to go.
 *   if !*is_connected, you should watch for GSK_SOCKET_ADDRESS_CONNECT_EVENTS.
 *   when an event occurs call gsk_socket_address_finish_fd().  if it returns
 *   TRUE the fd is ready.  if it returns FALSE:
 *      if *error != NULL, then an error occurred.
 *      if *error == NULL, then repeat waiting for an event.
 * if it returns -1, then an error occurred immediately.
 */
int               gsk_socket_address_connect_fd    (GskSocketAddress *address,
						    gboolean         *is_connected,
						    GError          **error);

/* NOTE: this function could also be called gsk_fd_finish_connecting();
   it has nothing to do with GskSocketAddress.
   it just pairs with gsk_socket_address_connect_fd(). */
gboolean          gsk_socket_address_finish_fd     (int               fd,
						    GError          **error);

/* --- for making a hash-table of addresses --- */
gboolean          gsk_socket_address_equals        (gconstpointer     address_a_ptr,
						    gconstpointer     address_b_ptr);
guint             gsk_socket_address_hash          (gconstpointer     address_ptr);


/*< private >*/
GQuark gsk_socket_address_get_remote_quark ();
GQuark gsk_socket_address_get_local_quark ();
GType gsk_socket_address_get_type(void) G_GNUC_CONST;
GType gsk_socket_address_ipv4_get_type(void) G_GNUC_CONST;
GType gsk_socket_address_ipv6_get_type(void) G_GNUC_CONST;
GType gsk_socket_address_local_get_type(void) G_GNUC_CONST;
GType gsk_socket_address_ethernet_get_type(void) G_GNUC_CONST;


G_END_DECLS

#endif
