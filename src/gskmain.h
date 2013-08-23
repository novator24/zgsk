#ifndef __GSK_MAIN_H_
#define __GSK_MAIN_H_

#include <glib.h>

G_BEGIN_DECLS

int gsk_main_run ();
void gsk_main_quit ();
void gsk_main_exit (int exit_status);

G_END_DECLS

#endif
