#include <string.h>
#include <signal.h>
#include "gskinit.h"
#include "gskerror.h"
#include "gskbuffer.h"
#include "gsknameresolver.h"
#include "gskmainloop.h"
#include "gskdebug.h"

_GskInitFlags gsk_init_flags = 0;
gpointer gsk_main_thread = NULL;

/**
 * gsk_init_info_get_defaults:
 * @info: the #GskInitInfo to fill.
 *
 * Obtain the default initialization information.
 * This should be run before gsk_init() or gsk_init_info_parse_args().
 *
 * This API has been deprecated for public use,
 * because it doesn't allow us to expand
 * GskInitInfo without breaking binary-compatibility.
 *
 * Use gsk_init_info_new() instead.
 */
void
gsk_init_info_get_defaults (GskInitInfo *info)
{
  info->prgname = NULL;
  info->needs_threads = 1;
}

/**
 * gsk_init_info_new:
 *
 * Create a new, default initialization-configuration object.
 *
 * returns: the newly allocated #GskInitInfo.
 */
GskInitInfo *
gsk_init_info_new (void)
{
  GskInitInfo *info = g_new (GskInitInfo, 1);
  gsk_init_info_get_defaults (info);
  return info;
}

/**
 * gsk_init_info_free:
 * @info: the object to free.
 *
 * Free a initialization-configuration object.
 */
void
gsk_init_info_free (GskInitInfo *info)
{
  g_free (info);
}

/**
 * gsk_init:
 * @argc: a reference to main()'s argc;
 * this will be decreased if arguments are parsed
 * out of the argument array.
 * @argv: a reference to main()'s argc;
 * this may have arguments removed.
 * @info: the #GskInitInfo to use as hints,
 * which will be filled with the
 * actual initialization information used.
 * If NULL, default initialization parameters
 * will be used.
 *
 * Initialize the GSK library.
 */
void
gsk_init                   (int         *argc,
			    char      ***argv,
			    GskInitInfo *info)
{
  g_type_init ();
  if (info == NULL)
    {
      info = g_newa (GskInitInfo, 1);
      gsk_init_info_get_defaults (info);
    }
  gsk_init_info_parse_args (info, argc, argv);
  gsk_init_raw (info);
}

/**
 * gsk_init_without_threads:
 * @argc: a reference to main()'s argc;
 * this will be decreased if arguments are parsed
 * out of the argument array.
 * @argv: a reference to main()'s argc;
 * this may have arguments removed.
 *
 * Initialize the GSK library indicating that you will not use threads.
 */
void
gsk_init_without_threads   (int         *argc,
			    char      ***argv)
{
  GskInitInfo info;
  g_type_init ();
  gsk_init_info_get_defaults (&info);
  info.needs_threads = FALSE;
  gsk_init_info_parse_args (&info, argc, argv);
  gsk_init_raw (&info);
}

static struct
{
  const char *name;
  guint flag;
} debug_flag_names[] = {
  { "io", GSK_DEBUG_IO },
  { "stream-data", GSK_DEBUG_STREAM_DATA },
  { "stream", GSK_DEBUG_STREAM },
  { "listener", GSK_DEBUG_STREAM_LISTENER },
  { "lifetime", GSK_DEBUG_LIFETIME },
  { "mainloop", GSK_DEBUG_MAIN_LOOP },
  { "dns", GSK_DEBUG_DNS },
  { "hook", GSK_DEBUG_HOOK },
  { "request", GSK_DEBUG_REQUEST },
  { "fd", GSK_DEBUG_FD },
  { "ssl", GSK_DEBUG_SSL },
  { "all", GSK_DEBUG_ALL },
  { NULL, 0 }
};

static void
handle_debug_flags (const char *opts)
{
  for (;;)
    {
      guint k;
      for (k = 0; debug_flag_names[k].name != NULL; k++)
	{
	  const char *o = debug_flag_names[k].name;
	  if (strncmp (opts, o, strlen (o)) == 0)
	    {
	      gsk_debug_add_flags (debug_flag_names[k].flag);
	      break;
	    }
	}
      if (debug_flag_names[k].name == NULL)
	{
	  char *tmp = g_strdup (opts);
	  char *comma = strchr (tmp, ',');
	  if (comma)
	    *comma = 0;
	  g_warning ("no debugging option `%s' found", tmp);
	  g_free (tmp);
	}
      opts = strchr (opts, ',');
      if (!opts)
	break;
      opts++;
    }
}

/**
 * gsk_init_info_parse_args:
 * @in_out: the #GskInitInfo to fill.
 * @argc: the argument count (may be modified)
 * @argv: the arguments (may be modified)
 *
 * Parse/modify arguments and return their values in @in_out.
 *
 * The only currently supported argument is --gsk-debug=FLAGS.
 */
void
gsk_init_info_parse_args (GskInitInfo *in_out,
			  int         *argc,
			  char      ***argv)
{
  gint i;
  g_type_init ();
  if (in_out->prgname == NULL && argv != NULL)
    in_out->prgname = (*argv)[0];

  for (i = 1; i < *argc; )
    {
      const char *arg = (*argv)[i];
      guint num_to_swallow = 0;

      /* handle --gsk-debug= */
      if (strncmp (arg, "--gsk-debug=", 12) == 0)
	{
	  const char *opts = arg + 12;
	  handle_debug_flags (opts);
	  num_to_swallow = 1;
	}

      if (strcmp (arg, "--g-fatal-warnings") == 0)
	{
	  g_log_set_always_fatal (G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL);
	  num_to_swallow = 1;
	}

      if (num_to_swallow)
	{
	  memcpy ((*argv) + i, (*argv) + i + num_to_swallow,
		  (*argc - num_to_swallow - i + 1) * sizeof (char *));
	  *argc -= num_to_swallow;
	}
      else
	{
	  i++;
	}
    }

  {
    const char *debug_flags = g_getenv ("GSK_DEBUG");
    if (debug_flags)
      handle_debug_flags (debug_flags);
  }
}

static void
gsk_socket_address_family_init (void)
{
  g_type_class_ref (GSK_TYPE_SOCKET_ADDRESS_IPV4);
  g_type_class_ref (GSK_TYPE_SOCKET_ADDRESS_IPV6);
}

void _gsk_hook_init(void);
void _gsk_name_resolver_init (void);
void _gsk_url_transfer_register_builtins (void);

/**
 * gsk_init_raw:
 * @info: information to use for initializing.
 *
 * Initialize GSK.
 */
void
gsk_init_raw (GskInitInfo *info)
{
  static gboolean has_initialized = FALSE;

  if (has_initialized)
    return;
  has_initialized = TRUE;

  if (info->prgname != NULL && g_get_prgname () == NULL)
    g_set_prgname (info->prgname);

  gsk_init_flags = 0;
  if (info->needs_threads)
    gsk_init_flags |= _GSK_INIT_SUPPORT_THREADS;

  g_type_init ();
  if (info->needs_threads)
    {
      g_thread_init (NULL);
      gsk_main_thread = g_thread_self ();
    }
  _gsk_hook_init ();
  _gsk_error_init ();
  _gsk_name_resolver_init ();
  _gsk_main_loop_init ();
  _gsk_url_transfer_register_builtins ();
  gsk_socket_address_family_init ();

  /* we always want to ignore SIGPIPE;
     we just handle errno==EPIPE properly. */
  signal (SIGPIPE, SIG_IGN);
}
