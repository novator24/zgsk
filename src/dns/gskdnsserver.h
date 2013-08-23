#ifndef __GSK_DNS_SERVER_H_
#define __GSK_DNS_SERVER_H_

#include "gskdnsresolver.h"
#include "../gskpacketqueue.h"

G_BEGIN_DECLS

typedef struct _GskDnsServerClass GskDnsServerClass;
typedef struct _GskDnsServer GskDnsServer;

GType gsk_dns_server_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_DNS_SERVER			(gsk_dns_server_get_type ())
#define GSK_DNS_SERVER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_DNS_SERVER, GskDnsServer))
#define GSK_DNS_SERVER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_DNS_SERVER, GskDnsServerClass))
#define GSK_DNS_SERVER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_DNS_SERVER, GskDnsServerClass))
#define GSK_IS_DNS_SERVER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_DNS_SERVER))
#define GSK_IS_DNS_SERVER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_DNS_SERVER))

GskDnsServer   *gsk_dns_server_new           (GskDnsResolver     *resolver,
					      GskPacketQueue     *packet_queue);
GskDnsResolver *gsk_dns_server_peek_resolver (GskDnsServer       *server);
void            gsk_dns_server_set_resolver  (GskDnsServer       *server,
					      GskDnsResolver     *resolver);

G_END_DECLS

#endif
