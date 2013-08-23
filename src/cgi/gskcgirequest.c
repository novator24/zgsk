/* Insert header here. */
#include "gskcgirequest.h"
static GObjectClass *parent_class = NULL;

/* --- functions --- */
static void
gsk_cgi_request_init (GskCgiRequest *cgi_request)
{
}
static void
gsk_cgi_request_class_init (GskCgiRequestClass *class)
{
  parent_class = g_type_class_peek_parent (class);
}

GType gsk_cgi_request_get_type()
{
  static GType cgi_request_type = 0;
  if (!cgi_request_type)
    {
      static const GTypeInfo cgi_request_info =
      {
	sizeof(GskCgiRequestClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_cgi_request_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskCgiRequest),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_cgi_request_init,
	NULL		/* value_table */
      };
      cgi_request_type = g_type_register_static (G_TYPE_OBJECT,
                                                  "GskCgiRequest",
						  &cgi_request_info, 0);
    }
  return cgi_request_type;
}
