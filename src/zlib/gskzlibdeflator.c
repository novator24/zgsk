#include "gskzlibdeflator.h"
#include "gskzlib.h"
#include "../gskmacros.h"
#include <stdlib.h>
#include <zlib.h>

static GObjectClass *parent_class = NULL;

#define MAX_BUFFER_SIZE		4096
#define DEFAULT_LEVEL		7
#define DEFAULT_FLUSH_MILLIS	-1

/* whether to use a zlib-allocator that zeros the memory
   before use.  without this, sometimes zlib
   will use uninitialized data... which is probably ok,
   but is a pain for valgrind users. */
#define VALGRIND_WORKAROUND     1

enum
{
  PROP_0,
  PROP_LEVEL,
  PROP_FLUSH_TIMEOUT,
  PROP_USE_GZIP
};

static guint
gsk_zlib_deflator_raw_read      (GskStream     *stream,
			 	 gpointer       data,
			 	 guint          length,
			 	 GError       **error)
{
  GskZlibDeflator *zlib_deflator = GSK_ZLIB_DEFLATOR (stream);
  guint rv = gsk_buffer_read (&zlib_deflator->compressed, data, length);

  if (!gsk_io_get_is_writable (zlib_deflator))
    {
      if (rv == 0 && zlib_deflator->compressed.size == 0)
	gsk_io_notify_read_shutdown (zlib_deflator);
    }
  else
    {
      if (zlib_deflator->compressed.size < MAX_BUFFER_SIZE)
	gsk_io_mark_idle_notify_write (zlib_deflator);
      if (zlib_deflator->compressed.size == 0)
	gsk_io_clear_idle_notify_read (zlib_deflator);
    }

  return rv;
}
static guint
gsk_zlib_deflator_raw_read_buffer(GskStream     *stream,
			 	  GskBuffer     *buffer,
			 	  GError       **error)
{
  GskZlibDeflator *zlib_deflator = GSK_ZLIB_DEFLATOR (stream);
  guint rv = gsk_buffer_drain (buffer, &zlib_deflator->compressed);

  if (!gsk_io_get_is_writable (zlib_deflator))
    {
      if (rv == 0)
	gsk_io_notify_read_shutdown (zlib_deflator);
    }
  else
    {
      gsk_io_mark_idle_notify_write (zlib_deflator);
      gsk_io_clear_idle_notify_read (zlib_deflator);
    }

  return rv;
}


static gboolean
do_sync (GskZlibDeflator *zlib_deflator,
         int flush,             /* set of Z_SYNC_FLUSH or Z_FINISH */
         GError **error)
{
  guint8 buf[4096];
  z_stream *zst = zlib_deflator->private_stream;
  int rv;

  if (zst == NULL)
    return TRUE;

  zst->next_in = NULL;
  zst->avail_in = 0;
  do
    {
      /* Set up output location */
      zst->next_out = buf;
      zst->avail_out = sizeof (buf);

      /* Compress */
      rv = deflate (zst, flush);
      if (rv == Z_OK || rv == Z_STREAM_END)
	gsk_buffer_append (&zlib_deflator->compressed, buf, zst->next_out - buf);
    }
  while (rv == Z_OK && zst->avail_out == 0);
  if (rv != Z_OK && rv != Z_STREAM_END)
    {
      GskErrorCode zerror_code = gsk_zlib_error_to_gsk_error (rv);
      const char *zmsg = gsk_zlib_error_to_message (rv);
      g_set_error (error, GSK_G_ERROR_DOMAIN, zerror_code,
		   "could not deflate: %s", zmsg);
      g_message ("error deflating");
      return FALSE;
    }
  if (zlib_deflator->compressed.size > 0)
    gsk_stream_mark_idle_notify_read (zlib_deflator);
  return TRUE;
}

static gboolean
do_background_flush (gpointer def)
{
  GskZlibDeflator *zlib_deflator = GSK_ZLIB_DEFLATOR (def);
  GError *error = NULL;
  if (!do_sync (zlib_deflator, Z_SYNC_FLUSH, &error))
    {
      gsk_io_set_gerror (GSK_IO (zlib_deflator),
			 GSK_IO_ERROR_SYNC,
			 error);
    }
  zlib_deflator->flush_source = NULL;
  return FALSE;
}

static gboolean
gsk_zlib_deflator_shutdown_write (GskIO      *io,
				  GError    **error)
{
  GskZlibDeflator *zlib_deflator = GSK_ZLIB_DEFLATOR (io);
  if (!do_sync (GSK_ZLIB_DEFLATOR (io), Z_FINISH, error))
    return FALSE;
  if (zlib_deflator->flush_source != NULL)
    {
      gsk_source_remove (zlib_deflator->flush_source);
      zlib_deflator->flush_source = NULL;
    }
  if (zlib_deflator->compressed.size == 0)
    gsk_io_notify_read_shutdown (zlib_deflator);
  else
    gsk_io_mark_idle_notify_read (zlib_deflator);
  return TRUE;
}

#if VALGRIND_WORKAROUND
static voidpf my_alloc (voidpf opaque, uInt items, uInt size)
{
  /* technically, zeroing the memory is unnecessary,
     but it suppresses valgrind warnings */
  return calloc (items, size);
}
static void my_free (voidpf opaque, voidpf address)
{
  free (address);
}
#else   /* !VALGRIND_WORKAROUND */
/* just use zlib's allocators, which is achieved by passing
   in NULL for the alloc and free functions */
#define my_alloc        NULL
#define my_free         NULL
#endif  /* !VALGRIND_WORKAROUND */

static guint
gsk_zlib_deflator_raw_write     (GskStream     *stream,
			 	 gconstpointer  data,
			 	 guint          length,
			 	 GError       **error)
{
  GskZlibDeflator *zlib_deflator = GSK_ZLIB_DEFLATOR (stream);
  z_stream *zst;
  guint8 buf[4096];
  int rv;
  if (zlib_deflator->private_stream == NULL)
    {
      zst = g_new (z_stream, 1);
      zlib_deflator->private_stream = zst;
      zst->next_in = (gpointer) data;
      zst->avail_in = length;
      zst->zalloc = my_alloc;
      zst->zfree = my_free;
      zst->opaque = NULL;
      deflateInit2 (zst,
                    zlib_deflator->level,
                    Z_DEFLATED,
                    (zlib_deflator->use_gzip ? 16 : 0) | 15, /* windowBits */
                    8,                          /* mem_level */
                    Z_DEFAULT_STRATEGY);
    }
  else
    {
      zst = zlib_deflator->private_stream;
      zst->next_in = (gpointer) data;
      zst->avail_in = length;
    }

  if (length == 0)
    return 0;

  do
    {
      /* Set up output location */
      zst->next_out = buf;
      zst->avail_out = sizeof (buf);

      /* Decompress */
      rv = deflate (zst, Z_NO_FLUSH);
      if (rv == Z_OK || rv == Z_STREAM_END)
	gsk_buffer_append (&zlib_deflator->compressed, buf, zst->next_out - buf);
    }
  while (rv == Z_OK && zst->avail_in > 0);
  g_return_val_if_fail (zst->avail_in == 0, length - zst->avail_in);
  if (rv != Z_OK && rv != Z_STREAM_END)
    {
      GskErrorCode zerror_code = gsk_zlib_error_to_gsk_error (rv);
      const char *zmsg = gsk_zlib_error_to_message (rv);
      g_set_error (error, GSK_G_ERROR_DOMAIN, zerror_code,
		   "could not deflate: %s", zmsg);
      g_message ("error deflating");
    }
  else if (zlib_deflator->flush_millis >= 0)
    {
      if (zlib_deflator->flush_millis == 0)
	{
	  if (zlib_deflator->flush_source == NULL)
	    zlib_deflator->flush_source = gsk_main_loop_add_idle (gsk_main_loop_default (),
								  do_background_flush,
								  g_object_ref (zlib_deflator),
								  g_object_unref);
	}
      else
	{
	  if (zlib_deflator->flush_source == NULL)
	    zlib_deflator->flush_source = gsk_main_loop_add_timer (gsk_main_loop_default (),
								   do_background_flush,
								   g_object_ref (zlib_deflator),
								   g_object_unref,
								   zlib_deflator->flush_millis,
								   -1);
	  else
	    gsk_source_adjust_timer (zlib_deflator->flush_source,
				     zlib_deflator->flush_millis,
				     -1);
	}
    }

  if (zlib_deflator->compressed.size > MAX_BUFFER_SIZE)
    gsk_io_clear_idle_notify_write (zlib_deflator);
  if (zlib_deflator->compressed.size > 0)
    gsk_io_mark_idle_notify_read (zlib_deflator);

  return length - zst->avail_in;
}

static void
gsk_zlib_deflator_set_property	      (GObject        *object,
				       guint           property_id,
				       const GValue   *value,
				       GParamSpec     *pspec)
{
  GskZlibDeflator *zlib_deflator = GSK_ZLIB_DEFLATOR (object);
  switch (property_id)
    {
    case PROP_LEVEL:
      zlib_deflator->level = g_value_get_int (value);
      break;

      /* set the flush-timeout.
       * remember: this code is rarely called, and so is not time critical.
       * in particular, we could use gsk_main_loop_adjust_timer(),
       * but we don't because there are too many cases to deal with.
       */
    case PROP_FLUSH_TIMEOUT:
      {
	int old_timeout = zlib_deflator->flush_millis;
	int new_timeout = g_value_get_int (value);
	if (old_timeout < 0)
	  old_timeout = -1;
	if (new_timeout < 0)
	  new_timeout = -1;
	if (new_timeout != old_timeout)
	  {
	    if (zlib_deflator->flush_source != NULL)
	      {
		gsk_source_remove (zlib_deflator->flush_source);
		zlib_deflator->flush_source = NULL;
	      }
	    if (new_timeout == 0)
	      {
		zlib_deflator->flush_source = gsk_main_loop_add_idle (gsk_main_loop_default (),
								      do_background_flush,
								      g_object_ref (zlib_deflator),
								      g_object_unref);
	      }
	    else if (new_timeout > 0)
	      {
		zlib_deflator->flush_source = gsk_main_loop_add_timer (gsk_main_loop_default (),
								       do_background_flush,
								       g_object_ref (zlib_deflator),
								       g_object_unref,
								       zlib_deflator->flush_millis,
								       -1);
	      }
	  }
	break;
      }
    case PROP_USE_GZIP:
      zlib_deflator->use_gzip = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gsk_zlib_deflator_get_property	      (GObject        *object,
				       guint           property_id,
				       GValue         *value,
				       GParamSpec     *pspec)
{
  GskZlibDeflator *deflator = GSK_ZLIB_DEFLATOR (object);
  switch (property_id)
    {
    case PROP_LEVEL:
      g_value_set_int (value, deflator->level);
      break;
    case PROP_FLUSH_TIMEOUT:
      g_value_set_int (value, deflator->flush_millis);
      break;
    case PROP_USE_GZIP:
      g_value_set_boolean (value, deflator->use_gzip);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gsk_zlib_deflator_finalize     (GObject *object)
{
  GskZlibDeflator *deflator = GSK_ZLIB_DEFLATOR (object);
  if (deflator->private_stream)
    {
      deflateEnd (deflator->private_stream);
      g_free (deflator->private_stream);
    }
  gsk_buffer_destruct (&deflator->compressed);
  (*parent_class->finalize) (object);
}

/* --- functions --- */
static void
gsk_zlib_deflator_init (GskZlibDeflator *zlib_deflator)
{
  zlib_deflator->flush_millis = DEFAULT_FLUSH_MILLIS;
  zlib_deflator->level = DEFAULT_LEVEL;
  gsk_io_mark_is_readable (zlib_deflator);
  gsk_io_mark_is_writable (zlib_deflator);
  gsk_io_mark_idle_notify_write (zlib_deflator);
}

static void
gsk_zlib_deflator_class_init (GskZlibDeflatorClass *class)
{
  GskIOClass *io_class = GSK_IO_CLASS (class);
  GskStreamClass *stream_class = GSK_STREAM_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GParamSpec *pspec;
  parent_class = g_type_class_peek_parent (class);
  stream_class->raw_read = gsk_zlib_deflator_raw_read;
  stream_class->raw_read_buffer = gsk_zlib_deflator_raw_read_buffer;
  stream_class->raw_write = gsk_zlib_deflator_raw_write;
  io_class->shutdown_write = gsk_zlib_deflator_shutdown_write;
  object_class->set_property = gsk_zlib_deflator_set_property;
  object_class->get_property = gsk_zlib_deflator_get_property;
  object_class->finalize = gsk_zlib_deflator_finalize;

  pspec = g_param_spec_int ("level", _("Level"), "compression level", 0, 9, DEFAULT_LEVEL,
			    G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_LEVEL, pspec);

  pspec = g_param_spec_int ("flush-timeout", _("Flush Timeout"),
			    _("number of milliseconds to wait before flushing the stream"),
			    -1, G_MAXINT, DEFAULT_FLUSH_MILLIS,
			    G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_FLUSH_TIMEOUT, pspec);

  pspec = g_param_spec_boolean ("use-gzip", _("Use Gzip"), "whether to gzip encapsulate the data",
			        FALSE, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_USE_GZIP, pspec);
}

GType gsk_zlib_deflator_get_type()
{
  static GType zlib_deflator_type = 0;
  if (!zlib_deflator_type)
    {
      static const GTypeInfo zlib_deflator_info =
      {
	sizeof(GskZlibDeflatorClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_zlib_deflator_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskZlibDeflator),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_zlib_deflator_init,
	NULL		/* value_table */
      };
      zlib_deflator_type = g_type_register_static (GSK_TYPE_STREAM,
                                                  "GskZlibDeflator",
						  &zlib_deflator_info, 0);
    }
  return zlib_deflator_type;
}

/**
 * gsk_zlib_deflator_new:
 * @compression_level: the level of compression to attain
 * in exchange for running slower.
 * @flush_millis: number of milliseconds to wait before
 * flushing all input characters to the output.
 * Use -1 to not set timeouts, which means the buffers
 * are only flushed after a write-shutdown.
 *
 * Create a new Zlib deflation stream.
 * This stream is written uncompressed input,
 * and then compressed output can be read back
 * from it.
 *
 * returns: the newly allocated deflator.
 */
GskStream *gsk_zlib_deflator_new (int compression_level,
                                  int flush_millis)
{
  if (compression_level == -1)
    compression_level = DEFAULT_LEVEL;
  return g_object_new (GSK_TYPE_ZLIB_DEFLATOR,
		       "level", compression_level,
                       "flush-timeout", flush_millis,
		       NULL);
}

GskStream *gsk_zlib_deflator_new2 (int compression_level,
                                   int flush_millis,
                                   gboolean use_gzip)
{
  if (compression_level == -1)
    compression_level = DEFAULT_LEVEL;
  return g_object_new (GSK_TYPE_ZLIB_DEFLATOR,
		       "level", compression_level,
                       "flush-timeout", flush_millis,
                       "use-gzip", use_gzip,
		       NULL);
}
