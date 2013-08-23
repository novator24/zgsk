#ifndef __GSK_ZLIB_DEFLATOR_H_
#define __GSK_ZLIB_DEFLATOR_H_

#include "../gskstream.h"
#include "../gskmainloop.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskZlibDeflator GskZlibDeflator;
typedef struct _GskZlibDeflatorClass GskZlibDeflatorClass;
/* --- type macros --- */
GType gsk_zlib_deflator_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_ZLIB_DEFLATOR			(gsk_zlib_deflator_get_type ())
#define GSK_ZLIB_DEFLATOR(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_ZLIB_DEFLATOR, GskZlibDeflator))
#define GSK_ZLIB_DEFLATOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_ZLIB_DEFLATOR, GskZlibDeflatorClass))
#define GSK_ZLIB_DEFLATOR_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_ZLIB_DEFLATOR, GskZlibDeflatorClass))
#define GSK_IS_ZLIB_DEFLATOR(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_ZLIB_DEFLATOR))
#define GSK_IS_ZLIB_DEFLATOR_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_ZLIB_DEFLATOR))

/* --- structures --- */
struct _GskZlibDeflatorClass 
{
  GskStreamClass stream_class;
};
struct _GskZlibDeflator 
{
  GskStream      stream;
  gpointer       private_stream;
  GskBuffer      compressed;
  guint          level;
  gint           flush_millis;
  GskSource     *flush_source;
  gboolean       use_gzip;
};

/* --- prototypes --- */
/* set to -1 for default compression level;
   otherwise use 0..9, like the arguments to gzip. */
GskStream *gsk_zlib_deflator_new (gint compression_level,
                                  int flush_millis);

GskStream *gsk_zlib_deflator_new2 (int compression_level,
                                   int flush_millis,
                                   gboolean use_gzip);
G_END_DECLS

#endif
