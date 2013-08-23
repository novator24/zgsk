#include "gskstreamlistener.h"
#include "gskerror.h"
#include "debug.h"
#include "gskmacros.h"

static GObjectClass *parent_class = NULL;

#define GSK_DEBUG_LISTENER(args) _GSK_DEBUG_PRINTF(GSK_DEBUG_STREAM_LISTENER,args)

/* --- public api --- */
/**
 * gsk_stream_listener_handle_accept:
 * @listener: object to handle accepted connections from.
 * @func: function to call if a connection is accepted.
 * @err_func: function to call if an error occurs.
 * @data: data to be passed to @func and @err_func.
 * @destroy: function to be notified with the trap is been undone.
 *
 * Public interface to trap when new connections are established.
 * Basically, @func will be called with new streams until an error occurs,
 * then @err_func and @destroy will be run.
 */
void
gsk_stream_listener_handle_accept   (GskStreamListener *listener,
				     GskStreamListenerAcceptFunc func,
				     GskStreamListenerErrorFunc err_func,
				     gpointer           data,
				     GDestroyNotify     destroy)
{
  g_return_if_fail (listener->accept_func == NULL);
  GSK_DEBUG_LISTENER (("gsk_stream_listener_handle_accept: %s[%p] func=%p",
	               g_type_name (G_OBJECT_TYPE (listener)), listener,
	               func));
  listener->accept_func = func;
  listener->error_func = err_func;
  listener->data = data;
  listener->destroy = destroy;
}

/* --- protected api --- */
/**
 * gsk_stream_listener_notify_accepted:
 * @stream_listener: object which accepted a new connection.
 * @new_stream: the newly accepted input stream.
 *
 * Called by a derived class to notify the system that 
 * a new stream has been accepted.
 */
void
gsk_stream_listener_notify_accepted (GskStreamListener *stream_listener,
				     GskStream         *new_stream)
{
  GError *error = NULL;

  GSK_DEBUG_LISTENER (("gsk_stream_listener_notify_accepted: %s[%p] received %s[%p]",
	               g_type_name (G_OBJECT_TYPE (stream_listener)), stream_listener,
	               g_type_name (G_OBJECT_TYPE (new_stream)), new_stream));

  if (stream_listener->accept_func == NULL)
    {
      g_warning ("no handler for accepting new connections from %s",
		 g_type_name (G_OBJECT_TYPE (stream_listener)));
      return;
    }
  if (!stream_listener->accept_func (new_stream, stream_listener->data, &error))
    {
      if (error == NULL)
	error = g_error_new (GSK_G_ERROR_DOMAIN,
			     GSK_ERROR_ACCEPTED_SOCKET_FAILED,
			     _("error processing accepted %s from %s"),
			     g_type_name (G_OBJECT_TYPE (new_stream)),
			     g_type_name (G_OBJECT_TYPE (stream_listener)));
      gsk_stream_listener_notify_error (stream_listener, error);
    }
}

/**
 * gsk_stream_listener_notify_error:
 * @stream_listener: object which has an error.
 * @error: the error which occurred.  The *callee* *takes* responsibility for error.
 *
 * Called by a derived class to notify the system that 
 * an error has occurred.
 */
void
gsk_stream_listener_notify_error    (GskStreamListener *stream_listener,
				     GError            *error)
{
  if (stream_listener->error_func != NULL)
    stream_listener->error_func (error, stream_listener->data);
  else
    {
      if (stream_listener->last_error != NULL)
	g_error_free (stream_listener->last_error);
      stream_listener->last_error = error;
    }
}

static void
gsk_stream_listener_finalize (GObject *object)
{
  GskStreamListener *listener = GSK_STREAM_LISTENER (object);
  if (listener->last_error != NULL)
    g_error_free (listener->last_error);
  if (listener->destroy)
    (*listener->destroy) (listener->data);
  (*parent_class->finalize) (object);
}

/* --- functions --- */
static void
gsk_stream_listener_init (GskStreamListener *stream_listener)
{
}

static void
gsk_stream_listener_class_init (GskStreamListenerClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  parent_class = g_type_class_peek_parent (class);
  object_class->finalize = gsk_stream_listener_finalize;
}

GType gsk_stream_listener_get_type()
{
  static GType stream_listener_type = 0;
  if (!stream_listener_type)
    {
      static const GTypeInfo stream_listener_info =
      {
	sizeof(GskStreamListenerClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_stream_listener_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskStreamListener),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_stream_listener_init,
	NULL		/* value_table */
      };
      GType parent = G_TYPE_OBJECT;
      stream_listener_type = g_type_register_static (parent,
                                                  "GskStreamListener",
						  &stream_listener_info, 0);
    }
  return stream_listener_type;
}
