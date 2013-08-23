#ifndef __GSK_ASYNC_CACHE_H_
#define __GSK_ASYNC_CACHE_H_

/*
 * GskAsyncCache -- base class for asynchronous caches with maximum age
 * for entries, suggested maximum number of entries.
 */

/* TODO: how about max_cached_bytes instead of max_keys...?
 * (with cost_func, etc.)
 */

#include <glib.h>
#include "../gskrequest.h"
#include "gskvaluerequest.h"

G_BEGIN_DECLS

typedef struct _GskAsyncCacheClass   GskAsyncCacheClass;
typedef struct _GskAsyncCache        GskAsyncCache;

typedef GskValueRequestClass         GskAsyncCacheRequestClass;
typedef struct _GskAsyncCacheRequest GskAsyncCacheRequest;

/**
 * GskAsyncCacheLoadFunc:
 * Callback used by the GskAsyncCache to get a GskRequest to load the value
 * for a key.
 */
typedef GskValueRequest * (*GskAsyncCacheLoadFunc) (gpointer     key,
						    gpointer     user_data,
						    GError     **error);

GType gsk_async_cache_get_type (void) G_GNUC_CONST;

#define GSK_TYPE_ASYNC_CACHE (gsk_async_cache_get_type ())
#define GSK_ASYNC_CACHE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_ASYNC_CACHE, GskAsyncCache))
#define GSK_ASYNC_CACHE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_ASYNC_CACHE, GskAsyncCacheClass))
#define GSK_ASYNC_CACHE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_ASYNC_CACHE, GskAsyncCacheClass))
#define GSK_IS_ASYNC_CACHE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_ASYNC_CACHE))
#define GSK_IS_ASYNC_CACHE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_ASYNC_CACHE))

struct _GskAsyncCacheClass
{
  GObjectClass object_class;

  GHashFunc      key_hash_func;
  GEqualFunc     key_equal_func;
  GCacheDupFunc  key_dup_func;
  GDestroyNotify key_destroy_func;
};

struct _GskAsyncCache
{
  GObject object;

  guint max_keys;
  guint max_age_seconds;

  GType output_type;
  GskAsyncCacheLoadFunc load_func;
  gpointer func_data;
  GDestroyNotify func_data_destroy;

  gpointer private;
};

GskValueRequest * gsk_async_cache_ref_value   (GskAsyncCache *cache,
					       gpointer       key);

gboolean          gsk_async_cache_unref_value (GskAsyncCache *cache,
					       gpointer       key);

void              gsk_async_cache_flush       (GskAsyncCache *cache);

/*
 *
 * GskAsyncCacheRequest
 *
 */

GType gsk_async_cache_request_get_type (void) G_GNUC_CONST;

#define GSK_TYPE_ASYNC_CACHE_REQUEST (gsk_async_cache_request_get_type ())
#define GSK_ASYNC_CACHE_REQUEST(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
			       GSK_TYPE_ASYNC_CACHE_REQUEST, \
			       GskAsyncCacheRequest))
#define GSK_IS_ASYNC_CACHE_REQUEST(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_ASYNC_CACHE_REQUEST))

struct _GskAsyncCacheRequest
{
  GskValueRequest value_request;

  /* private */
  GskAsyncCache *cache;
  gpointer key;
  GskValueRequest *delegated_request;
};

G_INLINE_FUNC
gpointer gsk_async_cache_request_get_key (GskAsyncCacheRequest *request);


#if defined (G_CAN_INLINE) || defined (__GSK_ASYNC_CACHE_C__)

G_INLINE_FUNC gpointer
gsk_async_cache_request_get_key (GskAsyncCacheRequest *request)
{
  return request->key;
}

#endif /* defined (G_CAN_INLINE) || defined (__GSK_ASYNC_CACHE_C__) */

G_END_DECLS

#endif
