/* Insert header here. */
#ifndef __MICRO_HTTP_LISTENER_H_
#define __MICRO_HTTP_LISTENER_H_

#include <gsk/gskactorlistener.h>
#include <gsk/protocols/gskhttpserver.h>
#include <gsk/protocols/gskhttpservlet.h>

G_BEGIN_DECLS

typedef struct _MicroHttpListener MicroHttpListener;
typedef struct _MicroHttpListenerClass MicroHttpListenerClass;

GtkType micro_http_listener_get_type();
#define MICRO_TYPE_HTTP_LISTENER			(micro_http_listener_get_type ())
#define MICRO_HTTP_LISTENER(obj)              (GTK_CHECK_CAST ((obj), MICRO_TYPE_HTTP_LISTENER, MicroHttpListener))
#define MICRO_HTTP_LISTENER_CLASS(klass)      (GTK_CHECK_CLASS_CAST ((klass), MICRO_TYPE_HTTP_LISTENER, MicroHttpListenerClass))
#define MICRO_HTTP_LISTENER_GET_CLASS(obj)    (MICRO_HTTP_LISTENER_CLASS(GTK_OBJECT(obj)->klass))
#define MICRO_IS_HTTP_LISTENER(obj)           (GTK_CHECK_TYPE ((obj), MICRO_TYPE_HTTP_LISTENER))
#define MICRO_IS_HTTP_LISTENER_CLASS(klass)   (GTK_CHECK_CLASS_TYPE ((klass), MICRO_TYPE_HTTP_LISTENER))

struct _MicroHttpListenerClass {
  GskActorListenerClass		actor_listener_class;
  void (*add_servlet)(MicroHttpListener *listener,
		      GskHttpPredicate  *predicate,
		      GskHttpServlet    *servlet);
};
struct _MicroHttpListener {
  GskActorListener		actor_listener;
  GskHttpServletList           *servlets;
};

G_END_DECLS

#endif
