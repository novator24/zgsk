#include "microhttplistener.h"
#include <gsk/xml/gskserver.h>
#include <gsk/gskmainlooppoll.h>

static void
micro_listener_register ()
{
  gtk_type_class (MICRO_TYPE_HTTP_LISTENER);
}

static void
kludges ()
{
#if HAVE_POLL
  gtk_type_class (GSK_TYPE_MAIN_LOOP_POLL);
#endif
}

GskServerRegisterFunc gsk_server_register_funcs[] =
{
  gsk_http_servlet_builtins_init,
  micro_listener_register,
  kludges
};
