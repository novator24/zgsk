#include "../gskthreadpool.h"
#include "../gskinit.h"


static guint count = 0;

static gpointer square_input (gpointer run_data)
{
  guint i = GPOINTER_TO_UINT (run_data);
  return GUINT_TO_POINTER (i * i);
}

static gpointer square_input_and_sleep (gpointer run_data)
{
  guint i = GPOINTER_TO_UINT (run_data);
  g_usleep (g_random_int_range (0, 100 * 1000));
  return GUINT_TO_POINTER (i * i);
}

static void
confirm_squared_input_and_inc_count (gpointer run_data, gpointer result_data)
{
  guint i = GPOINTER_TO_UINT (run_data);
  guint i_squared = GPOINTER_TO_UINT (result_data);
  g_assert (i * i == i_squared);
  count++;
}

static void
set_count_to_user_data (gpointer user_data)
{
  count = GPOINTER_TO_UINT (user_data);
}

int main (int argc, char **argv)
{
  GskThreadPool *pool;
  guint i;
  gsk_init (&argc, &argv, NULL);

  pool = gsk_thread_pool_new (gsk_main_loop_default (), 10);
  
  count = 0;
  for (i = 0; i < 100; i++)
    gsk_thread_pool_push (pool,
			  square_input_and_sleep,
			  confirm_squared_input_and_inc_count,
			  GUINT_TO_POINTER (i), NULL);
  while (count < 100)
    gsk_main_loop_run (gsk_main_loop_default (), -1, NULL);

  count = 0;
  for (i = 0; i < 100; i++)
    gsk_thread_pool_push (pool,
			  square_input,
			  confirm_squared_input_and_inc_count,
			  GUINT_TO_POINTER (i), NULL);
  while (count < 100)
    gsk_main_loop_run (gsk_main_loop_default (), -1, NULL);

  count = 0;
  gsk_thread_pool_destroy (pool, set_count_to_user_data,
			   GUINT_TO_POINTER (31415));
  while (count != 31415)
    gsk_main_loop_run (gsk_main_loop_default (), -1, NULL);

  return 0;
}
