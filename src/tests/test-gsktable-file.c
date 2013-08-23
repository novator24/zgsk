/* TODO: test reader api */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "gskinit.h"
#include "gsktable.h"
#include "gsktable-file.h"

static void gen_kv_0 (guint   index,
                      GByteArray *key,
                      GByteArray *value)
{
  guint32 index_be32 = GUINT32_TO_BE (index);
  guint32 suffix_len = index % 33;
  char suffix_char = G_CSET_a_2_z[index % 26];
  char value_buf[64];
  g_byte_array_set_size (key, 4 + suffix_len);
  memcpy (key->data, &index_be32, 4);
  memset (key->data + 4, suffix_char, suffix_len);
  g_snprintf (value_buf, sizeof (value_buf), "%u-%x", index,index);
  g_byte_array_set_size (value, strlen (value_buf));
  memcpy (value->data, value_buf, value->len);
}

typedef void (*GenKeyValue) (guint index,
                             GByteArray *key_out,
                             GByteArray *value_out);
static void
inject_entries (GskTableFile *file,
                GenKeyValue   gen_kv,
                guint         start,
                guint         end)
{
  guint i;
  GByteArray *key = g_byte_array_new ();
  GByteArray *value = g_byte_array_new ();
  for (i = start; i < end; i++)
    {
      GError *error = NULL;
      gen_kv (i, key, value);
      switch (gsk_table_file_feed_entry (file, key->len, key->data, value->len, value->data, &error))
        {
        case GSK_TABLE_FEED_ENTRY_WANT_MORE:
        case GSK_TABLE_FEED_ENTRY_SUCCESS:
          break;
        default:
          g_error ("gsk_table_file_feed_entry: %s", error->message);
        }
    }
  g_byte_array_free (key, TRUE);
  g_byte_array_free (value, TRUE);
}
static void
inject_entries_til_success (GskTableFile *file,
                            GenKeyValue   gen_kv,
                            guint         start,
                            guint        *end_out)
{
  guint i = start;
  GByteArray *key = g_byte_array_new ();
  GByteArray *value = g_byte_array_new ();
  while (TRUE)
    {
      GError *error = NULL;
      gen_kv (i++, key, value);
      switch (gsk_table_file_feed_entry (file, key->len, key->data, value->len, value->data, &error))
        {
        case GSK_TABLE_FEED_ENTRY_SUCCESS:
          *end_out = i;
          g_byte_array_free (key, TRUE);
          g_byte_array_free (value, TRUE);
          return;
        case GSK_TABLE_FEED_ENTRY_WANT_MORE:
          break;
        default:
          g_error ("gsk_table_file_feed_entry: %s", error->message);
        }
    }
}

static gint
compare_by_memcmp (guint         test_key_len,
                   const guint8 *test_key,
                   gpointer      compare_data)
{
  GByteArray *array = compare_data;
  if (test_key_len > array->len)
    {
      int rv = memcmp (test_key, array->data, array->len);
      return rv ? rv : 1;
    }
  else if (test_key_len < array->len)
    {
      int rv = memcmp (test_key, array->data, test_key_len);
      return rv ? rv : -1;
    }
  else
    return memcmp (test_key, array->data, test_key_len);
}

static void
query_entries (GskTableFile *file,
               GenKeyValue   gen_kv,
               guint         start,
               guint         end,
               guint         step)
{
  GByteArray *key = g_byte_array_new ();
  GByteArray *value = g_byte_array_new ();
  guint i;
  GskTableFileQuery query = GSK_TABLE_FILE_QUERY_INIT;
  GError *error = NULL;
  query.compare = compare_by_memcmp;
  query.compare_data = key;

  for (i = start; i < end; i += step)
    {
      gen_kv (i, key, value);
      if (!gsk_table_file_query (file, &query, &error))
        g_error ("gsk_table_file_query: %s", error->message);
    }
  g_byte_array_free (key, TRUE);
  g_byte_array_free (value, TRUE);
  gsk_table_file_query_clear (&query);
}

static void
finish_file (GskTableFile *file)
{
  gboolean ready;
  GError *error = NULL;
  if (!gsk_table_file_done_feeding (file, &ready, &error))
    g_error ("gsk_table_file_done_feeding: %s", error->message);
  while (!ready)
    if (!gsk_table_file_build_file (file, &ready, &error))
      g_error ("gsk_table_file_build_file: %s", error->message);
}

static void
run_test (GskTableFileFactory *factory,
          const char          *dir,
          guint64              id)
{
  GskTableFile *file;
  GskTableFileHints hints = GSK_TABLE_FILE_HINTS_DEFAULTS;
  GError *error = NULL;
  guint end;

  /* --- small test --- */
  file = gsk_table_file_factory_create_file (factory, dir, id, &hints, &error);
  if (file == NULL)
    g_error ("gsk_table_file_factory_create_file: %s", error->message);
  inject_entries (file, gen_kv_0, 0, 20);
  inject_entries_til_success (file, gen_kv_0, 20, &end);
  query_entries (file, gen_kv_0, 0, end, 1);

  {
    guint state_len;
    guint8 *state_data;
    if (!gsk_table_file_get_build_state (file, &state_len, &state_data, &error))
      g_error ("gsk_table_file_get_build_state: %s", error->message);
    if (!gsk_table_file_destroy (file, dir, FALSE, &error))
      g_error ("gsk_table_file_destroy: %s", error->message);
    file = gsk_table_file_factory_open_building_file (factory, dir, id, state_len, state_data, &error);
    if (file == NULL)
      g_error ("gsk_table_file_factory_open_building_file: %s", error->message);
    g_free (state_data);
  }

  query_entries (file, gen_kv_0, 0, end, 1);
  inject_entries (file, gen_kv_0, end, end + 20);
  end += 20;
  inject_entries_til_success (file, gen_kv_0, end, &end);
  query_entries (file, gen_kv_0, 0, end, 1);
  inject_entries (file, gen_kv_0, end, end + 1);
  end += 1;
  finish_file (file);
  query_entries (file, gen_kv_0, 0, end, 1);

  if (!gsk_table_file_destroy (file, dir, FALSE, &error))
    g_error ("gsk_table_file_destroy: %s", error->message);
  file = gsk_table_file_factory_open_file (factory, dir, id, &error);
  if (file == NULL)
    g_error ("gsk_table_file_factory_open_building_file: %s", error->message);

  query_entries (file, gen_kv_0, 0, end, 1);

  if (!gsk_table_file_destroy (file, dir, TRUE, &error))
    g_error ("gsk_table_file_destroy: %s", error->message);
}
static void
run_test_big (GskTableFileFactory *factory,
              const char          *dir,
              guint64              id)
{
  GskTableFile *file;
  GskTableFileHints hints = GSK_TABLE_FILE_HINTS_DEFAULTS;
  GError *error = NULL;
  guint end;

  file = gsk_table_file_factory_create_file (factory, dir, id, &hints, &error);
  if (file == NULL)
    g_error ("gsk_table_file_factory_create_file: %s", error->message);
  inject_entries (file, gen_kv_0, 0, 1000*1000);
  inject_entries_til_success (file, gen_kv_0, 1000*1000, &end);
  query_entries (file, gen_kv_0, 0, end, 101);
  inject_entries (file, gen_kv_0, end, 4*1000*1000);
  inject_entries_til_success (file, gen_kv_0, 4*1000*1000, &end);
  query_entries (file, gen_kv_0, 0, end, 503);

  {
    guint state_len;
    guint8 *state_data;
    if (!gsk_table_file_get_build_state (file, &state_len, &state_data, &error))
      g_error ("gsk_table_file_get_build_state: %s", error->message);
    if (!gsk_table_file_destroy (file, dir, FALSE, &error))
      g_error ("gsk_table_file_destroy: %s", error->message);
    file = gsk_table_file_factory_open_building_file (factory, dir, id, state_len, state_data, &error);
    if (file == NULL)
      g_error ("gsk_table_file_factory_open_building_file: %s", error->message);
    g_free (state_data);
  }

  query_entries (file, gen_kv_0, 0, end, 503);
  inject_entries (file, gen_kv_0, end, end+51);
  finish_file (file);
  query_entries (file, gen_kv_0, 0, end, end+51);
  end += 51;
  query_entries (file, gen_kv_0, 0, end, 503);

  if (!gsk_table_file_destroy (file, dir, TRUE, &error))
    g_error ("gsk_table_file_destroy: %s", error->message);
}

int
main(int    argc,
     char **argv)
{
  GskTableFileFactory *factory;
  char *dir;
  gsk_init_without_threads (&argc, &argv);
  dir = g_strdup_printf ("test-table-dir-%08x", (guint32) g_random_int ());
  if (mkdir (dir, 0755) < 0)
    g_error ("error making test dir %s: %s", dir, g_strerror (errno));
  factory = gsk_table_file_factory_new_flat ();

  g_printerr ("running small test... ");
  run_test (factory, dir, 1000);
  g_printerr ("done.\n");

  g_printerr ("running big test... ");
  run_test_big (factory, dir, 1001);
  g_printerr ("done.\n");

  if (rmdir (dir) < 0)
    g_error ("rmdir(%s) failed: %s", dir, g_strerror (errno));
  g_free (dir);
  return 0;
}
