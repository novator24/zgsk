#include "gskpersistentconnection.h"
#include "gskstreamclient.h"
#include "gsknameresolver.h"

G_DEFINE_TYPE(GskPersistentConnection, gsk_persistent_connection, GSK_TYPE_STREAM);

static guint handle_connected_signal_id = 0;
static guint handle_disconnected_signal_id = 0;

static void gsk_persistent_connection_set_poll_read (GskIO    *io,
                                                     gboolean  should_poll);
static void gsk_persistent_connection_set_poll_write(GskIO    *io,
                                                     gboolean  should_poll);

static inline void
maybe_message (GskPersistentConnection *connection,
               const char              *verb)
{
  if (connection->debug_connection)
    {
      char *location = gsk_socket_address_to_string (connection->address);
      g_message ("%s %s", verb, location);
      g_free (location);
    }
}

static void
gsk_persistent_connection_handle_disconnected (GskPersistentConnection *connection)
{
  maybe_message (connection, "disconnected from");
}

static void
gsk_persistent_connection_handle_connected (GskPersistentConnection *connection)
{
  maybe_message (connection, "connected to");
}

static guint
gsk_persistent_connection_raw_read(GskStream     *stream,
			 	   gpointer       data,
			 	   guint          length,
			 	   GError       **error)
{
  GskPersistentConnection *connection = GSK_PERSISTENT_CONNECTION (stream);
  if (connection->transport == NULL)
    return 0;
  return gsk_stream_read (connection->transport, data, length, error);
}

static guint
gsk_persistent_connection_raw_write(GskStream     *stream,
			 	    gconstpointer  data,
			 	    guint          length,
			 	    GError       **error)
{
  GskPersistentConnection *connection = GSK_PERSISTENT_CONNECTION (stream);
  if (connection->transport == NULL)
    return 0;
  return gsk_stream_write (connection->transport, data, length, error);
}

static guint
gsk_persistent_connection_raw_read_buffer(GskStream     *stream,
				          GskBuffer     *buffer,
				          GError       **error)
{
  GskPersistentConnection *connection = GSK_PERSISTENT_CONNECTION (stream);
  if (connection->transport == NULL)
    return 0;
  return gsk_stream_read_buffer (connection->transport, buffer, error);
}

static guint
gsk_persistent_connection_raw_write_buffer(GskStream    *stream,
                                           GskBuffer     *buffer,
				           GError       **error)
{
  GskPersistentConnection *connection = GSK_PERSISTENT_CONNECTION (stream);
  if (connection->transport == NULL)
    return 0;
  return gsk_stream_write_buffer (connection->transport, buffer, error);
}

static void
shutdown_transport (GskPersistentConnection *connection)
{
  if (gsk_io_has_write_hook (connection->transport))
    gsk_io_untrap_writable (connection->transport);
  if (gsk_io_has_read_hook (connection->transport))
    gsk_io_untrap_readable (connection->transport);
  gsk_io_shutdown (GSK_IO (connection->transport), NULL);
  if (connection->state == GSK_PERSISTENT_CONNECTION_CONNECTING)
    g_signal_handler_disconnect (G_OBJECT (connection->transport),
                         connection->transport_on_connect_signal_handler);
  g_signal_handler_disconnect (G_OBJECT (connection->transport),
                       connection->transport_on_error_signal_handler);
  g_object_unref (connection->transport);
  connection->transport = NULL;
}

static void
gsk_persistent_connection_finalize (GObject *object)
{
  GskPersistentConnection *connection = GSK_PERSISTENT_CONNECTION (object);
  if (connection->transport != NULL)
    shutdown_transport (connection);
  if (connection->retry_timeout_source)
    {
      GskSource *source = connection->retry_timeout_source;
      connection->retry_timeout_source = NULL;
      gsk_source_remove (source);
    }
  G_OBJECT_CLASS (gsk_persistent_connection_parent_class)->finalize (object);
}

static void
gsk_persistent_connection_init (GskPersistentConnection *connection)
{
  gsk_io_mark_is_readable (connection);
  gsk_io_mark_is_writable (connection);
}

static void
gsk_persistent_connection_class_init (GskPersistentConnectionClass *class)
{
  GskIOClass *io_class = GSK_IO_CLASS (class);
  GskStreamClass *stream_class = GSK_STREAM_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  class->handle_connected = gsk_persistent_connection_handle_connected;
  class->handle_disconnected = gsk_persistent_connection_handle_disconnected;
  io_class->set_poll_read = gsk_persistent_connection_set_poll_read;
  io_class->set_poll_write = gsk_persistent_connection_set_poll_write;
  stream_class->raw_read = gsk_persistent_connection_raw_read;
  stream_class->raw_write = gsk_persistent_connection_raw_write;
  stream_class->raw_read_buffer = gsk_persistent_connection_raw_read_buffer;
  stream_class->raw_write_buffer = gsk_persistent_connection_raw_write_buffer;
  object_class->finalize = gsk_persistent_connection_finalize;


  handle_connected_signal_id
    = g_signal_new ("handle-connected",
                    G_OBJECT_CLASS_TYPE (object_class),
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GskPersistentConnectionClass, handle_connected),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__VOID,
                    G_TYPE_NONE,
                    0);
  handle_disconnected_signal_id
    = g_signal_new ("handle-disconnected",
                    G_OBJECT_CLASS_TYPE (object_class),
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GskPersistentConnectionClass, handle_disconnected),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__VOID,
                    G_TYPE_NONE,
                    0);
}

static gboolean handle_retry_timeout_expired (gpointer data);

static void
setup_timeout (GskPersistentConnection *connection)
{
  g_return_if_fail (connection->retry_timeout_source == NULL);
  connection->retry_timeout_source
    = gsk_main_loop_add_timer (gsk_main_loop_default (),
                               handle_retry_timeout_expired,
                               connection,
                               NULL,
                               connection->retry_timeout_ms,
                               -1);
  connection->state = GSK_PERSISTENT_CONNECTION_WAITING;
}

static void
handle_transport_connected (GskStream *stream,
                            GskPersistentConnection *connection)
{
  g_return_if_fail (connection->transport == stream);
  g_return_if_fail (connection->state == GSK_PERSISTENT_CONNECTION_CONNECTING);
  connection->state = GSK_PERSISTENT_CONNECTION_CONNECTED;
  g_signal_handler_disconnect (stream,
                               connection->transport_on_connect_signal_handler);
  g_signal_emit (connection, handle_connected_signal_id, 0);
}

static gboolean
handle_transport_readable (GskStream               *transport,
                           GskPersistentConnection *connection)
{
  g_return_val_if_fail (connection->transport == transport, FALSE);
  gsk_io_notify_ready_to_read (connection);
  return TRUE;
}

static gboolean
handle_transport_read_shutdown (GskStream           *transport,
                                GskPersistentConnection *connection)
{
  g_return_val_if_fail (connection->transport == transport, FALSE);
  g_assert (connection->state == GSK_PERSISTENT_CONNECTION_CONNECTED
        ||  connection->state == GSK_PERSISTENT_CONNECTION_CONNECTING);
  if (gsk_io_has_write_hook (transport))
    gsk_io_untrap_writable (transport);
  shutdown_transport (connection);
  connection->state = GSK_PERSISTENT_CONNECTION_WAITING;
  g_signal_emit (connection, handle_disconnected_signal_id, 0);
  setup_timeout (connection);
  return FALSE;
}

static gboolean
handle_transport_writable (GskStream           *transport,
                           GskPersistentConnection *connection)
{
  g_return_val_if_fail (connection->transport == transport, FALSE);
  gsk_io_notify_ready_to_write (connection);
  return TRUE;
}

static void
handle_transport_error (GskStream *transport,
                        GskPersistentConnection *connection)
{
  g_return_if_fail (connection->transport == transport);
  if (connection->warn_on_transport_errors)
    g_warning ("error in transport: %s", GSK_IO (transport)->error->message);
  shutdown_transport (connection);
  g_signal_emit (connection, handle_disconnected_signal_id, 0);
  setup_timeout (connection);
}

static gboolean
handle_transport_write_shutdown (GskStream           *transport,
                                 GskPersistentConnection *connection)
{
  g_return_val_if_fail (connection->transport == transport, FALSE);
  g_assert (connection->state == GSK_PERSISTENT_CONNECTION_CONNECTED
         || connection->state == GSK_PERSISTENT_CONNECTION_CONNECTING);

  if (gsk_io_has_read_hook (transport))
    gsk_io_untrap_readable (transport);
  shutdown_transport (connection);
  connection->state = GSK_PERSISTENT_CONNECTION_WAITING;
  g_signal_emit (connection, handle_disconnected_signal_id, 0);
  setup_timeout (connection);

  return FALSE;
}

static void
retry_connection (GskPersistentConnection *connection,
                  GskSocketAddress        *address)
{
  GError *error = NULL;
  GskStream *transport = gsk_stream_new_connecting (address, &error);
  if (transport == NULL)
    {
      gsk_io_set_gerror (GSK_IO (connection),
                         GSK_IO_ERROR_CONNECT,
                         error);
      setup_timeout (connection);
      return;
    }
  connection->transport = transport;
  if (GSK_STREAM_FD (transport)->is_resolving_name
   || gsk_io_get_is_connecting (transport))
    {
      connection->state = GSK_PERSISTENT_CONNECTION_CONNECTING;
      connection->transport_on_connect_signal_handler
        = g_signal_connect (transport,
                            "on-connect",
                            G_CALLBACK (handle_transport_connected),
                            connection);
    }
  else
    {
      connection->state = GSK_PERSISTENT_CONNECTION_CONNECTED;
      g_signal_emit (connection, handle_connected_signal_id, 0);
    }
  if (gsk_io_is_polling_for_read (connection))
    gsk_io_trap_readable (transport,
                          handle_transport_readable,
                          handle_transport_read_shutdown,
                          connection,
                          NULL);
  if (gsk_io_is_polling_for_write (connection))
    gsk_io_trap_writable (transport,
                          handle_transport_writable,
                          handle_transport_write_shutdown,
                          connection,
                          NULL);
  connection->transport_on_error_signal_handler
    = g_signal_connect (transport,
                        "on-error",
                        G_CALLBACK (handle_transport_error),
                        connection);
}

static void gsk_persistent_connection_set_poll_read (GskIO    *io,
                                                     gboolean  should_poll)
{
  GskPersistentConnection *connection = GSK_PERSISTENT_CONNECTION (io);
  if (connection->transport)
    {
      if (should_poll)
        gsk_io_trap_readable (GSK_IO (connection->transport),
                              handle_transport_readable,
                              handle_transport_read_shutdown,
                              connection,
                              NULL);
      else
        gsk_io_untrap_readable (GSK_IO (connection->transport));
    }
}

static void gsk_persistent_connection_set_poll_write (GskIO    *io,
                                                      gboolean  should_poll)
{
  GskPersistentConnection *connection = GSK_PERSISTENT_CONNECTION (io);
  if (connection->transport)
    {
      if (should_poll)
        gsk_io_trap_writable (GSK_IO (connection->transport),
                              handle_transport_writable,
                              handle_transport_write_shutdown,
                              connection,
                              NULL);
      else
        gsk_io_untrap_writable (GSK_IO (connection->transport));
    }
}

static gboolean
handle_retry_timeout_expired (gpointer data)
{
  GskPersistentConnection *connection = GSK_PERSISTENT_CONNECTION (data);
  connection->retry_timeout_source = NULL;
  if (connection->address != NULL)
    retry_connection (connection, connection->address);
  else
    g_warning ("no address???");
  return FALSE;
}

GskStream *
gsk_persistent_connection_new (GskSocketAddress *address,
                               guint             retry_timeout_ms)
{
  GskPersistentConnection *connection = g_object_new (GSK_TYPE_PERSISTENT_CONNECTION, NULL);
  connection->address = g_object_ref (address);
  connection->retry_timeout_ms = retry_timeout_ms;
  retry_connection (connection, address);
  return GSK_STREAM (connection);
}

GskStream *
gsk_persistent_connection_new_lookup (const char *host,
                                      guint       port,
                                      guint       retry_timeout_ms)
{
  GskSocketAddress *symbolic = gsk_socket_address_symbolic_ipv4_new (host, port);
  GskStream *pc = gsk_persistent_connection_new (symbolic, retry_timeout_ms);
  g_object_unref (symbolic);
  return pc;
}

void gsk_persistent_connection_restart (GskPersistentConnection *connection,
                                        guint                    retry_wait_ms)
{
  if (connection->transport != NULL)
    {
      shutdown_transport (connection);
      g_signal_emit (connection, handle_disconnected_signal_id, 0);
    }
  if (connection->retry_timeout_source != NULL)
    {
      gsk_source_remove (connection->retry_timeout_source);
      connection->retry_timeout_source = NULL;
    }
  connection->retry_timeout_source
    = gsk_main_loop_add_timer (gsk_main_loop_default (),
                               handle_retry_timeout_expired,
                               connection,
                               NULL,
                               retry_wait_ms,
                               -1);
  connection->state = GSK_PERSISTENT_CONNECTION_WAITING;
}
