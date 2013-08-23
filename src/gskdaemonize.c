#include "gskdaemonize.h"
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static gboolean has_restart_exit_code = FALSE;
static gint global_restart_exit_code = 0;
static gboolean restart_on_error_signals = FALSE;
static gboolean do_fork = FALSE;
static char *pid_filename = NULL;
static guint restart_sleep_length = 1;

static inline gboolean
is_error_signal (guint signal_no)
{
  return signal_no == SIGILL
      || signal_no == SIGILL
      || signal_no == SIGABRT
      || signal_no == SIGSEGV
      || signal_no == SIGIOT
      || signal_no == SIGBUS
      || signal_no == SIGFPE;
}


void gsk_daemonize_set_defaults  (GskDaemonizeFlags flags,
                                  guint             restart_exit_code)
{
  if (flags & GSK_DAEMONIZE_RESTART_ERROR_SIGNALS)
    restart_on_error_signals = TRUE;
  else
    restart_on_error_signals = FALSE;
  if (flags & GSK_DAEMONIZE_FORK)
    do_fork = TRUE;
  else
    do_fork = FALSE;
  if (flags & GSK_DAEMONIZE_SUPPORT_RESTART_EXIT_CODE)
    {
      has_restart_exit_code = TRUE;
      global_restart_exit_code = restart_on_error_signals;
    }
  else
    has_restart_exit_code = FALSE;
}

#define SWALLOW_ARG(count)              \
    { memmove ((*argv_inout) + i,       \
               (*argv_inout) + (i + count), \
               sizeof(char*) * (*argc_inout + 1 - (i + count))); \
      *argc_inout -= count; }
static const char *
test_opt (const char *opt_name,
          gint        i,
          int        *argc_inout,
          char     ***argv_inout)
{
  const char *arg = (*argv_inout)[i];
  guint opt_len = strlen (opt_name);
  if (arg[0] == '-' && arg[1] == '-')
    {
      if (strcmp (arg + 2, opt_name) == 0)
        {
          const char *rv;
          if (i + 1 >= *argc_inout)
            return NULL;
          rv = (*argv_inout)[i+1];
          SWALLOW_ARG (2);
          return rv;
        }
      if (strncmp (arg + 2, opt_name, opt_len) == 0
       && arg[2 + opt_len] == '=')
        {
          const char *rv = arg + 2 + opt_len + 1;
          SWALLOW_ARG (1);
          return rv;
        }
    }
  return NULL;
}

void
gsk_daemonize_parse_options (int              *argc_inout,
                             char           ***argv_inout)
{
  gint i;
  for (i = 0; i < *argc_inout; )
    {
      const char *arg = (*argv_inout)[i];
      const char *opt;
      if (strcmp (arg, "--foreground") == 0)
        {
          do_fork = FALSE;
          SWALLOW_ARG (1);
        }
      else if (strcmp (arg, "--background") == 0)
        {
          do_fork = TRUE;
          SWALLOW_ARG (1);
        }
      else if ((opt=test_opt("pidfile",i,argc_inout,argv_inout)) != NULL)
        {
	  g_free (pid_filename);
          pid_filename = g_strdup (opt);
        }
      else if (strcmp (arg, "--no-autorestart") == 0)
        {
          restart_on_error_signals = FALSE;
          SWALLOW_ARG (1);
        }
      else if (strcmp (arg, "--autorestart") == 0)
        {
          restart_on_error_signals = TRUE;
          SWALLOW_ARG (1);
        }
      else
	{
	  i++;
	}
    }
}

void gsk_daemonize_set_pid_filename (const char *filename)
{
  char *dup = g_strdup (filename);
  g_free (pid_filename);
  pid_filename = dup;
}

void gsk_daemonize_print_options (void)
{
  g_print ("  --background       Fork to put this process in the background.%s\n"
           "  --foreground       Do not fork: put this process in the foreground.%s\n"
           "  --pidfile=FILE     Write pid to this file.\n",
          do_fork ? " [default]" : "",
          do_fork ? "" : " [default]");

  /* only print options in the opposite of their default sense */
  if (restart_on_error_signals)
  g_print ("  --no-autorestart   Do not restart the process on error signals.\n");
  else
  g_print ("  --autorestart      Restart the process on error signals.\n");
}

void
gsk_maybe_daemonize (void)
{
  if (do_fork)
    {
      int fork_rv;

      fflush (stdin);
      fflush (stdout);
      while ((fork_rv=fork()) < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }
#if 0           /* the code quoted here is the traditional fork guard. */
                /* it seems dubious though -- let's abort instead. */
          if (errno == EAGAIN)
            {
              sleep(2);
              continue;
            }
#endif
          g_error ("error forking: %s", g_strerror (errno));
        }
      if (fork_rv > 0)
	{
	  FILE *pid_fp;

	  pid_fp = fopen (pid_filename, "w");
	  if (pid_fp == NULL)
	    g_error ("error opening pid file %s", pid_filename);
	  fprintf (pid_fp, "%u\n", fork_rv);
	  fclose (pid_fp);

	  exit (0);
	}
      
      /* child continues */
    }

  if (has_restart_exit_code || restart_on_error_signals)
    {
      int fork_rv;
restart:
      while ((fork_rv=fork()) < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }
#if 0           /* the code quoted here is the traditional fork guard. */
                /* it seems dubious though -- let's abort instead. */
          if (errno == EAGAIN)
            {
              sleep(2);
              continue;
            }
#endif
          g_error ("error forking: %s", g_strerror (errno));
        }
      if (fork_rv > 0)
        {
          int status;
          if (pid_filename != NULL)
            {
              FILE *pid_fp = fopen (pid_filename, "w");
	      if (pid_fp == NULL)
		g_error ("error opening pid file %s", pid_filename);
              fprintf (pid_fp, "%u\n", fork_rv);
	      fclose (pid_fp);
            }
          if (waitpid (fork_rv, &status, 0) < 0)
            {
              g_error ("error running waitpid itself");
            }
          if (pid_filename != NULL)
            unlink (pid_filename);
          if (WIFEXITED (status))
            {
              int exit_status = WEXITSTATUS (status);
              if (has_restart_exit_code && exit_status == global_restart_exit_code)
                {
                  sleep (restart_sleep_length);
                  goto restart;
                }
              _exit (exit_status);
            }
          else if (WIFSIGNALED (status))
            {
              int signalno = WTERMSIG (status);
              if (restart_on_error_signals && is_error_signal (signalno))
                {
                  sleep (restart_sleep_length);
                  goto restart;
                }
              kill (getpid (), signalno);
            }
          else
            {
              g_error ("program terminated, but not by signal or exit?");
            }
        }
    }
}
