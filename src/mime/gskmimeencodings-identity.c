#include "gskmimeencodings.h"
#include "../gsksimplefilter.h"

typedef struct _GskMimeIdentityFilter GskMimeIdentityFilter;
typedef struct _GskMimeIdentityFilterClass GskMimeIdentityFilterClass;
GType gsk_mime_identity_filter_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MIME_IDENTITY_FILTER			(gsk_mime_identity_filter_get_type ())
#define GSK_MIME_IDENTITY_FILTER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_MIME_IDENTITY_FILTER, GskMimeIdentityFilter))
#define GSK_MIME_IDENTITY_FILTER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_MIME_IDENTITY_FILTER, GskMimeIdentityFilterClass))
#define GSK_MIME_IDENTITY_FILTER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_MIME_IDENTITY_FILTER, GskMimeIdentityFilterClass))
#define GSK_IS_MIME_IDENTITY_FILTER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_MIME_IDENTITY_FILTER))
#define GSK_IS_MIME_IDENTITY_FILTER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_MIME_IDENTITY_FILTER))

struct _GskMimeIdentityFilterClass 
{
  GskSimpleFilterClass simple_filter_class;
};
struct _GskMimeIdentityFilter 
{
  GskSimpleFilter      simple_filter;
};
static GObjectClass *parent_class = NULL;

static gboolean
gsk_mime_identity_filter_process (GskSimpleFilter *filter,
                                  GskBuffer       *dst,
                                  GskBuffer       *src,
                                  GError         **error)
{
  gsk_buffer_drain (dst, src);
  return TRUE;
}

static gboolean
gsk_mime_identity_filter_flush (GskSimpleFilter *filter,
                                GskBuffer       *dst,
                                GskBuffer       *src,
                                GError         **error)
{
  g_return_val_if_fail (src->size == 0, FALSE);
  return TRUE;
}

/* --- functions --- */
static void
gsk_mime_identity_filter_init (GskMimeIdentityFilter *mime_identity_filter)
{
  g_assert (gsk_io_get_is_writable (mime_identity_filter));
  g_assert (gsk_io_get_is_readable (mime_identity_filter));
}
static void
gsk_mime_identity_filter_class_init (GskMimeIdentityFilterClass *class)
{
  GskSimpleFilterClass *simple_filter_class = GSK_SIMPLE_FILTER_CLASS (class);
  parent_class = g_type_class_peek_parent (class);
  simple_filter_class->process = gsk_mime_identity_filter_process;
  simple_filter_class->flush = gsk_mime_identity_filter_flush;
}

GType gsk_mime_identity_filter_get_type()
{
  static GType mime_identity_filter_type = 0;
  if (!mime_identity_filter_type)
    {
      static const GTypeInfo mime_identity_filter_info =
      {
	sizeof(GskMimeIdentityFilterClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_mime_identity_filter_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskMimeIdentityFilter),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_mime_identity_filter_init,
	NULL		/* value_table */
      };
      mime_identity_filter_type = g_type_register_static (GSK_TYPE_SIMPLE_FILTER,
                                                  "GskMimeIdentityFilter",
						  &mime_identity_filter_info, 0);
    }
  return mime_identity_filter_type;
}

/**
 * gsk_mime_identity_filter_new:
 *
 * A filter which gives the exact same output as it receives input.
 *
 * returns: the newly allocated identity filter.
 */
GskStream *
gsk_mime_identity_filter_new (void)
{
  return g_object_new (GSK_TYPE_MIME_IDENTITY_FILTER, NULL);
}
