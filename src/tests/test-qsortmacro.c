#include "../gskqsortmacro.h"
#include <stdlib.h>

//#define TIMING_PRIME    10007
#define TIMING_PRIME    101

static void
test_array (guint N,
            guint *array,
	    guint start,
	    guint skip)
{
  guint i;
  /*g_message ("test_array: N=%u, start=%u, skip=%u",N,start,skip);*/
  for (i = 0; i < N; i++)
    array[i] = (start + skip * i) % N;
  GSK_QSORT (array, guint, N, GSK_QSORT_SIMPLE_COMPARATOR);
  for (i = 0; i < N; i++)
    if (array[i] != i)
      g_error ("array position %u was filled with %u",i,array[i]);
}

static void
test_prime (guint prime)
{
  guint i,j;
  guint *array = g_newa (guint, prime);
  for (i = 0; i < prime; i++)
    for (j = 1; j < prime; j++)
      test_array (prime, array, i, j);
}

static int
compare_uints (gconstpointer a, gconstpointer b)
{
  guint ai = * (guint*) a;
  guint bi = * (guint*) b;
  return (ai < bi) ? -1
       : (ai > bi) ? 1
       : 0;
}

static void
test_array_libc (guint N,
            guint *array,
	    guint start,
	    guint skip)
{
  guint i;
  for (i = 0; i < N; i++)
    array[i] = (start + skip * i) % N;
  qsort (array, N, sizeof (guint), compare_uints);
  for (i = 0; i < N; i++)
    if (array[i] != i)
      g_error ("array position %u was filled with %u",i,array[i]);
}

static void
test_prime_libc (guint prime)
{
  guint i,j;
  guint *array = g_newa (guint, prime);
  for (i = 0; i < prime; i++)
    for (j = 1; j < prime; j++)
      test_array_libc (prime, array, i, j);
}


int main()
{
  GTimer *timer;
  test_prime (2);
  test_prime (3);
  test_prime (5);
  test_prime (7);

  timer = g_timer_new ();
  test_prime (TIMING_PRIME);
  g_message ("macro=%.7f", g_timer_elapsed (timer, NULL));

  g_timer_reset (timer);
  test_prime_libc (TIMING_PRIME);
  g_message ("libc=%.7f", g_timer_elapsed (timer, NULL));

  {
    guint *array = g_new (guint, 200*1000);
    test_array (200*1000, array, 0, 1);
    test_array (200*1000, array, 100*1000, 1);
    test_array (200*1000, array, 0, 3);
    test_array (200*1000, array, 100*1000, 3);
    g_free (array);
  }
  {
    guint *array = g_new (guint, 512);
    test_array (512, array, 0, 1);
    test_array (512, array, 100, 1);
    test_array (512, array, 0, 3);
    test_array (512, array, 100, 3);
    g_free (array);
  }
  return 0;
}
