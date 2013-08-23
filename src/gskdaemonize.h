#ifndef __GSK_DAEMONIZE_H_
#define __GSK_DAEMONIZE_H_

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  GSK_DAEMONIZE_FORK	= (1<<0),

  /* trap SIGILL, SIGABRT, SIGSEGV, SIGIOT, SIGBUG, SIGFPE (where available) */
  GSK_DAEMONIZE_RESTART_ERROR_SIGNALS = (1<<1),
  GSK_DAEMONIZE_SUPPORT_RESTART_EXIT_CODE = (1<<2)
} GskDaemonizeFlags;

#define GSK_DAEMONIZE_DEFAULT_RESTART_EXIT_CODE         100

void gsk_daemonize_set_defaults  (GskDaemonizeFlags flags,
                                  guint             restart_exit_code);
void gsk_daemonize_set_pid_filename (const char *filename);
void gsk_daemonize_parse_options (int              *argc_inout,
                                  char           ***argv_inout);

/* this should be called after set_defaults,
   so that it can affect the printout. */
void gsk_daemonize_print_options (void);
void gsk_maybe_daemonize         (void);

G_END_DECLS

#endif
