#define G_IMPLEMENT_INLINES
#define __GSK_ASYNC_CACHE_C__
#include "../gskerror.h"
#include "gskasynccache.h"

/*
 *
 * GskAsyncCache
 *
 */

typedef struct _CacheNode CacheNode;

struct _CacheNode
{
  gpointer key;
  GValue value;

  GTimeVal expires;

  /* A node is in the destroy queue iff refcount == 0. */
  guint refcount;
  CacheNode *destroy_prev, *destroy_next;

  /* True if the cache has been flushed but we couldn't free this node
   * because it was still referenced.
   */
  gboolean dirty;
};

typedef struct _GskAsyncCachePrivate GskAsyncCachePrivate;

struct _GskAsyncCachePrivate
{
  GHashTable *lookup;
  guint num_keys;

  /* LRU queue of nodes with refcount 0. */
  CacheNode *destroy_first, *destroy_last;
};

static GObjectClass *gsk_async_cache_parent_class = NULL;

/*
 * CacheNode functions
 */

static __inline__ void
cache_node_real_free (GskAsyncCache *cache, CacheNode *node)
{
  GskAsyncCacheClass *async_cache_class = GSK_ASYNC_CACHE_GET_CLASS (cache);
  GskAsyncCachePrivate *private = (GskAsyncCachePrivate *) cache->private;

  g_return_if_fail (async_cache_class);
  g_return_if_fail (async_cache_class->key_destroy_func);
  g_return_if_fail (private);

  (*async_cache_class->key_destroy_func) (node->key); 
  g_value_unset (&node->value); 

  g_free (node); 

  g_return_if_fail (private->num_keys > 0);
  --private->num_keys;
}

/* Remove the node from the destroy queue. */
static void
cache_node_remove_from_destroy_queue (GskAsyncCache *cache, CacheNode *node)
{
  GskAsyncCachePrivate *private = (GskAsyncCachePrivate *) cache->private;

  g_return_if_fail (private);

  if (node->destroy_prev)
    node->destroy_prev->destroy_next = node->destroy_next;
  else
    {
      g_return_if_fail (node == private->destroy_first);
      private->destroy_first = node->destroy_next;
    }

  if (node->destroy_next)
    node->destroy_next->destroy_prev = node->destroy_prev;
  else
    {
      g_return_if_fail (node == private->destroy_last);
      private->destroy_last = node->destroy_prev;
    }

  node->destroy_prev = node->destroy_next = NULL;
}

/* Free a node, removing it from the cache's lookup.
 */
static void
cache_node_free (GskAsyncCache *cache, CacheNode *node)
{
  GskAsyncCachePrivate *private = (GskAsyncCachePrivate *) cache->private;

  g_return_if_fail (private);
  g_return_if_fail (node->refcount == 0);
  g_return_if_fail (node->destroy_prev == NULL);
  g_return_if_fail (node->destroy_next == NULL);

  g_hash_table_remove (private->lookup, node->key);
  cache_node_real_free (cache, node);
}

/* Free a node without worrying about bookkeeping (GHFunc called from
 * gsk_async_cache_finalize).
 */
static void
cache_node_obliterate (gpointer key, gpointer value, gpointer user_data)
{
  GskAsyncCache *cache = GSK_ASYNC_CACHE (user_data);
  CacheNode *node = value;

  (void) key;
  cache_node_real_free (cache, node);
}

/* Flush one node from the cache (GHRFunc called from gsk_async_cache_flush).
 * If it's referenced, mark it as dirty, otherwise get rid of it.
 */
static gboolean
cache_node_flush (gpointer key, gpointer value, gpointer user_data)
{
  GskAsyncCache *cache = GSK_ASYNC_CACHE (user_data);
  CacheNode *node = value;

  (void) key;

  if (node->refcount == 0)
    {
      cache_node_remove_from_destroy_queue (cache, node);
      cache_node_real_free (cache, node);
      return TRUE;
    }
  else
    {
      node->dirty = TRUE;
      return FALSE;
    }
}

static void
run_destroy_queue (GskAsyncCache *cache)
{
  GskAsyncCachePrivate *private = (GskAsyncCachePrivate *) cache->private;
  CacheNode *node;

  while (private->destroy_first && private->num_keys >= cache->max_keys)
    {
      node = private->destroy_first;
      private->destroy_first = node->destroy_next;

      if (private->destroy_first)
        private->destroy_first->destroy_prev = NULL;
      else
        {
          g_return_if_fail (node == private->destroy_last);
          private->destroy_last = NULL;
        }
      g_return_if_fail (node->refcount == 0);
      node->destroy_prev = node->destroy_next = NULL;
      cache_node_free (cache, node);
    }
}

static CacheNode *
cache_node_new (GskAsyncCache *cache, gpointer key, const GValue *value)
{
  GskAsyncCacheClass *async_cache_class = GSK_ASYNC_CACHE_GET_CLASS (cache);
  GskAsyncCachePrivate *private = (GskAsyncCachePrivate *) cache->private;
  CacheNode *node;

  g_return_val_if_fail (async_cache_class->key_dup_func, NULL);

  run_destroy_queue (cache);

  node = g_new0 (CacheNode, 1);
  node->key = (*async_cache_class->key_dup_func) (key);
  g_value_init (&node->value, cache->output_type);
  g_value_copy (value, &node->value);
  node->refcount = 1;
  g_get_current_time (&node->expires);
  g_time_val_add (&node->expires, G_USEC_PER_SEC * cache->max_age_seconds);

  g_hash_table_insert (private->lookup, node->key, node);
  ++private->num_keys;

  return node;
}

static __inline__ gboolean
cache_node_expired (CacheNode *node)
{
  GTimeVal current_time;

  g_get_current_time (&current_time);
  return (current_time.tv_sec > node->expires.tv_sec) || 
	   (current_time.tv_sec == node->expires.tv_sec &&
	      current_time.tv_usec > node->expires.tv_usec);
}

static void
cache_node_ref (GskAsyncCache *cache, CacheNode *node)
{
  if (node->refcount == 0)
    cache_node_remove_from_destroy_queue (cache, node);
  ++node->refcount;
}

static void
cache_node_unref (GskAsyncCache *cache, CacheNode *node)
{
  GskAsyncCachePrivate *private = (GskAsyncCachePrivate *) cache->private;

  g_return_if_fail (private);
  g_return_if_fail (node->refcount > 0);
  g_return_if_fail (node->destroy_prev == NULL);
  g_return_if_fail (node->destroy_next == NULL);

  if (--node->refcount == 0)
    {
      run_destroy_queue (cache);

      /* If we have enough space and the node isn't dirty or expired,
       * push it onto the back of the destroy queue.
       */
      if (node->dirty || cache_node_expired (node) ||
	  private->num_keys >= cache->max_keys)
	{
	  cache_node_free (cache, node);
	}
      else
        {
          node->destroy_prev = private->destroy_last;
          node->destroy_next = NULL;

          if (private->destroy_last)
            {
              g_return_if_fail (private->destroy_first);
              private->destroy_last->destroy_next = node;
            }
          else
            {
              g_return_if_fail (private->destroy_first == NULL);
              private->destroy_first = node;
            }
          private->destroy_last = node;
        }
    }
}

/*
 * Public interface.
 */

GskValueRequest *
gsk_async_cache_ref_value (GskAsyncCache *cache, gpointer key)
{
  GskAsyncCacheRequest *request;

  request = g_object_new (GSK_TYPE_ASYNC_CACHE_REQUEST, NULL);
  request->cache = cache;
  g_object_ref (cache);
  g_return_val_if_fail (GSK_ASYNC_CACHE_GET_CLASS (cache)->key_dup_func, NULL);
  request->key = (*GSK_ASYNC_CACHE_GET_CLASS (cache)->key_dup_func) (key);

  return GSK_VALUE_REQUEST (request);
}

gboolean
gsk_async_cache_unref_value (GskAsyncCache *cache, gpointer key)
{
  GskAsyncCachePrivate *private = (GskAsyncCachePrivate *) cache->private;
  CacheNode *node;

  g_return_val_if_fail (private, FALSE);
  g_return_val_if_fail (private->lookup, FALSE);

  node = g_hash_table_lookup (private->lookup, key);
  if (node)
    {
      cache_node_unref (cache, node);
      return TRUE;
    }
  else
    return FALSE;
}

void
gsk_async_cache_flush (GskAsyncCache *cache)
{
  GskAsyncCachePrivate *private = (GskAsyncCachePrivate *) cache->private;

  g_return_if_fail (private);
  g_return_if_fail (private->lookup);

  g_hash_table_foreach_remove (private->lookup, cache_node_flush, cache);
}

/* GObject methods. */

static void
gsk_async_cache_finalize (GObject *object)
{
  GskAsyncCache *cache = GSK_ASYNC_CACHE (object);
  GskAsyncCachePrivate *private = (GskAsyncCachePrivate *) cache->private;

  g_return_if_fail (private);
  g_return_if_fail (private->lookup);

  g_hash_table_foreach (private->lookup, cache_node_obliterate, cache);
  g_hash_table_destroy (private->lookup);
  g_free (private);

  if (cache->func_data_destroy)
    (*cache->func_data_destroy) (cache->func_data);

  (*gsk_async_cache_parent_class->finalize) (object);
}

static void
gsk_async_cache_init (GskAsyncCache *cache)
{
  GskAsyncCacheClass *async_cache_class = GSK_ASYNC_CACHE_GET_CLASS (cache);
  GskAsyncCachePrivate *private;

  private = g_new0 (GskAsyncCachePrivate, 1);
  private->lookup = g_hash_table_new (async_cache_class->key_hash_func,
				      async_cache_class->key_equal_func);
  cache->private = private;
}

static void
gsk_async_cache_class_init (GObjectClass *object_class)
{
  gsk_async_cache_parent_class = g_type_class_peek_parent (object_class);
  object_class->finalize = gsk_async_cache_finalize;
}

GType
gsk_async_cache_get_type (void)
{
  static GType type = 0;
  if (G_UNLIKELY (type == 0))
    {
      static const GTypeInfo type_info =
	{
	  sizeof (GskAsyncCacheClass),
	  (GBaseInitFunc) NULL,
	  (GBaseFinalizeFunc) NULL,
	  (GClassInitFunc) gsk_async_cache_class_init,
	  NULL,		/* class_finalize */
	  NULL,		/* class_data */
	  sizeof (GskAsyncCache),
	  0,		/* n_preallocs */
	  (GInstanceInitFunc) gsk_async_cache_init,
	  NULL		/* value_table */
	};
      type = g_type_register_static (G_TYPE_OBJECT,
				     "GskAsyncCache",
				     &type_info,
				     0);
    }
  return type;
}

/*
 *
 * GskAsyncCacheRequest
 *
 */

static GObjectClass *gsk_async_cache_request_parent_class = NULL;

static void
gsk_async_cache_request_cancelled (GskRequest *request_parent)
{
  GskAsyncCacheRequest *request = GSK_ASYNC_CACHE_REQUEST (request_parent);
  GskValueRequest *delegated_request = request->delegated_request;

  g_return_if_fail (gsk_request_get_is_cancellable (delegated_request));
  gsk_request_clear_is_running (request);
  gsk_request_mark_is_cancelled (request);
  if (delegated_request)
    gsk_request_cancel (delegated_request);
  g_object_unref (request);
}

/* GskAsyncCacheLoadedFunc */
static void
delegated_request_done (GskRequest *delegated_request,
			gpointer    user_data)
{
  GskValueRequest *value_request = GSK_VALUE_REQUEST (delegated_request);
  GskAsyncCacheRequest *request = GSK_ASYNC_CACHE_REQUEST (user_data);

  g_return_if_fail (value_request);
  g_return_if_fail (request);
  g_return_if_fail (request->cache);

  if (gsk_request_had_error (delegated_request))
    {
      const GError *error;
      error = gsk_request_get_error (delegated_request);
      gsk_request_set_error (request, g_error_copy (error));
    }
  else
    {
      const GValue *value;
      value = gsk_value_request_get_value (value_request);
      g_return_if_fail (value);
      if (!g_value_type_compatible (G_VALUE_TYPE (value),
				    request->cache->output_type))
	{
	  GError *error;
	  error = g_error_new (GSK_G_ERROR_DOMAIN,
			       GSK_ERROR_UNKNOWN, /* TODO: error code */
			       "request for a %s returned a %s instead",
			       g_type_name (request->cache->output_type),
			       g_type_name (G_VALUE_TYPE (value)));
	  gsk_request_set_error (request, error);
	}
      else
	{
	  g_value_init (&request->value_request.value,
			request->cache->output_type);
	  g_value_copy (value, &request->value_request.value);
	  cache_node_new (request->cache,
			  request->key,
			  &request->value_request.value);
	}
    }
  gsk_request_done (request);
  g_object_unref (request);
}

static void
gsk_async_cache_request_start (GskRequest *request_parent)
{
  GskAsyncCacheRequest *request = GSK_ASYNC_CACHE_REQUEST (request_parent);
  GskAsyncCache *cache = request->cache;
  GskAsyncCachePrivate *private = cache->private;
  gpointer key = request->key;
  CacheNode *node;
  GskValueRequest *delegated_request;
  GError *error = NULL;

  g_return_if_fail (!gsk_request_get_is_running (request));
  g_return_if_fail (!gsk_request_get_is_cancelled (request));
  g_return_if_fail (!gsk_request_get_is_done (request));

  /* Return existing value if we have one cached ... */
  node = g_hash_table_lookup (private->lookup, key);
  if (node)
    {
      if (cache_node_expired (node) && node->refcount == 0)
	{
	  cache_node_free (cache, node);
	}
      else
	{
	  /* ... and either it hasn't expired, or it's already referenced
	   * (so we can't unref it).
	   */
	  cache_node_ref (cache, node);
	  g_value_init (&request->value_request.value, cache->output_type);
	  g_value_copy (&node->value, &request->value_request.value);
	  gsk_request_done (request);
	  return;
	}
    }

  g_return_if_fail (*request->cache->load_func);
  delegated_request = (*request->cache->load_func) (request->key,
						    request->cache->func_data,
						    &error);
  if (delegated_request == NULL)
    {
      gsk_request_set_error (request, error);
      gsk_request_done (request);
      return;
    }

  g_object_ref (request);
  gsk_request_mark_is_running (request);
  request->delegated_request = delegated_request;

  g_signal_connect (delegated_request,
		    "done",
		    G_CALLBACK (delegated_request_done),
		    request);

  if (gsk_request_get_is_cancellable (delegated_request))
    gsk_request_mark_is_cancellable (request);

  gsk_request_start (delegated_request);
}

/* GObject methods. */

static void
gsk_async_cache_request_finalize (GObject *object)
{
  GskAsyncCacheRequest *request = GSK_ASYNC_CACHE_REQUEST (object);

  if (request->cache)
    {
      if (request->key)
	{
	  GskAsyncCacheClass *async_cache_class =
	    GSK_ASYNC_CACHE_GET_CLASS (request->cache);

	  g_return_if_fail (async_cache_class);
	  g_return_if_fail (async_cache_class->key_destroy_func);
	  (*async_cache_class->key_destroy_func) (request->key);
	}
      g_object_unref (request->cache);
    }

  (*gsk_async_cache_request_parent_class->finalize) (object);
}

static void
gsk_async_cache_request_class_init (GskRequestClass *request_class)
{
  gsk_async_cache_request_parent_class =
    g_type_class_peek_parent (request_class);
  G_OBJECT_CLASS (request_class)->finalize = gsk_async_cache_request_finalize;
  request_class->start = gsk_async_cache_request_start;
  request_class->cancelled = gsk_async_cache_request_cancelled;
}

GType
gsk_async_cache_request_get_type (void)
{
  static GType type = 0;
  if (G_UNLIKELY (type == 0))
    {
      static const GTypeInfo type_info =
	{
	  sizeof (GskAsyncCacheRequestClass),
	  (GBaseInitFunc) NULL,
	  (GBaseFinalizeFunc) NULL,
	  (GClassInitFunc) gsk_async_cache_request_class_init,
	  NULL,		/* class_finalize */
	  NULL,		/* class_data */
	  sizeof (GskAsyncCacheRequest),
	  0,		/* n_preallocs */
	  (GInstanceInitFunc) NULL,
	  NULL		/* value_table */
	};
      type = g_type_register_static (GSK_TYPE_VALUE_REQUEST,
				     "GskAsyncCacheRequest",
				     &type_info,
				     0);
    }
  return type;
}
