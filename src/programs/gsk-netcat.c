/* netcat-like program
 *    For testing, and for exposition.
 */
#include "../gskstreamclient.h"
#include "../ssl/gskstreamssl.h"
#include "../ssl/gskstreamlistenerssl.h"
#include "../gskstreamlistenersocket.h"
#include "../gskstreamfd.h"
#include "../gskmain.h"
#include "../gskinit.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "../gsknameresolver.h"

static void
usage (void)
{
  g_printerr ("gsk-netcat --client HOST PORT\n"
              "gsk-netcat --server PORT\n"
              "\n"
              "Options allowed by both client and server:\n"
              "  --ssl[=CERT:KEY:PASSWORD]   Use SSL\n"
              "\n"
              "Options allowed by server:\n"
              "  --proxy=HOST:PORT           Proxy to given host/port pair.\n"
             );
  exit(1);
}

typedef enum
{
  MODE_NONE,
  MODE_SERVER,
  MODE_CLIENT
} Mode;

typedef struct
{
  char *host;
  guint port;
  GskSocketAddress *address;
  GHookList traps;
  gboolean doing_name_lookup;
} HostPort;


/* --- Options --- */
static Mode mode = MODE_NONE;

/* general options: SSL */
static gboolean use_ssl = FALSE;
static char *ssl_cert = NULL;
static char *ssl_key = NULL;
static char *ssl_password = NULL;

/* server options */
static int server_port;
static HostPort proxy_hostport;

/* client options */
static HostPort client_hostport;

/* --- Name Resolution Handling (With Fatal Error Handling) --- */
static void
host_port_init (HostPort *hp)
{
  hp->host = NULL;
  hp->port = 0;
  hp->address = NULL;
  hp->doing_name_lookup = FALSE;
  g_hook_list_init (&hp->traps, sizeof (GHook));
}

static void
connect_to_stdio (GskStream *str, gboolean terminate_on_input_death)
{
  GskStream *in, *out;
  GError *error  =NULL;
  in = gsk_stream_fd_new_auto (STDIN_FILENO);
  out = gsk_stream_fd_new_auto (STDOUT_FILENO);
  g_assert (in && out);
  if (!gsk_stream_attach (in, str, &error))
    g_error ("error connecting input: %s", error->message);
  if (!gsk_stream_attach (str, out, &error))
    g_error ("error connecting output: %s", error->message);
  if (terminate_on_input_death)
    g_object_weak_ref (G_OBJECT (in), (GWeakNotify) gsk_main_quit, NULL);
  g_object_unref (in);
  g_object_unref (out);
}

/* --- Client --- */
void
setup_client (void)
{
  GError *error = NULL;
  GskStream *str;
  if (client_hostport.port == 0 || client_hostport.host == NULL)
    g_error ("client usage error: you must specify a host and port");
  client_hostport.address = gsk_socket_address_symbolic_ipv4_new (client_hostport.host,
                                                                  client_hostport.port);
  str = gsk_stream_new_connecting (client_hostport.address, &error);
  if (str == NULL)
    g_error ("client: error connecting to %s, port %u: %s",
             client_hostport.host, client_hostport.port, error->message);

  if (use_ssl)
    {
      GskStream *ssl_client = gsk_stream_ssl_new_client (ssl_cert,
                                                         ssl_key,
                                                         ssl_password,
                                                         str, &error);
      if (ssl_client == NULL)
        g_error ("error creating SSL stream: %s", error->message);
      g_object_unref (str);
      str = ssl_client;
    }

  connect_to_stdio (str, TRUE);
  g_object_weak_ref (G_OBJECT (str), (GWeakNotify) gsk_main_quit, NULL);
  g_object_unref (str);
}

/* --- Server --- */
static gboolean
handle_server_listener_accept (GskStream *new_connection,
                               gpointer data,
                               GError **error)
{
  connect_to_stdio (new_connection, TRUE);
  g_object_unref (new_connection);
  return TRUE;
}

static void
handle_server_listener_error (GError *error, gpointer data)
{
  g_error ("server listener error: %s" ,error->message);
}

void
setup_server (void)
{
  /* Create the listener, either regular, or with SSL. */
  GskSocketAddress *address;
  GskStreamListener *listener;
  GError *error = NULL;

  address = gsk_socket_address_ipv4_new (gsk_ipv4_ip_address_any, server_port);
  listener = gsk_stream_listener_socket_new_bind (address, &error);
  if (listener == NULL)
    g_error ("error binding to %u: %s", server_port, error->message);
  g_object_unref (address);
  if (use_ssl)
    {
      GskStreamListener *ssl_listener = gsk_stream_listener_ssl_new (listener, ssl_cert, ssl_key);
      /* XXX: don't we need to handle ssl_password???? */
      g_object_unref (listener);
      listener = ssl_listener;
    }

  gsk_stream_listener_handle_accept (listener,
                                     handle_server_listener_accept,
                                     handle_server_listener_error,
                                     NULL, NULL);
}

/* --- Main --- */
int main(int argc, char **argv)
{
  int i;
  gsk_init_without_threads (&argc, &argv);

  host_port_init (&proxy_hostport);
  host_port_init (&client_hostport);

  /* parse arguments */
  for (i = 1; i < argc; i++)
    {
      const char *arg = argv[i];
      if (strcmp (arg, "--client") == 0)
        mode = MODE_CLIENT;
      else if (strcmp (arg, "--server") == 0)
        mode = MODE_SERVER;
      else if (strcmp (arg, "--ssl") == 0)
        use_ssl = TRUE;
      else if (g_str_has_prefix (arg, "--ssl="))
        {
          char **pieces;
          use_ssl = TRUE;
          pieces = g_strsplit (strchr(arg,'=') + 1, ":", 3);
          if (pieces[0] && pieces[0][0])
            ssl_cert = pieces[0];
          if (pieces[0] && pieces[1] && pieces[1][0])
            ssl_key = pieces[1];
          if (pieces[0] && pieces[1] && pieces[2] && pieces[2][0])
            ssl_password = pieces[2];
          g_free (pieces);
        }
      else if (arg[0] == '-' || mode == MODE_NONE)
        usage ();
      else if (mode == MODE_SERVER)
        {
          /* a port number is allowed */
          if (server_port != 0)
            g_error ("too many arguments at '%s' (already got last argument, the server port)", arg);
          server_port = atoi (arg);
          if (server_port == 0)
            g_error ("error parsing port from %s", arg);
        }
      else if (mode == MODE_CLIENT)
        {
          /* a host, then a port is allowed */
          if (client_hostport.host == NULL)
            client_hostport.host = g_strdup (arg);
          else if (client_hostport.port != 0)
            g_error ("too many arguments at '%s' (already got last argument, the client port)", arg);
          else
            {
              client_hostport.port = atoi (arg);
              if (client_hostport.port == 0)
                g_error ("error parsing port from %s", arg);
            }
        }
      else
        g_assert_not_reached ();        /* invalid 'mode' */
    }

  switch (mode)
    {
    case MODE_CLIENT:
      setup_client ();
      break;
    case MODE_SERVER:
      setup_server ();
      break;
    default:
      usage ();
    }

  return gsk_main_run();
}
