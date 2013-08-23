#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "../gskinit.h"
#include "../gskmain.h"
#include "../gskdebug.h"
#include "../store/gskfilestreammap.h"
#include "../store/gskxmlformat.h"
#include "../store/gskstore.h"
#include "testobject.h"

static gboolean had_error = FALSE;
static guint num_tests_pending = 0;

typedef struct _TestInfo TestInfo;

struct _TestInfo
{
  guint test_number;
  GskStore *store;
  char *key;
  TestObject *object;
  GskStoreRequest *save_request;
  GskStoreRequest *load_request;
  GskStoreRequest *exists_request;
  GskStoreRequest *delete_request;
};

static void
handle_delete_request_done (GskRequest *delete_request, gpointer user_data)
{
  TestInfo *test_info = (TestInfo *) user_data;
  guint parsed_number;

  g_return_if_fail (delete_request == GSK_REQUEST (test_info->delete_request));
  parsed_number =
    strtoul (gsk_store_request_get_key (delete_request), NULL, 10);
  g_return_if_fail (parsed_number == test_info->test_number);

  if (gsk_request_had_error (delete_request))
    {
      const GError *error;
      error = gsk_request_get_error (delete_request);
      g_warning ("%u delete request failed: %s",
		 test_info->test_number,
		 error ? error->message : "(no message)");
      had_error = TRUE;
      gsk_main_quit ();
      return;
    }

  fprintf (stderr, ".");
  if (--num_tests_pending == 0)
    gsk_main_quit ();
}

static void
handle_exists_request_done (GskRequest *exists_request, gpointer user_data)
{
  TestInfo *test_info = (TestInfo *) user_data;
  guint parsed_number;
  GskStoreRequest *delete_request;
  GError *error = NULL;

  g_return_if_fail (exists_request == GSK_REQUEST (test_info->exists_request));
  if (gsk_request_had_error (exists_request))
    {
      const GError *err;
      err = gsk_request_get_error (exists_request);
      g_warning ("%u exists request failed: %s",
		 test_info->test_number,
		 err ? err->message : "(no message)");
      had_error = TRUE;
      gsk_main_quit ();
      return;
    }
  parsed_number =
    strtoul (gsk_store_request_get_key (exists_request), NULL, 10);
  g_return_if_fail (parsed_number == test_info->test_number);

  if (!gsk_store_request_get_exists (exists_request))
    {
      g_warning ("test %u key did not exist", test_info->test_number);
      had_error = TRUE;
      gsk_main_quit ();
      return;
    }

  g_object_unref (exists_request);
  test_info->exists_request = NULL;

  delete_request = gsk_store_delete (test_info->store, test_info->key, &error);
  if (delete_request == NULL)
    {
      g_warning ("test %u failed getting delete request: %s",
		 test_info->test_number,
		 error ? error->message : "(no message)");
      had_error = TRUE;
      gsk_main_quit ();
      return;
    }
  test_info->delete_request = delete_request;

  g_signal_connect (delete_request,
		    "done",
		    G_CALLBACK (handle_delete_request_done),
		    test_info);
  gsk_request_start (delete_request);
}

static void
handle_load_request_done (GskRequest *load_request, gpointer user_data)
{
  TestInfo *test_info = (TestInfo *) user_data;
  guint parsed_number;
  TestObject *loaded_object;
  GskStoreRequest *exists_request;
  GError *error = NULL;

  g_return_if_fail (load_request == GSK_REQUEST (test_info->load_request));
  if (gsk_request_had_error (load_request))
    {
      const GError *err;
      err = gsk_request_get_error (load_request);
      g_warning ("%u load request failed: %s",
		 test_info->test_number,
		 err ? err->message : "(no message)");
      had_error = TRUE;
      gsk_main_quit ();
      return;
    }
  parsed_number = strtoul (gsk_store_request_get_key (load_request), NULL, 10);
  g_return_if_fail (parsed_number == test_info->test_number);

  loaded_object = gsk_store_request_get_object (load_request);
  g_return_if_fail (loaded_object);

  if (!test_objects_equal (loaded_object, test_info->object, TRUE))
    {
      g_warning ("test %u loaded object not equal to original",
		 test_info->test_number);
      had_error = TRUE;
      gsk_main_quit ();
      return;
    }

  g_object_unref (load_request);
  test_info->load_request = NULL;

  exists_request = gsk_store_exists (test_info->store, test_info->key, &error);
  if (exists_request == NULL)
    {
      g_warning ("test %u failed getting exists request: %s",
		 test_info->test_number,
		 error ? error->message : "(no message)");
      had_error = TRUE;
      gsk_main_quit ();
      return;
    }
  test_info->exists_request = exists_request;

  g_signal_connect (exists_request,
		    "done",
		    G_CALLBACK (handle_exists_request_done),
		    test_info);
  gsk_request_start (exists_request);
}

static void
handle_save_request_done (GskRequest *save_request, gpointer user_data)
{
  TestInfo *test_info = (TestInfo *) user_data;
  GskStoreRequest *load_request;
  guint parsed_number;
  GError *error = NULL;

  g_return_if_fail (save_request == GSK_REQUEST (test_info->save_request));
  if (gsk_request_had_error (save_request))
    {
      const GError *err;
      err = gsk_request_get_error (save_request);
      g_warning ("%u save request failed: %s",
		 test_info->test_number,
		 err ? err->message : "(no message)");
      had_error = TRUE;
      gsk_main_quit ();
      return;
    }
  parsed_number = strtoul (gsk_store_request_get_key (save_request), NULL, 10);
  g_return_if_fail (parsed_number == test_info->test_number);

  g_object_unref (save_request);
  test_info->save_request = NULL;

  load_request = gsk_store_load (test_info->store,
				 test_info->key,
				 TEST_OBJECT_TYPE,
				 &error);
  if (load_request == NULL)
    {
      g_warning ("test %u failed getting request to load object: %s",
		 test_info->test_number,
		 error ? error->message : "(no message)");
      had_error = TRUE;
      gsk_main_quit ();
      return;
    }
  test_info->load_request = load_request;

  g_signal_connect (load_request,
		    "done",
		    G_CALLBACK (handle_load_request_done),
		    test_info);
  gsk_request_start (load_request);
}

static void
start_test (GskStore *store,
    guint test_number,
    double p)
{
  TestInfo *test_info;
  TestObject *object;
  GskStoreRequest *save_request;
  char *key;
  GValue value = { 0, { { 0 }, { 0 } } };
  GError *error = NULL;

  object = test_object_random (p);
  g_value_init (&value, TEST_OBJECT_TYPE);
  g_value_set_object (&value, object);
  key = g_strdup_printf ("%u", test_number);
  save_request = gsk_store_save (store, key, &value, &error);
  if (save_request == NULL)
    {
      g_warning ("%u failed getting request to save object: %s",
		 test_number,
		 error ? error->message : "(no error)");
      had_error = TRUE;
      return;
    }

  test_info = g_new0 (TestInfo, 1);
  test_info->test_number = test_number;
  test_info->store = store;
  g_object_ref (store);
  test_info->key = key;
  test_info->object = object;
  test_info->save_request = save_request;

  g_signal_connect (save_request,
		    "done",
		    G_CALLBACK (handle_save_request_done),
		    test_info);
  ++num_tests_pending;
  gsk_request_start (save_request);
}

int
main (int argc, char *argv[])
{
  char *storedir;
  GskStreamMap *stream_map;
  GskStore *store;
  GskStorageFormat *storage_format;
  GskStoreFormatEntry *format_entry;
  guint i;

  srand48 (1);
  gsk_init_without_threads (&argc, &argv);
  //gsk_debug_set_flags (GSK_DEBUG_ALL);

  storedir = g_strdup_printf ("store.%d", getpid ());
  if (mkdir (storedir, 0700) != 0)
    {
      g_warning ("error creating directory %s", storedir);
      exit (1);
    }

  stream_map = GSK_STREAM_MAP (gsk_file_stream_map_new (storedir));
  g_return_val_if_fail (stream_map, 1);

  storage_format = g_object_new (GSK_TYPE_XML_FORMAT, NULL);
  g_return_val_if_fail (storage_format, 1);

  format_entry = g_object_new (GSK_TYPE_STORE_FORMAT_ENTRY, NULL);
  g_return_val_if_fail (format_entry, 1);
  format_entry->storage_format = storage_format;
  format_entry->format_id = 0;
  format_entry->value_type = G_TYPE_INVALID;

  store = g_object_new (GSK_TYPE_STORE, NULL);
  g_return_val_if_fail (store, 1);
  store->stream_map = stream_map;
  store->format_entries = g_ptr_array_new ();
  g_ptr_array_add (store->format_entries, format_entry);

  for (i = 0; i < 100; ++i)
    start_test (store, i, 0.99);

  gsk_main_run ();

  fprintf (stderr, "\n");
  rmdir (storedir);

  return had_error ? 1 : 0;
}
