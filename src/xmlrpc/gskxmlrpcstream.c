#include "gskxmlrpcstream.h"
#include "../gskmacros.h"

static GObjectClass *parent_class = NULL;

struct _GskXmlrpcIncoming
{
  GskXmlrpcRequest *request;
  GskXmlrpcResponse *response;
  GskXmlrpcIncoming *next;
};

struct _GskXmlrpcOutgoing
{
  GskXmlrpcRequest *request;
  GskXmlrpcResponseNotify notify;
  gpointer data;
  GDestroyNotify destroy;
  GskXmlrpcOutgoing *next;
};

/* --- GskStream methods --- */
static gboolean
gsk_xmlrpc_stream_shutdown_write (GskIO      *io,
				  GError    **error)
{
  GskXmlrpcStream *xmlrpc_stream = GSK_XMLRPC_STREAM (io);

  /* XXX: error conditions... */

  if (xmlrpc_stream->last_request == NULL
   && xmlrpc_stream->outgoing.size != 0)
    gsk_io_notify_read_shutdown (io);
  return TRUE;
}

static gboolean
gsk_xmlrpc_stream_shutdown_read (GskIO      *io,
				 GError    **error)
{
  GskXmlrpcStream *xmlrpc_stream = GSK_XMLRPC_STREAM (io);
  if (xmlrpc_stream->outgoing.size != 0)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_LINGERING_DATA,
		   "data waiting to be written on shutdown stream");
      return FALSE;
    }
  if (gsk_io_get_is_writable (io))
    {
      if (!gsk_io_write_shutdown (io, error))
	return FALSE;
    }
  return TRUE;
}

static guint
gsk_xmlrpc_stream_raw_read (GskStream     *stream,
                            gpointer       data,
                            guint          length,
                            GError       **error)
{
  GskXmlrpcStream *xmlrpc_stream = GSK_XMLRPC_STREAM (stream);
  guint rv = gsk_buffer_read (&xmlrpc_stream->outgoing, data, length);
  if (xmlrpc_stream->outgoing.size == 0)
    {
      gsk_stream_clear_idle_notify_read (stream);
      if (!gsk_io_get_is_writable (stream)
       && xmlrpc_stream->last_request == NULL)
	gsk_io_notify_read_shutdown (stream);
    }
  return rv;
}

static guint
gsk_xmlrpc_stream_raw_read_buffer (GskStream     *stream,
                                   GskBuffer     *buffer,
                                   GError       **error)
{
  GskXmlrpcStream *xmlrpc_stream = GSK_XMLRPC_STREAM (stream);
  guint rv = gsk_buffer_drain (buffer, &xmlrpc_stream->outgoing);
  if (xmlrpc_stream->outgoing.size == 0)
    gsk_stream_clear_idle_notify_read (stream);
  return rv;
}

/* Handle a request from the other side. */
static void
handle_request (GskXmlrpcStream *stream, GskXmlrpcRequest *request)
{
  GskXmlrpcIncoming *incoming = g_new (GskXmlrpcIncoming, 1);
  incoming->request = request;
  incoming->response = NULL;
  incoming->next = NULL;
  if (stream->next_to_dequeue == NULL)
    gsk_hook_mark_idle_notify (GSK_XMLRPC_STREAM_REQUEST_HOOK (stream));
  if (stream->first_unhandled_request == NULL)
    {
      stream->first_unhandled_request = incoming;
      stream->last_request = incoming;
      stream->next_to_dequeue = incoming;
    }
  else
    {
      stream->last_request->next = incoming;
      stream->last_request = incoming;
      if (stream->next_to_dequeue == NULL)
	stream->next_to_dequeue = incoming;
    }
}

/* Handle a response from the other side. */
static gboolean
handle_response (GskXmlrpcStream *stream, GskXmlrpcResponse *response)
{
  GskXmlrpcOutgoing *outgoing;
  if (stream->first_unresponded_request == NULL)
    return FALSE;

  outgoing = stream->first_unresponded_request;
  stream->first_unresponded_request = outgoing->next;
  if (stream->first_unresponded_request == NULL)
    stream->last_unresponded_request = NULL;

  (*outgoing->notify) (outgoing->request, response, outgoing->data);
  if (outgoing->destroy != NULL)
    (*outgoing->destroy) (outgoing->data);
  gsk_xmlrpc_request_unref (outgoing->request);
  g_free (outgoing);
  return TRUE;
}

static guint
gsk_xmlrpc_stream_raw_write (GskStream     *stream,
                             gconstpointer  data,
                             guint          length,
                             GError       **error)
{
  GskXmlrpcStream *xmlrpc_stream = GSK_XMLRPC_STREAM (stream);
  GskXmlrpcParser *parser = xmlrpc_stream->parser;
  GskXmlrpcRequest *request;
  GskXmlrpcResponse *response;
  if (!gsk_xmlrpc_parser_feed (parser, data, length, error))
    return 0;
  while ((request=gsk_xmlrpc_parser_get_request (parser)) != NULL)
    handle_request (xmlrpc_stream, request);
  while ((response=gsk_xmlrpc_parser_get_response (parser)) != NULL)
    if (!handle_response (xmlrpc_stream, response))
      {
	g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		     _("writing to XMLRPC stream: got unsolicited response"));
	return 0;
      }
  return length;
}

static void
gsk_xmlrpc_stream_finalize (GObject *object)
{
  GskXmlrpcStream *xmlrpc_stream = GSK_XMLRPC_STREAM (object);
  gsk_xmlrpc_parser_free (xmlrpc_stream->parser);
  gsk_hook_destruct(&xmlrpc_stream->incoming_request_hook);
  (*parent_class->finalize) (object);
  
}

/* --- functions --- */
static void
gsk_xmlrpc_stream_init (GskXmlrpcStream *xmlrpc_stream)
{
  xmlrpc_stream->parser = gsk_xmlrpc_parser_new (xmlrpc_stream);
  gsk_stream_mark_is_readable (GSK_STREAM (xmlrpc_stream));
  gsk_stream_mark_is_writable (GSK_STREAM (xmlrpc_stream));
  GSK_HOOK_INIT (xmlrpc_stream, GskXmlrpcStream, incoming_request_hook, 0,
		 set_poll_requestable, shutdown_requestable);
  GSK_HOOK_SET_FLAG (GSK_XMLRPC_STREAM_REQUEST_HOOK (xmlrpc_stream), IS_AVAILABLE);
}

static void
gsk_xmlrpc_stream_class_init (GskXmlrpcStreamClass *class)
{
  GskStreamClass *stream_class = GSK_STREAM_CLASS (class);
  GskIOClass *io_class = GSK_IO_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  parent_class = g_type_class_peek_parent (class);
  io_class->shutdown_read = gsk_xmlrpc_stream_shutdown_read;
  io_class->shutdown_write = gsk_xmlrpc_stream_shutdown_write;
  stream_class->raw_read = gsk_xmlrpc_stream_raw_read;
  stream_class->raw_write = gsk_xmlrpc_stream_raw_write;
  stream_class->raw_read_buffer = gsk_xmlrpc_stream_raw_read_buffer;
  object_class->finalize = gsk_xmlrpc_stream_finalize;
  GSK_HOOK_CLASS_INIT (G_OBJECT_CLASS (class), "incoming-request-hook", GskXmlrpcStream, incoming_request_hook);
}

GType gsk_xmlrpc_stream_get_type()
{
  static GType xmlrpc_stream_type = 0;
  if (!xmlrpc_stream_type)
    {
      static const GTypeInfo xmlrpc_stream_info =
      {
	sizeof(GskXmlrpcStreamClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_xmlrpc_stream_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskXmlrpcStream),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_xmlrpc_stream_init,
	NULL		/* value_table */
      };
      xmlrpc_stream_type = g_type_register_static (GSK_TYPE_STREAM,
                                                  "GskXmlrpcStream",
						  &xmlrpc_stream_info, 0);
    }
  return xmlrpc_stream_type;
}

/* Handle incoming requests. */
/**
 * gsk_xmlrpc_stream_get_request:
 * @stream: the stream to dequeue an incomiung request from.
 *
 * Grab a new request from the stream.
 * The caller should eventually respond to it with
 * gsk_xmlrpc_stream_respond().   
 *
 * returns: a reference to a remote request which the caller
 * must call gsk_xmlrpc_request_unref() on eventually,
 * or NULL if no unhandled requests are available.
 */
GskXmlrpcRequest *gsk_xmlrpc_stream_get_request (GskXmlrpcStream *stream)
{
  GskXmlrpcRequest *request;
  if (stream->next_to_dequeue == NULL)
    return NULL;
  request = gsk_xmlrpc_request_ref (stream->next_to_dequeue->request);
  stream->next_to_dequeue = stream->next_to_dequeue->next;
  if (stream->next_to_dequeue == NULL)
    gsk_hook_clear_idle_notify (GSK_XMLRPC_STREAM_REQUEST_HOOK (stream));
  return request;
}

static void
try_flushing_incoming_requests (GskXmlrpcStream *stream)
{
  gboolean mark_idle_notify = FALSE;
  while (stream->first_unhandled_request != NULL
      && stream->first_unhandled_request->response != NULL)
    {
      GskXmlrpcIncoming *incoming = stream->first_unhandled_request;
      stream->first_unhandled_request = incoming->next;
      if (stream->first_unhandled_request == NULL)
	stream->last_request = NULL;

      g_assert (incoming != stream->next_to_dequeue);

      gsk_xmlrpc_response_to_buffer (incoming->response, &stream->outgoing);
      mark_idle_notify = TRUE;

      gsk_xmlrpc_request_unref (incoming->request);
      gsk_xmlrpc_response_unref (incoming->response);
      g_free (incoming);
    }
  if (mark_idle_notify)
    gsk_stream_mark_idle_notify_read (GSK_STREAM (stream));
}

/**
 * gsk_xmlrpc_stream_respond:
 * @stream: the stream where the incoming request came in.
 * @request: the request initiated by the other side.
 * @response: local response to the request.
 *
 * Give the RPC result to the other side of this connection.
 */
void              gsk_xmlrpc_stream_respond     (GskXmlrpcStream *stream,
						 GskXmlrpcRequest *request,
						 GskXmlrpcResponse *response)
{
  GskXmlrpcIncoming *incoming;
  for (incoming = stream->first_unhandled_request;
       incoming != NULL;
       incoming = incoming->next)
    if (incoming->request == request)
      break;
  g_return_if_fail (incoming->response == NULL);
  incoming->response = gsk_xmlrpc_response_ref (response);
  try_flushing_incoming_requests (stream);
}

/* Make outgoing requests. */
/**
 * gsk_xmlrpc_stream_make_request:
 * @stream: the stream to make the request on.
 * @request: the request to issue.
 * @notify: callback to eventaully invoke with the remote response,
 * if we get it.
 * @data: opaque user data to pass to the @notify function eventually.
 * @destroy: callback to invoke after the handler is run,
 * or if the stream shuts down before a response is obtained.
 *
 * Make a request (a method call) to the other side of this
 * #GskXmlrpcStream.  When a response is received,
 * @notify will be called, then destroy will be called.
 *
 * If the stream shuts down before a notify is obtained,
 * then just @destroy is run.
 */
void              gsk_xmlrpc_stream_make_request (GskXmlrpcStream *stream,
						  GskXmlrpcRequest *request,
						  GskXmlrpcResponseNotify notify,
						  gpointer data,
						  GDestroyNotify destroy)
{
  GskXmlrpcOutgoing *outgoing = g_new (GskXmlrpcOutgoing, 1);
  outgoing->request = g_object_ref (request);
  outgoing->notify = notify;
  outgoing->data = data;
  outgoing->destroy = destroy;
  outgoing->next = NULL;

  if (stream->first_unresponded_request == NULL)
    stream->first_unresponded_request = outgoing;
  else
    stream->last_unresponded_request->next = outgoing;
  stream->last_unresponded_request = outgoing;

  gsk_xmlrpc_request_to_buffer (request, &stream->outgoing);
  gsk_stream_mark_idle_notify_read (GSK_STREAM (stream));
}
