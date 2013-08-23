/* Insert header here. */

#include "gsksmallhttpclient.h"

/* --- prototypes --- */
static GtkObjectClass *parent_class = NULL;

/* --- functions --- */
static void
gsk_small_http_client_init (GskSmallHttpClient *small_http_client)
{
}
static void
gsk_small_http_client_class_init (GskSmallHttpClientClass *small_http_client_class)
{
}

GtkType gsk_small_http_client_get_type()
{
  static GtkType small_http_client_type = 0;
  if (!small_http_client_type)
    {
      static const GtkTypeInfo small_http_client_info =
      {
	"GskSmallHttpClient",
	sizeof(GskSmallHttpClient),
	sizeof(GskSmallHttpClientClass),
	(GtkClassInitFunc) gsk_small_http_client_class_init,
	(GtkObjectInitFunc) gsk_small_http_client_init,
	/* reserved_1 */ NULL,
	/* reserved_2 */ NULL,
	(GtkClassInitFunc) NULL
      };
      GtkType parent = GSK_TYPE_HTTP_CLIENT;
      small_http_client_type = gtk_type_unique (parent, &small_http_client_info);
      parent_class = gtk_type_class (parent);
    }
  return small_http_client_type;
}
