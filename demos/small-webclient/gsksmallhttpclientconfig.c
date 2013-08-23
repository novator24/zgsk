/* Insert header here. */

#include "gsksmallhttpclientconfig.h"

/* --- prototypes --- */
static GtkObjectClass *parent_class = NULL;

/* --- functions --- */
static void
gsk_small_http_client_config_init (GskSmallHttpClientConfig *small_http_client_config)
{
}
static void
gsk_small_http_client_config_class_init (GskSmallHttpClientConfigClass *small_http_client_config_class)
{
}

GtkType gsk_small_http_client_config_get_type()
{
  static GtkType small_http_client_config_type = 0;
  if (!small_http_client_config_type)
    {
      static const GtkTypeInfo small_http_client_config_info =
      {
	"GskSmallHttpClientConfig",
	sizeof(GskSmallHttpClientConfig),
	sizeof(GskSmallHttpClientConfigClass),
	(GtkClassInitFunc) gsk_small_http_client_config_class_init,
	(GtkObjectInitFunc) gsk_small_http_client_config_init,
	/* reserved_1 */ NULL,
	/* reserved_2 */ NULL,
	(GtkClassInitFunc) NULL
      };
      GtkType parent = GTK_TYPE_OBJECT;
      small_http_client_config_type = gtk_type_unique (parent, &small_http_client_config_info);
      parent_class = gtk_type_class (parent);
    }
  return small_http_client_config_type;
}
