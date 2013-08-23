#ifndef __GSK_DNS_LOCAL_RESOLVER_H_
#define __GSK_DNS_LOCAL_RESOLVER_H_

#include "gskdnsrrcache.h"
#include "gskdnsresolver.h"

G_BEGIN_DECLS

typedef struct _GskDnsLocalResolverClass GskDnsLocalResolverClass;
typedef struct _GskDnsLocalResolver GskDnsLocalResolver;

GType gsk_dns_local_resolver_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_DNS_LOCAL_RESOLVER			(gsk_dns_local_resolver_get_type ())
#define GSK_DNS_LOCAL_RESOLVER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_DNS_LOCAL_RESOLVER, GskDnsLocalResolver))
#define GSK_DNS_LOCAL_RESOLVER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_DNS_LOCAL_RESOLVER, GskDnsLocalResolverClass))
#define GSK_DNS_LOCAL_RESOLVER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_DNS_LOCAL_RESOLVER, GskDnsLocalResolverClass))
#define GSK_IS_DNS_LOCAL_RESOLVER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_DNS_LOCAL_RESOLVER))
#define GSK_IS_DNS_LOCAL_RESOLVER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_DNS_LOCAL_RESOLVER))


GskDnsResolver *gsk_dns_local_resolver_new   (GskDnsRRCache      *rr_cache);

/* --- implementation details --- */
typedef enum
{
  GSK_DNS_LOCAL_NO_DATA,
  GSK_DNS_LOCAL_PARTIAL_DATA,
  GSK_DNS_LOCAL_NEGATIVE,
  GSK_DNS_LOCAL_SUCCESS
} GskDnsLocalResult;

GskDnsLocalResult gsk_dns_local_resolver_answer(GskDnsRRCache      *rr_cache,
					        GskDnsQuestion     *question,
				                GskDnsMessage      *results);
G_END_DECLS

#endif
