#include "gskftpclient.h"

static GObjectClass *parent_class = NULL;

/* --- functions --- */
static void
gsk_ftp_client_init (GskFtpClient *ftp_client)
{
}

static void
gsk_ftp_client_class_init (GskFtpClientClass *class)
{
  parent_class = g_type_class_peek_parent (class);
}

GType gsk_ftp_client_get_type()
{
  static GType ftp_client_type = 0;
  if (!ftp_client_type)
    {
      static const GTypeInfo ftp_client_info =
      {
	sizeof(GskFtpClientClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_ftp_client_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskFtpClient),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_ftp_client_init,
	NULL		/* value_table */
      };
      ftp_client_type = g_type_register_static (GSK_TYPE_STREAM,
                                                  "GskFtpClient",
						  &ftp_client_info, 0);
    }
  return ftp_client_type;
}

GskFtpClient *gsk_ftp_client_new (void)
{
  return g_object_new (GSK_TYPE_FTP_CLIENT, NULL);
}

void
gsk_ftp_client_issue_request (GskFtpClient    *client,
			      GskFtpRequest   *request)
{
  ....
}

void
gsk_ftp_client_trap_response  (GskFtpClient    *client,
			       GskFtpClientHandleResponse handler,
			       gpointer         data,
			       GDestroyNotify   destroy)
{
  g_return_if_fail (client->handler == NULL);
  g_return_if_fail (handler != NULL);
  client->handler = handler;
  client->handler_data = data;
  client->handler_data_destroy = destroy;
}
