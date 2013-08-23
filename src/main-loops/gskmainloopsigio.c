/* Insert header here. */
#include "gskmainloopsigio.h"
static GObjectClass *parent_class = NULL;

/* --- functions --- */
static void
gsk_main_loop_sigio_init (GskMainLoopSigio *main_loop_sigio)
{
}
static void
gsk_main_loop_sigio_class_init (GskMainLoopSigioClass *class)
{
  parent_class = g_type_class_peek_parent (class);
}

GType gsk_main_loop_sigio_get_type()
{
  static GType main_loop_sigio_type = 0;
  if (!main_loop_sigio_type)
    {
      static const GTypeInfo main_loop_sigio_info =
      {
	sizeof(GskMainLoopSigioClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_main_loop_sigio_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskMainLoopSigio),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_main_loop_sigio_init,
	NULL		/* value_table */
      };
      main_loop_sigio_type = g_type_register_static (GSK_TYPE_MAIN_LOOP,
                                                  "GskMainLoopSigio",
						  &main_loop_sigio_info, 0);
    }
  return main_loop_sigio_type;
}
