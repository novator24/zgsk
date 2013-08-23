#include <stdio.h>
#include <stdlib.h>
#include "../gsknameresolver.h"
#include "../gskdebug.h"
#include "../gskinit.h"
#include "../gskmain.h"

static GskNameResolverFamily family;


static void next_name_resolution (void);

static void
got_name_successfully (GskSocketAddress *address,
		       gpointer          unused)
{
  char *str = gsk_socket_address_to_string (address);
  g_assert (unused == NULL);
  g_print ("%s\n", str);
  g_free (str);

  next_name_resolution ();
}

static void
got_error (GError *error,
	   gpointer unused)
{
  g_assert (unused == NULL);
  g_print ("failed: %s\n", error->message);
  fflush(stdout);
  
  next_name_resolution ();
}

static void
next_name_resolution (void)
{
  char buf[8192];
  static gboolean tried_next_name_resolution_recurse = FALSE;
  static gboolean next_name_resolution_running = FALSE;

  if (next_name_resolution_running)
    {
      tried_next_name_resolution_recurse = TRUE;
      return;
    }
  next_name_resolution_running = TRUE;
  do
    {
      tried_next_name_resolution_recurse = FALSE;
      if (!fgets (buf, sizeof (buf), stdin))
        gsk_main_quit ();
      else
        {
          GskNameResolverTask *task;
          g_strstrip (buf);
          g_printerr ("looking up '%s'\n", buf);
          task = gsk_name_resolve (family, buf, got_name_successfully, got_error, NULL, NULL);
          gsk_name_resolver_task_unref (task);
        }
    }
  while (tried_next_name_resolution_recurse);
  next_name_resolution_running = FALSE;
}

int main (int argc, char **argv)
{
  gsk_init_without_threads (&argc, &argv);

  if (argc != 2)
    g_error ("%s requires exactly 1 argument, family", argv[0]);

  family = gsk_name_resolver_family_get_by_name (argv[1]);
  if (!family)
    g_error ("invalid name-resolver family '%s'", argv[1]);

  next_name_resolution ();
  return gsk_main_run ();
}
