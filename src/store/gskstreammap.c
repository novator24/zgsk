#define G_IMPLEMENT_INLINES
#define __GSK_STREAM_MAP_C__
#include "gskstreammap.h"

/*
 * GskStreamMap
 */

GType
gsk_stream_map_get_type (void)
{
  static GType type = 0;
  if (G_UNLIKELY (type == 0))
    {
      static const GTypeInfo type_info =
	{
	  sizeof (GskStreamMapIface),
	  NULL, /* base_init */
	  NULL, /* base_finalize */
	  NULL,
	  NULL, /* class_finalize */
	  NULL, /* class_data */
	  0,
	  0,
	  NULL,
	  NULL
	};
      type = g_type_register_static (G_TYPE_INTERFACE,
				     "GskStreamMap",
				     &type_info,
				     G_TYPE_FLAG_ABSTRACT);
    }
  return type;
}

/*
 * GskStreamMapRequest
 */

static GObjectClass *gsk_stream_map_request_parent_class = NULL;

static void
gsk_stream_map_request_finalize (GObject *object)
{
  GskStreamMapRequest *request = GSK_STREAM_MAP_REQUEST (object);
  if (request->key)
    g_free (request->key);
  (*gsk_stream_map_request_parent_class->finalize) (object);
}

static void
gsk_stream_map_request_class_init (GskRequestClass *request_class)
{
  gsk_stream_map_request_parent_class =
    g_type_class_peek_parent (request_class);
  G_OBJECT_CLASS (request_class)->finalize = gsk_stream_map_request_finalize;
}

GType
gsk_stream_map_request_get_type (void)
{
  static GType type = 0;
  if (G_UNLIKELY (type == 0))
    {
      static const GTypeInfo type_info =
	{
	  sizeof (GskStreamMapRequestClass),
	  (GBaseInitFunc) NULL,
	  (GBaseFinalizeFunc) NULL,
	  (GClassInitFunc) gsk_stream_map_request_class_init,
	  NULL,		/* class_finalize */
	  NULL,		/* class_data */
	  sizeof (GskStreamMapRequest),
	  0,		/* n_preallocs */
	  (GInstanceInitFunc) NULL,
	  NULL		/* value_table */
	};
      type = g_type_register_static (GSK_TYPE_REQUEST,
				     "GskStreamMapRequest",
				     &type_info,
				     0);
    }
  return type;
}
