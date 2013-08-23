#include "../gsklog.h"
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

static void
usage ()
{
  g_printerr ("usage: test-gsklog OPTIONS\n\n"
              "OPTIONS is a list of:\n"
              "  --trap DOMAIN LEVELS FILE FORMAT\n"
              "  --log DOMAIN LEVEL STRING\n"
              "\nLEVELS is a comma-separated list of LEVEL.\n"
              "LEVEL is one of: debug info warning error critical.\n"
             );
  exit(1);
}

static GLogLevelFlags
level_string_to_flags (const char *str)
{
#define CHECK_LEVEL(string, short_tag)  \
  if (strcmp (str, string) == 0)        \
    return G_LOG_LEVEL_##short_tag
  CHECK_LEVEL("debug", DEBUG);
  CHECK_LEVEL("info", INFO);
  CHECK_LEVEL("warning", WARNING);
  CHECK_LEVEL("error", ERROR);
  CHECK_LEVEL("critical", CRITICAL);

  g_return_val_if_reached (G_LOG_LEVEL_INFO);
}

static GLogLevelFlags
level_mask_to_flags (const char *str)
{
  char **sp = g_strsplit (str, ",", 0);
  char **sp_at = sp;
  GLogLevelFlags flags = 0;
  while (*sp_at)
    flags |= level_string_to_flags (*sp_at++);
  g_strfreev(sp);
  return flags;
}

int main(int argc, char **argv)
{
  int i;
  if (argc < 2)
    usage ();
  for (i = 1; i < argc; )
    {
      if (strcmp (argv[i], "--trap") == 0)
        {
          g_assert (i + 3 < argc);
          gsk_log_trap_domain_to_file (argv[i+1], level_mask_to_flags (argv[i+2]),
                                       argv[i+3], argv[i+4]);
          i += 4;
        }
      else if (strcmp (argv[i], "--log") == 0)
        {
          g_assert (i + 3 < argc);
          g_log (argv[i+1], level_string_to_flags (argv[i+2]),
                 "%s", argv[i+3]);
          i += 4;
        }
      else
        usage ();
    }
  return 0;
}
