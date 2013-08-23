#ifndef __GSK_HTTP_CLIENT_H_
#define __GSK_HTTP_CLIENT_H_

#include "gskhttpheader.h"
#include "gskhttprequest.h"
#include "gskhttpresponse.h"
#include "../gskstream.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskHttpClient GskHttpClient;
typedef struct _GskHttpClientClass GskHttpClientClass;
typedef struct _GskHttpClientRequest GskHttpClientRequest;
/* --- type macros --- */
GType gsk_http_client_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_HTTP_CLIENT			(gsk_http_client_get_type ())
#define GSK_HTTP_CLIENT(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_HTTP_CLIENT, GskHttpClient))
#define GSK_HTTP_CLIENT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_HTTP_CLIENT, GskHttpClientClass))
#define GSK_HTTP_CLIENT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_HTTP_CLIENT, GskHttpClientClass))
#define GSK_IS_HTTP_CLIENT(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_HTTP_CLIENT))
#define GSK_IS_HTTP_CLIENT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_HTTP_CLIENT))

typedef void (*GskHttpClientResponse) (GskHttpRequest  *request,
				       GskHttpResponse *response,
				       GskStream       *input,
				       gpointer         hook_data);
typedef gboolean (*GskHttpClientTrap) (GskHttpClient   *client,
				       gpointer         data);

/* --- structures --- */
struct _GskHttpClientClass 
{
  GskStreamClass stream_class;
  void         (*set_poll_requestable) (GskHttpClient *client,
					gboolean       do_poll);
  void         (*shutdown_requestable) (GskHttpClient *client);
};
struct _GskHttpClient 
{
  /*< private >*/
  GskStream             stream;
  GskHook               requestable;
  GskBuffer             incoming_data;
  GskHttpClientRequest *first_request;
  GskHttpClientRequest *last_request;
  GskHttpClientRequest *outgoing_request;
};

#define GSK_HTTP_CLIENT_FAST_NOTIFY		(1<<0)
#define GSK_HTTP_CLIENT_DEFERRED_SHUTDOWN	(1<<1)
#define GSK_HTTP_CLIENT_REQUIRES_READ_SHUTDOWN	(1<<2)
#define GSK_HTTP_CLIENT_PROPAGATE_CONTENT_READ_SHUTDOWN (1<<3)

#define GSK_HTTP_CLIENT_HOOK(client)	(&GSK_HTTP_CLIENT (client)->requestable)
#define GSK_HTTP_CLIENT_IS_FAST(client)	((GSK_HTTP_CLIENT_HOOK (client)->user_flags & GSK_HTTP_CLIENT_FAST_NOTIFY) == GSK_HTTP_CLIENT_FAST_NOTIFY)
#define gsk_http_client_trap_requestable(client,func,shutdown,data,destroy)  \
	gsk_hook_trap (GSK_HTTP_CLIENT_HOOK (client), (GskHookFunc) func,    \
		       (GskHookFunc) shutdown, data, destroy)
#define gsk_http_client_untrap_requestable(client)			     \
        gsk_hook_untrap (GSK_HTTP_CLIENT_HOOK (client))
#define gsk_http_client_is_requestable(client)				     \
        GSK_HOOK_TEST_IS_AVAILABLE (GSK_HTTP_CLIENT_HOOK (client))

/* --- prototypes --- */
GskHttpClient *gsk_http_client_new (void);
void gsk_http_client_notify_fast (GskHttpClient     *client,
				  gboolean           is_fast);
void gsk_http_client_request (GskHttpClient         *client,
			      GskHttpRequest        *request,
			      GskStream             *post_data,
			      GskHttpClientResponse  handle_response,
			      gpointer               hook_data,
			      GDestroyNotify         hook_destroy);

void gsk_http_client_shutdown_when_done (GskHttpClient *client);
void gsk_http_client_propagate_content_read_shutdown (GskHttpClient *client);
G_END_DECLS


#endif
