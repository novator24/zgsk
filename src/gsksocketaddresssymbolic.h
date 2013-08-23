/* "symbolic" addresses */

#ifndef __GSK_SOCKET_ADDRESS_SYMBOLIC_H_
#define __GSK_SOCKET_ADDRESS_SYMBOLIC_H_

#include "gsksocketaddress.h"

typedef struct _GskSocketAddressSymbolicClass GskSocketAddressSymbolicClass;
typedef struct _GskSocketAddressSymbolic GskSocketAddressSymbolic;
typedef struct _GskSocketAddressSymbolicIpv4Class GskSocketAddressSymbolicIpv4Class;
typedef struct _GskSocketAddressSymbolicIpv4 GskSocketAddressSymbolicIpv4;

G_BEGIN_DECLS

GType gsk_socket_address_symbolic_get_type(void) G_GNUC_CONST;
GType gsk_socket_address_symbolic_ipv4_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_SOCKET_ADDRESS_SYMBOLIC              (gsk_socket_address_symbolic_get_type ())
#define GSK_SOCKET_ADDRESS_SYMBOLIC(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_SOCKET_ADDRESS_SYMBOLIC, GskSocketAddressSymbolic))
#define GSK_SOCKET_ADDRESS_SYMBOLIC_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_SOCKET_ADDRESS_SYMBOLIC, GskSocketAddressSymbolicClass))
#define GSK_SOCKET_ADDRESS_SYMBOLIC_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_SOCKET_ADDRESS_SYMBOLIC, GskSocketAddressSymbolicClass))
#define GSK_IS_SOCKET_ADDRESS_SYMBOLIC(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_SOCKET_ADDRESS_SYMBOLIC))
#define GSK_IS_SOCKET_ADDRESS_SYMBOLIC_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_SOCKET_ADDRESS_SYMBOLIC))
#define GSK_TYPE_SOCKET_ADDRESS_SYMBOLIC_IPV4              (gsk_socket_address_symbolic_ipv4_get_type ())
#define GSK_SOCKET_ADDRESS_SYMBOLIC_IPV4(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_SOCKET_ADDRESS_SYMBOLIC_IPV4, GskSocketAddressSymbolicIpv4))
#define GSK_SOCKET_ADDRESS_SYMBOLIC_IPV4_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_SOCKET_ADDRESS_SYMBOLIC_IPV4, GskSocketAddressSymbolicIpv4Class))
#define GSK_SOCKET_ADDRESS_SYMBOLIC_IPV4_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_SOCKET_ADDRESS_SYMBOLIC_IPV4, GskSocketAddressSymbolicIpv4Class))
#define GSK_IS_SOCKET_ADDRESS_SYMBOLIC_IPV4(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_SOCKET_ADDRESS_SYMBOLIC_IPV4))
#define GSK_IS_SOCKET_ADDRESS_SYMBOLIC_IPV4_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_SOCKET_ADDRESS_SYMBOLIC_IPV4))

typedef void (*GskSocketAddressSymbolicResolveFunc) (GskSocketAddressSymbolic *orig,
                                                     GskSocketAddress         *resolved,
                                                     gpointer                  user_data);
typedef void (*GskSocketAddressSymbolicErrorFunc)   (GskSocketAddressSymbolic *orig,
                                                     const GError             *error,
                                                     gpointer                  user_data);

struct _GskSocketAddressSymbolicClass
{
  GskSocketAddressClass socket_address_class;
  gpointer (*create_name_resolver) (GskSocketAddressSymbolic *symbolic);
  void     (*start_resolution)     (GskSocketAddressSymbolic *,
                                    gpointer                  name_resolver,
                                    GskSocketAddressSymbolicResolveFunc r,
                                    GskSocketAddressSymbolicErrorFunc e,
                                    gpointer                  user_data,
                                    GDestroyNotify            destroy);
  void     (*cancel_resolution)    (GskSocketAddressSymbolic *symbolic,
                                    gpointer                  name_resolver);

};
struct _GskSocketAddressSymbolic
{
  GskSocketAddress      socket_address;
  char                 *name;
};
struct _GskSocketAddressSymbolicIpv4Class
{
  GskSocketAddressSymbolicClass base_class;
};
struct _GskSocketAddressSymbolicIpv4
{
  GskSocketAddressSymbolic base_instance;
  guint16                  port;
};

GskSocketAddress *gsk_socket_address_symbolic_ipv4_new (const char *name,
                                                        guint16     port);


gpointer gsk_socket_address_symbolic_create_name_resolver
                                     (GskSocketAddressSymbolic *symbolic);
void     gsk_socket_address_symbolic_start_resolution 
                                     (GskSocketAddressSymbolic *symbolic,
                                      gpointer                  name_resolver,
                                      GskSocketAddressSymbolicResolveFunc r,
                                      GskSocketAddressSymbolicErrorFunc e,
                                      gpointer                  user_data,
                                      GDestroyNotify            destroy);
void     gsk_socket_address_symbolic_cancel_resolution
                                     (GskSocketAddressSymbolic *symbolic,
                                      gpointer                  name_resolver);

G_END_DECLS

#endif /* __GSK_SOCKET_ADDRESS_SYMBOLIC_H_ */
