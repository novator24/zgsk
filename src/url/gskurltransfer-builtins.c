/* This function is called by gsk_init() */
#include "gskurltransferhttp.h"
#include "gskurltransferfile.h"

void
_gsk_url_transfer_register_builtins (void)
{
  GskUrlTransferClass *class;

  class = g_type_class_ref (GSK_TYPE_URL_TRANSFER_HTTP);
  gsk_url_transfer_class_register (GSK_URL_SCHEME_HTTP, class);
  gsk_url_transfer_class_register (GSK_URL_SCHEME_HTTPS, class);

  class = g_type_class_ref (GSK_TYPE_URL_TRANSFER_FILE);
  gsk_url_transfer_class_register (GSK_URL_SCHEME_FILE, class);
}
