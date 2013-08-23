#include "../gskmainloop.h"
#include "../gskmain.h"
#include "../gskinit.h"
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

typedef struct _SignalInfo SignalInfo;
struct _SignalInfo
{
  GskMainLoop *main_loop;
  int sig_no;
  int test_num;
  gboolean got_destroyed;
};

static gboolean deliver_signal (gpointer data)
{
  SignalInfo *info = data;
  kill (getpid (), info->sig_no);
  return FALSE;
}
  
static gboolean signal_handler (gint signal_no, gpointer data)
{
  SignalInfo *info = data;
  int test_num = (info->test_num)++;
  g_assert (signal_no == info->sig_no);
  switch (test_num)
    {
      case 0:
        /* add a timeout to invoke the next signal */
	gsk_main_loop_add_timer (info->main_loop,
	                         deliver_signal, info, NULL, 10, 10);
	return TRUE;
      case 1:
	gsk_main_loop_quit (info->main_loop);
	return FALSE;
    }
  g_assert_not_reached ();
  return FALSE;
}

static void mark_got_destroyed(gpointer data)
{
  SignalInfo *info = data;
  info->got_destroyed = 1;
}

/* --- main --- */
int main (int argc, char **argv)
{
  SignalInfo signal_info;
  GskMainLoop *main_loop;
  gsk_init_without_threads (&argc, &argv);
  
  main_loop = gsk_main_loop_new (0);
  signal_info.test_num = 0;
  signal_info.sig_no = SIGUSR1;
  signal_info.got_destroyed = FALSE;
  signal_info.main_loop = main_loop;
  gsk_main_loop_add_signal (main_loop, signal_info.sig_no,
                            signal_handler, &signal_info, 
			    mark_got_destroyed);
  kill (getpid (), signal_info.sig_no);
  while (!main_loop->quit)
    gsk_main_loop_run (main_loop, -1, NULL);
  g_assert (signal_info.got_destroyed);
  g_object_unref (G_OBJECT (main_loop));
  return 0;
}
