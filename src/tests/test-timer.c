#include "../gskmainloop.h"
#include <stdlib.h>
#include "../gskmain.h"
#include "../gskinit.h"

typedef struct _SourceInfo SourceInfo;
struct _SourceInfo
{
  GTimeVal intended_runtime;
  gint period;		/* possibly -1 */
  GskSource *source;
};

static GTimeVal last_runtime = {0, 0};
static guint source_count = 0;
static guint init_second = 0;

#define SLOW	0

#if SLOW
#define MAX_RANDOM_PERIOD		1000
#define PERCENT_ODDS_OF_STOPPING	10
#else
#define MAX_RANDOM_PERIOD		500
#define PERCENT_ODDS_OF_STOPPING	40
#endif

#define NUM_INITIAL_SOURCES		1000

static void
add_ms (GTimeVal *tv_inout, guint ms)
{
  tv_inout->tv_usec += (ms % 1000) * 1000;
  tv_inout->tv_sec += (ms / 1000);
  if (tv_inout->tv_usec >= 1000*1000)
    {
      tv_inout->tv_usec -= 1000*1000;
      tv_inout->tv_sec++;
    }
}

static gboolean
is_greater_than (GTimeVal *a, GTimeVal *b)
{
  return (a->tv_sec > b->tv_sec)
    || ( (a->tv_sec == b->tv_sec) && (a->tv_usec > b->tv_usec) );
}

static gboolean
run_timer (gpointer data)
{
  SourceInfo *info = data;
  if (is_greater_than (&last_runtime, &info->intended_runtime))
    {
      g_error ("out-of-order timer execution: expected to be run at %u.%03ums, "
	       "it already got event intended for time %u.%03ums",
	       ((guint) info->intended_runtime.tv_sec - init_second) * 1000 + (guint) info->intended_runtime.tv_usec / 1000,
	       (guint) info->intended_runtime.tv_usec % 1000,
	       ((guint) last_runtime.tv_sec - init_second) * 1000 + (guint) last_runtime.tv_usec / 1000,
	       (guint) last_runtime.tv_usec % 1000);
    }

  last_runtime = info->intended_runtime;
  if (info->period == -1 || rand() < RAND_MAX / 100 * PERCENT_ODDS_OF_STOPPING)
    {
      info->period = -1;
      return FALSE;
    }
  if (rand() < RAND_MAX / 10)
    {
      info->period = rand () / (RAND_MAX / MAX_RANDOM_PERIOD);
      gsk_source_adjust_timer (info->source, info->period, info->period);
      info->intended_runtime = gsk_main_loop_default()->current_time;
    }
  add_ms (&info->intended_runtime, info->period);
  return TRUE;
}

static void
destroy_timer (gpointer data)
{
  SourceInfo *info = data;
  g_assert (info->period == -1);
  g_free (info);
  source_count--;
  if (source_count == 0)
    gsk_main_quit ();
  g_printerr (".");
}

static void
make_random_source (void)
{
  SourceInfo *info = g_new0 (SourceInfo, 1);
  int rt_ms = rand () / (RAND_MAX / MAX_RANDOM_PERIOD);
  info->period = rand () / (RAND_MAX / MAX_RANDOM_PERIOD) - 1;
  info->intended_runtime = gsk_main_loop_default()->current_time;
  add_ms (&info->intended_runtime, rt_ms);
  info->source = gsk_main_loop_add_timer (gsk_main_loop_default(),
					  run_timer, info, destroy_timer,
					  rt_ms, info->period);
  source_count++;
}


static guint count2 = 0;
static gboolean
test_timer_2 (gpointer data)
{
  GTimeVal tv2;
  GTimeVal *ct;
  if (count2 > 100)
    return FALSE;
  count2++;
  g_get_current_time (&tv2);
  ct = &gsk_main_loop_default ()->current_time;
  g_message ("%u.%06u / %u.%06u [delta=%d]",
             (guint)ct->tv_sec, (guint)ct->tv_usec,
             (guint)tv2.tv_sec, (guint)tv2.tv_usec,
             (gint)(((gint)tv2.tv_sec - ct->tv_sec)*1000000
             + ((gint)tv2.tv_usec - ct->tv_usec)));
  return TRUE;
}
static void test_2 (void)
{
  gsk_main_loop_add_timer (gsk_main_loop_default (),
                           test_timer_2, NULL, NULL,
                           100, 100);
  while (count2 < 100)
    gsk_main_loop_run (gsk_main_loop_default (), -1, NULL);
}

int main (int argc, char **argv)
{
  guint i;
  gsk_init_without_threads (&argc, &argv);
  test_2 ();
  last_runtime = gsk_main_loop_default ()->current_time;
  init_second = last_runtime.tv_sec;
  for (i = 0; i < NUM_INITIAL_SOURCES; i++)
    make_random_source ();
  g_printerr ("Sources initialized... ");
  gsk_main_run ();
  g_printerr (" done.\n");
  return 0;
}
