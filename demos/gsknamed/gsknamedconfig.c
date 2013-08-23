#include "gsknamedconfig.h"
#include <gsk/protocols/gskdnsimplementations.h>
#include "textnode.h"
#include "gsksyslog.h"
#include <ctype.h>

/* --- helper functions --- */
/* XXX: lowercase_string and LOWER_CASE_COPY_ON_STACK are
        copied from gskdnsimplementations.c, hmm. */
static char * lowercase_string (char *out, const char *in)
{
  char *rv = out;
  while (*in != 0)
    {
      if ('A' <= *in && *in <= 'Z')
	*out = *in + ('a' - 'A');
      else
	*out = *in;
      out++;
      in++;
    }
  *out = 0;
  return rv;
}

/* Make a lower-cased copy of STR, on the stack. */
#define LOWER_CASE_COPY_ON_STACK(str)			\
	lowercase_string (alloca (strlen (str) + 1), str)

/* forward declarations */
static GskSimpleAcl * make_acl_from_node_list (GSList     *nodes,
					       GHashTable *acls_by_name);

/* --- textnode helpers --- */

/* list of TextNodes each of which should become an IP address */
static GSList *
parse_ip_addr_list (GSList *list)
{
  GSList *rv = NULL;
  while (list != NULL)
    {
      TextNode *node = list->data;
      const char *at;
      guint8 ip_addr[4];
      list = list->next;
      if (node->is_list)
	continue;
      at = node->info.name;
      if (gsk_dns_parse_ip_address (&at, ip_addr))
	rv = g_slist_prepend (rv, g_memdup (ip_addr, 4));
      else
	{
	  g_warning ("Error parsing IP Address from %s", node->info.name);
	}
    }
  return g_slist_reverse (rv);
}

static gboolean
parse_boolean (const char *txt)
{
  if (strcmp (txt, "1") == 0
   || strcasecmp (txt, "t") == 0
   || strcasecmp (txt, "true") == 0
   || strcasecmp (txt, "y") == 0
   || strcasecmp (txt, "yes") == 0)
    return TRUE;
  return FALSE;
}

static gboolean try_string_arg (const char *orig_str,
                                const char *cmd,
                                char      **option_in_out)
{
  int len = strlen (cmd);
  if (strncasecmp (orig_str, cmd, len) != 0
   || (orig_str[len] != ' ' && orig_str[len] != 0))
    return FALSE;
  orig_str += len;
  gsk_skip_whitespace (orig_str);
  g_free (*option_in_out);
  *option_in_out = g_strdup (orig_str);
  return TRUE;
}

static gboolean
try_ip_addr_list (const char    *name,
		  const char    *real_name,
		  GSList        *body,
		  GSList       **ip_addrs_out)
{
  int rn_length = strlen (real_name);
  if (strncasecmp (name, real_name, rn_length) != 0
   || name[rn_length] == 0
   || !isspace (name[rn_length]))
    return FALSE;
  *ip_addrs_out = g_slist_concat (*ip_addrs_out, 
				  parse_ip_addr_list (body));
  return TRUE;
}
static gboolean
try_boolean (const char   *arg,
	     const char   *real_name,
	     guint8       *bool_out)
{
  int rn_length = strlen (real_name);
  if (strncasecmp (arg, real_name, rn_length) != 0
   || arg[rn_length] == 0
   || !isspace (arg[rn_length]))
    return FALSE;
  *bool_out = parse_boolean (arg + rn_length + 1);
  return TRUE;
}
static gboolean
parse_size   (const char   *at,
	      guint        *size_out,
	      guint         default_val)
{
  guint rv;
  const char *endp;
  if (strncasecmp (at, "unlimited", 9) == 0)
    {
      *size_out = G_MAXINT;
      return TRUE;
    }

  if (strncasecmp (at, "default", 7) == 0)
    {
      *size_out = default_val;
      return TRUE;
    }

  if (!isdigit(*at))
    return FALSE;
  rv = strtoul (at, (char **) &endp, 10);
  if (*endp == 'k' || *endp == 'K')
    rv *= 1024;
  else if (*endp == 'm' || *endp == 'M')
    rv *= 1024 * 1024;
  else if (*endp == 'g' || *endp == 'G')
    rv *= 1024 * 1024 * 1024;
  *size_out = rv;
  return TRUE;
}

static gboolean
try_uint     (const char   *arg,
	      const char   *real_name,
	      guint        *out)
{
  int rn_length = strlen (real_name);
  if (strncasecmp (arg, real_name, rn_length) != 0
   || arg[rn_length] == 0
   || !isspace (arg[rn_length]))
    return FALSE;
  *out = (guint) strtoul (arg + rn_length + 1, NULL, 10);
  return TRUE;
}

static gboolean
try_size      (const char    *arg,
	       const char   *real_name,
	       guint         default_val,
	       guint        *out)
{
  int rn_length = strlen (real_name);
  if (strncasecmp (arg, real_name, rn_length) != 0
   || arg[rn_length] == 0
   || !isspace (arg[rn_length]))
    return FALSE;
  return parse_size (arg, out, default_val);
}

static gboolean
try_acl       (const char    *arg,
	       const char    *real_name,
	       GSList        *body,
	       GHashTable    *acls_by_name,
	       GskSimpleAcl **in_out)
{
  GskSimpleAcl *acl;
  int rn_length = strlen (real_name);
  if (strncasecmp (arg, real_name, rn_length) != 0
   || arg[rn_length] == 0
   || !isspace (arg[rn_length]))
    return FALSE;

  acl = make_acl_from_node_list (body, acls_by_name);
  if (acl == NULL)
    {
      g_warning ("error parsing ACL from config");
      return FALSE;
    }
  if (*in_out != NULL)
    gsk_simple_acl_destroy (*in_out);
  *in_out = acl;
  return TRUE;
}

/*
 *                   __ _                       _   _
 *   ___ ___  _ __  / _(_) __ _ _   _ _ __ __ _| |_(_) ___  _ __
 *  / __/ _ \| '_ \| |_| |/ _` | | | | '__/ _` | __| |/ _ \| '_ \
 * | (_| (_) | | | |  _| | (_| | |_| | | | (_| | |_| | (_) | | | |
 *  \___\___/|_| |_|_| |_|\__, |\__,_|_|  \__,_|\__|_|\___/|_| |_|
 *                        |___/
 */
/*
 *                   __ _                              _
 *   ___ ___  _ __  / _(_) __ _   _ __   __ _ _ __ ___(_)_ __   __ _
 *  / __/ _ \| '_ \| |_| |/ _` | | '_ \ / _` | '__/ __| | '_ \ / _` |
 * | (_| (_) | | | |  _| | (_| | | |_) | (_| | |  \__ \ | | | | (_| |
 *  \___\___/|_| |_|_| |_|\__, | | .__/ \__,_|_|  |___/_|_| |_|\__, |
 *                        |___/  |_|                           |___/
 */
static GskNamedConfig *
gsk_named_config_new ()
{
  GskNamedConfig *config = g_new (GskNamedConfig, 1);
  config->acls_by_name = g_hash_table_new (g_str_hash, g_str_equal);
  config->zones_by_name = g_hash_table_new (g_str_hash, g_str_equal);
  config->log_channels_by_name = g_hash_table_new (g_str_hash, g_str_equal);
  config->log_categories_by_name = g_hash_table_new (g_str_hash, g_str_equal);
  config->debug_level = 0;
  config->version_id = NULL;
  config->directory_path = NULL;
  config->named_xfer_path = NULL;
  config->dump_file_path = NULL;
  config->memstats_file_path = NULL;
  config->pid_file_path = NULL;
  config->stats_file_path = NULL;

  config->master_name_checking = GSK_NAMED_NAME_CHECK_FAIL;
  config->slave_name_checking = GSK_NAMED_NAME_CHECK_WARN;
  config->response_name_checking = GSK_NAMED_NAME_CHECK_NONE;

  config->also_notify = NULL;
  config->forward_ip_addrs = NULL;

  config->lame_ttl = 600;
  config->max_ncache_ttl = 10800;
  config->min_roots = 2;
  config->serial_queries = 4;
  config->transfers_in = 10;
  config->transfers_out = 10;
  config->transfers_per_ns = 2;
  config->max_ixfr_log_size = 1024;
  config->coresize = 16384;
  config->datasize = 16384;
  config->max_files_size = 16384;
  config->stacksize = 8192;


  /* these 4 numbers are in minutes! */
  config->max_transfer_time_in = 120;
  config->heartbeat_interval = 60;
  config->interface_interval = 60;
  config->statistics_interval = 60;

  config->forward_only = 0;
  config->auth_nxdomain = 1;
  config->deallocate_on_exit = 1;
  config->dialup = 0;
  config->fake_iquery = 0;
  config->fetch_glue = 1;
  config->has_old_clients = 1;
  config->host_statistics = 1;
  config->maintain_ixfr_base = 0;
  config->notify = 1;
  config->recursion = 1;
  config->rfc2308_type1 = 1;
  config->use_id_pool = 0;
  config->treat_cr_as_space = 0;

  config->may_query = NULL;
  config->may_transfer = NULL;
  config->may_recurse = NULL;
  config->blackhole = NULL;
  config->topology = NULL;
  config->sortlist = NULL;

  return config;
}

/* --- Section `acl'  :  Define Access Control Lists. --- */
static void
gsk_named_config_add_acl (GskNamedConfig *config,
			  const char     *acl_name,
			  GskSimpleAcl   *acl)
{
  GskSimpleAcl *old_acl = g_hash_table_lookup (config->acls_by_name, acl_name);
  if (old_acl == NULL)
    g_hash_table_insert (config->acls_by_name, g_strdup (acl_name), acl);
  else
    {
      g_hash_table_insert (config->acls_by_name, (gpointer) acl_name, acl);
      gsk_simple_acl_destroy (old_acl);
    }
}

static void
gsk_named_config_defaults (GskNamedConfig *config)
{
  gsk_named_config_add_acl (config, "any", gsk_simple_acl_new (TRUE));
  gsk_named_config_add_acl (config, "none", gsk_simple_acl_new (FALSE));
}

static gboolean
parse_one_acl_entry (GskSimpleAcl   *acl,
		     const char     *rule,
		     GHashTable     *acls_by_name)
{
  gboolean negated = FALSE;
  if (*rule == '!')
    {
      negated = TRUE;
      rule++;
      gsk_skip_whitespace (rule);
    }

  if (isdigit (*rule))
    {
      guint8 addr[4];
      int bits;
      const char *at = rule;
      int i = 0;
      /* ip-address or ip-prefix */
      while (i < 4)
	{
	  const char *endp = at;
	  addr[i++] = (guint8) strtol (rule, (char **) &endp, 10);
	  if (rule == endp)
	    return FALSE;
	  if (*endp == '.')
	    at = endp + 1;
	  else if (*endp == '/')
	    {
	      at = endp;
	      break;
	    }
	  else
	    at = endp;
	  gsk_skip_whitespace (at);
	  if (!isdigit (*at))
	    break;
	}
      if (*at == '/')
	bits = strtol (at + 1, NULL, 10);
      else
	bits = i * 8;
      if (negated)
	gsk_simple_acl_append_reject (acl, addr, bits);
      else
	gsk_simple_acl_append_accept (acl, addr, bits);
    }
  else
    {
      const char *end_rule = rule;
      char *name;
      GskSimpleAcl *sub_acl;
      /* another ACL */
      gsk_skip_nonwhitespace (end_rule);
      name = g_new (char, end_rule - rule + 1);
      memcpy (name, rule, end_rule - rule);
      name[end_rule - rule] = 0;
      sub_acl = g_hash_table_lookup (acls_by_name, name);
      if (sub_acl == NULL)
	{
	  g_warning ("No Acl matching `%s' defining another acl.", name);
	  return FALSE;
	}
      gsk_simple_acl_append_sub (acl, negated, sub_acl);
    }
  return TRUE;
}

static GskSimpleAcl *
make_acl_from_node_list (GSList *nodes, GHashTable *acls_by_name)
{
  GskSimpleAcl *acl = gsk_simple_acl_new (FALSE);
  while (nodes != NULL)
    {
      TextNode *node = nodes->data;
      if (node->is_list)
	{
	  g_warning ("found nesting node in `acl': not allowed");
	  gsk_simple_acl_destroy (acl);
	  return NULL;
	}
      if (!parse_one_acl_entry (acl, node->info.name, acls_by_name))
	{
	  g_warning ("error parsing acl from `%s'", node->info.name);
	  gsk_simple_acl_destroy (acl);
	  return NULL;
	}
      nodes = nodes->next;
    }
  return acl;
}

/* --- Section `zone'  :  Define Named Zones --- */

static void
zone_shallow_free (GskNamedZone *zone)
{
  g_free (zone->zone_name);
  g_free (zone->file);
  g_slist_foreach (zone->forward_ip_addrs, (GFunc) g_free, NULL);
  g_slist_free (zone->forward_ip_addrs);
  g_free (zone);
}

static gboolean 
parse_zone_entry (GskNamedZone   *zone,
		  TextNode       *node,
                  TextNode       *next_node,
		  GskNamedConfig *config)
{
  const char *name;
  const char *arg;
  if (node->is_list)
    return FALSE;
  name = node->info.name;
  arg = name;
  gsk_skip_nonwhitespace (arg);
  gsk_skip_whitespace (arg);

  if (strncasecmp (name, "type ", 5) == 0)
    {
      if (strncasecmp (arg, "master", 6) == 0)
	zone->zone_type = GSK_NAMED_MASTER;
      else if (strncasecmp (arg, "slave", 5) == 0)
	zone->zone_type = GSK_NAMED_SLAVE;
      else if (strncasecmp (arg, "stub", 5) == 0)
	zone->zone_type = GSK_NAMED_STUB;
      else if (strncasecmp (arg, "hint", 5) == 0)
	zone->zone_type = GSK_NAMED_HINT;
      else if (strncasecmp (arg, "forward", 5) == 0)
	zone->zone_type = GSK_NAMED_FORWARD;
      else
	{
	  g_warning ("unknown zone-type `%s'", arg);
	  return FALSE;
	}
      return TRUE;
    }
  if (strncasecmp (name, "forward ", 8) == 0)
    {
      zone->forward_only = (strcasecmp (name, "forward only") == 0);
      return TRUE;
    }
  if (strncasecmp (name, "forwarders", 8) == 0)
    {
      GSList *nodes;
      if (next_node == NULL || !next_node->is_list)
        {
          g_warning ("expected list argument to forwarders");
          return FALSE;
        }
      nodes = next_node->info.list;
      zone->forward_ip_addrs = g_slist_concat (zone->forward_ip_addrs,
					       parse_ip_addr_list (nodes));
      return TRUE;
    }
  if (strncasecmp (name, "check-names ", 12) == 0)
    {
      if (strcasecmp (name, "check-names warn") == 0)
        zone->name_checking = GSK_NAMED_NAME_CHECK_WARN;
      else if (strcasecmp (name, "check-names fail") == 0)
        zone->name_checking = GSK_NAMED_NAME_CHECK_FAIL;
      else if (strcasecmp (name, "check-names ignore") == 0)
        zone->name_checking = GSK_NAMED_NAME_CHECK_NONE;
      else
        {
          g_warning ("error parsing check-names argument: %s", name+12);
          return FALSE;
        }
      return TRUE;
    }
  if (strcasecmp (name, "allow-update") == 0)
    {
      if (!next_node->is_list)
	{
	  g_warning ("%s requires a list argument for the acl", name);
	  return FALSE;
	}
      if (zone->may_update != NULL)
	gsk_simple_acl_destroy (zone->may_update);
      zone->may_update = make_acl_from_node_list (next_node->info.list,
						  config->acls_by_name);
      return TRUE;
    }
  if (strcasecmp (name, "allow-query") == 0)
    {
      if (!next_node->is_list)
	{
	  g_warning ("%s requires a list argument for the acl", name);
	  return FALSE;
	}
      if (zone->may_query != NULL)
	gsk_simple_acl_destroy (zone->may_query);
      zone->may_query = make_acl_from_node_list (next_node->info.list,
						 config->acls_by_name);
      return TRUE;
    }
  if (strcasecmp (name, "allow-transfer") == 0)
    {
      if (!next_node->is_list)
	{
	  g_warning ("%s requires a list argument for the acl", name);
	  return FALSE;
	}
      if (zone->may_query != NULL)
	gsk_simple_acl_destroy (zone->may_transfer);
      zone->may_transfer = make_acl_from_node_list (next_node->info.list,
						    config->acls_by_name);
      return TRUE;
    }
  if (strncasecmp (name, "dialup ", 7) == 0)
    {
      zone->dialup = parse_boolean (name + 7);
      return TRUE;
    }
  if (strncasecmp (name, "notify ", 7) == 0)
    {
      zone->notify = parse_boolean (name + 7);
      return TRUE;
    }
  if (try_ip_addr_list (name, "also-notify",
			next_node->info.list, &zone->also_notify))
    return TRUE;
  if (strncasecmp (name, "ixfr-base ", 10) == 0)
    {
      g_free (zone->ixfr_base);
      zone->ixfr_base = g_strstrip (g_strdup (name + 10));
      return TRUE;
    }

  return FALSE;
}

static gboolean
parse_zone_from_list (GskNamedConfig   *config,
		      GSList           *list)
{
  /* `zone ZONE-NAME CLASS { BODY }' */
  TextNode *first_node, *body_node;
  GskNamedZone *zone;
  GskNamedZone *old_zone;
  GSList *subnode_list;
  if (list == NULL || list->next == NULL)
    {
      g_warning ("too few arguments to `zone'");
      return FALSE;
    }
  first_node = list->data;
  if (list->next->next != NULL)
    {
      g_warning ("too many arguments to `zone'");
      return FALSE;
    }
  body_node = list->next->data;
  if (first_node->is_list || !body_node->is_list)
    {
      g_warning ("bad signature for nodes under `zone'");
      return FALSE;
    }
  
  zone = g_new (GskNamedZone, 1);

  /* copy the zone name and lower-case it. */
  {
    const char *txt = first_node->info.name;
    const char *end;
    gsk_skip_nonwhitespace (txt);
    gsk_skip_whitespace (txt);
    end = txt;
    gsk_skip_nonwhitespace (end);

    zone->zone_name = g_new (char, end - txt + 1);
    memcpy (zone->zone_name, txt, end - txt);
    zone->zone_name[end - txt] = 0;
  }

  /* XXX: we never parse the Class. */

  zone->file = NULL;
  zone->may_update = NULL;
  zone->may_query = NULL;
  zone->may_transfer = NULL;
  zone->dialup = FALSE;
  zone->notify = FALSE;
  zone->has_transfer_source = FALSE;
  zone->forward_ip_addrs = NULL;
  zone->forward_only = FALSE;
  zone->also_notify = NULL;

  for (subnode_list = body_node->info.list;
       subnode_list != NULL;
       subnode_list = subnode_list->next)
    {
      TextNode *subnode = subnode_list->data;
      TextNode *next;
      next = subnode_list->next != NULL ? subnode_list->next->data : NULL;
      if (!parse_zone_entry (zone, subnode, next, config))
	{
	  zone_shallow_free (zone);
	  return FALSE;
	}
    }

  old_zone = g_hash_table_lookup (config->zones_by_name, zone->zone_name);
  if (old_zone != NULL)
    {
      g_hash_table_remove (config->zones_by_name, zone->zone_name);
      zone_shallow_free (old_zone);
    }
  g_hash_table_insert (config->zones_by_name, zone->zone_name, zone);
  return TRUE;
}

/* --- Section `options'  :  Set global options */
static gboolean
parse_one_option(GskNamedConfig    *config,
		 const char        *arg,
		 GSList            *body)
{
  if (try_string_arg (arg, "version", &config->version_id)
   || try_string_arg (arg, "directory", &config->directory_path)
   || try_string_arg (arg, "named_xfer", &config->named_xfer_path)
   || try_string_arg (arg, "dump-file", &config->dump_file_path)
   || try_string_arg (arg, "memstatistics-file", &config->memstats_file_path)
   || try_string_arg (arg, "pid-file", &config->pid_file_path)
   || try_string_arg (arg, "statistics-file", &config->stats_file_path))
    return TRUE;
  if (try_boolean (arg, "auth-nxdomain", &config->auth_nxdomain)
   || try_boolean (arg, "deallocate-on-exit", &config->deallocate_on_exit)
   || try_boolean (arg, "dialup", &config->dialup)
   || try_boolean (arg, "fake-iquery", &config->fake_iquery)
   || try_boolean (arg, "fetch-glue", &config->fetch_glue)
   || try_boolean (arg, "has-old-clients", &config->has_old_clients)
   || try_boolean (arg, "host-statistics", &config->host_statistics)
   || try_boolean (arg, "maintain-ixfr-base", &config->maintain_ixfr_base)
   || try_boolean (arg, "notify", &config->notify)
   || try_boolean (arg, "recursion", &config->recursion)
   || try_boolean (arg, "rfc2308-type1", &config->rfc2308_type1)
   || try_boolean (arg, "use-id-pool", &config->use_id_pool)
   || try_boolean (arg, "treat-cr-as-space", &config->treat_cr_as_space))
    return TRUE;

  if (try_ip_addr_list (arg, "also-notify", body, &config->also_notify)
   || try_ip_addr_list (arg, "forwarders", body, &config->forward_ip_addrs))
    return TRUE;

  if (try_uint (arg, "lame-ttl", &config->lame_ttl)
   || try_uint (arg, "max-transfer-time-in", &config->max_transfer_time_in)
   || try_uint (arg, "max-ncache-ttl", &config->max_ncache_ttl)
   || try_uint (arg, "min-roots", &config->min_roots)
   || try_uint (arg, "serial-queries", &config->serial_queries)
   || try_uint (arg, "transfers-in", &config->transfers_in)
   || try_uint (arg, "transfers-out", &config->transfers_out)
   || try_uint (arg, "transfers-per-ns", &config->transfers_per_ns)
   || try_size (arg, "max-ixfr-log-size",8*1024*1024,&config->max_ixfr_log_size)
   || try_size (arg, "coresize", 8*1024*1024, &config->coresize)
   || try_size (arg, "datasize", 8*1024*1024, &config->datasize)
   || try_size (arg, "files", 16*1024*1024, &config->max_files_size)
   || try_size (arg, "stacksize", 8*1024*1024, &config->stacksize)
   || try_uint (arg, "heartbeat-interval", &config->heartbeat_interval)
   || try_uint (arg, "interface-interval", &config->interface_interval)
   || try_uint (arg, "statistics-interval", &config->statistics_interval))
    return TRUE;

  if (strcasecmp (arg, "forward only") == 0)
    {
      config->forward_only = TRUE;
      return TRUE;
    }
  if (strcasecmp (arg, "forward first") == 0)
    {
      config->forward_only = FALSE;
      return TRUE;
    }
  if (strncasecmp (arg, "check-names ", 12) == 0)
    {
      const char *at = arg + 12;
      GskNamedNameCheckMode *cm = NULL;
      if (strncasecmp (at, "master", 6) == 0)
	cm = &config->master_name_checking;
      else if (strncasecmp (at, "slave", 5) == 0)
	cm = &config->slave_name_checking;
      else if (strncasecmp (at, "response", 8) == 0)
	cm = &config->response_name_checking;
      else
	{
	  g_warning ("error parsing check-names in options section: %s", at);
	  return FALSE;
	}
      gsk_skip_nonwhitespace (at);
      gsk_skip_whitespace (at);
      if (strcasecmp (at, "warn") == 0)
        *cm = GSK_NAMED_NAME_CHECK_WARN;
      else if (strcasecmp (at, "fail") == 0)
        *cm = GSK_NAMED_NAME_CHECK_FAIL;
      else if (strcasecmp (at, "ignore") == 0)
        *cm = GSK_NAMED_NAME_CHECK_NONE;
      else
	{
	  g_warning ("error parsing type of name-checking in options");
	  return FALSE;
	}
      return TRUE;
    }

  {
    GHashTable *acl_hash = config->acls_by_name;
    if (try_acl (arg, "allow-query", body, acl_hash, &config->may_query)
     || try_acl (arg, "allow-transfer", body, acl_hash, &config->may_transfer)
     || try_acl (arg, "allow-recursion", body, acl_hash, &config->may_recurse)
     || try_acl (arg, "blackhole", body, acl_hash, &config->blackhole)
     || try_acl (arg, "topology", body, acl_hash, &config->topology)
     || try_acl (arg, "sortlist", body, acl_hash, &config->sortlist))
    return TRUE;
  }

  if (strcasecmp (arg, "transfer-format one-answer") == 0)
    {
      config->support_many_answer_xfers = 0;
      return TRUE;
    }
  if (strcasecmp (arg, "transfer-format many-answers") == 0)
    {
      config->support_many_answer_xfers = 1;
      return TRUE;
    }

  /* Documented options that we don't support. */
#if 0
  [ listen-on [ port ip_port ] { address_match_list }; ]
  [ query-source [ address ( ip_addr | * ) ] [ port ( ip_port | * ) ] ; ]
  [ transfer-source ip_addr; ]
  [ rrset-order { order_spec ; [ order_spec ; ... ] ] }; 
#endif

  return FALSE;
}

/* --- Section `logging'  :  Setup logging. */
static void
gsk_named_log_channel_destroy (GskNamedLogChannel *channel)
{
  g_free (channel->log_channel_name);
  g_free (channel->severity);

  switch (channel->channel_style)
    {
    case GSK_NAMED_LOG_CHANNEL_FILE:
      g_free (channel->info.file_style.filename);
      if (channel->info.file_style.fp != NULL)
	fclose (channel->info.file_style.fp);
      break;

    default:
      break;
    }
  g_free (channel);
}

static void
gsk_named_log_category_destroy (GskNamedLogCategory *category)
{
  g_free (category->log_category);
  g_slist_free (category->channels);
  g_free (category);
}

static GskNamedLogChannel *
parse_log_channel (const char   *log_name,
		   GSList       *body)
{
  const char *desc;
  GskNamedLogChannel *channel;
  if (body == NULL || ((TextNode *)(body->data))->is_list)
    {
      g_warning ("log channel %s did not contain one text entry", log_name);
      return NULL;
    }
  desc = ((TextNode *)(body->data))->info.name;

  channel = g_new0 (GskNamedLogChannel, 1);
  channel->log_channel_name = g_strdup (log_name);
  channel->channel_style = GSK_NAMED_LOG_CHANNEL_NULL;

  if (strncasecmp (desc, "file ", 5) == 0)
    {
      const char *end;
      /* file path_name
	        [ versions ( number | unlimited ) ]
		[ size size_spec ] */
      desc += 5;
      gsk_skip_whitespace (desc);
      end = desc;
      gsk_skip_nonwhitespace (end);
      if (end == desc)
	{
	  g_warning ("expected filename after `file'");
	  goto fail;
	}
      channel->channel_style = GSK_NAMED_LOG_CHANNEL_FILE;
      channel->info.file_style.filename = g_new (char, end - desc + 1);
      memcpy (channel->info.file_style.filename, desc, end - desc);
      channel->info.file_style.filename[end - desc] = 0;

      gsk_skip_whitespace (desc);
      while (*desc != 0)
	{
	  if (strncasecmp (desc, "versions ", 9) == 0)
	    {
	      gsk_skip_nonwhitespace (desc);
	      gsk_skip_whitespace (desc);

	      /* we ignore this field */
	      gsk_skip_nonwhitespace (desc);
	      gsk_skip_whitespace (desc);
	    }
	  else if (strncasecmp (desc, "size ", 5) == 0)
	    {
	      if (!parse_size (desc+5, &channel->info.file_style.size, 0))
		{
		  g_warning ("error parsing size in file log channel");
		  goto fail;
		}
	      desc += 5;
	      gsk_skip_nonwhitespace (desc);
	      gsk_skip_whitespace (desc);
	    }
	  else
	    {
	      /* ignore other junk */
	      gsk_skip_nonwhitespace (desc);
	      gsk_skip_whitespace (desc);
	    }
	}
    }
  else if (strncasecmp (desc, "syslog ", 7) == 0)
    {
      GskSyslogFacility facility;
      channel->channel_style = GSK_NAMED_LOG_CHANNEL_SYSLOG;
      desc += 7;
      gsk_skip_whitespace (desc);
      if (gsk_syslog_facility_parse (desc, &facility))
	channel->info.syslog_style.facility = facility;
    }
  else if (strncasecmp (desc, "null", 4) == 0)
    {
    }
  else
    {
      g_warning ("logging body `%s' unrecognized", desc);
      return NULL;
    }

  body = body->next;
  while (body != NULL)
    {
      TextNode *node = body->data;
      const char *opt;
      body = body->next;
      if (node->is_list)
        {
          g_warning ("logging channel {} must not contain substructure");
          goto fail;
        }
      opt = node->info.name;
      if (strncasecmp (opt, "severity ", 9) == 0)
        {
          g_free (channel->severity);
          channel->severity = g_strstrip (g_strdup (opt + 9));
        }
      else if (strncasecmp (opt, "print-category ", 15) == 0)
        {
          channel->print_category = parse_boolean (opt + 15);
        }
      else if (strncasecmp (opt, "print-severity ", 15) == 0)
        {
          channel->print_severity = parse_boolean (opt + 15);
        }
      else if (strncasecmp (opt, "print-time ", 11) == 0)
        {
          channel->print_time = parse_boolean (opt + 11);
        }
      else
        {
          g_warning ("unrecognized log channel option: %s", opt);
          goto fail;
        }
    }
  return channel;

fail:
  if (channel != NULL)
    gsk_named_log_channel_destroy (channel);
  return NULL;
}

static GskNamedLogCategory *
parse_log_category (const char   *log_name,
		    GSList       *body,
                    GHashTable   *channels_by_name)
{
  GSList *channels = NULL;
  GskNamedLogCategory *category;
  while (body != NULL)
    {
      TextNode *node = body->data;
      GskNamedLogChannel *channel;
      body = body->next;

      if (node->is_list)
	{
	  g_warning ("log-category may not contain substructure");
	  g_slist_free (channels);
	  return NULL;
	}
      channel = g_hash_table_lookup (channels_by_name, node->info.name);
      if (channel == NULL)
	{
	  g_warning ("log channel `%s' not found for category %s",
		     node->info.name, log_name);
	  g_slist_free (channels);
	  return NULL;
	}
      channels = g_slist_prepend (channels, channel);
    }
  category = g_new (GskNamedLogCategory, 1);
  category->channels = channels;
  category->log_category = g_strdup (log_name);
  return category;
}

static gboolean
parse_logging_option (GskNamedConfig    *config,
		      const char        *command,
		      GSList            *body)
{
  if (strncasecmp (command, "channel ", 8) == 0)
    {
      GskNamedLogChannel *channel = parse_log_channel (command + 9, body);
      GskNamedLogChannel *old_channel;
      if (channel == NULL)
	{
	  g_warning ("parsing channel (at %s) failed", command + 9);
	  return FALSE;
	}
      old_channel = g_hash_table_lookup (config->log_channels_by_name,
					 channel->log_channel_name);
      if (old_channel != NULL)
	{
	  g_hash_table_remove (config->log_channels_by_name,
			       channel->log_channel_name);
	  gsk_named_log_channel_destroy (old_channel);
	}
      g_hash_table_insert (config->log_channels_by_name,
			   channel->log_channel_name, channel);
      return TRUE;
    }
  else if (strncasecmp (command, "category ", 9) == 0)
    {
      GskNamedLogCategory *category;
      GskNamedLogCategory *old_category;
      category = parse_log_category (command + 9, body,
                                     config->log_channels_by_name);
      old_category = g_hash_table_lookup (config->log_categories_by_name,
					  category->log_category);
      if (category == NULL)
	{
	  g_warning ("parsing category (at %s) failed", command + 9);
	  return FALSE;
	}
      if (old_category != NULL)
	{
	  g_hash_table_remove (config->log_categories_by_name,
			       category->log_category);
	  gsk_named_log_category_destroy (old_category);
	}
      g_hash_table_insert (config->log_categories_by_name,
			   category->log_category, category);
      return TRUE;
    }
  else
    {
      g_warning ("expected channel or category (at `%s')", command);
      return FALSE;
    }
}

/* --- Section `server'  :  Remote nameserver information --- */
static GskNamedRemoteServer *
parse_remote_server (TextNode  *header,
		     TextNode  *body)
{

  GskNamedRemoteServer remote_server;
  GSList *list;
  if (header->is_list || !body->is_list)
    {
      g_warning ("bad syntax, around `server'");
      return NULL;
    }
  for (list = body->info.list; list != NULL; list = list->next)
    {
      TextNode *at = list->data;
      const char *txt;
      if (at->is_list)
	{
	  g_warning ("server section should not have any substructure");
	  return NULL;
	}
      txt = at->info.name;
      if (try_boolean (txt, "bogus", &remote_server.is_bogus)
       || try_boolean (txt, "support-ixfr", &remote_server.support_ixfr)
       || try_uint (txt, "max-transfers", &remote_server.max_transfers))
	continue;
      if (strncasecmp (txt, "transfers-format ", 17) == 0)
	{
	  txt += 17;
	  if (strcasecmp (txt, "one-answer") == 0)
	    remote_server.support_many_answer_xfers = 0;
	  else if (strcasecmp (txt, "many-answers") == 0)
	    remote_server.support_many_answer_xfers = 1;
	  else
	    {
	      g_warning ("either `one-answer' or `many-answers' expected");
	      return NULL;
	    }
	  continue;
	}

      g_warning ("expected command `%s' in server section", txt);
      return NULL;
    }
  return g_memdup (&remote_server, sizeof (remote_server));
}


/* --- parse the named.config --- */
GskNamedConfig *
gsk_named_config_parse (const char *bind_filename)
{
  GskNamedConfig *config;
  TextNode *node = NULL;
  TextNodeParser *parser;

  parser = text_node_parser_new (bind_filename);
  if (parser == NULL)
    {
      g_warning ("error opening %s", bind_filename);
      return NULL;
    }

  config = gsk_named_config_new ();
  gsk_named_config_defaults (config);

  while ((node = text_node_parser_parse (parser)) != NULL)
    {
      GSList *list;
      TextNode *header;
      if (!node->is_list || node->info.list == NULL)
	{
	  text_node_destroy (node);
	  continue;
	}
      list = node->info.list;
      header = list->data;
      if (header->is_list)
	{
	  text_node_destroy (node);
	  continue;
	}

      if (strncasecmp (header->info.name, "acl ", 4) == 0)
	{
	  TextNode *name_node, *body_node;
	  GskSimpleAcl *acl;
	  acl = make_acl_from_node_list (body_node->info.list,
					 config->acls_by_name);
	  if (acl == NULL)
	    {
	      g_warning ("error parsing acl");
	      goto fail;
	    }
	  gsk_named_config_add_acl (config, name_node->info.name, acl);
	  text_node_destroy (node);
	  continue;
	}

      if (strncasecmp (header->info.name, "zone ", 5) == 0)
	{
	  if (!parse_zone_from_list (config, list->next))
	    {
	      g_warning ("syntax error in `zone'");
	      goto fail;
	    }
	  text_node_destroy (node);
	  continue;
	}
      if (strncasecmp (header->info.name, "include ", 8) == 0)
	{
	  char *fname = g_strdup (header->info.name);
	  g_strstrip (fname);
	  if (!text_node_parser_include (parser, fname))
	    {
	      g_warning ("error including %s", fname);
	      goto fail;
	    }
	  continue;
	}
      if (strncasecmp (header->info.name, "key ", 4) == 0)
	{
	  g_warning ("We don't support the `key' section.  Sorry.");
	  goto fail;
	}
      if (strncasecmp (header->info.name, "logging ", 8) == 0)
	{
	  TextNode *body;
	  GSList *sublist;
	  if (list->next == NULL)
	    {
	      g_warning ("section `logging' needs an argument");
	      goto fail;
	    }
	  body = list->next->data;
	  if (!body->is_list)
	    {
	      g_warning ("`logging' section needs a body");
	      goto fail;
	    }
	  for (sublist = body->info.list;
	       sublist != NULL;
	       sublist = sublist->next->next)
	    {
	      TextNode *node = sublist->data;
	      TextNode *next;
	      if (sublist->next == NULL)
	        {
		  g_warning ("name without a body");
		  goto fail;
		}
	      next = sublist->next->data;
	      if (node->is_list)
		{
		  g_warning ("`logging' name was a brace instead");
		  goto fail;
		}
	      if (!next->is_list)
		{
		  g_warning ("`logging' body was an identifier instead");
		  goto fail;
		}
	      if (!parse_logging_option (config, node->info.name,
					 next->info.list))
		{
		  g_warning ("`logging': error parsing command `%s'",
			     node->info.name);
		  goto fail;
		}
	    }
	  continue;
	}
      if (strncasecmp (header->info.name, "options ", 8) == 0)
	{
	  TextNode *body;
	  GSList *sublist;
	  if (list->next == NULL)
	    {
	      g_warning ("section `options' needs an argument");
	      goto fail;
	    }
	  body = list->next->data;
	  if (!body->is_list)
	    {
	      g_warning ("`options' section needs a body");
	      goto fail;
	    }
	  for (sublist = body->info.list;
	       sublist != NULL;
	       sublist = sublist->next)
	    {
	      TextNode *node = sublist->data;
	      TextNode *next_node = sublist->next ? sublist->next->data : NULL;
	      GSList *arg_list = NULL;
	      if (node->is_list)
		{
		  g_warning ("`options' should not have any nested structure");
		  goto fail;
		}
	      if (next_node && next_node->is_list)
		arg_list = next_node->info.list;
	      if (!parse_one_option (config, node->info.name, arg_list))
		{
		  g_warning ("`options': error parsing command `%s'",
			     node->info.name);
		  goto fail;
		}
	    }
	  continue;
	}
      if (strncasecmp (header->info.name, "controls ", 9) == 0)
	{
	  g_warning ("We don't support the `controls' section.  Sorry.");
	  goto fail;
	}
      if (strncasecmp (header->info.name, "server ", 7) == 0)
	{
	  GskNamedRemoteServer *remote;
	  TextNode *next_node = list->next ? list->next->data : NULL;
	  if (next_node == NULL || !next_node->is_list)
	    {
	      g_warning ("parse_remote_server: no list of args");
	      goto fail;
	    }
	  remote = parse_remote_server (header, next_node);
	  g_hash_table_insert (config->servers_by_ip, remote, remote);
	  continue;
	}
      if (strncasecmp (header->info.name, "trusted-key ", 12) == 0)
	{
	  g_warning ("We don't support the `trusted-key' section.  Sorry.");
	  goto fail;
	}
      g_warning ("cannot parse `%s'", header->info.name);
      goto fail;
    }
  return config;

fail:
  if (node != NULL)
    text_node_destroy (node);
  text_node_parser_destroy (parser);
  return NULL;
}

/*
 *  _                   _
 * | | ___   __ _  __ _(_)_ __   __ _
 * | |/ _ \ / _` |/ _` | | '_ \ / _` |
 * | | (_) | (_| | (_| | | | | | (_| |
 * |_|\___/ \__, |\__, |_|_| |_|\__, |
 *          |___/ |___/         |___/
 */
static void
gsk_named_log_channel_write_string (GskNamedLogChannel *channel,
				    const char         *str)
{
  switch (channel->channel_style)
    {
    case GSK_NAMED_LOG_CHANNEL_NULL:
      break;

    case GSK_NAMED_LOG_CHANNEL_SYSLOG:
      gsk_syslog (channel->info.syslog_style.facility,
                  channel->info.syslog_style.priority,
		  str);
      break;

    case GSK_NAMED_LOG_CHANNEL_FILE:
      fprintf (channel->info.file_style.fp, "%s\n", str);
      break;

    default:
      g_assert_not_reached ();
      break;
    }
}

static void
named_config_logv(GskNamedConfig *config,
		  const char     *category,
		  const char     *format,
		  va_list         args)
{
  char *str;
  GskNamedLogCategory *cat;
  cat = g_hash_table_lookup (config->log_categories_by_name, category);
  if (cat == NULL)
    {
      gsk_log_debug ("No log channel for `%s' found", category);
      return;
    }
  str = g_strdup_vprintf (format, args);
  g_slist_foreach (cat->channels,
		   (GFunc) gsk_named_log_channel_write_string,
		   str);
  g_free (str);
}

void
gsk_named_config_log (GskNamedConfig *config,
		      const char     *category,
		      const char     *format,
		      ...)
{
  va_list args;
  va_start (args, format);
  named_config_logv (config, category, format, args);
  va_end (args);
}

void
gsk_named_debug_log  (GskNamedConfig *config,
		      int             debug_level,
		      const char     *format,
		      ...)
{
  va_list args;
  if (debug_level <= config->debug_level)
    {
      va_start (args, format);
      named_config_logv (config, "debug", format, args);
      va_end (args);
    }
}

GskNamedZone *
gsk_named_config_find_zone (GskNamedConfig        *config,
			    const char            *domain_name)
{
  GskNamedZone *rv;
  char *name = LOWER_CASE_COPY_ON_STACK (domain_name);
  while (name != NULL)
    {
      rv = g_hash_table_lookup (config->zones_by_name, name);
      if (rv != NULL)
	return rv;
    }
  return g_hash_table_lookup (config->zones_by_name, ".");
}

static void
destroy_acl_table_entry (gpointer key, gpointer value, gpointer data)
{
  g_free (key);
  gsk_simple_acl_destroy ((GskSimpleAcl *) value);
  (void) data;
}

static void
destroy_2nd_param_zone (gpointer key, gpointer value, gpointer data)
{
  GskNamedZone *zone = value;
  (void) key; (void) data;
  zone_shallow_free (zone);
}

static void
destroy_2nd_param_log_channel (gpointer key, gpointer value, gpointer data)
{
  GskNamedLogChannel *channel = value;
  (void) key; (void) data;
  gsk_named_log_channel_destroy (channel);
}
static void
destroy_2nd_param_log_category (gpointer key, gpointer value, gpointer data)
{
  GskNamedLogCategory *category = value;
  (void) key; (void) data;
  gsk_named_log_category_destroy (category);
}

void
gsk_named_config_destroy  (GskNamedConfig        *config)
{
  g_hash_table_foreach (config->zones_by_name, destroy_2nd_param_zone, NULL);
  g_hash_table_destroy (config->zones_by_name);
  g_hash_table_foreach (config->acls_by_name, destroy_acl_table_entry, NULL);
  g_hash_table_destroy (config->acls_by_name);
  g_hash_table_foreach (config->log_channels_by_name,
			destroy_2nd_param_log_channel,
			NULL);
  g_hash_table_destroy (config->log_channels_by_name);
  g_hash_table_foreach (config->log_categories_by_name,
			destroy_2nd_param_log_category,
			NULL);
  g_hash_table_destroy (config->log_categories_by_name);
  g_free (config);
}

