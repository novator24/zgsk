#ifndef __GSK_DNS_CLIENT_H_
#define __GSK_DNS_CLIENT_H_

#include "gskdns.h"
#include "gskdnsrrcache.h"
#include "../gskpacketqueue.h"
#include <glib-object.h>

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskDnsClient GskDnsClient;
typedef struct _GskDnsClientClass GskDnsClientClass;

/* --- type macros --- */
GType gsk_dns_client_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_DNS_CLIENT			(gsk_dns_client_get_type ())
#define GSK_DNS_CLIENT(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_DNS_CLIENT, GskDnsClient))
#define GSK_DNS_CLIENT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_DNS_CLIENT, GskDnsClientClass))
#define GSK_DNS_CLIENT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_DNS_CLIENT, GskDnsClientClass))
#define GSK_IS_DNS_CLIENT(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_DNS_CLIENT))
#define GSK_IS_DNS_CLIENT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_DNS_CLIENT))

/* --- prototypes --- */
typedef enum
{
  GSK_DNS_CLIENT_STUB_RESOLVER = (1<<0)
} GskDnsClientFlags;

GskDnsClient   *gsk_dns_client_new           (GskPacketQueue     *packet_queue,
					      GskDnsRRCache      *rr_cache,
					      GskDnsClientFlags   flags);
void            gsk_dns_client_add_searchpath(GskDnsClient       *client,
					      const char         *searchpath);
void            gsk_dns_client_add_ns        (GskDnsClient       *client,
					      GskSocketAddressIpv4 *address);
void            gsk_dns_client_set_cache     (GskDnsClient       *client,
					      GskDnsRRCache      *rr_cache);
void            gsk_dns_client_set_flags     (GskDnsClient       *client,
					      GskDnsClientFlags   flags);
GskDnsClientFlags gsk_dns_client_get_flags   (GskDnsClient       *client);
gboolean    gsk_dns_client_parse_system_files(GskDnsClient       *client);


G_END_DECLS

#endif
