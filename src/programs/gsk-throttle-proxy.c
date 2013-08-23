/* a throttling transparent proxy. */
#include <string.h>
#include <stdlib.h>
#include "../gsk.h"
#include "../gsklistmacros.h"
#include "../http/gskhttpcontent.h"

typedef struct _GskThrottleProxyConnection GskThrottleProxyConnection;
typedef struct _Side Side;

/* configuration */
guint upload_per_second_base = 10*1024;
guint download_per_second_base = 100*1024;
guint upload_per_second_noise = 1*1024;
guint download_per_second_noise = 10*1024;

/* if TRUE, shut-down the read and write ends of the connection
   independently.  if FALSE, either propagation a read or a write
   shutdown into both.  */
gboolean half_shutdowns = TRUE;

GskSocketAddress *bind_addr = NULL;
GskSocketAddress *server_addr = NULL;
GskSocketAddress *bind_status_addr = NULL;

static guint n_connections_accepted = 0;
static guint64 n_bytes_read_total = 0;
static guint64 n_bytes_written_total = 0;

struct _Side
{
  GskThrottleProxyConnection *connection;

  GskStream *read_side;		/* client for upload, server for download */
  GskStream *write_side;	/* client for upload, server for download */
  gboolean read_side_blocked;
  gboolean write_side_blocked;

  /* sides are in this list if their xferred_in_last_second==max
     but buffer.size < max_buffer */
  Side *next_throttled, *prev_throttled;
  gboolean throttled;

  guint max_xfer_per_second;
  gulong last_xfer_second;
  guint xferred_in_last_second;

  GskBuffer buffer;

  guint max_buffer;/* should be set to max_xfer_per_second or a bit more */

  guint total_read, total_written;
};

struct _GskThrottleProxyConnection
{
  Side upload;
  Side download;

  guint ref_count;

  GskThrottleProxyConnection *prev, *next;
};

static GskThrottleProxyConnection *first_conn, *last_conn;
#define GET_CONNECTION_LIST() \
  GskThrottleProxyConnection *, first_conn, last_conn, prev, next

static Side *first_throttled, *last_throttled;
#define GET_THROTTLED_LIST() \
  Side *, first_throttled, last_throttled, prev_throttled, next_throttled

#define CURRENT_SECOND() (gsk_main_loop_default ()->current_time.tv_sec)

/* must be called whenever side->buffer changes "emptiness" */
static inline void
update_write_block (Side   *side)
{
  gboolean old_val = side->write_side_blocked;
  gboolean val = (side->read_side != NULL && side->buffer.size == 0);
  side->write_side_blocked = val;

  if (old_val && !val)
    gsk_io_unblock_write (side->write_side);
  else if (!old_val && val)
    gsk_io_block_write (side->write_side);
}

/* must be called whenever side->buffer changes "emptiness" */
static inline void
update_read_block (Side   *side)
{
  gboolean was_throttled = side->throttled;
  gboolean old_val = side->read_side_blocked;
  gboolean xfer_blocked = side->xferred_in_last_second >= side->max_xfer_per_second;
  gboolean buf_blocked = side->buffer.size >= side->max_buffer;
  gboolean val = xfer_blocked || buf_blocked;

  side->throttled = xfer_blocked && !buf_blocked;
  side->read_side_blocked = val;

  if (side->throttled && !was_throttled)
    {
      /* put in throttled list */
      GSK_LIST_APPEND (GET_THROTTLED_LIST (), side);
    }
  else if (!side->throttled && was_throttled)
    {
      /* remove from throttled list */
      GSK_LIST_REMOVE (GET_THROTTLED_LIST (), side);
    }

  if (old_val && !val)
    gsk_io_unblock_read (side->read_side);
  else if (!old_val && val)
    gsk_io_block_read (side->read_side);
}

static void
connection_unref (GskThrottleProxyConnection *conn)
{
  if (--(conn->ref_count) == 0)
    {
      GSK_LIST_REMOVE (GET_CONNECTION_LIST (), conn);
      gsk_buffer_destruct (&conn->upload.buffer);
      gsk_buffer_destruct (&conn->download.buffer);
      g_free (conn);
    }
}

static gboolean
handle_side_writable (GskStream *stream,
                      gpointer data)
{
  Side *side = data;
  GError *error = NULL;
  guint written = gsk_stream_write_buffer (stream, &side->buffer, &error);
  if (error)
    {
      g_warning ("error writing to stream %p: %s",
                 stream, error->message);
      g_error_free (error);
    }
  n_bytes_written_total += written;
  side->total_written += written;
  update_write_block (side);
  update_read_block (side);
  if (written == 0 && side->read_side == NULL && side->buffer.size == 0)
    {
      update_write_block (side);
      if (half_shutdowns)
        gsk_io_write_shutdown (side->write_side, NULL);
      else
        gsk_io_shutdown (GSK_IO (side->write_side), NULL);
    }
  return TRUE;
}

static gboolean
handle_side_write_shutdown (GskStream *stream,
                            gpointer   data)
{
  Side *side = data;
  if (side->buffer.size > 0)
    g_warning ("write-side shut down while data still pending");
  if (side->read_side)
    {
      if (half_shutdowns)
        gsk_io_read_shutdown (side->read_side, NULL);
      else
        gsk_io_shutdown (GSK_IO (side->read_side), NULL);
    }
  return FALSE;
}

static void
handle_side_write_destroy (gpointer data)
{
  Side *side = data;
  g_object_unref (side->write_side);
  side->write_side = NULL;
  connection_unref (side->connection);
}

static gboolean
handle_side_readable (GskStream *stream,
                      gpointer data)
{
  Side *side = data;
  gulong cur_sec = CURRENT_SECOND ();
  GError *error = NULL;
  guint max_read;
  guint nread;
  char *tmp;
  if (cur_sec == side->last_xfer_second)
    {
      max_read = side->max_xfer_per_second - side->xferred_in_last_second;
    }
  else
    {
      side->xferred_in_last_second = 0;
      side->last_xfer_second = cur_sec;
      max_read = side->max_xfer_per_second;
    }
  if (max_read + side->buffer.size > side->max_buffer)
    {
      if (side->buffer.size > side->max_buffer)
        max_read = 0;
      else 
        max_read = side->max_buffer - side->buffer.size;
    }

  tmp = g_malloc (max_read);
  nread = gsk_stream_read (stream, tmp, max_read, &error);
  if (error != NULL)
    {
      g_warning ("error reading from stream %p: %s",
		 stream, error->message);
      g_error_free (error);
    }
  /* TODO: use append_foreign if nread is big */
  gsk_buffer_append (&side->buffer, tmp, nread);

  g_free (tmp);
  n_bytes_read_total += nread;
  side->total_read += nread;

  side->xferred_in_last_second += nread;
  g_assert (side->xferred_in_last_second <= side->max_xfer_per_second);
  update_write_block (side);
  update_read_block (side);
  return TRUE;
}

static gboolean
handle_side_read_shutdown (GskStream *stream,
                           gpointer data)
{
  return FALSE;
}

static void
handle_side_read_destroy (gpointer data)
{
  Side *side = data;
  g_object_unref (side->read_side);
  side->read_side = NULL;
  if (side->buffer.size == 0 && side->write_side != NULL)
    {
      update_write_block (side);
      if (half_shutdowns)
        gsk_io_write_shutdown (side->write_side, NULL);
      else
        gsk_io_shutdown (GSK_IO (side->write_side), NULL);
    }
  connection_unref (side->connection);
}

static void
side_init (Side      *side,
           GskThrottleProxyConnection *conn,
           GskStream *read_side,
           GskStream *write_side,
           guint      max_xfer_per_second)
{
  side->connection = conn;
  side->read_side = read_side;
  side->write_side = write_side;
  side->read_side_blocked = FALSE;
  side->write_side_blocked = FALSE;
  side->throttled = FALSE;
  side->next_throttled = side->prev_throttled = NULL;
  side->max_xfer_per_second = max_xfer_per_second;
  side->last_xfer_second = gsk_main_loop_default ()->current_time.tv_sec;
  side->xferred_in_last_second = 0;
  gsk_buffer_construct (&side->buffer);
  side->max_buffer = max_xfer_per_second;
  side->total_read = 0;
  side->total_written = 0;

  conn->ref_count += 2;

  g_object_ref (read_side);
  gsk_io_trap_readable (read_side,
                        handle_side_readable,
                        handle_side_read_shutdown,
                        side,
                        handle_side_read_destroy);

  g_object_ref (write_side);
  gsk_io_trap_writable (write_side,
                        handle_side_writable,
                        handle_side_write_shutdown,
                        side,
                        handle_side_write_destroy);
}


/* --- handle a new stream --- */
static guint
pick_rand (guint base, guint noise)
{
  return base + (noise ? g_random_int_range (0, noise) : 0);
}

static gboolean
handle_accept  (GskStream    *stream,
                gpointer      data,
                GError      **error)
{
  GskThrottleProxyConnection *conn = g_new (GskThrottleProxyConnection, 1);
  GError *e = NULL;
  GskStream *server = gsk_stream_new_connecting (server_addr, &e);
  if (e)
    g_error ("gsk_stream_new_connecting failed: %s", e->message);
  n_connections_accepted++;
  conn->ref_count = 1;
  GSK_LIST_APPEND (GET_CONNECTION_LIST (), conn);
  side_init (&conn->upload, conn, stream, server,
             pick_rand (upload_per_second_base, upload_per_second_noise));
  side_init (&conn->download, conn, server, stream,
             pick_rand (download_per_second_base, download_per_second_noise));
  connection_unref (conn);
  g_object_unref (stream);
  g_object_unref (server);
  return TRUE;
}

static void
handle_listener_error  (GError *error,
                        gpointer data)
{
  g_error ("handle_listener_error: %s", error->message);
}

/* --- unblock throttled streams every second --- */
static gboolean
unblock_timer_func (gpointer data)
{
  Side *at = first_throttled;
  gulong sec = CURRENT_SECOND ();
  while (at)
    {
      Side *next = at->next_throttled;
      g_assert (at->throttled);
      g_assert (at->read_side_blocked);
      if (sec > at->last_xfer_second)
        {
	  at->last_xfer_second = sec;
	  at->xferred_in_last_second = 0;
          update_read_block (at);
        }
      at = next;
    }

  /* schedule next timeout */
  gsk_main_loop_add_timer_absolute (gsk_main_loop_default (),
                                    unblock_timer_func, NULL, NULL,
                                    sec + 1, 0);

  return FALSE;
}

static void
usage (void)
{
  g_printerr ("usage: %s --bind=LISTEN_ADDR --server=CONNECT_ADDR OPTIONS\n\n",
              g_get_prgname ());
  g_printerr ("Bind to LISTEN_ADDR; whenever we receive a connection,\n"
              "proxy to CONNECT_ADDR, obeying thottling constraints.\n"
              "\n"
              "Options:\n"
              "  --bind-status=STATUS_ADDR  Report status on this addr.\n"
              "  --upload-rate=BPS          ...\n"
              "  --download-rate=BPS        ...\n"
              "  --upload-rate-noise=BPS    ...\n"
              "  --download-rate-noise=BPS  ...\n"
              "  --full-shutdowns\n"
              "  --half-shutdowns\n"
             );
  exit (1);
}

static void
dump_side_to_buffer (Side *side, GskBuffer *out)
{
  gsk_buffer_printf (out, "<td>%sreadable%s, %swritable%s, %u buffered [total read/written=%u/%u]</td>\n",
                     side->read_side ? "" : "NOT ",
                     side->throttled ? " [throttled]" :
                          side->read_side_blocked ? " [blocked]" : "",
                     side->write_side ? "" : "NOT ",
                     side->write_side_blocked ? " [blocked]" : "",
                     side->buffer.size,
                     side->total_read, side->total_written);
}

static GskHttpContentResult
create_status_page (GskHttpContent   *content,
                    GskHttpContentHandler *handler,
                    GskHttpServer  *server,
                    GskHttpRequest *request,
                    GskStream      *post_data,
                    gpointer        data)
{
  GskThrottleProxyConnection *conn;
  GskBuffer buffer = GSK_BUFFER_STATIC_INIT;
  GskHttpResponse *response;
  GskStream *stream;
  gsk_buffer_printf (&buffer, "<html><head>\n");
  gsk_buffer_printf (&buffer, "<title>GskThrottleProxy Status Page</title>\n");
  gsk_buffer_printf (&buffer, "</head>\n");
  gsk_buffer_printf (&buffer, "<body>\n");
  gsk_buffer_printf (&buffer, "<h1>Statistics</h1>\n");
  gsk_buffer_printf (&buffer, "<br>%u connections accepted.\n",
                     n_connections_accepted);
  gsk_buffer_printf (&buffer, "<br>%"G_GUINT64_FORMAT" bytes read.\n",
                     n_bytes_read_total);
  gsk_buffer_printf (&buffer, "<br>%"G_GUINT64_FORMAT" bytes written.\n",
                     n_bytes_written_total);
  gsk_buffer_printf (&buffer, "<h1>Connections</h1>\n");
  gsk_buffer_printf (&buffer, "<table>\n"
                              " <tr><th>Connection Pointer</th>"
                                   "<th>RefCount</th>"
                                   "<th>Upload</th>"
                                   "<th>Download</th>"
                               "</tr>\n");
  for (conn = first_conn; conn; conn = conn->next)
    {
      gsk_buffer_printf (&buffer, 
                        " <tr><td>%p</td><td>%u</td>", conn, conn->ref_count);
      dump_side_to_buffer (&conn->upload, &buffer);
      dump_side_to_buffer (&conn->download, &buffer);
      gsk_buffer_printf (&buffer, "</tr>\n");
    }
  gsk_buffer_printf (&buffer, "</table>\n</body>\n</html>\n");
  response = gsk_http_response_from_request (request, 200, buffer.size);
  gsk_http_header_set_content_type (response, "text");
  gsk_http_header_set_content_subtype (response, "html");
  stream = gsk_memory_buffer_source_new (&buffer);
  gsk_http_server_respond (server, request, response, stream);
  g_object_unref (response);
  g_object_unref (stream);

  return GSK_HTTP_CONTENT_OK;
}
                                   
/* --- main --- */
int main(int argc, char **argv)
{
  guint i;
  GskStreamListener *listener;
  GError *error = NULL;
  gsk_init_without_threads (&argc, &argv);
  for (i = 1; i < (guint) argc; i++)
    {
      if (g_str_has_prefix (argv[i], "--bind="))
        {
          const char *bind_str = strchr (argv[i], '=') + 1;
          if (bind_addr != NULL)
            g_error ("--bind may only be given once");
          if (g_ascii_isdigit (bind_str[0]))
            {
              bind_addr = gsk_socket_address_ipv4_new (gsk_ipv4_ip_address_any,
                                                       atoi (bind_str));
            }
          else
            {
              bind_addr = gsk_socket_address_local_new (bind_str);
            }
        }
      else if (g_str_has_prefix (argv[i], "--bind-status="))
        {
          const char *bind_str = strchr (argv[i], '=') + 1;
          if (bind_status_addr != NULL)
            g_error ("--bind-status may only be given once");
          if (g_ascii_isdigit (bind_str[0]))
            {
              bind_status_addr = gsk_socket_address_ipv4_new (gsk_ipv4_ip_address_any,
                                                       atoi (bind_str));
            }
          else
            {
              bind_status_addr = gsk_socket_address_local_new (bind_str);
            }
        }
      else if (g_str_has_prefix (argv[i], "--server="))
        {
          const char *server_str = strchr (argv[i], '=') + 1;
          const char *colon = strchr (server_str, ':');
          if (server_addr != NULL)
            g_error ("--server may only be given once");
          if (colon != NULL && strchr (server_str, '/') == NULL)
            {
              /* host:port */
              char *host = g_strndup (server_str, colon - server_str);
              guint port = atoi (colon + 1);
              server_addr = gsk_socket_address_symbolic_ipv4_new (host, port);
              g_free (host);
            }
          else
            {
              /* unix */
              server_addr = gsk_socket_address_local_new (server_str);
            }
        }
      else if (g_str_has_prefix (argv[i], "--upload-rate="))
        upload_per_second_base = atoi (strchr (argv[i], '=') + 1);
      else if (g_str_has_prefix (argv[i], "--download-rate="))
        download_per_second_base = atoi (strchr (argv[i], '=') + 1);
      else if (g_str_has_prefix (argv[i], "--upload-rate-noise="))
        upload_per_second_noise = atoi (strchr (argv[i], '=') + 1);
      else if (g_str_has_prefix (argv[i], "--download-rate-noise="))
        download_per_second_noise = atoi (strchr (argv[i], '=') + 1);
      else if (strcmp (argv[i], "--half-shutdowns") == 0)
        half_shutdowns = TRUE;
      else if (strcmp (argv[i], "--full-shutdowns") == 0)
        half_shutdowns = FALSE;
      else
        usage ();
    }

  if (server_addr == NULL)
    g_error ("missing --server=ADDRESS: try --help");
  if (bind_addr == NULL)
    g_error ("missing --bind=ADDRESS: try --help");

  listener = gsk_stream_listener_socket_new_bind (bind_addr, &error);
  if (listener == NULL)
    g_error ("bind failed: %s", error->message);
  gsk_stream_listener_handle_accept (listener,
                                     handle_accept,
                                     handle_listener_error,
                                     NULL, NULL);

  if (bind_status_addr != NULL)
    {
      GskHttpContentHandler *handler;
      GskHttpContent *content = gsk_http_content_new ();
      GskHttpContentId id = GSK_HTTP_CONTENT_ID_INIT;
      handler = gsk_http_content_handler_new (create_status_page, NULL, NULL);
      id.path = "/";
      gsk_http_content_add_handler (content, &id, handler, GSK_HTTP_CONTENT_REPLACE);
      gsk_http_content_handler_unref (handler);
      if (!gsk_http_content_listen (content, bind_status_addr, &error))
        g_error ("error listening: %s", error->message);
    }

  gsk_main_loop_add_timer_absolute (gsk_main_loop_default (),
                                    unblock_timer_func, NULL, NULL,
                                    gsk_main_loop_default ()->current_time.tv_sec + 1, 0);

  return gsk_main_run ();
}
