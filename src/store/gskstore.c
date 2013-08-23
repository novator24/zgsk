#define G_IMPLEMENT_INLINES
#define __GSK_STORE_C__
#include <string.h>
#include "gskstreammap.h"
#include "../gskstreamtransferrequest.h"
#include "gskstore.h"

static GskStoreFormatEntry *
get_format_entry_for_type (GskStore *store, GType value_type, GError **error)
{
  GPtrArray *format_entries = store->format_entries;
  guint i;

  (void) error;
  g_return_val_if_fail (format_entries, NULL);
  for (;;)
    {
      for (i = 0; i < format_entries->len; ++i)
	{
	  GskStoreFormatEntry *entry = g_ptr_array_index (format_entries, i);
	  if (entry->value_type == value_type)
	    return entry;
	}
      if (value_type == G_TYPE_INVALID)
	break;
      value_type = g_type_parent (value_type);
    }
  return NULL;
}

static GskStoreFormatEntry *
get_format_entry_for_id (GskStore *store, guint32 format_id, GError **error)
{
  GPtrArray *format_entries = store->format_entries;
  guint i;

  (void) error;
  g_return_val_if_fail (format_entries, NULL);
  for (i = 0; i < format_entries->len; ++i)
    {
      GskStoreFormatEntry *entry = g_ptr_array_index (format_entries, i);
      if (entry->format_id == format_id)
	return entry;
    }
  return NULL;
}

/*
 *
 * Save
 *
 */

typedef struct _SaveInfo SaveInfo;

struct _SaveInfo
{
  GskStreamTransferRequest *xfer_request;
};

/* Signal handler invoked when the GskStreamTransferRequest is done
 * transferring the stream of serialized data to the write-stream
 * from the GskStreamMap.
 */
static void
save_handle_xfer_request_done (GskRequest *xfer_request, gpointer user_data)
{
  GskStoreRequest *store_request = GSK_STORE_REQUEST (user_data);
  SaveInfo *save_info = (SaveInfo *) store_request->private;

  g_return_if_fail (store_request->request_type == GSK_STORE_REQUEST_SAVE);
  g_return_if_fail (save_info->xfer_request ==
		    GSK_STREAM_TRANSFER_REQUEST (xfer_request));

  if (gsk_request_had_error (xfer_request))
    {
      GError *error;
      error = g_error_copy (gsk_request_get_error (xfer_request));
      gsk_request_set_error (store_request, error);
    }
  g_object_unref (xfer_request);
  save_info->xfer_request = NULL;

  gsk_request_done (store_request);
  g_object_unref (store_request);
}

static void
save_start (GskStoreRequest *store_request)
{
  SaveInfo *save_info = (SaveInfo *) store_request->private;
  GskStreamTransferRequest *xfer_request;

  g_return_if_fail (store_request->request_type == GSK_STORE_REQUEST_SAVE);
  xfer_request = save_info->xfer_request;
  g_return_if_fail (xfer_request);

  g_object_ref (store_request);
  g_signal_connect (xfer_request,
		    "done",
		    G_CALLBACK (save_handle_xfer_request_done),
		    store_request);
  gsk_request_start (xfer_request);
}

GskStoreRequest *
gsk_store_save (GskStore      *store,
		const char    *key,
		const GValue  *value,
		GError       **error)
{
  GskStreamMap *stream_map = store->stream_map;
  GskStoreFormatEntry *format_entry;
  GskStream *serialize_stream;
  GskStream *write_stream;
  GskStreamTransferRequest *xfer_request;
  GskStoreRequest *store_request;
  SaveInfo *save_info;

  format_entry = get_format_entry_for_type (store, G_VALUE_TYPE (value), error);
  if (format_entry == NULL)
    {
      g_return_val_if_fail (error == NULL || *error, NULL);
      return NULL;
    }
  g_return_val_if_fail (format_entry->storage_format, NULL);
  serialize_stream = gsk_storage_format_serialize (format_entry->storage_format,
						   value,
						   error);
  if (serialize_stream == NULL)
    {
      g_return_val_if_fail (error == NULL || *error, NULL);
      return NULL;
    }
  write_stream = gsk_stream_map_set (stream_map, key, error);
  if (write_stream == NULL)
    {
      g_object_unref (serialize_stream);
      g_return_val_if_fail (error == NULL || *error, NULL);
      return NULL;
    }
  xfer_request =
    gsk_stream_transfer_request_new (serialize_stream, write_stream);
  g_return_val_if_fail (xfer_request, NULL);
  g_object_unref (serialize_stream);
  g_object_unref (write_stream);

/* HACK: maybe GskStreamTransferRequest should support this. */
  {
    guint32 format_id_out = format_entry->format_id;

#if G_BYTE_ORDER != G_LITTLE_ENDIAN
    format_id_out = GUINT32_TO_LE (format_id_out);
#endif
    gsk_buffer_append (&xfer_request->buffer,
		       &format_id_out,
		       sizeof (format_id_out));
  }

  save_info = g_new0 (SaveInfo, 1);
  save_info->xfer_request = xfer_request;

  store_request = g_object_new (GSK_TYPE_STORE_REQUEST, NULL);
  store_request->request_type = GSK_STORE_REQUEST_SAVE;
  store_request->key = g_strdup (key);
  store_request->private = save_info;
  return store_request;
}

/*
 *
 * Load
 *
 */

typedef struct _LoadInfo LoadInfo;

struct _LoadInfo
{
  GType value_type;
  GskStream *read_stream;
  GskStore *store;
};

static void
load_handle_deserialize_request_done (GskRequest *request, gpointer user_data)
{
  GskStoreRequest *store_request = GSK_STORE_REQUEST (user_data);
  GskValueRequest *deserialize_request = GSK_VALUE_REQUEST (request);
  GError *error = NULL;
  const GValue *value;

  if (gsk_request_had_error (deserialize_request))
    {
      error = g_error_copy (gsk_request_get_error (deserialize_request));
      gsk_request_set_error (store_request, error);
      gsk_request_done (store_request);
      g_object_unref (store_request);
      return;
    }

  value = gsk_value_request_get_value (deserialize_request);
  if (value == NULL)
    {
      error = g_error_new (GSK_G_ERROR_DOMAIN,
			   GSK_ERROR_UNKNOWN, /* TODO: code */
			   "couldn't get value from a %s",
			   g_type_name (G_OBJECT_TYPE (deserialize_request)));
      gsk_request_set_error (store_request, error);
      gsk_request_done (store_request);
      g_object_unref (store_request);
      return;
    }

  g_value_init (&store_request->value_request.value, G_VALUE_TYPE (value));
  g_value_copy (value, &store_request->value_request.value);
  gsk_request_done (store_request);
  g_object_unref (store_request);
}

static gboolean
load_handle_input_is_readable (GskIO *io, gpointer user_data)
{
  GskStoreRequest *store_request = GSK_STORE_REQUEST (user_data);
  LoadInfo *load_info = (LoadInfo *) store_request->private;
  GskStream *read_stream;
  guint32 format_id;
  GskStoreFormatEntry *format_entry;
  GskValueRequest *deserialize_request;
  GError *error = NULL;
  guint num_read;

  g_return_val_if_fail (store_request->request_type == GSK_STORE_REQUEST_LOAD,
			FALSE);
  g_return_val_if_fail (load_info, FALSE);
  read_stream = load_info->read_stream;
  g_return_val_if_fail (read_stream == GSK_STREAM (io), FALSE);

  /* XXX: We assume we can read 4 bytes in one try. Probably safe for
   * any reasonable stream, but...
   */
  num_read = gsk_stream_read (read_stream,
			      &format_id,
			      sizeof (format_id),
			      &error);
  if (error)
    {
      gsk_request_set_error (store_request, error);
      gsk_request_done (store_request);
      return FALSE;
    }
  g_return_val_if_fail (num_read == sizeof (format_id), FALSE);
#if G_BYTE_ORDER != G_LITTLE_ENDIAN
  format_id = GUINT32_FROM_LE (format_id);
#endif

  gsk_io_untrap_readable (read_stream);

  g_return_val_if_fail (load_info->store, FALSE);
  format_entry = get_format_entry_for_id (load_info->store, format_id, &error);
  if (format_entry == NULL)
    {
      gsk_request_set_error (store_request, error);
      gsk_request_done (store_request);
      return FALSE;
    }

  deserialize_request =
    gsk_storage_format_deserialize (format_entry->storage_format,
				    read_stream,
				    load_info->value_type,
				    &error);
  if (deserialize_request == NULL)
    {
      gsk_request_set_error (store_request, error);
      gsk_request_done (store_request);
      g_return_val_if_fail (error, FALSE);
      return FALSE;
    }

  g_object_ref (store_request);
  g_signal_connect (deserialize_request,
		    "done",
		    G_CALLBACK (load_handle_deserialize_request_done),
		    store_request);
  gsk_request_start (deserialize_request);

  /* Note: we've already untrapped the hook above, so the return value
   * is meaningless.
   */
  return FALSE;
}

static gboolean
load_handle_input_shutdown_read (GskIO *io, gpointer user_data)
{
  GskStoreRequest *store_request = GSK_STORE_REQUEST (user_data);
  GError *error;

  (void) io;
  g_return_val_if_fail (store_request->request_type == GSK_STORE_REQUEST_LOAD,
			FALSE);

  error = g_error_new (GSK_G_ERROR_DOMAIN,
		       GSK_ERROR_UNKNOWN, /* TODO: code */
		       "premature end of stream (%s)",
		       g_type_name (G_OBJECT_TYPE (io)));
  gsk_request_set_error (store_request, error);
  gsk_request_done (store_request);
  return FALSE;
}

static void
load_handle_input_is_readable_destroy (gpointer user_data)
{
  GskStoreRequest *store_request = GSK_STORE_REQUEST (user_data);
  LoadInfo *load_info = (LoadInfo *) store_request->private;

  g_return_if_fail (store_request->request_type == GSK_STORE_REQUEST_LOAD);
  g_return_if_fail (load_info);

  g_object_unref (load_info->read_stream);
  load_info->read_stream = NULL;
  g_object_unref (store_request);
}

static void
load_start (GskStoreRequest *store_request)
{
  LoadInfo *load_info = (LoadInfo *) store_request->private;
  GskStream *read_stream;

  g_return_if_fail (store_request->request_type == GSK_STORE_REQUEST_LOAD);
  g_return_if_fail (load_info);
  read_stream = load_info->read_stream;
  g_return_if_fail (read_stream);
  g_return_if_fail (gsk_stream_get_is_readable (read_stream));
  g_return_if_fail (!gsk_io_has_read_hook (read_stream));

  g_object_ref (store_request);
  gsk_io_trap_readable (GSK_IO (read_stream),
			load_handle_input_is_readable,
			load_handle_input_shutdown_read,
			store_request,
			load_handle_input_is_readable_destroy);
}

GskStoreRequest *
gsk_store_load (GskStore    *store,
		const char  *key,
		GType        value_type,
		GError     **error)
{
  GskStreamMap *stream_map = store->stream_map;
  GskStream *read_stream;
  GskStoreRequest *store_request;
  LoadInfo *load_info;

  read_stream = gsk_stream_map_get (stream_map, key, error);
  if (read_stream == NULL)
    {
      g_return_val_if_fail (error == NULL || *error, NULL);
      return NULL;
    }

  load_info = g_new0 (LoadInfo, 1);
  load_info->store = store;
  g_object_ref (store);
  load_info->value_type = value_type;
  load_info->read_stream = read_stream;

  store_request = g_object_new (GSK_TYPE_STORE_REQUEST, NULL);
  store_request->request_type = GSK_STORE_REQUEST_LOAD;
  store_request->key = g_strdup (key);
  store_request->private = load_info;
  return store_request;
}

/*
 *
 * Delete
 *
 */

typedef struct _DeleteInfo DeleteInfo;

struct _DeleteInfo
{
  GskStreamMapRequest *delete_request;
};

/* Signal handler invoked when the GskStreamMapRequest is done. */
static void
delete_handle_request_done (GskRequest *delete_request, gpointer user_data)
{
  GskStoreRequest *store_request = GSK_STORE_REQUEST (user_data);
  DeleteInfo *delete_info = (DeleteInfo *) store_request->private;

  g_return_if_fail (store_request->request_type == GSK_STORE_REQUEST_DELETE);
  g_return_if_fail (delete_info);
  g_return_if_fail (delete_info->delete_request ==
		    GSK_STREAM_MAP_REQUEST (delete_request));

  g_object_unref (store_request);
  if (gsk_request_had_error (delete_request))
    {
      GError *error;
      error = g_error_copy (gsk_request_get_error (delete_request));
      gsk_request_set_error (store_request, error);
    }
  g_object_unref (delete_request);
  delete_info->delete_request = NULL;

  gsk_request_done (store_request);
}

static void
delete_start (GskStoreRequest *store_request)
{
  DeleteInfo *delete_info = (DeleteInfo *) store_request->private;

  g_return_if_fail (store_request->request_type == GSK_STORE_REQUEST_DELETE);
  g_return_if_fail (delete_info);

  g_object_ref (store_request);
  g_signal_connect (delete_info->delete_request,
		    "done",
		    G_CALLBACK (delete_handle_request_done),
		    store_request);
  gsk_request_start (delete_info->delete_request);
}

GskStoreRequest *
gsk_store_delete (GskStore    *store,
		  const char  *key,
		  GError     **error)
{
  GskStreamMap *stream_map = store->stream_map;
  GskStreamMapRequest *delete_request;
  DeleteInfo *delete_info;
  GskStoreRequest *store_request;

  delete_request = gsk_stream_map_delete (stream_map, key, error);
  if (delete_request == NULL)
    {
      g_return_val_if_fail (error == NULL || *error, NULL);
      return NULL;
    }

  delete_info = g_new0 (DeleteInfo, 1);
  delete_info->delete_request = delete_request;

  store_request = g_object_new (GSK_TYPE_STORE_REQUEST, NULL);
  store_request->request_type = GSK_STORE_REQUEST_DELETE;
  store_request->key = g_strdup (key);
  store_request->private = delete_info;
  return store_request;
}

/*
 *
 * Exists
 *
 */

typedef struct _ExistsInfo ExistsInfo;

struct _ExistsInfo
{
  GskStreamMapRequest *exists_request;
};

/* Signal handler invoked when the GskStreamMapRequest is done. */
static void
exists_handle_request_done (GskRequest *exists_request, gpointer user_data)
{
  GskStoreRequest *store_request = GSK_STORE_REQUEST (user_data);
  ExistsInfo *exists_info = (ExistsInfo *) store_request->private;

  g_return_if_fail (store_request->request_type == GSK_STORE_REQUEST_EXISTS);
  g_return_if_fail (exists_info);
  g_return_if_fail (exists_info->exists_request ==
		    GSK_STREAM_MAP_REQUEST (exists_request));

  g_object_unref (store_request);
  if (gsk_request_had_error (exists_request))
    {
      GError *error;
      error = g_error_copy (gsk_request_get_error (exists_request));
      gsk_request_set_error (store_request, error);
    }
  g_value_init (&store_request->value_request.value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&store_request->value_request.value,
		       gsk_stream_map_request_get_exists (exists_request));
  g_object_unref (exists_request);
  exists_info->exists_request = NULL;

  gsk_request_done (store_request);
}

static void
exists_start (GskStoreRequest *store_request)
{
  ExistsInfo *exists_info = (ExistsInfo *) store_request->private;

  g_return_if_fail (store_request->request_type == GSK_STORE_REQUEST_EXISTS);
  g_return_if_fail (exists_info);

  g_object_ref (store_request);
  g_signal_connect (exists_info->exists_request,
		    "done",
		    G_CALLBACK (exists_handle_request_done),
		    store_request);
  gsk_request_start (exists_info->exists_request);
}

GskStoreRequest *
gsk_store_exists (GskStore    *store,
		  const char  *key,
		  GError     **error)
{
  GskStreamMap *stream_map = store->stream_map;
  GskStreamMapRequest *exists_request;
  ExistsInfo *exists_info;
  GskStoreRequest *store_request;

  exists_request = gsk_stream_map_exists (stream_map, key, error);
  if (exists_request == NULL)
    {
      g_return_val_if_fail (error == NULL || *error, NULL);
      return NULL;
    }

  exists_info = g_new0 (ExistsInfo, 1);
  exists_info->exists_request = exists_request;

  store_request = g_object_new (GSK_TYPE_STORE_REQUEST, NULL);
  store_request->request_type = GSK_STORE_REQUEST_EXISTS;
  store_request->key = g_strdup (key);
  store_request->private = exists_info;
  return store_request;
}

GType
gsk_store_get_type (void)
{
  static GType type = 0;
  if (type == 0)
    {
      static const GTypeInfo info =
	{
	  sizeof (GskStoreClass),
	  (GBaseInitFunc) NULL,
	  (GBaseFinalizeFunc) NULL,
	  (GClassInitFunc) NULL,
	  NULL,		/* class_finalize */
	  NULL,		/* class_data */
	  sizeof (GskStore),
	  0,		/* n_preallocs */
	  (GInstanceInitFunc) NULL,
	  NULL		/* value_table */
	};
      type = g_type_register_static (G_TYPE_OBJECT,
				     "GskStore",
				     &info,
				     0);
    }
  return type;
}

/*
 *
 * GskStoreRequest
 *
 */

static GObjectClass *gsk_store_request_parent_class = NULL;

/* GskRequest methods. */
/* TODO: cancelled methods */

static void
gsk_store_request_cancelled (GskRequest *request)
{
  GskStoreRequest *store_request = GSK_STORE_REQUEST (request);
  switch (store_request->request_type)
    {
      case GSK_STORE_REQUEST_LOAD:
//	load_cancel (store_request);
	break;
      case GSK_STORE_REQUEST_SAVE:
//	save_cancel (store_request);
	break;
      case GSK_STORE_REQUEST_DELETE:
//	delete_cancel (store_request);
	break;
      case GSK_STORE_REQUEST_EXISTS:
//	exists_cancel (store_request);
	break;
      default:
        g_return_if_reached ();
        break;
    }
}

static void
gsk_store_request_start (GskRequest *request)
{
  GskStoreRequest *store_request = GSK_STORE_REQUEST (request);
  switch (store_request->request_type)
    {
      case GSK_STORE_REQUEST_LOAD:
	load_start (store_request);
	break;
      case GSK_STORE_REQUEST_SAVE:
	save_start (store_request);
	break;
      case GSK_STORE_REQUEST_DELETE:
	delete_start (store_request);
	break;
      case GSK_STORE_REQUEST_EXISTS:
	exists_start (store_request);
	break;
      default:
        g_return_if_reached ();
        break;
    }
}

/* GObject methods. */

static void
gsk_store_request_finalize (GObject *object)
{
  GskStoreRequest *store_request = GSK_STORE_REQUEST (object);

  if (store_request->key)
    g_free (store_request->key);

  switch (store_request->request_type)
    {
      case GSK_STORE_REQUEST_LOAD:
	/* TODO */
	break;
      case GSK_STORE_REQUEST_SAVE:
	/* TODO */
	break;
      case GSK_STORE_REQUEST_DELETE:
	/* TODO */
	break;
      case GSK_STORE_REQUEST_EXISTS:
	/* TODO */
	break;
      default:
	g_return_if_reached ();
        break;
    }
  (*gsk_store_request_parent_class->finalize) (object);
}

static void
gsk_store_request_class_init (GskRequestClass *request_class)
{
  gsk_store_request_parent_class = g_type_class_peek_parent (request_class);

  G_OBJECT_CLASS (request_class)->finalize = gsk_store_request_finalize;
  request_class->start = gsk_store_request_start;
  request_class->cancelled = gsk_store_request_cancelled;
}

GType
gsk_store_request_get_type (void)
{
  static GType type = 0;
  if (G_UNLIKELY (type == 0))
    {
      static const GTypeInfo type_info =
	{
	  sizeof (GskStoreRequestClass),
	  (GBaseInitFunc) NULL,
	  (GBaseFinalizeFunc) NULL,
	  (GClassInitFunc) gsk_store_request_class_init,
	  NULL,		/* class_finalize */
	  NULL,		/* class_data */
	  sizeof (GskStoreRequest),
	  0,		/* n_preallocs */
	  (GInstanceInitFunc) NULL,
	  NULL		/* value_table */
	};
      type = g_type_register_static (GSK_TYPE_VALUE_REQUEST,
				     "GskStoreRequest",
				     &type_info,
				     0);
    }
  return type;
}

/*
 *
 * GskStoreFormatEntry
 *
 */

static GObjectClass *gsk_store_format_entry_parent_class = NULL;

/* GObject methods. */

static void
gsk_store_format_entry_finalize (GObject *object)
{
  GskStoreFormatEntry *store_format_entry = GSK_STORE_FORMAT_ENTRY (object);

  if (store_format_entry->storage_format)
    g_object_unref (store_format_entry->storage_format);

  (*gsk_store_format_entry_parent_class->finalize) (object);
}

static void
gsk_store_format_entry_class_init (GObjectClass *object_class)
{
  gsk_store_format_entry_parent_class = g_type_class_peek_parent (object_class);
  object_class->finalize = gsk_store_format_entry_finalize;
}

GType
gsk_store_format_entry_get_type (void)
{
  static GType type = 0;
  if (G_UNLIKELY (type == 0))
    {
      static const GTypeInfo type_info =
	{
	  sizeof (GskStoreFormatEntryClass),
	  (GBaseInitFunc) NULL,
	  (GBaseFinalizeFunc) NULL,
	  (GClassInitFunc) gsk_store_format_entry_class_init,
	  NULL,		/* class_finalize */
	  NULL,		/* class_data */
	  sizeof (GskStoreFormatEntry),
	  0,		/* n_preallocs */
	  (GInstanceInitFunc) NULL,
	  NULL		/* value_table */
	};
      type = g_type_register_static (G_TYPE_OBJECT,
				     "GskStoreFormatEntry",
				     &type_info,
				     0);
    }
  return type;
}
