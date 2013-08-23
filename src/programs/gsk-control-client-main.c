#include "../control/gskcontrolclient.h"
#include "../config.h"
#include "../gskinit.h"
#include <stdlib.h>
#include <stdio.h>

#if SUPPORT_READLINE
#include READLINE_HEADER_NAME
#include READLINE_HISTORY_HEADER_NAME
#define free_readline_result(ptr)       /* no-op */
#else
static char *
my_readline (const char *prompt)
{
  char buf[8192];
  char *rv;
  if (prompt)
    fputs (prompt, stdout);
  rv = g_strdup (fgets (buf, sizeof(buf), stdin));
  if (rv)
    g_strchomp (rv);
  return rv;
}
#define free_readline_result(ptr)       g_free(ptr)
#define readline        my_readline
#define add_history(line)
#endif

static void usage ()
{
  g_print ("usage: %s --socket=SOCKET OPTIONS\n", g_get_prgname ());
  gsk_control_client_print_command_line_usage (GSK_CONTROL_CLIENT_OPTIONS_DEFAULT);
  exit (1);
}

int main(int argc, char **argv)
{
  int i;
  GskControlClient *cc;
  gsk_init_without_threads (&argc, &argv);
  g_set_prgname ("gsk-control-client");
  cc = gsk_control_client_new (NULL);
  if (!gsk_control_client_parse_command_line_args
      (cc, &argc, &argv, GSK_CONTROL_CLIENT_OPTIONS_DEFAULT))
    return 0;
  for (i = 1; i < argc; i++)
    {
      /* there are no special options for this program */
      usage ();
    }
  if (!gsk_control_client_has_address (cc))
    usage ();

  for (;;)
    {
      char *prompt = gsk_control_client_get_prompt_string (cc);
      char *cmd_orig = readline (prompt);
      char *cmd = cmd_orig;
      if (cmd == NULL)
        break;
      while (*cmd && isspace (*cmd))
        cmd++;
      if (*cmd == 0)
        continue;
      if (*cmd == '#')
        {
          add_history (cmd);
          continue;
        }
      gsk_control_client_run_command_line (cc, cmd);
      add_history (cmd);
      free_readline_result (cmd_orig);
      g_free (prompt);
    }
  return 0;
}
