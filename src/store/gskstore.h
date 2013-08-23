#ifndef __GSK_STORE_H_
#define __GSK_STORE_H_

/*
 * GskStore -- an object store.
 *
 * GskStoreRequest -- a request for some operation on a key from a GskStore.
 *
 * GskStoreFormatEntry -- associates a GskStorageFormat with an integer ID
 * and a GType.
 */

/*
 * Notes: user is responsible for maintaining a compatible list of
 * format entries on all clients using the store, with stable IDs, etc.
 * (Otherwise, we would also have to serialize the GskStorageFormats
 * using a known GskStorageFormat, etc.; having a user-specified
 * list seems less of a security risk.  But there's nothing preventing
 * someone from writing a "meta-format" that loads other formats
 * dynamically, if that's what they want.  The XML format is already
 * pretty damn open, too.)
 *
 * To figure out which format to use, we start with the value's
 * leaf type and work upwards, looking for a matching format entry.
 */

#include <glib-object.h>
#include "../gskstream.h"
#include "gskvaluerequest.h"
#include "gskstorageformat.h"
#include "gskstreammap.h"

G_BEGIN_DECLS

typedef GObjectClass                GskStoreClass;
typedef struct _GskStore            GskStore;

typedef GskRequestClass             GskStoreRequestClass;
typedef struct _GskStoreRequest     GskStoreRequest;

typedef GObjectClass                GskStoreFormatEntryClass;
typedef struct _GskStoreFormatEntry GskStoreFormatEntry;

typedef enum
  {
    GSK_STORE_REQUEST_LOAD,
    GSK_STORE_REQUEST_SAVE,
    GSK_STORE_REQUEST_DELETE,
    GSK_STORE_REQUEST_EXISTS
  }
GskStoreRequestType;

/*
 * GskStore
 */

GType gsk_store_get_type (void) G_GNUC_CONST;

#define GSK_TYPE_STORE (gsk_store_get_type ())
#define GSK_STORE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_STORE, GskStore))
#define GSK_STORE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_STORE, GskStoreClass))
#define GSK_STORE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_STORE, GskStoreClass))
#define GSK_IS_STORE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_STORE))
#define GSK_IS_STORE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_STORE))

struct _GskStore
{
  GObject object;

  GskStreamMap *stream_map;
  GPtrArray *format_entries; /* of GskStoreFormatEntry */
};

GskStoreRequest * gsk_store_save        (GskStore      *store,
					 const char    *key,
					 const GValue  *value,
					 GError       **error);

GskStoreRequest * gsk_store_load        (GskStore      *store,
					 const char    *key,
					 GType          value_type,
					 GError       **error);

GskStoreRequest * gsk_store_delete      (GskStore      *store,
					 const char    *key,
					 GError       **error);

GskStoreRequest * gsk_store_exists      (GskStore      *store,
					 const char    *key,
					 GError       **error);

G_INLINE_FUNC
GskStoreRequest * gsk_store_save_object (GskStore      *store,
					 const char    *key,
					 gpointer      object,
					 GError      **error);
/*
 * GskStoreRequest
 */

GType gsk_store_request_get_type (void) G_GNUC_CONST;

#define GSK_TYPE_STORE_REQUEST (gsk_store_request_get_type ())
#define GSK_STORE_REQUEST(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
			       GSK_TYPE_STORE_REQUEST, \
			       GskStoreRequest))
#define GSK_STORE_REQUEST_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
			    GSK_TYPE_STORE_REQUEST, \
			    GskStoreRequestClass))
#define GSK_STORE_REQUEST_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
			      GSK_TYPE_STORE_REQUEST, \
			      GskStoreRequestClass))
#define GSK_IS_STORE_REQUEST(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_STORE_REQUEST))
#define GSK_IS_STORE_REQUEST_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_STORE_REQUEST))

struct _GskStoreRequest
{
  GskValueRequest value_request;

  GskStoreRequestType request_type;
  char *key;
  gpointer private;
};

G_INLINE_FUNC
const char * gsk_store_request_get_key    (gpointer request);

G_INLINE_FUNC G_CONST_RETURN
GValue *     gsk_store_request_get_value  (gpointer request);

G_INLINE_FUNC
gboolean     gsk_store_request_get_exists (gpointer request);

G_INLINE_FUNC
gpointer     gsk_store_request_get_object (gpointer request);

/*
 * GskStoreFormatEntry
 */

GType gsk_store_format_entry_get_type (void) G_GNUC_CONST;

#define GSK_TYPE_STORE_FORMAT_ENTRY (gsk_store_format_entry_get_type ())
#define GSK_STORE_FORMAT_ENTRY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
			       GSK_TYPE_STORE_FORMAT_ENTRY, \
			       GskStoreFormatEntry))
#define GSK_STORE_FORMAT_ENTRY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
			    GSK_TYPE_STORE_FORMAT_ENTRY, \
			    GskStoreFormatEntryClass))
#define GSK_STORE_FORMAT_ENTRY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
			      GSK_TYPE_STORE_FORMAT_ENTRY, \
			      GskStoreFormatEntryClass))
#define GSK_IS_STORE_FORMAT_ENTRY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_STORE_FORMAT_ENTRY))
#define GSK_IS_STORE_FORMAT_ENTRY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_STORE_FORMAT_ENTRY))

struct _GskStoreFormatEntry
{
  GObject object;

  guint32 format_id;
  GType value_type;
  GskStorageFormat *storage_format;
};

/*
 * Inline functions.
 */

#if defined (G_CAN_INLINE) || defined (__GSK_STORE_C__)

G_INLINE_FUNC GskStoreRequest *
gsk_store_save_object (GskStore    *store,
		       const char  *key,
		       gpointer    object,
		       GError    **error)
{
  GValue value = { 0, { { 0 }, { 0 } } };
  GskStoreRequest *store_request;

  g_value_init (&value, G_OBJECT_TYPE (object));
  g_value_set_object (&value, object);
  store_request = gsk_store_save (store, key, &value, error);
  g_value_unset (&value);
  return store_request;
}

G_INLINE_FUNC const char *
gsk_store_request_get_key (gpointer ptr)
{
  GskStoreRequest *store_request = GSK_STORE_REQUEST (ptr);
  g_return_val_if_fail (store_request, FALSE);
  g_return_val_if_fail (GSK_IS_STORE_REQUEST (store_request), FALSE);
  return store_request->key;
}

G_INLINE_FUNC G_CONST_RETURN GValue *
gsk_store_request_get_value (gpointer request)
{
  g_return_val_if_fail (request, NULL);
  g_return_val_if_fail (GSK_IS_STORE_REQUEST (request), NULL);
  g_return_val_if_fail (gsk_request_get_is_done (request), NULL);
  g_return_val_if_fail (!gsk_request_had_error (request), NULL);
  g_return_val_if_fail (GSK_STORE_REQUEST (request)->request_type ==
			  GSK_STORE_REQUEST_LOAD, NULL);
  return gsk_value_request_get_value (request);
}

G_INLINE_FUNC gpointer
gsk_store_request_get_object (gpointer request)
{
  const GValue *value;

  g_return_val_if_fail (request, NULL);
  g_return_val_if_fail (GSK_IS_STORE_REQUEST (request), NULL);
  g_return_val_if_fail (gsk_request_get_is_done (request), NULL);
  g_return_val_if_fail (!gsk_request_had_error (request), NULL);
  g_return_val_if_fail (GSK_STORE_REQUEST (request)->request_type ==
			  GSK_STORE_REQUEST_LOAD, NULL);

  value = gsk_value_request_get_value (request);
  g_return_val_if_fail (value, NULL);
  g_return_val_if_fail (g_type_is_a (G_VALUE_TYPE (value), G_TYPE_OBJECT),
			NULL);

  return g_value_dup_object (value);
}

G_INLINE_FUNC gboolean
gsk_store_request_get_exists (gpointer ptr)
{
  GskStoreRequest *store_request = GSK_STORE_REQUEST (ptr);
  g_return_val_if_fail (store_request, FALSE);
  g_return_val_if_fail (GSK_IS_STORE_REQUEST (store_request), FALSE);
  g_return_val_if_fail (gsk_request_get_is_done (store_request), FALSE);
  g_return_val_if_fail (!gsk_request_had_error (store_request), FALSE);
  g_return_val_if_fail (store_request->request_type == GSK_STORE_REQUEST_EXISTS,
			FALSE);
  return g_value_get_boolean (gsk_value_request_get_value (store_request));
}

#endif /* defined (G_CAN_INLINE) || defined (__GSK_STORE_C__) */

G_END_DECLS

#endif
