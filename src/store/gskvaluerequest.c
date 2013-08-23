#define G_IMPLEMENT_INLINES
#define __GSK_VALUE_REQUEST_C__
#include "gskvaluerequest.h"

static GObjectClass *parent_class = NULL;

/* GObject methods. */

static void
gsk_value_request_finalize (GObject *object)
{
  GskValueRequest *request = GSK_VALUE_REQUEST (object);
  if (G_VALUE_TYPE (&request->value))
    g_value_unset (&request->value);
  (*parent_class->finalize) (object);
}

static void
gsk_value_request_class_init (GskRequestClass *request_class)
{
  parent_class = g_type_class_peek_parent (request_class);
  G_OBJECT_CLASS (request_class)->finalize = gsk_value_request_finalize;
}

GType
gsk_value_request_get_type (void)
{
  static GType type = 0;
  if (G_UNLIKELY (type == 0))
    {
      static const GTypeInfo type_info =
	{
	  sizeof (GskValueRequestClass),
	  (GBaseInitFunc) NULL,
	  (GBaseFinalizeFunc) NULL,
	  (GClassInitFunc) gsk_value_request_class_init,
	  NULL,		/* class_finalize */
	  NULL,		/* class_data */
	  sizeof (GskValueRequest),
	  0,		/* n_preallocs */
	  (GInstanceInitFunc) NULL,
	  NULL		/* value_table */
	};
      type = g_type_register_static (GSK_TYPE_REQUEST,
				     "GskValueRequest",
				     &type_info,
				     0);
    }
  return type;
}
