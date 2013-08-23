#ifndef __GSK_NAMED_CONFIG_H_
#define __GSK_NAMED_CONFIG_H_

#include "gsksimpleacl.h"
#include <stdio.h>

G_BEGIN_DECLS

/* --- types of zones (straight from bind's configuration) --- */
typedef enum
{
  /* owns a master cache */
  GSK_NAMED_MASTER,

  /* synchronizes with a master cache using zone transfers */
  GSK_NAMED_SLAVE,

  /* synchronizes only NS records from a master host */
  GSK_NAMED_STUB,

  /* forwards all requests for this zone to the listed IPs */
  GSK_NAMED_FORWARD,

  /* caching nameserver behavior */
  GSK_NAMED_HINT
} GskNamedZoneType;

typedef enum
{
  /* Don't do name checking on this type of record. */
  GSK_NAMED_NAME_CHECK_NONE,

  /* Warn (in the logs) but proceed normally if the name check fails. */
  GSK_NAMED_NAME_CHECK_WARN,

  /* Return failure (stop the transaction) if the name check fails. */
  GSK_NAMED_NAME_CHECK_FAIL
} GskNamedNameCheckMode;

typedef struct _GskNamedZone GskNamedZone;
typedef struct _GskNamedLogCategory GskNamedLogCategory;
typedef struct _GskNamedConfig GskNamedConfig;
typedef struct _GskNamedLogChannel GskNamedLogChannel;
typedef struct _GskNamedRemoteServer GskNamedRemoteServer;

struct _GskNamedZone
{
  char *zone_name;

  GskNamedZoneType zone_type;

  char *file;
  char *ixfr_base;

  GskSimpleAcl *may_update;
  GskSimpleAcl *may_query;
  GskSimpleAcl *may_transfer;

  gboolean dialup;
  gboolean notify;

  /* --- for secondaries: where to transfer the master list from --- */
  gboolean has_transfer_source;
  guint8 transfer_source[4];

  /* --- name checking --- */
  GskNamedNameCheckMode name_checking;

  /* --- forwarding --- */
  /* IP addresses to which to forward a request from this zone. */
  GSList *forward_ip_addrs;

  /* the `forward' field: whether to try a normal 
   * lookup after forwarding fails. */
  gboolean forward_only;

  GSList *also_notify;
};

/* information about a remote nameserver (the `server' config entries) */
struct _GskNamedRemoteServer
{
  /* must be first element */
  guint8   ip_address[4];

  /* boolean values. */
  guint8   is_bogus;
  guint8   support_ixfr;
  guint8   support_many_answer_xfers;

  guint    max_transfers;
};

typedef enum
{
  GSK_NAMED_LOG_CHANNEL_FILE,
  GSK_NAMED_LOG_CHANNEL_SYSLOG,
  GSK_NAMED_LOG_CHANNEL_NULL,
} GskNamedLogChannelStyle;

struct _GskNamedLogChannel
{
  char *log_channel_name;
  GskNamedLogChannelStyle channel_style;
  union
  {
    struct {
      char *filename;
      FILE *fp;
      guint size; /* may be 0 for no-limit */
    } file_style;
    struct {
      /* 3rd parameter to openlog(3) */
      int facility;
      /* 1st parameter to syslog(3) */
      int priority;
    } syslog_style;
  } info;

  char *severity;
  guint print_severity : 1;
  guint print_time : 1;
  guint print_category : 1;
};

struct _GskNamedLogCategory
{
  char *log_category;
  GSList       *channels;
};

struct _GskNamedConfig
{
  /* map from an ACL-name to that ACL */
  GHashTable   *acls_by_name;

  /* map from zone-name to a GskNamedZone */
  GHashTable   *zones_by_name;

  /* map from log-channel-name to GskNameLogChannel */
  GHashTable   *log_channels_by_name;

  /* map from log-category-name to GskNameLogCategory */
  GHashTable   *log_categories_by_name;

  /* Map from nameservers to IP number (key is a poitner to 4-guint8's) */
  GHashTable   *servers_by_ip;

  gint         debug_level;

  /* Options from the options {} section of named.conf */
  char        *version_id;
  char        *directory_path;
  char        *named_xfer_path;
  char        *dump_file_path;
  char        *memstats_file_path;
  char        *pid_file_path;
  char        *stats_file_path;

  /* booleans from the options{} section */
  guint8       auth_nxdomain;
  guint8       deallocate_on_exit;
  guint8       dialup;
  guint8       fake_iquery;
  guint8       fetch_glue;
  guint8       has_old_clients;
  guint8       host_statistics;
  guint8       maintain_ixfr_base;
  guint8       notify;
  guint8       recursion;
  guint8       rfc2308_type1;
  guint8       use_id_pool;
  guint8       treat_cr_as_space;
  guint8       forward_only;
  guint8       support_many_answer_xfers;

  GSList      *also_notify;
  GSList      *forward_ip_addrs;

  /* see named documentation */
  guint        lame_ttl;
  guint        max_transfer_time_in;
  guint        max_ncache_ttl;
  guint        min_roots;
  guint        serial_queries;
  guint        transfers_in;
  guint        transfers_out;
  guint        transfers_per_ns;
  guint        max_ixfr_log_size;
  guint        coresize;
  guint        datasize;
  guint        max_files_size;
  guint        stacksize;
  guint        heartbeat_interval;
  guint        interface_interval;
  guint        statistics_interval;

  /* --- name checking --- */
  GskNamedNameCheckMode master_name_checking;
  GskNamedNameCheckMode slave_name_checking;
  GskNamedNameCheckMode response_name_checking;

  GskSimpleAcl *may_query;
  GskSimpleAcl *may_transfer;
  GskSimpleAcl *may_recurse;
  GskSimpleAcl *blackhole;
  GskSimpleAcl *topology;
  GskSimpleAcl *sortlist;
};

GskNamedConfig * gsk_named_config_parse       (const char     *bind_filename);
void             gsk_named_config_destroy     (GskNamedConfig *config);

void             gsk_named_config_log         (GskNamedConfig *config,
                                               const char     *category,
                                               const char     *format,
                                               ...);
void             gsk_named_debug_log          (GskNamedConfig *config,
                                               int             debug_level,
                                               const char     *format,
                                               ...);
GskNamedZone *   gsk_named_config_find_zone   (GskNamedConfig  *config,
			                       const char      *domain_name);
G_END_DECLS

#endif
