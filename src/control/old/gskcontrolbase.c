/* Insert header here. */
#include "gskcontrolbase.h"
static GObjectClass *parent_class = NULL;

/* --- functions --- */
static void
gsk_control_base_init (GskControlBase *control_base)
{
}
static void
gsk_control_base_class_init (GskControlBaseClass *class)
{
  parent_class = g_type_class_peek_parent (class);
}

GType gsk_control_base_get_type()
{
  static GType control_base_type = 0;
  if (!control_base_type)
    {
      static const GTypeInfo control_base_info =
      {
	sizeof(GskControlBaseClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_control_base_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskControlBase),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_control_base_init,
	NULL		/* value_table */
      };
      control_base_type = g_type_register_static (GSK_TYPE_STREAM,
                                                  "GskControlBase",
						  &control_base_info, 0);
    }
  return control_base_type;
}
