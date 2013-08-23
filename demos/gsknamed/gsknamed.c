#include "gsknamedconfig.h"
#include "gsknamedresolver.h"
#include <gsk/protocols/gskdnsimplementations.h>
#include <gsk/gskmain.h>

typedef struct _NamedCommandOptions NamedCommandOptions;
struct _NamedCommandOptions
{
  const char       *config_file;
  int               debug_level;
  gboolean          daemonize;
  gboolean          force_stderr_logging;
  int               port;
  const char       *chroot_dir;
  int               uid;
  gboolean          stub;
  guint             cache_max_records;
  guint64           cache_max_bytes;
};

static void
init_name_command_options (NamedCommandOptions *options)
{
  options->config_file = NULL;
  options->debug_level = 0;
  options->daemonize = TRUE;
  options->force_stderr_logging = FALSE;
  options->port = GSK_DNS_PORT;
  options->chroot_dir = NULL;
  options->uid = -1;
  options->stub = FALSE;
  options->cache_max_bytes = 0;
  options->cache_max_records = 0;
}

static gboolean
parse_bind_options (int argc, char **argv,
		    NamedCommandOptions *options)
{
  int i;
  for (i = 1; i < argc; i++)
    {
      if (strcmp (argv[i], "-c") == 0)
	{
	  options->config_file = argv[++i];
	  if (options->config_file == NULL)
	    g_error ("the -c option requires an argument");
	  continue;
	}
      if (strcmp (argv[i], "-d") == 0)
	{
	  const char *str = argv[++i];
	  if (str == NULL)
	    g_error ("the -d option requires an argument");
	  options->debug_level = atoi (str);
	  g_message ("Setting debug level to %d", options->debug_level);
	  continue;
	}
      if (strcmp (argv[i], "-p") == 0)
	{
	  const char *str = argv[++i];
	  if (str == NULL)
	    g_error ("the -p option requires an argument");
	  options->port = atoi (str);
	  if (options->port <= 0)
	    g_error ("port must be >0");
	  continue;
	}
      if (strcmp (argv[i], "-f") == 0)
	{
	  options->daemonize = FALSE;
	  continue;
	}
      if (strcmp (argv[i], "-g") == 0)
	{
	  options->daemonize = FALSE;
	  options->force_stderr_logging = FALSE;
	  continue;
	}
      if (strcmp (argv[i], "-t") == 0)
	{
	  options->chroot_dir = argv[++i];
	  if (options->chroot_dir == NULL)
	    g_error ("the -t option requires an argument");
	  continue;
	}

      /* XXX: parsing /etc/passwd would be good. */
      if (strcmp (argv[i], "-u") == 0)
	{
	  const char *str = argv[++i];
	  if (str == NULL)
	    g_error ("the -u option requires an argument");
	  options->uid = atoi (str);
	  if (options->uid < 0)
	    g_error ("uid must be >=0");
	  continue;
	}
      g_error ("unrecognized option `%s'", argv[i]);
    }

  return TRUE;
}

static GskDnsRRCache *
named_command_options_make_cache (NamedCommandOptions *command_options)
{
  return gsk_dns_rr_cache_new (command_options->cache_max_records,
			       command_options->cache_max_bytes);
}

static GskNamedConfig *
named_command_options_make_config (NamedCommandOptions *command_options)
{
  const char *config_file = command_options->config_file;
  GskNamedConfig *config;
  if (config_file == NULL)
    {
      config_file = "/etc/named/conf";
      g_message ("Using default config file `%s'", config_file);
    }
  config = gsk_named_config_parse (config_file);
  if (config == NULL)
    g_error ("Error parsing named-configuration from %s", config_file);
  return config;
}

/* --- main program --- */
int main (int argc, char **argv)
{
  GskDatagramSocket *dg_socket;
  GskActor *dns_udp_actor;
  GskDnsClient *client;
  GskMainLoop *main_loop;
  NamedCommandOptions options;
  GskDnsRRCache *rr_cache;
  GskNamedResolver *named;
  GskNamedConfig *named_config;
  GskDnsServer *server;

  gsk_init (&argc, &argv);
  init_name_command_options (&options);
  if (!parse_bind_options (argc, argv, &options))
    g_error ("error parsing command-line arguments");

  rr_cache = named_command_options_make_cache (&options);
  named_config = named_command_options_make_config (&options);

  dg_socket = gsk_datagram_socket_new_bound_udp (options.port);
  g_assert (dg_socket != NULL);
  dns_udp_actor = gsk_dns_udp_actor_new (dg_socket);
  g_assert (dns_udp_actor != NULL);

  client = gsk_dns_client_new (GSK_DNS_RECEIVER (dns_udp_actor),
			       GSK_DNS_TRANSMITTER (dns_udp_actor),
			       rr_cache,
			       0);
  named = gsk_dns_named_resolver_new (named_config,
				      rr_cache,
				      GSK_DNS_RESOLVER (client));
  server = gsk_dns_server_new (GSK_DNS_RESOLVER (named),
			       GSK_DNS_RECEIVER (dns_udp_actor),
			       GSK_DNS_TRANSMITTER (dns_udp_actor));

  gsk_actor_set_main_loop (GSK_ACTOR (dns_udp_actor), main_loop);
  gsk_actor_set_main_loop (GSK_ACTOR (client), main_loop);
  gsk_actor_set_main_loop (GSK_ACTOR (server), main_loop);

  while (!main_loop->quit)
    gsk_main_loop_run (main_loop, -1, NULL);
  gtk_object_unref (GTK_OBJECT (main_loop));
  return 0;
}
