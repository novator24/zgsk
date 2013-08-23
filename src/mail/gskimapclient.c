#include "gskimapclient.h"
static GObjectClass *parent_class = NULL;

/* --- functions --- */
static void
gsk_imap_client_init (GskImapClient *imap_client)
{
}
static void
gsk_imap_client_class_init (GskImapClientClass *class)
{
  parent_class = g_type_class_peek_parent (class);
}

GType gsk_imap_client_get_type()
{
  static GType imap_client_type = 0;
  if (!imap_client_type)
    {
      static const GTypeInfo imap_client_info =
      {
	sizeof(GskImapClientClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_imap_client_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskImapClient),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_imap_client_init,
	NULL		/* value_table */
      };
      imap_client_type = g_type_register_static (GSK_TYPE_STREAM,
                                                  "GskImapClient",
						  &imap_client_info, 0);
    }
  return imap_client_type;
}
