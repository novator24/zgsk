#ifndef __GSK_NAMED_RESOLVER_H_
#define __GSK_NAMED_RESOLVER_H_

#include "gsknamedconfig.h"

#include  <gsk/protocols/gskdnsinterfaces.h>
#include  <gsk/protocols/gskdnsrrcache.h>

G_BEGIN_DECLS

typedef struct _GskNamedResolver GskNamedResolver;
typedef struct _GskNamedResolverClass GskNamedResolverClass;

GtkType gsk_named_resolver_get_type();
#define GSK_TYPE_NAMED_RESOLVER			(gsk_named_resolver_get_type ())
#define GSK_NAMED_RESOLVER(obj)              (GTK_CHECK_CAST ((obj), GSK_TYPE_NAMED_RESOLVER, GskNamedResolver))
#define GSK_NAMED_RESOLVER_CLASS(klass)      (GTK_CHECK_CLASS_CAST ((klass), GSK_TYPE_NAMED_RESOLVER, GskNamedResolverClass))
#define GSK_NAMED_RESOLVER_GET_CLASS(obj)    (GSK_NAMED_RESOLVER_CLASS(GTK_OBJECT(obj)->klass))
#define GSK_IS_NAMED_RESOLVER(obj)           (GTK_CHECK_TYPE ((obj), GSK_TYPE_NAMED_RESOLVER))
#define GSK_IS_NAMED_RESOLVER_CLASS(klass)   (GTK_CHECK_CLASS_TYPE ((klass), GSK_TYPE_NAMED_RESOLVER))

struct _GskNamedResolverClass
{
  GtkObjectClass	object_class;
};
struct _GskNamedResolver
{
  GtkObject		object;

  GskNamedConfig       *config;
  GskDnsRRCache        *rr_cache;

  /* for recursive queries we cannot locally answer. */
  GskDnsResolver       *resolver;

  /* for setting up the server */
  GskDnsTransmitter    *transmitter;
  GskDnsReceiver       *receiver;

  /* we set this up before we resolve DNS packets,
     a pretty ugly hack though. */
  gboolean             has_tmp_ip_info;
  guint8               tmp_ip_info[4];
};

GskNamedResolver * gsk_dns_named_resolver_new (GskNamedConfig  *named_config,
			                       GskDnsRRCache   *rr_cache,
			                       GskDnsResolver  *resolver);

G_END_DECLS

#endif
