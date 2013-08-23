#include "microhttplistener.h"
#include <gsk/gskconstraint.h>

static GtkObjectClass *parent_class = NULL;

enum
{
  ADD_SERVLET,
  LAST_SIGNAL
};

static guint signal_ids[LAST_SIGNAL] = { 0 };

static void
micro_http_listener_add_servlet (MicroHttpListener *micro,
				 GskHttpPredicate  *predicate,
				 GskHttpServlet    *servlet)
{
  if (! GSK_IS_HTTP_PREDICATE (predicate))
    {
      g_warning ("micro-http-listener: add-servlet: invalid predicate");
      return;
    }
  if (! GSK_IS_HTTP_SERVLET (servlet))
    {
      g_warning ("micro-http-listener: add-servlet: invalid servlet");
      return;
    }

  gsk_http_servlet_list_append (micro->servlets, predicate, servlet);
}

static gboolean
micro_http_listener_on_accept (GskActorListener *listener,
			       GskMainLoop      *main_loop,
			       GskStreamSocket  *accepted,
			       GskSocketAddress *addr)
{
  GskHttpServer *server = GSK_HTTP_SERVER (gsk_gtk_object_new (GSK_TYPE_HTTP_SERVER));
  GskHttpServletList *list = MICRO_HTTP_LISTENER (listener)->servlets;
  if (addr->address_family != GSK_SOCKET_ADDRESS_IPv4)
    g_message ("accepted connection from unknown family");
  else
    g_message ("accepted connection from %d.%d.%d.%d",
	       addr->ipv4.ip_address[0],
	       addr->ipv4.ip_address[1],
	       addr->ipv4.ip_address[2],
	       addr->ipv4.ip_address[3]);
  server->servlets = list;
  gsk_http_servlet_list_ref (list);
  gsk_actor_stream_socket_set_socket (GSK_ACTOR_STREAM_SOCKET (server), accepted);
  gsk_actor_set_main_loop (GSK_ACTOR (server), main_loop);
  gtk_object_unref (GTK_OBJECT (server));
  return TRUE;
}

static void
micro_http_listener_finalize (GtkObject *object)
{
  gsk_http_servlet_list_unref (MICRO_HTTP_LISTENER (object)->servlets);
  (*parent_class->finalize) (object);
}

static void
micro_http_listener_init (MicroHttpListener* http_listener)
{
  http_listener->servlets = gsk_http_servlet_list_new ();
}
static void micro_http_listener_class_init (GskActorListenerClass* listener_class)
{
  MicroHttpListenerClass *mclass = MICRO_HTTP_LISTENER_CLASS (listener_class);
  GtkObjectClass *class = GTK_OBJECT_CLASS (listener_class);
  mclass->add_servlet = micro_http_listener_add_servlet;
  class->finalize = micro_http_listener_finalize;
  listener_class->on_accept = micro_http_listener_on_accept;
  signal_ids[ADD_SERVLET] = gtk_signal_new ("add-servlet",
					    GTK_RUN_LAST | GTK_RUN_ACTION,
					    class->type,
					    GTK_SIGNAL_OFFSET (MicroHttpListenerClass,
							       add_servlet),
					    gtk_marshal_NONE__POINTER_POINTER,
					    GTK_TYPE_NONE,
					    2,
					    GTK_TYPE_OBJECT,
					    GTK_TYPE_OBJECT);
  gtk_object_class_add_signals (class, signal_ids, LAST_SIGNAL);

  gsk_constraint_add_for_signal (signal_ids[ADD_SERVLET],
				0,
				gsk_constraint_test_implements,
				GUINT_TO_POINTER (GSK_TYPE_HTTP_PREDICATE_IFACE),
				NULL);
  gsk_constraint_add_for_signal (signal_ids[ADD_SERVLET],
				1,
				gsk_constraint_test_implements,
				GUINT_TO_POINTER (GSK_TYPE_HTTP_SERVLET_IFACE),
				NULL);
}

GtkType micro_http_listener_get_type()
{
  static GtkType http_listener_type = 0;
  if (!http_listener_type) {
    static const GtkTypeInfo http_listener_info =
    {
      "MicroHttpListener",
      sizeof(MicroHttpListener),
      sizeof(MicroHttpListenerClass),
      (GtkClassInitFunc) micro_http_listener_class_init,
      (GtkObjectInitFunc) micro_http_listener_init,
      /* reserved_1 */ NULL,
      /* reserved_2 */ NULL,
      (GtkClassInitFunc) NULL
    };
    GtkType parent = GSK_TYPE_ACTOR_LISTENER;
    http_listener_type = gtk_type_unique (parent, &http_listener_info);
    parent_class = gtk_type_class (parent);
  }
  return http_listener_type;
}
