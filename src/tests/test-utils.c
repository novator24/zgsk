#include <glib.h>
#include "../gskutils.h"
#include "../common/gsktimegm.h"
#include <string.h>

int main ()
{
  char *tmp;
  guint8 *tmp2;
  char *tmp3;
  gsize rv;
  GError *error = NULL;

  tmp = gsk_escape_memory ((guint8*)"abc\"\\\0", 6);
  g_assert (strcmp (tmp, "abc\\\"\\\\\\0") == 0);
  g_free (tmp);

  tmp = gsk_escape_memory_hex ((guint8*)"ABC012", 7);
  g_assert (strcmp (tmp, "41424330313200") == 0);
  tmp2 = gsk_unescape_memory_hex (tmp, -1, &rv, &error);
  g_assert (tmp2 != NULL);
  g_assert (rv == 7);
  g_assert (memcmp ("ABC012", tmp2, rv) == 0);
  g_free (tmp);
  g_free (tmp2);

  tmp = g_strdup_printf ("dir-%d", g_random_int ());
  g_assert (!g_file_test (tmp, G_FILE_TEST_EXISTS));
  tmp3 = g_strdup_printf ("%s/a/b/c/d/e/f/g/h/j/i", tmp);
  if (!gsk_mkdir_p (tmp3, 0755, &error))
    g_error ("error making %s: %s", tmp3, error->message);
  g_assert (g_file_test (tmp, G_FILE_TEST_IS_DIR));
  g_assert (g_file_test (tmp3, G_FILE_TEST_IS_DIR));
  g_free (tmp3);
  tmp3 = g_strdup_printf ("%s/A/B/C/D/E/F/G/H/J/I", tmp);
  if (!gsk_mkdir_p (tmp3, 0755, &error))
    g_error ("error making %s: %s", tmp, error->message);
  g_assert (g_file_test (tmp, G_FILE_TEST_IS_DIR));
  g_assert (g_file_test (tmp3, G_FILE_TEST_IS_DIR));
  g_free (tmp3);
  if (!gsk_rm_rf (tmp, &error))
    g_error ("error deleting %s: %s", tmp, error->message);
  g_assert (!g_file_test (tmp, G_FILE_TEST_EXISTS));
  g_free (tmp);

  {
    struct tm tm;
    memset (&tm,0,sizeof(tm));
    tm.tm_year = 70;
    tm.tm_mday = 1;
    g_assert(gsk_timegm (&tm) == 0);
    tm.tm_year = 71;
    g_assert(gsk_timegm (&tm) == 365*86400);
    tm.tm_year = 80;
    g_assert(gsk_timegm (&tm) == (365*10+2)*86400);

    /* test against a precomputed value */
    tm.tm_year = 100;
    tm.tm_mday = 1;
    tm.tm_mon = 8;
    tm.tm_mday = 3;
    tm.tm_hour = 3;
    tm.tm_min = 34;
    tm.tm_sec = 17;
    g_assert (gsk_timegm(&tm) == 967952057);
  }
  
  return 0;
}
