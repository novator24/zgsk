#ifndef __GSK_XMLRPC_STREAM_H_
#define __GSK_XMLRPC_STREAM_H_

#include "gskxmlrpc.h"
#include "../gskstream.h"

G_BEGIN_DECLS

/*Defined in gskxmlrpc.h */
/* --- typedefs --- */
/* typedef struct _GskXmlrpcStream GskXmlrpcStream; */
/* typedef struct _GskXmlrpcStreamClass GskXmlrpcStreamClass; */
/* typedef struct _GskXmlrpcOutgoing GskXmlrpcOutgoing; */
/* typedef struct _GskXmlrpcIncoming GskXmlrpcIncoming; */

/* --- type macros --- */
GType gsk_xmlrpc_stream_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_XMLRPC_STREAM			(gsk_xmlrpc_stream_get_type ())
#define GSK_XMLRPC_STREAM(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_XMLRPC_STREAM, GskXmlrpcStream))
#define GSK_XMLRPC_STREAM_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_XMLRPC_STREAM, GskXmlrpcStreamClass))
#define GSK_XMLRPC_STREAM_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_XMLRPC_STREAM, GskXmlrpcStreamClass))
#define GSK_IS_XMLRPC_STREAM(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_XMLRPC_STREAM))
#define GSK_IS_XMLRPC_STREAM_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_XMLRPC_STREAM))

/* --- structures --- */
struct _GskXmlrpcStreamClass 
{
  GskStreamClass stream_class;

  void (*set_poll_requestable) (GskXmlrpcStream *,
				gboolean polling);
  void (*shutdown_requestable) (GskXmlrpcStream *);
};

struct _GskXmlrpcStream 
{
  GskStream      stream;
  GskXmlrpcParser *parser;

  /* handle incoming requests */
  GskXmlrpcIncoming *first_unhandled_request;
  GskXmlrpcIncoming *next_to_dequeue;
  GskXmlrpcIncoming *last_request;
  GskHook incoming_request_hook;

  /* handle outgoing requests */
  GskXmlrpcOutgoing *first_unresponded_request;
  GskXmlrpcOutgoing *last_unresponded_request;

  /* queue outgoing response and request data here */
  GskBuffer outgoing;
};


/* --- prototypes --- */

/* trap this hook to get notification when a request is available. */
#define GSK_XMLRPC_STREAM_REQUEST_HOOK(stream) (&(GSK_XMLRPC_STREAM (stream)->incoming_request_hook))

#define gsk_xmlrpc_stream_request_trap(server,func,shutdown,data,destroy) \
                               gsk_hook_trap (GSK_XMLRPC_STREAM_REQUEST_HOOK (server), (GskHookFunc) func,    \
                               (GskHookFunc) shutdown, data, destroy)

#define gsk_xmlrpc_stream_request_untrap(server) gsk_hook_untrap (GSK_XMLRPC_STREAM_REQUEST_HOOK (server))

/* Handle incoming requests. */
GskXmlrpcRequest *gsk_xmlrpc_stream_get_request (GskXmlrpcStream *stream);
void              gsk_xmlrpc_stream_respond     (GskXmlrpcStream *stream,
						 GskXmlrpcRequest *request,
						 GskXmlrpcResponse *response);

/* Make outgoing requests. */
typedef void (*GskXmlrpcResponseNotify) (GskXmlrpcRequest  *request,
					 GskXmlrpcResponse *response,
					 gpointer           data);
void              gsk_xmlrpc_stream_make_request (GskXmlrpcStream *stream,
						  GskXmlrpcRequest *request,
						  GskXmlrpcResponseNotify notify,
						  gpointer data,
						  GDestroyNotify destroy);

G_END_DECLS

#endif
