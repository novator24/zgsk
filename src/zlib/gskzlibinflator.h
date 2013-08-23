#ifndef __GSK_ZLIB_INFLATOR_H_
#define __GSK_ZLIB_INFLATOR_H_

#include "../gskstream.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskZlibInflator GskZlibInflator;
typedef struct _GskZlibInflatorClass GskZlibInflatorClass;
/* --- type macros --- */
GType gsk_zlib_inflator_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_ZLIB_INFLATOR			(gsk_zlib_inflator_get_type ())
#define GSK_ZLIB_INFLATOR(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_ZLIB_INFLATOR, GskZlibInflator))
#define GSK_ZLIB_INFLATOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_ZLIB_INFLATOR, GskZlibInflatorClass))
#define GSK_ZLIB_INFLATOR_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_ZLIB_INFLATOR, GskZlibInflatorClass))
#define GSK_IS_ZLIB_INFLATOR(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_ZLIB_INFLATOR))
#define GSK_IS_ZLIB_INFLATOR_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_ZLIB_INFLATOR))

/* --- structures --- */
struct _GskZlibInflatorClass 
{
  GskStreamClass stream_class;
};
struct _GskZlibInflator 
{
  GskStream      stream;
  gpointer       private_stream;
  GskBuffer      decompressed;
  gboolean       use_gzip;
};

/* --- prototypes --- */
GskStream *gsk_zlib_inflator_new (void);
GskStream *gsk_zlib_inflator_new2 (gboolean use_gzip);

G_END_DECLS

#endif
