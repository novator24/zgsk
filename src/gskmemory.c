#include <string.h>
#include "gskmemory.h"

/* === GskMemoryBufferSource === */
typedef struct _GskMemoryBufferSource GskMemoryBufferSource;
typedef struct _GskMemoryBufferSourceClass GskMemoryBufferSourceClass;
static GType gsk_memory_buffer_source_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MEMORY_BUFFER_SOURCE			(gsk_memory_buffer_source_get_type ())
#define GSK_MEMORY_BUFFER_SOURCE(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_MEMORY_BUFFER_SOURCE, GskMemoryBufferSource))
#define GSK_MEMORY_BUFFER_SOURCE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_MEMORY_BUFFER_SOURCE, GskMemoryBufferSourceClass))
#define GSK_MEMORY_BUFFER_SOURCE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_MEMORY_BUFFER_SOURCE, GskMemoryBufferSourceClass))
#define GSK_IS_MEMORY_BUFFER_SOURCE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_MEMORY_BUFFER_SOURCE))
#define GSK_IS_MEMORY_BUFFER_SOURCE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_MEMORY_BUFFER_SOURCE))

struct _GskMemoryBufferSourceClass 
{
  GskStreamClass stream_class;
};
struct _GskMemoryBufferSource 
{
  GskStream      stream;
  GskBuffer      buffer;
};
static GObjectClass *global_stream_class = NULL;

static guint
gsk_memory_buffer_source_raw_read  (GskStream     *stream,
			 	    gpointer       data,
			 	    guint          length,
			 	    GError       **error)
{
  GskMemoryBufferSource *source = GSK_MEMORY_BUFFER_SOURCE (stream);
  guint rv = gsk_buffer_read (&source->buffer, data, length);
  if (rv == 0 && source->buffer.size == 0)
    gsk_io_notify_read_shutdown (stream);
  return rv;
}

static guint
gsk_memory_buffer_source_raw_read_buffer (GskStream     *stream,
					  GskBuffer     *buffer,
					  GError       **error)
{
  GskMemoryBufferSource *source = GSK_MEMORY_BUFFER_SOURCE (stream);
  guint rv = gsk_buffer_drain (buffer, &source->buffer);
  if (rv == 0)
    gsk_io_notify_read_shutdown (stream);
  return rv;
}

static void
gsk_memory_buffer_source_finalize(GObject *object)
{
  GskMemoryBufferSource *source = GSK_MEMORY_BUFFER_SOURCE (object);
  gsk_buffer_destruct (&source->buffer);
  (*global_stream_class->finalize) (object);
}

static void
gsk_memory_buffer_source_init (GskMemoryBufferSource *memory_buffer_source)
{
  gsk_stream_mark_is_readable (memory_buffer_source);
  gsk_stream_mark_never_blocks_read (memory_buffer_source);
}

static void
gsk_memory_buffer_source_class_init (GskMemoryBufferSourceClass *class)
{
  GskStreamClass *stream_class = GSK_STREAM_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  global_stream_class = g_type_class_peek_parent (class);
  stream_class->raw_read = gsk_memory_buffer_source_raw_read;
  stream_class->raw_read_buffer = gsk_memory_buffer_source_raw_read_buffer;
  object_class->finalize = gsk_memory_buffer_source_finalize;
}

static GType gsk_memory_buffer_source_get_type()
{
  static GType memory_buffer_source_type = 0;
  if (!memory_buffer_source_type)
    {
      static const GTypeInfo memory_buffer_source_info =
      {
	sizeof(GskMemoryBufferSourceClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_memory_buffer_source_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskMemoryBufferSource),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_memory_buffer_source_init,
	NULL		/* value_table */
      };
      memory_buffer_source_type = g_type_register_static (GSK_TYPE_STREAM,
                                                  "GskMemoryBufferSource",
						  &memory_buffer_source_info, 0);
    }
  return memory_buffer_source_type;
}

/**
 * gsk_memory_buffer_source_new:
 * @buffer: buffer whose contents should be readable from the new stream.
 * It will be immediately (before the function returns) drained of all
 * data, and will not be used any more by the stream.
 *
 * Create a read-only #GskStream that will drain the 
 * data from @buffer and may it available for reading on the stream.
 *
 * returns: the new read-only stream.
 */
GskStream *
gsk_memory_buffer_source_new (GskBuffer              *buffer)
{
  GskMemoryBufferSource *source;
  g_return_val_if_fail (buffer != NULL, NULL);
  source = g_object_new (GSK_TYPE_MEMORY_BUFFER_SOURCE, NULL);
  gsk_buffer_drain (&source->buffer, buffer);
  return GSK_STREAM (source);
}

/* === GskMemorySlabSource === */
typedef struct _GskMemorySlabSource GskMemorySlabSource;
typedef struct _GskMemorySlabSourceClass GskMemorySlabSourceClass;
GType gsk_memory_slab_source_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MEMORY_SLAB_SOURCE			(gsk_memory_slab_source_get_type ())
#define GSK_MEMORY_SLAB_SOURCE(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_MEMORY_SLAB_SOURCE, GskMemorySlabSource))
#define GSK_MEMORY_SLAB_SOURCE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_MEMORY_SLAB_SOURCE, GskMemorySlabSourceClass))
#define GSK_MEMORY_SLAB_SOURCE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_MEMORY_SLAB_SOURCE, GskMemorySlabSourceClass))
#define GSK_IS_MEMORY_SLAB_SOURCE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_MEMORY_SLAB_SOURCE))
#define GSK_IS_MEMORY_SLAB_SOURCE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_MEMORY_SLAB_SOURCE))

struct _GskMemorySlabSourceClass 
{
  GskStreamClass stream_class;
};
struct _GskMemorySlabSource 
{
  GskStream      	  stream;
  gconstpointer           data;
  guint                   data_len;
  GDestroyNotify          destroy;
  gpointer                destroy_data;
};

static guint
gsk_memory_slab_source_raw_read  (GskStream     *stream,
				  gpointer       data,
				  guint          length,
				  GError       **error)
{
  GskMemorySlabSource *source = GSK_MEMORY_SLAB_SOURCE (stream);
  guint rv = MIN (source->data_len, length);
  if (rv != 0)
    {
      memcpy (data, source->data, rv);
      source->data = ((const char *)source->data) + rv;
      source->data_len -= rv;
    }

  if (source->data_len == 0)
    gsk_io_notify_read_shutdown (stream);
  return rv;
}

static guint
gsk_memory_slab_source_raw_read_buffer (GskStream     *stream,
					GskBuffer     *buffer,
					GError       **error)
{
  GskMemorySlabSource *source = GSK_MEMORY_SLAB_SOURCE (stream);
  guint rv = source->data_len;
  if (rv != 0)
    {
      gsk_buffer_append_foreign (buffer, source->data, source->data_len,
				 source->destroy, source->destroy_data);
      source->data_len = 0;
      source->destroy = NULL;
    }
  gsk_io_notify_read_shutdown (stream);
  return rv;
}

static void
gsk_memory_slab_source_finalize (GObject *object)
{
  GskMemorySlabSource *source = GSK_MEMORY_SLAB_SOURCE (object);
  if (source->destroy)
    source->destroy (source->destroy_data);
  global_stream_class->finalize (object);
}

static void
gsk_memory_slab_source_init (GskMemorySlabSource *memory_slab_source)
{
  gsk_stream_mark_is_readable (memory_slab_source);
  gsk_stream_mark_never_blocks_read (memory_slab_source);
}

static void
gsk_memory_slab_source_class_init (GskMemorySlabSourceClass *class)
{
  GskStreamClass *stream_class = GSK_STREAM_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  global_stream_class = g_type_class_peek_parent (class);
  stream_class->raw_read = gsk_memory_slab_source_raw_read;
  stream_class->raw_read_buffer = gsk_memory_slab_source_raw_read_buffer;
  object_class->finalize = gsk_memory_slab_source_finalize;
}

GType gsk_memory_slab_source_get_type()
{
  static GType memory_slab_source_type = 0;
  if (!memory_slab_source_type)
    {
      static const GTypeInfo memory_slab_source_info =
      {
	sizeof(GskMemorySlabSourceClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_memory_slab_source_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskMemorySlabSource),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_memory_slab_source_init,
	NULL		/* value_table */
      };
      memory_slab_source_type = g_type_register_static (GSK_TYPE_STREAM,
                                                  "GskMemorySlabSource",
						  &memory_slab_source_info, 0);
    }
  return memory_slab_source_type;
}

/**
 * gsk_memory_slab_source_new:
 * @data: binary data which will be readable from the returned stream.
 * @data_len: length of the returned stream.
 * @destroy: method called by the stream once @data is completely used
 * (ie, the user has read all the data or shutdown the stream)
 * @destroy_data: data passed to @destroy.
 *
 * Create a read-only stream which has certain, given data available for reading.
 *
 * For efficiency, this code does not copy the data, but rather calls
 * a user-supplied destroy method when we are done.  
 *
 * One possibility if you need the data copied, is to call
 * g_memdup on data, then pass in g_free for @destroy and the copy of the data
 * as @destroy_data.
 *
 * returns: the new read-only stream.
 */
GskStream *
gsk_memory_slab_source_new   (gconstpointer           data,
			      guint                   data_len,
			      GDestroyNotify          destroy,
			      gpointer                destroy_data)
{
  GskMemorySlabSource *slab_source;
  slab_source = g_object_new (GSK_TYPE_MEMORY_SLAB_SOURCE, NULL);
  slab_source->data = data;
  slab_source->data_len = data_len;
  slab_source->destroy = destroy;
  slab_source->destroy_data = destroy_data;
  return GSK_STREAM (slab_source);
}

/**
 * gsk_memory_source_new_printf:
 * @format: a printf(3) format string.
 * @...: arguments, as used by printf(3).
 *
 * Create a read-only stream which has the result of doing the
 * sprintf available for reading.
 *
 * returns: the new read-only stream.
 */
GskStream *
gsk_memory_source_new_printf (const char             *format,
			      ...)
{
  char *str;
  va_list args;
  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);
  return gsk_memory_slab_source_new (str, strlen (str), g_free, str);
}

/**
 * gsk_memory_source_new_vprintf:
 * @format: a printf(3) format string.
 * @args: arguments, as used by vprintf(3).
 *
 * Create a read-only stream which has the result of doing the
 * vsprintf available for reading.  This is useful for
 * chaining from other printf-like functions.
 *
 * returns: the new read-only stream.
 */
GskStream *
gsk_memory_source_new_vprintf (const char             *format,
			       va_list                 args)
{
  char *str = g_strdup_vprintf (format, args);
  return gsk_memory_slab_source_new (str, strlen (str), g_free, str);
}

/**
 * gsk_memory_source_static_string:
 * @str: the static string which is the content of the stream.
 *
 * Create a read-only stream which has @str available
 * for reading.
 *
 * Note that the NUL will not be read from the stream.
 *
 * returns: the new read-only stream.
 */
GskStream *
gsk_memory_source_static_string (const char *str)
{
  return gsk_memory_slab_source_new (str, strlen (str), NULL, NULL);
}

/**
 * gsk_memory_source_static_string_n:
 * @str: the static string or binary-data which is the content of the stream.
 * @length: length of the stream in bytes.
 *
 * Create a read-only stream which has @length bytes starting at @str available
 * for reading.
 *
 * returns: the new read-only stream.
 */
GskStream *
gsk_memory_source_static_string_n (const char *str,
				   guint       length)
{
  return gsk_memory_slab_source_new (str, length, NULL, NULL);
}

/* --- streams which can be written to --- */

/* === GskMemorySink === */

/* Insert header here. */
typedef struct _GskMemorySink GskMemorySink;
typedef struct _GskMemorySinkClass GskMemorySinkClass;
GType gsk_memory_sink_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MEMORY_SINK			(gsk_memory_sink_get_type ())
#define GSK_MEMORY_SINK(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_MEMORY_SINK, GskMemorySink))
#define GSK_MEMORY_SINK_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_MEMORY_SINK, GskMemorySinkClass))
#define GSK_MEMORY_SINK_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_MEMORY_SINK, GskMemorySinkClass))
#define GSK_IS_MEMORY_SINK(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_MEMORY_SINK))
#define GSK_IS_MEMORY_SINK_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_MEMORY_SINK))

struct _GskMemorySinkClass 
{
  GskStreamClass stream_class;
};
struct _GskMemorySink 
{
  GskStream      stream;

  /* underlying data store */
  GskBuffer      buffer;

  /* from the user; callback and destroy are set to NULL when they are
     run, since they should only be run once. */
  GskMemoryBufferCallback callback;
  gpointer                data;
  GDestroyNotify          destroy;
};

static guint
gsk_memory_sink_raw_write       (GskStream     *stream,
			 	 gconstpointer  data,
			 	 guint          length,
			 	 GError       **error)
{
  gsk_buffer_append (&GSK_MEMORY_SINK (stream)->buffer, data, length);
  return length;
}

static guint
gsk_memory_sink_raw_write_buffer(GskStream    *stream,
				 GskBuffer     *buffer,
				 GError       **error)
{
  return gsk_buffer_drain (&GSK_MEMORY_SINK (stream)->buffer, buffer);
}

static gboolean
gsk_memory_sink_shutdown_write(GskIO      *io,
			       GError    **error)
{
  GskMemorySink *sink = GSK_MEMORY_SINK (io);
  if (sink->callback)
    {
      GskMemoryBufferCallback callback = sink->callback;
      sink->callback = NULL;
      (*callback) (&sink->buffer, sink->data);
    }
  gsk_buffer_destruct (&sink->buffer);
  return TRUE;
}

static void
gsk_memory_sink_finalize (GObject *object)
{
  GskMemorySink *sink = GSK_MEMORY_SINK (object);
  gsk_buffer_destruct (&sink->buffer);
  if (sink->destroy != NULL)
    (*sink->destroy) (sink->data);
  (*global_stream_class->finalize) (object);
}

static void
gsk_memory_sink_init (GskMemorySink *memory_sink)
{
  gsk_io_mark_is_writable (memory_sink);
  gsk_stream_mark_never_blocks_write (memory_sink);
  gsk_stream_mark_never_partial_writes (memory_sink);
}

static void
gsk_memory_sink_class_init (GskMemorySinkClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GskIOClass *io_class = GSK_IO_CLASS (class);
  GskStreamClass *stream_class = GSK_STREAM_CLASS (class);

  global_stream_class = g_type_class_peek_parent (class);

  object_class->finalize = gsk_memory_sink_finalize;
  io_class->shutdown_write = gsk_memory_sink_shutdown_write;
  stream_class->raw_write = gsk_memory_sink_raw_write;
  stream_class->raw_write_buffer = gsk_memory_sink_raw_write_buffer;
}

GType gsk_memory_sink_get_type()
{
  static GType memory_sink_type = 0;
  if (!memory_sink_type)
    {
      static const GTypeInfo memory_sink_info =
      {
	sizeof(GskMemorySinkClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_memory_sink_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskMemorySink),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_memory_sink_init,
	NULL		/* value_table */
      };
      memory_sink_type = g_type_register_static (GSK_TYPE_STREAM,
                                                  "GskMemorySink",
						  &memory_sink_info, 0);
    }
  return memory_sink_type;
}

/**
 * gsk_memory_buffer_sink_new:
 * @callback: function to call when the buffer has been filled.
 * @data: user-data to pass to the @callback.
 * @destroy: function to call when we are done with the data.
 *
 * Create a sink for binary data which just fills
 * a binary buffer.  When the stream is done,
 * the @callback will be run with the full buffer.
 *
 * returns: the writable stream whose destination is a #GskBuffer.
 */
GskStream *gsk_memory_buffer_sink_new   (GskMemoryBufferCallback callback,
					 gpointer                data,
					 GDestroyNotify          destroy)
{
  GskMemorySink *sink = g_object_new (GSK_TYPE_MEMORY_SINK, NULL);
  sink->callback = callback;
  sink->data = data;
  sink->destroy = destroy;
  return GSK_STREAM (sink);
}
