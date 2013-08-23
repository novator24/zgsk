#include "../gskmainloop.h"
#include "../gskfork.h"
#include "../gskinit.h"
#include <unistd.h>
#include <signal.h>

typedef struct _TestInfo TestInfo;
struct _TestInfo
{
  gboolean is_done;
  GskMainLoopWaitInfo *wait_info;
};

static void
preserve_wait_info (GskMainLoopWaitInfo *info,
		    gpointer user_data)
{
  TestInfo *test_info = user_data;
  *(test_info->wait_info) = *info;
  test_info->is_done = TRUE;
}

static void
do_test (GskForkFunc func, gpointer data, GskMainLoopWaitInfo *wait_info)
{
  TestInfo test_info = { FALSE, wait_info };
  int pid = gsk_fork (func, data, NULL);
  g_assert (pid >= 0);
  gsk_main_loop_add_waitpid (gsk_main_loop_default (), pid, preserve_wait_info,
			     &test_info, NULL);
  while (!test_info.is_done)
    gsk_main_loop_run (gsk_main_loop_default (), -1, NULL);
  g_assert (wait_info->pid == pid);
  g_printerr (".");
}

static int do_sleep = 0;

static int
return_ptr_as_int (gpointer data)
{
  sleep (do_sleep);
  return GPOINTER_TO_UINT (data);
}
static int
exit_with_int (gpointer data)
{
  sleep (do_sleep);
  _exit (GPOINTER_TO_UINT (data));
  return 0;
}
static int
raise_int (gpointer data)
{
  sleep (do_sleep);
  kill (getpid(), GPOINTER_TO_UINT (data));
  return 0;
}

int main(int argc, char **argv)
{
  GskMainLoopWaitInfo wait_info;

  gsk_init_without_threads (&argc, &argv);

  for (do_sleep = 0; do_sleep < 2; do_sleep++)
    {
      g_printerr ("Doing test %s sleep: ", do_sleep ? "with" : "without");

      do_test (exit_with_int, GUINT_TO_POINTER (0), &wait_info);
      g_assert (wait_info.exited);
      g_assert (wait_info.d.exit_status == 0);
      g_assert (wait_info.dumped_core == 0);

      do_test (exit_with_int, GUINT_TO_POINTER (1), &wait_info);
      g_assert (wait_info.exited);
      g_assert (wait_info.d.exit_status == 1);
      g_assert (wait_info.dumped_core == 0);

      do_test (exit_with_int, GUINT_TO_POINTER (14), &wait_info);
      g_assert (wait_info.exited);
      g_assert (wait_info.d.exit_status == 14);
      g_assert (wait_info.dumped_core == 0);

      do_test (return_ptr_as_int, GUINT_TO_POINTER (0), &wait_info);
      g_assert (wait_info.exited);
      g_assert (wait_info.d.exit_status == 0);
      g_assert (wait_info.dumped_core == 0);

      do_test (return_ptr_as_int, GUINT_TO_POINTER (1), &wait_info);
      g_assert (wait_info.exited);
      g_assert (wait_info.d.exit_status == 1);
      g_assert (wait_info.dumped_core == 0);

      do_test (return_ptr_as_int, GUINT_TO_POINTER (14), &wait_info);
      g_assert (wait_info.exited);
      g_assert (wait_info.d.exit_status == 14);
      g_assert (wait_info.dumped_core == 0);

      do_test (raise_int, GUINT_TO_POINTER (SIGSEGV), &wait_info);
      g_assert (!wait_info.exited);
      g_assert (wait_info.d.signal == SIGSEGV);

      do_test (raise_int, GUINT_TO_POINTER (SIGTERM), &wait_info);
      g_assert (!wait_info.exited);
      g_assert (wait_info.d.signal == SIGTERM);

      do_test (raise_int, GUINT_TO_POINTER (SIGKILL), &wait_info);
      g_assert (!wait_info.exited);
      g_assert (wait_info.d.signal == SIGKILL);
      g_printerr ("\n");
    }

  return 0;
}
