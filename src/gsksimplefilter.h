#ifndef __GSK_SIMPLE_FILTER_H_
#define __GSK_SIMPLE_FILTER_H_

#include "gskstream.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskSimpleFilter GskSimpleFilter;
typedef struct _GskSimpleFilterClass GskSimpleFilterClass;
/* --- type macros --- */
GType gsk_simple_filter_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_SIMPLE_FILTER			(gsk_simple_filter_get_type ())
#define GSK_SIMPLE_FILTER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_SIMPLE_FILTER, GskSimpleFilter))
#define GSK_SIMPLE_FILTER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_SIMPLE_FILTER, GskSimpleFilterClass))
#define GSK_SIMPLE_FILTER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_SIMPLE_FILTER, GskSimpleFilterClass))
#define GSK_IS_SIMPLE_FILTER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_SIMPLE_FILTER))
#define GSK_IS_SIMPLE_FILTER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_SIMPLE_FILTER))

/* --- structures --- */
struct _GskSimpleFilterClass 
{
  GskStreamClass stream_class;
  gboolean (*process)(GskSimpleFilter *filter,
		      GskBuffer       *dst,
		      GskBuffer       *src,
		      GError         **error);
  gboolean (*flush)  (GskSimpleFilter *filter,
		      GskBuffer       *dst,
		      GskBuffer       *src,
		      GError         **error);
};
struct _GskSimpleFilter 
{
  GskStream      stream;

  /*< private >*/
  GskBuffer read_buffer;
  GskBuffer write_buffer;
  guint     max_read_buffer_size;
  guint     max_write_buffer_size;
};
/* --- prototypes --- */
G_END_DECLS

#endif
