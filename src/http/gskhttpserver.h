#ifndef __GSK_HTTP_SERVER_H_
#define __GSK_HTTP_SERVER_H_

#include "gskhttpheader.h"
#include "gskhttprequest.h"
#include "gskhttpresponse.h"
#include "../gskstream.h"
#include "../gskmainloop.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskHttpServer GskHttpServer;
typedef struct _GskHttpServerResponse GskHttpServerResponse;
typedef struct _GskHttpServerClass GskHttpServerClass;
/* --- type macros --- */
GType gsk_http_server_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_HTTP_SERVER			(gsk_http_server_get_type ())
#define GSK_HTTP_SERVER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_HTTP_SERVER, GskHttpServer))
#define GSK_HTTP_SERVER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_HTTP_SERVER, GskHttpServerClass))
#define GSK_HTTP_SERVER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_HTTP_SERVER, GskHttpServerClass))
#define GSK_IS_HTTP_SERVER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_HTTP_SERVER))
#define GSK_IS_HTTP_SERVER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_HTTP_SERVER))

typedef gboolean (*GskHttpServerTrap) (GskHttpServer *server,
				       gpointer       data);

/* --- structures --- */
struct _GskHttpServerClass 
{
  GskStreamClass stream_class;
  void         (*set_poll_request) (GskHttpServer *server,
				    gboolean       do_poll);
  void         (*shutdown_request) (GskHttpServer *server);
};
struct _GskHttpServer 
{
  GskStream      stream;
  GskHook        has_request_hook;
  GskBuffer      incoming;
  GskHttpServerResponse *first_response;
  GskHttpServerResponse *last_response;
  GskHttpServerResponse *trapped_response;
  guint read_poll : 1;
  guint write_poll : 1;
  guint got_close : 1;
  gint keepalive_idle_timeout_ms;       /* or -1 for no timeout */
  GskSource *keepalive_idle_timeout;
};

/* --- prototypes --- */
#define GSK_HTTP_SERVER_HOOK(client)	(&GSK_HTTP_SERVER (client)->has_request_hook)
#define gsk_http_server_trap(server,func,shutdown,data,destroy)  	     \
	gsk_hook_trap (GSK_HTTP_SERVER_HOOK (server), (GskHookFunc) func,    \
		       (GskHookFunc) shutdown, data, destroy)
#define gsk_http_server_untrap(server)			     	             \
        gsk_hook_untrap (GSK_HTTP_SERVER_HOOK (server))


GskHttpServer  *gsk_http_server_new         (void);
gboolean        gsk_http_server_get_request (GskHttpServer   *server,
					     GskHttpRequest **request_out,
					     GskStream      **post_data_out);
void            gsk_http_server_respond     (GskHttpServer   *server,
					     GskHttpRequest  *request,
					     GskHttpResponse *response,
					     GskStream       *content);
void            gsk_http_server_set_idle_timeout
                                            (GskHttpServer   *server,
                                             gint             millis);




G_END_DECLS

#endif
