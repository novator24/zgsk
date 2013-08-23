#define USE_G_MAIN_LOOP	0

#include <glib.h>
#include "gskmain.h"

/**
 * gsk_main_run:
 * 
 * Run the main loop until it terminates, returning the value which 
 * should be returned by main().
 *
 * returns: the exit code for this process.
 */
#if USE_G_MAIN_LOOP
int gsk_main_run ()
{
  GMainLoop *loop = g_main_loop_new (g_main_context_default (), FALSE);
  g_main_loop_run (loop);
  g_main_loop_unref (loop);
}

#else	/* !USE_G_MAIN_LOOP */

#include "gskmainloop.h"

int gsk_main_run ()
{
  GskMainLoop *loop = gsk_main_loop_default ();
  while (gsk_main_loop_should_continue (loop))
    gsk_main_loop_run (loop, -1, NULL);
  return loop->exit_status;
}

/**
 * gsk_main_quit:
 * 
 * Quit the program by stopping gsk_main_run().
 */
void
gsk_main_quit ()
{
  GskMainLoop *loop = gsk_main_loop_default ();
  gsk_main_loop_quit (loop);
}

/**
 * gsk_main_exit:
 * @exit_status: desired exit-status code for this process.
 * 
 * Exit the program by stopping gsk_main_run().
 */
void
gsk_main_exit (int exit_status)
{
  GskMainLoop *loop = gsk_main_loop_default ();
  loop->exit_status = exit_status;
  gsk_main_loop_quit (loop);
}

#endif	/* !USE_G_MAIN_LOOP */
