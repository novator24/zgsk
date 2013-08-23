#include "../gskstreamlistenersocket.h"

typedef struct
{
  GskSocketAddress *bind_address;
  GskStreamListenerSocket *listener;
  GskSocketAddress *connect_address;
} Mapping;

static gboolean
handle_new_connection (GskStream *incoming,
		       gpointer   data,
		       GError   **error)
{
  Mapping *mapping = data;
  GError *error = NULL;
  GskStream *outgoing = gsk_stream_new_connecting (mapping->connect_address, &error);
  if (outgoing == NULL)
    {
      g_warning ("error connecting to remote host: %s", error->message);
      return FALSE;
    }
typedef gboolean (*GskStreamListenerAcceptFunc)(GskStream    *stream,
						gpointer      data,
						GError      **error);
gsk_stream_listener_handle_accept   (GskStreamListener *listener,
				          GskStreamListenerAcceptFunc func,
					  GskStreamListenerErrorFunc err_func,
					  gpointer           data,
					  GDestroyNotify     destroy);

