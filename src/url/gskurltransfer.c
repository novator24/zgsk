#include <string.h>
#include "gskurltransfer.h"
#include "../gskmemory.h"

/**
 * gsk_url_transfer_result_name:
 *
 * @result: the enumeration value.
 *
 * Convert a GskUrlTransferResult value
 * into a human-readable string.
 *
 * returns: the constant string.
 */
const char *
gsk_url_transfer_result_name (GskUrlTransferResult result)
{
  switch (result)
    {
    case GSK_URL_TRANSFER_ERROR_BAD_REQUEST:
      return "Error: Bad Request";
    case GSK_URL_TRANSFER_ERROR_BAD_NAME:
      return "Error: Bad Name";
    case GSK_URL_TRANSFER_ERROR_NO_SERVER:
      return "Error: No Server";
    case GSK_URL_TRANSFER_ERROR_NOT_FOUND:
      return "Error: Not Found";
    case GSK_URL_TRANSFER_ERROR_SERVER_ERROR:
      return "Error: Server Error";
    case GSK_URL_TRANSFER_ERROR_UNSUPPORTED:
      return "Error: Unsupported";
    case GSK_URL_TRANSFER_ERROR_TIMED_OUT:
      return "Error: Timed Out";
    case GSK_URL_TRANSFER_ERROR_REDIRECT_LOOP:
      return "Error: Redirect Loop";
    case GSK_URL_TRANSFER_REDIRECT:
      return "Redirect";
    case GSK_URL_TRANSFER_CANCELLED:
      return "Cancelled";
    case GSK_URL_TRANSFER_SUCCESS:
      return "Success";
    default:
      g_warning ("requested name of invalid transfer result %u", result);
      g_return_val_if_reached (NULL);
    }
}

G_DEFINE_TYPE(GskUrlTransfer, gsk_url_transfer, G_TYPE_OBJECT);

typedef enum
{
  GSK_URL_TRANSFER_STATE_CONSTRUCTING,
  GSK_URL_TRANSFER_STATE_STARTED,
  GSK_URL_TRANSFER_STATE_DONE,
  GSK_URL_TRANSFER_STATE_ERROR
} GskUrlTransferState;

static inline void
gsk_url_transfer_redirect_free_1 (GskUrlTransferRedirect *redirect)
{
  g_object_unref (redirect->url);
  if (redirect->request)
    g_object_unref (redirect->request);
  if (redirect->response)
    g_object_unref (redirect->response);
  g_free (redirect);
}

static void
gsk_url_transfer_finalize (GObject *object)
{
  GskUrlTransfer *transfer = GSK_URL_TRANSFER (object);
  GskUrlTransferRedirect *redir_at;
  g_assert (transfer->transfer_state != GSK_URL_TRANSFER_STATE_STARTED);

  if (transfer->url)
    g_object_unref (transfer->url);
  redir_at = transfer->first_redirect;
  while (redir_at)
    {
      GskUrlTransferRedirect *next = redir_at->next;
      gsk_url_transfer_redirect_free_1 (redir_at);
      redir_at = next;
    }
  if (transfer->address)
    g_object_unref (transfer->address);
  if (transfer->address_hint)
    g_object_unref (transfer->address_hint);

  /* may be available: protocol-specific headers */
  if (transfer->request)
    g_object_unref (transfer->request);
  if (transfer->response)
    g_object_unref (transfer->response);

  if (transfer->content)
    g_object_unref (transfer->content);
  if (transfer->upload_destroy != NULL)
    (*transfer->upload_destroy) (transfer->upload_data);

  g_clear_error (&transfer->error);

  G_OBJECT_CLASS (gsk_url_transfer_parent_class)->finalize (object);
}

static void
gsk_url_transfer_real_timed_out (GskUrlTransfer *transfer)
{
  gsk_url_transfer_take_error (transfer,
                               g_error_new (GSK_G_ERROR_DOMAIN,
                                            GSK_ERROR_OPERATION_TIMED_OUT,
                                            "Transfer with URL timed out"));
  if (transfer->transfer_state != GSK_URL_TRANSFER_STATE_DONE)
    gsk_url_transfer_notify_done (transfer, GSK_URL_TRANSFER_ERROR_TIMED_OUT);
}

static char    *
gsk_url_transfer_real_get_constructing_state (GskUrlTransfer *transfer)
{
  if (transfer->url)
    {
      char *url_str = gsk_url_to_string (transfer->url);
      char *rv = g_strdup_printf ("NOT STARTED: %s", url_str);
      g_free (url_str);
      return rv;
    }
  return g_strdup ("NOT STARTED: (no url)");
}

static char    *
gsk_url_transfer_real_get_running_state (GskUrlTransfer *transfer)
{
  if (transfer->url)
    {
      char *url_str = gsk_url_to_string (transfer->url);
      char *rv = g_strdup_printf ("RUNNING: %s", url_str);
      g_free (url_str);
      return rv;
    }
  return g_strdup ("RUNNING: (no url!?!)");
}

static char    *
gsk_url_transfer_real_get_done_state (GskUrlTransfer *transfer)
{
  if (transfer->url)
    {
      char *url_str = gsk_url_to_string (transfer->url);
      char *rv = g_strdup_printf ("DONE: %s: %s", url_str,
                          gsk_url_transfer_result_name (transfer->result));
      g_free (url_str);
      return rv;
    }
  return g_strdup_printf ("DONE: [no url]: %s",
                          gsk_url_transfer_result_name (transfer->result));
}

static void
gsk_url_transfer_init (GskUrlTransfer *transfer)
{
  transfer->transfer_state = GSK_URL_TRANSFER_STATE_CONSTRUCTING;
  transfer->follow_redirects = 1;
}

static void
gsk_url_transfer_class_init (GskUrlTransferClass *transfer_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (transfer_class);
  object_class->finalize = gsk_url_transfer_finalize;
  transfer_class->timed_out = gsk_url_transfer_real_timed_out;
  transfer_class->get_constructing_state = gsk_url_transfer_real_get_constructing_state;
  transfer_class->get_running_state = gsk_url_transfer_real_get_running_state;
  transfer_class->get_done_state = gsk_url_transfer_real_get_done_state;
}


/**
 * gsk_url_transfer:
 * @url: the URL to which to upload or from which to download data.
 * @upload_func: optional function that can create the upload's content
 * as a #GskStream.
 * @upload_data: data which can be used by the upload function.
 * @upload_destroy: optional function that will be notified when upload()
 * will no longer be called (note that the streams it created may
 * still be extant though).
 * @handler: function to be called with the transfer request is
 * done.  (The transfer content itself is just provided as a stream
 * though-- only after reading the stream is the transfer truly done)
 * This function may also be called in a number of error cases.
 * @data: data to pass to the handler function.
 * @destroy: function to call when you are done with data.
 * @error: place to put the error if anything goes wrong.
 *
 * Begin a upload and/or download with a URL.
 * There is no way to cancel this transfer.
 *
 * If you wish to perform an upload,
 * provide a function that can create the stream of content to
 * upload on demand.  Note that the upload_destroy() method
 * is called only once the transfer is done and all the upload streams
 * are finalized.  Therefore, you can assume that the upload_data
 * will be available for all your upload-streams.
 * 
 * The handler/data/destroy triple is used for result notification.
 * handler() is always invoked exactly once.  To find out how things
 * went, the handler() should almost always start by
 * examining transfer->result.
 *
 * returns: whether the transfer began.
 * Unsupported URL schemes and malformed URLs are the
 * most common ways for this function to fail.
 */
gboolean
gsk_url_transfer            (GskUrl            *url,
                             GskUrlUploadFunc   upload_func,
                             gpointer           upload_data,
                             GDestroyNotify     upload_destroy,
                             GskUrlTransferFunc handler,
                             gpointer           data,
                             GDestroyNotify     destroy,
                             GError           **error)
{
  GskUrlTransfer *transfer = gsk_url_transfer_new (url);
  if (transfer == NULL)
    {
      g_set_error (error,
                   GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_INVALID_ARGUMENT,
                   "could not create Transfer object for url of scheme %s", url->scheme_name);
      return FALSE;
    }
  gsk_url_transfer_set_handler (transfer, handler, data, destroy);
  if (upload_func != NULL)
    gsk_url_transfer_set_upload (transfer, upload_func, upload_data, upload_destroy);
  if (!gsk_url_transfer_start (transfer, error))
    return FALSE;
  g_object_unref (transfer);
  return TRUE;
}

static gboolean
handle_timeout (gpointer data)
{
  GskUrlTransfer *transfer = GSK_URL_TRANSFER (data);
  GskUrlTransferClass *class = GSK_URL_TRANSFER_GET_CLASS (transfer);
  g_return_val_if_fail (transfer->transfer_state == GSK_URL_TRANSFER_STATE_STARTED, FALSE);
  transfer->timeout_source = NULL;
  transfer->timed_out = TRUE;

  /* hold a reference to transfer temporarily to ensure
     that the transfer doesn't die in the middle */
  g_object_ref (transfer);
  class->timed_out (transfer);
  g_object_unref (transfer); /* transfer may now be dead */
  return FALSE;
}

/**
 * gsk_url_transfer_start:
 * @transfer: the Transfer to affect.
 * @error: place to put the error if anything goes wrong.
 *
 * Begin the upload and/or download.  (Maybe start with name-lookup).
 *
 * returns: whether the transfer started successfully.
 * If it returns TRUE, you are guaranteed to receive your
 * done-notification.  If is returns FALSE, you will definitely not
 * receive done-notification.
 */
gboolean
gsk_url_transfer_start      (GskUrlTransfer     *transfer,
                             GError            **error)
{
  GskUrlTransferClass *class = GSK_URL_TRANSFER_GET_CLASS (transfer);
  g_assert (class->start != NULL);
  g_return_val_if_fail (transfer->transfer_state == GSK_URL_TRANSFER_STATE_CONSTRUCTING, FALSE);
  g_object_ref (transfer);
  transfer->transfer_state = GSK_URL_TRANSFER_STATE_STARTED;
  if (!class->start (transfer, error))
    {
      transfer->transfer_state = GSK_URL_TRANSFER_STATE_ERROR;
      g_object_unref (transfer);
      return FALSE;
    }
  if (transfer->has_timeout
   && transfer->transfer_state == GSK_URL_TRANSFER_STATE_STARTED)
    {
      transfer->timeout_source = gsk_main_loop_add_timer (gsk_main_loop_default (),
                                                          handle_timeout,
                                                          transfer,
                                                          NULL,
                                                          transfer->timeout_ms,
                                                          -1);
    }
  return TRUE;
}

/**
 * gsk_url_transfer_set_handler:
 * @transfer: the Transfer to affect.
 * @handler: function to be called with the transfer request is
 * done.  (The transfer content itself is just provided as a stream
 * though-- only after reading the stream is the transfer truly done)
 * This function may also be called in a number of error cases.
 * @data: data to pass to the handler function.
 * @destroy: function to call when you are done with data.
 *
 * The handler/data/destroy triple is used for result notification.
 * handler() is always invoked exactly once.  To find out how things
 * went, the handler() should almost always start by
 * examining transfer->result.
 */
void
gsk_url_transfer_set_handler(GskUrlTransfer     *transfer,
                             GskUrlTransferFunc  handler,
                             gpointer            data,
                             GDestroyNotify      destroy)
{
  g_return_if_fail (transfer->transfer_state == GSK_URL_TRANSFER_STATE_CONSTRUCTING);
  g_return_if_fail (transfer->handler == NULL);
  transfer->handler = handler;
  transfer->handler_data = data;
  transfer->handler_data_destroy = destroy;
}

/**
 * gsk_url_transfer_set_url:
 * @transfer: the Transfer to affect.
 * @url: the URL to which to upload or from which to download data.
 *
 * Set the URL that is the target of this transfer.
 * This can only be done once, before the transfer
 * is started.
 *
 * You seldom need to use this function, as it
 * is called by gsk_url_transfer_new().
 */
void
gsk_url_transfer_set_url    (GskUrlTransfer     *transfer,
                             GskUrl             *url)
{
  g_return_if_fail (transfer->transfer_state == GSK_URL_TRANSFER_STATE_CONSTRUCTING);
  g_return_if_fail (transfer->url == NULL);
  g_return_if_fail (GSK_IS_URL (url));
  transfer->url = g_object_ref (url);
}

/**
 * gsk_url_transfer_set_timeout:
 * @transfer: the Transfer to affect.
 * @millis: milliseconds to wait before aborting the transfer.
 *
 * Set the timeout on the download.
 *
 * This can be used to avoid hanging on slow servers.
 *
 * This must be called before the transfer is started
 * (with gsk_url_transfer_start).
 */
void
gsk_url_transfer_set_timeout(GskUrlTransfer     *transfer,
                             guint               millis)
{
  g_return_if_fail (transfer->transfer_state == GSK_URL_TRANSFER_STATE_CONSTRUCTING);
  transfer->has_timeout = 1;
  transfer->timeout_ms = millis;
}

/**
 * gsk_url_transfer_clear_timeout:
 * @transfer: the Transfer to affect.
 *
 * Clear the timeout on the download.
 *
 * This must be called before the transfer is started
 * (with gsk_url_transfer_start).
 */
void
gsk_url_transfer_clear_timeout(GskUrlTransfer     *transfer)
{
  g_return_if_fail (transfer->transfer_state == GSK_URL_TRANSFER_STATE_CONSTRUCTING);
  transfer->has_timeout = 0;
}

/**
 * gsk_url_transfer_set_follow_redirects:
 * @transfer: the Transfer to affect.
 * @follow_redirs: whether to follow redirect responses.
 *
 * Configure how the transfer will behave when it encounters
 * redirection responses.
 *
 * The default behavior is to follow redirects,
 * adding them to the list of redirects, but not notifying the
 * user until we reach a real page (or error).
 *
 * If follow_redirects is FALSE, then we are done
 * even if the download led to a redirect.
 */
void
gsk_url_transfer_set_follow_redirects(GskUrlTransfer     *transfer,
                                      gboolean            follow_redirs)
{
  g_return_if_fail (transfer->transfer_state == GSK_URL_TRANSFER_STATE_CONSTRUCTING);
  transfer->follow_redirects = follow_redirs ? 1 : 0;
}


/**
 * gsk_url_transfer_set_address_hint:
 * @transfer: the Transfer to affect.
 * @address: the socket-address to use for connecting,
 * possibly with the wrong port (the port will be overridden by
 * the URL's port).
 *
 * To avoid DNS lookups in very bulky transfer situations,
 * DNS may be bypassed and replaced with this address.
 *
 * Chances are, you want to suppress redirects to:
 * otherwise, DNS may be used on the redirected URLs.
 */
void
gsk_url_transfer_set_address_hint (GskUrlTransfer *transfer,
                                   GskSocketAddress *address)
{
  g_return_if_fail (transfer->transfer_state == GSK_URL_TRANSFER_STATE_CONSTRUCTING);
  g_return_if_fail (transfer->address_hint == NULL);
  transfer->address_hint = g_object_ref (address);
}


/**
 * gsk_url_transfer_set_upload:
 * @transfer: the Transfer to affect.
 * @func: function that can create the upload's content
 * as a #GskStream.
 * @data: data which can be used by the upload function.
 * @destroy: optional function that will be notified when upload()
 * will no longer be called (note that the streams it created may
 * still be extant though).
 *
 * Set the upload stream as generally as possible.
 * Actually you must provide a function
 * that can make an upload stream on demand--
 * this is necessary to get redirects right.
 *
 * The destroy() function will be called after no more upload-streams
 * need to be created-- it is quite possible that not all upload-streams
 * have been finalized by the time the destroy() is invoked.
 *
 * If you don't care about redirects, you can
 * use gsk_url_transfer_set_oneshot_upload().
 *
 * If you have a slab of memory that you want to use as the upload stream,
 * consider using gsk_url_transfer_set_upload_packet().
 */
void
gsk_url_transfer_set_upload (GskUrlTransfer     *transfer,
                             GskUrlUploadFunc    func,
                             gpointer            data,
                             GDestroyNotify      destroy)
{
  g_return_if_fail (transfer->transfer_state == GSK_URL_TRANSFER_STATE_CONSTRUCTING);
  g_return_if_fail (transfer->upload_func == NULL);
  g_return_if_fail (func != NULL);
  transfer->upload_func = func;
  transfer->upload_data = data;
  transfer->upload_destroy = destroy;
}

/**
 * gsk_url_transfer_set_upload_packet:
 * @transfer: the Transfer to affect.
 * @packet: the GskPacket containing the upload content as data.
 *
 * Set the upload stream in an easy, reliable way using a GskPacket.
 */
static GskStream *
make_packet_into_stream (gpointer data,
                         gssize  *size_out,
                         GError **error)
{
  GskPacket *packet = data;
  GskStream *rv = gsk_memory_slab_source_new (packet->data, packet->len,
                                              (GDestroyNotify) gsk_packet_unref,
                                              gsk_packet_ref (packet));
  *size_out = packet->len;
  return rv;
}

void
gsk_url_transfer_set_upload_packet (GskUrlTransfer *transfer,
                                    GskPacket      *packet)
{
  gsk_url_transfer_set_upload (transfer,
                               make_packet_into_stream,
                               gsk_packet_ref (packet),
                               (GDestroyNotify) gsk_packet_unref);
}

/**
 * gsk_url_transfer_set_oneshot_upload:
 * @transfer: the Transfer to affect.
 * @size: the length of the stream in bytes, or -1 if you don't know.
 * @stream: the upload content stream.
 *
 * Set the content to upload to the remote URL,
 * as a #GskStream.
 *
 * Since streams can only be read once,
 * this method only works on URLs that do not require
 * redirection.
 */
typedef struct
{
  GskStream *stream;
  gssize size;
} ReturnStreamOnce;

static GskStream *
return_stream_once      (gpointer data,
                         gssize  *size_out,
                         GError **error)
{
  ReturnStreamOnce *once = data;
  GskStream *rv;
  if (once->stream == NULL)
    {
      g_set_error (error,
                   GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_TOO_MANY_LINKS,
                   "one-shot upload transfer was redirected: cannot re-upload data");
      return NULL;
    }
  rv = once->stream;
  once->stream = NULL;
  *size_out = once->size;
  return rv;
}
static void
destroy_return_stream_once (gpointer data)
{
  ReturnStreamOnce *once = data;
  if (once->stream)
    g_object_unref (once->stream);
  g_free (once);
}

void
gsk_url_transfer_set_oneshot_upload (GskUrlTransfer *transfer,
                                     GskStream      *stream,
                                     gssize          size)
{
  ReturnStreamOnce *once;
  g_return_if_fail (GSK_IS_STREAM (stream));
  once = g_new (ReturnStreamOnce, 1);
  once->stream = g_object_ref (stream);
  once->size = size;
  gsk_url_transfer_set_upload (transfer,
                               return_stream_once,
                               once,
                               destroy_return_stream_once);
}

/**
 * gsk_url_transfer_cancel:
 * @transfer: the Transfer to affect.
 *
 * Abort a running transfer.
 *
 * If you registered a handler, it will be called with 
 * result GSK_URL_TRANSFER_CANCELLED.
 */
void
gsk_url_transfer_cancel     (GskUrlTransfer     *transfer)
{
  GskUrlTransferClass *class = GSK_URL_TRANSFER_GET_CLASS (transfer);
  g_return_if_fail (transfer->transfer_state == GSK_URL_TRANSFER_STATE_STARTED);
  if (class->cancel == NULL)
    {
      g_warning ("%s does not implement cancel()!", G_OBJECT_CLASS_NAME (class));
      return;
    }
  class->cancel (transfer);
}

/* --- Protected API --- */
/**
 * gsk_url_transfer_has_upload:
 * @transfer: the Transfer to query.
 *
 * Figure out whether this transfer has upload data.
 *
 * This function should only be needed by implementors
 * of types of GskUrlTransfer.
 *
 * returns: whether the transfer has upload data.
 */
gboolean
gsk_url_transfer_has_upload      (GskUrlTransfer     *transfer)
{
  return transfer->upload_func != NULL;
}

/**
 * gsk_url_transfer_create_upload:
 * @transfer: the Transfer to use.
 * @size_out: the size of the stream in bytes, or -1 if the size is unknown.
 * @error: optional location to store the #GError if there is a problem.
 *
 * Create a upload stream for this transfer based on the user's creator
 * function.
 *
 * This function should only be needed by implementors
 * of types of GskUrlTransfer.
 *
 * returns: a newly allocated #GskStream, or NULL if an error occurs.
 */
GskStream *
gsk_url_transfer_create_upload   (GskUrlTransfer     *transfer,
                                  gssize             *size_out,
                                  GError            **error)
{
  g_return_val_if_fail (transfer->upload_func != NULL, NULL);
  *size_out = -1;
  return transfer->upload_func (transfer->upload_data, size_out, error);
}

/**
 * gsk_url_transfer_peek_expects_download_stream:
 * @transfer: the Transfer to use.
 * returns: whether this transfer has a download handler.
 *
 * This function can be used to see if download-content is expected.
 *
 * This function should only be needed by implementors
 * of types of GskUrlTransfer.
 */
gboolean
gsk_url_transfer_peek_expects_download_stream (GskUrlTransfer *transfer)
{
  g_return_val_if_fail (transfer->transfer_state == GSK_URL_TRANSFER_STATE_STARTED, FALSE);
  return transfer->handler != NULL;
}

/**
 * gsk_url_transfer_set_address:
 * @transfer: the Transfer to affect.
 * @addr: the address of the host whose lookup was completed.
 *
 * Set the socket-address for informational purposes.
 * This is occasionally interesting to the user of the Transfer.
 *
 * This function should only be needed by implementors
 * of types of GskUrlTransfer.
 */
void
gsk_url_transfer_set_address     (GskUrlTransfer     *transfer,
                                  GskSocketAddress   *addr)
{
  g_object_ref (addr);
  if (transfer->address)
    g_object_unref (transfer->address);
  transfer->address = addr;
}

static inline gboolean
strings_equal (const char *a, const char *b)
{
  if (a == NULL)
    return b == NULL;
  else if (b == NULL)
    return FALSE;
  else
    return strcmp (a, b) == 0;
}

static gboolean
urls_equal_up_to_fragment (const GskUrl *a,
                           const GskUrl *b)
{
  return a->scheme == b->scheme
      && strings_equal (a->host, b->host)
      && strings_equal (a->password, b->password)
      && gsk_url_get_port (a) == gsk_url_get_port (b)
      && strings_equal (a->user_name, b->user_name)
      && strings_equal (a->path, b->path)
      && strings_equal (a->query, b->query);
}

/**
 * gsk_url_transfer_add_redirect:
 * @transfer: the Transfer to affect.
 * @request: request object for this segment of the transfer.
 * @response: response object for this segment of the transfer.
 * @is_permanent: whether the content is permanently relocated to this address.
 * @dest_url: the URL to which we have been redirected.
 *
 * Add an entry to the list of redirects
 * that we have encountered while trying to
 * service this request.
 *
 * Most users of GskUrlTransfer won't care about these redirects--
 * they are provided to the rare client that cares about the redirect-path.
 * More commonly, users merely wish to suppress redirect handling: that can be done
 * more easily by gsk_url_transfer_set_follow_redirects().
 *
 * This function should only be needed by implementors
 * of types of GskUrlTransfer.
 *
 * returns: whether the redirect was allowed (it is disallowed if
 * it is a circular redirect. In that case, we will set 'transfer->error',
 * and call gsk_url_transfer_notify_done().
 */
gboolean
gsk_url_transfer_add_redirect    (GskUrlTransfer     *transfer,
                                  GObject            *request,
                                  GObject            *response,
                                  gboolean            is_permanent,
                                  GskUrl             *dest_url)
{
  GskUrlTransferRedirect *redirect;
  g_return_val_if_fail (transfer->transfer_state == GSK_URL_TRANSFER_STATE_STARTED, TRUE);
  g_return_val_if_fail (GSK_IS_URL (dest_url), TRUE);

  /* Detect circular references. */
  if (urls_equal_up_to_fragment (dest_url, transfer->url))
    goto circular_redirect;
  for (redirect = transfer->first_redirect; redirect != NULL; redirect = redirect->next)
    if (urls_equal_up_to_fragment (redirect->url, dest_url))
      goto circular_redirect;

  redirect = g_new (GskUrlTransferRedirect, 1);
  redirect->is_permanent = is_permanent;
  redirect->url = g_object_ref (dest_url);
  redirect->request = request ? g_object_ref (request) : transfer->request ? g_object_ref (transfer->request) : NULL;
  redirect->response = response ? g_object_ref (response) : NULL;
  redirect->next = NULL;

  if (transfer->first_redirect == NULL)
    transfer->first_redirect = redirect;
  else
    transfer->last_redirect->next = redirect;
  transfer->last_redirect = redirect;

  transfer->redirect_is_permanent = is_permanent;
  transfer->redirect_url = dest_url;
  return TRUE;


circular_redirect:
  gsk_url_transfer_take_error (transfer,
                               g_error_new (GSK_G_ERROR_DOMAIN,
                                            GSK_ERROR_CIRCULAR,
                                            "circular redirects encountered"));
  gsk_url_transfer_notify_done (transfer, GSK_URL_TRANSFER_ERROR_REDIRECT_LOOP);
  return FALSE;
}

/**
 * gsk_url_transfer_set_download:
 * @transfer: the Transfer to affect.
 * @content: the content-stream for the downloaded data.
 *
 * Set the incoming-content that is associated with this transfer.
 * This will used by the user of the Transfer.
 *
 * This function should only be needed by implementors
 * of types of GskUrlTransfer.
 */
void
gsk_url_transfer_set_download    (GskUrlTransfer     *transfer,
                                  GskStream          *content)
{
  g_return_if_fail (transfer->transfer_state == GSK_URL_TRANSFER_STATE_STARTED);
  g_return_if_fail (transfer->content == NULL);
  g_return_if_fail (GSK_IS_STREAM (content));
  transfer->content = g_object_ref (content);
}

/**
 * gsk_url_transfer_set_request:
 * @transfer: the Transfer to affect.
 * @request: the request object to store in the transfer information.
 *
 * Set the outgoing-request header data for this transaction.
 *
 * This function should only be needed by implementors
 * of types of GskUrlTransfer.
 */
void
gsk_url_transfer_set_request     (GskUrlTransfer     *transfer,
                                  GObject            *request)
{
  GObject *old_request = transfer->request;
  g_return_if_fail (transfer->transfer_state == GSK_URL_TRANSFER_STATE_STARTED);
  g_return_if_fail (G_IS_OBJECT (request));
  transfer->request = g_object_ref (request);
  if (old_request)
    g_object_unref (old_request);
}

/**
 * gsk_url_transfer_set_response:
 * @transfer: the Transfer to affect.
 * @response: the response object to store in the transfer information.
 *
 * Set the incoming-response header data for this transaction.
 *
 * This function should only be needed by implementors
 * of types of GskUrlTransfer.
 */
void
gsk_url_transfer_set_response    (GskUrlTransfer     *transfer,
                                  GObject            *response)
{
  GObject *old_response = transfer->response;
  g_return_if_fail (transfer->transfer_state == GSK_URL_TRANSFER_STATE_STARTED);
  g_return_if_fail (transfer->response == NULL);
  transfer->response = g_object_ref (response);
  if (old_response)
    g_object_unref (old_response);
}

/**
 * gsk_url_transfer_set_error:
 * @transfer: the Transfer to affect.
 * @error: the error to associate with the transfer.
 *
 * Set the error field for this transaction.
 * A copy of the error parameter is made.
 *
 * This function should only be needed by implementors
 * of types of GskUrlTransfer.
 */
void
gsk_url_transfer_set_error       (GskUrlTransfer     *transfer,
                                  const GError       *error)
{
  GError *copy = g_error_copy (error);
  g_return_if_fail (error != NULL);
  if (transfer->error)
    g_error_free (transfer->error);
  transfer->error = copy;
}

/**
 * gsk_url_transfer_take_error:
 * @transfer: the Transfer to affect.
 * @error: the error to associate with the transfer.
 *
 * Set the error field for this transaction.
 * The error parameter will be freed eventually by the
 * #GskUrlTransfer.
 *
 * This function should only be needed by implementors
 * of types of GskUrlTransfer.
 */
void
gsk_url_transfer_take_error      (GskUrlTransfer     *transfer,
                                  GError             *error)
{
  g_return_if_fail (error != NULL);
  if (error == transfer->error)
    return;
  if (transfer->error)
    g_error_free (transfer->error);
  transfer->error = error;
}

/**
 * gsk_url_transfer_is_done:
 * @transfer: the Transfer to query.
 *
 * Find out whether the transfer is done.
 * The transfer is done iff the callback has been invoked.
 *
 * returns: whether the function is done.
 */
gboolean
gsk_url_transfer_is_done (GskUrlTransfer *transfer)
{
  return (transfer->transfer_state == GSK_URL_TRANSFER_STATE_DONE);
}

/**
 * gsk_url_transfer_notify_done:
 * @transfer: the Transfer to affect.
 * @result: the transfer's result status code.
 *
 * Transition the transfer to the DONE state,
 * and invoke the user's callback (if any).
 * This function may only be invoked once per transfer.
 *
 * This function should only be needed by implementors
 * of types of GskUrlTransfer.
 */
void
gsk_url_transfer_notify_done     (GskUrlTransfer     *transfer,
                                  GskUrlTransferResult result)
{
  g_assert (transfer->transfer_state == GSK_URL_TRANSFER_STATE_STARTED);
  transfer->transfer_state = GSK_URL_TRANSFER_STATE_DONE;
  transfer->result = result;

  if (transfer->timeout_source)
    {
      GskSource *timeout = transfer->timeout_source;
      transfer->timeout_source = NULL;
      gsk_source_remove (timeout);
    }

  if (transfer->handler != NULL)
    transfer->handler (transfer, transfer->handler_data);

  /* we must relinquish these, or else there will circular ref-count leaks
     if the user tries to do tricks like the above commented code. */
  if (transfer->content != NULL)
    {
      GskStream *tmp = transfer->content;
      transfer->content = NULL;
      g_object_unref (tmp);
    }
  if (transfer->upload_func != NULL)
    {
      gpointer data = transfer->upload_data;
      GDestroyNotify destroy = transfer->upload_destroy;
      transfer->upload_func = NULL;
      transfer->upload_data = NULL;
      transfer->upload_destroy = NULL;
      if (destroy)
        destroy (data);
    }

  if (transfer->handler_data_destroy)
    transfer->handler_data_destroy (transfer->handler_data);

  transfer->handler = NULL;
  transfer->handler_data_destroy = NULL;

  g_object_unref (transfer);
}

/* Registering a transfer type */
static GHashTable *scheme_to_slist_of_classes = NULL;
/**
 * gsk_url_transfer_class_register:
 * @scheme: the URL scheme that this class of transfer can handle.
 * @transfer_class: the class that can handle the URL type.
 *
 * Register a class of URL transfer that can
 * handle a given scheme.
 * It will only be instantiated if the class' test method
 * returns TRUE, to indicate that it can handle the specific URL.
 */
void
gsk_url_transfer_class_register  (GskUrlScheme            scheme,
                                  GskUrlTransferClass    *transfer_class)
{
  GSList *list;
  if (scheme_to_slist_of_classes == NULL)
    scheme_to_slist_of_classes = g_hash_table_new (NULL, NULL);
  list = g_hash_table_lookup (scheme_to_slist_of_classes, GUINT_TO_POINTER (scheme));
  if (list == NULL)
    {
      list = g_slist_prepend (NULL, transfer_class);
      g_hash_table_insert (scheme_to_slist_of_classes, GUINT_TO_POINTER (scheme), list);
    }
  else
    list = g_slist_append (list, transfer_class);
}

/**
 * gsk_url_transfer_new:
 * @url: the URL to create a transfer object for.
 *
 * Create a URL transfer of the appropriate type for the given URL.
 * We try the registered classes, in order.
 *
 * returns: a newly allocated Transfer object, or NULL if no transfer-class
 * could handle the URL.
 */
GskUrlTransfer *
gsk_url_transfer_new             (GskUrl             *url)
{
  GSList *list = g_hash_table_lookup (scheme_to_slist_of_classes, GUINT_TO_POINTER (url->scheme));
  while (list != NULL)
    {
      GskUrlTransferClass *class = GSK_URL_TRANSFER_CLASS (list->data);
      if (class->test == NULL || class->test (class, url))
        {
          GskUrlTransfer *transfer = g_object_new (G_OBJECT_CLASS_TYPE (class), NULL);
          gsk_url_transfer_set_url (transfer, url);
          return transfer;
        }
      list = list->next;
    }
  return NULL;
}

/**
 * gsk_url_transfer_get_state_string:
 * @transfer: the transfer to describe.
 *
 * Get a newly allocated, human-readable description
 * of the state of the transfer.
 *
 * returns: the newly-allocated string.
 */
char *
gsk_url_transfer_get_state_string (GskUrlTransfer *transfer)
{
  GskUrlTransferClass *class = GSK_URL_TRANSFER_GET_CLASS (transfer);
  switch (transfer->transfer_state)
    {
    case GSK_URL_TRANSFER_STATE_CONSTRUCTING:
      return class->get_constructing_state (transfer);
    case GSK_URL_TRANSFER_STATE_STARTED:
      return class->get_running_state (transfer);
    case GSK_URL_TRANSFER_STATE_DONE:
      return class->get_done_state (transfer);
    default:
      return g_strdup ("gsk_url_transfer_get_state_string: INVALID state");
    }
}


/* --- convert a transfer to a stream --- */
GType gsk_url_transfer_stream_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_URL_TRANSFER_STREAM              (gsk_url_transfer_stream_get_type ())
#define GSK_URL_TRANSFER_STREAM(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_URL_TRANSFER_STREAM, GskUrlTransferStream))
#define GSK_URL_TRANSFER_STREAM_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_URL_TRANSFER_STREAM, GskUrlTransferStreamClass))
#define GSK_URL_TRANSFER_STREAM_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_URL_TRANSFER_STREAM, GskUrlTransferStreamClass))
#define GSK_IS_URL_TRANSFER_STREAM(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_URL_TRANSFER_STREAM))
#define GSK_IS_URL_TRANSFER_STREAM_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_URL_TRANSFER_STREAM))
typedef struct _UfUrlTransferStreamClass GskUrlTransferStreamClass;
typedef struct _UfUrlTransferStream GskUrlTransferStream;
struct _UfUrlTransferStreamClass
{
  GskStreamClass base_class;
};
struct _UfUrlTransferStream
{
  GskStream base_instance;
  GskUrlTransfer *transfer;
  GskStream *substream;
};

G_DEFINE_TYPE(GskUrlTransferStream, gsk_url_transfer_stream, GSK_TYPE_STREAM);

static void
gsk_url_transfer_stream_finalize (GObject *object)
{
  GskUrlTransferStream *transfer_stream = GSK_URL_TRANSFER_STREAM (object);
  g_assert (transfer_stream->transfer == NULL);
  if (transfer_stream->substream)
    {
      gsk_io_untrap_readable (transfer_stream->substream);
      g_object_unref (transfer_stream->substream);
    }
  G_OBJECT_CLASS (gsk_url_transfer_stream_parent_class)->finalize (object);
}
static gboolean
handle_substream_is_readable (GskIO *io, gpointer data)
{
  gsk_io_notify_ready_to_read (data);
  return TRUE;
}
static gboolean
handle_substream_read_shutdown (GskIO *io, gpointer data)
{
  GskUrlTransferStream *transfer_stream = GSK_URL_TRANSFER_STREAM (data);
  gsk_io_notify_read_shutdown (GSK_IO (transfer_stream));
  if (transfer_stream->substream)
    {
      gsk_io_untrap_readable (transfer_stream->substream);
      g_object_unref (transfer_stream->substream);
      transfer_stream->substream = NULL;
    }
  return FALSE;
}

static void
gsk_url_transfer_stream_set_poll_read   (GskIO      *io,
				        gboolean    do_poll)
{
  GskUrlTransferStream *transfer_stream = GSK_URL_TRANSFER_STREAM (io);
  if (transfer_stream->substream == NULL)
    return;
  if (do_poll)
    gsk_io_trap_readable (transfer_stream->substream,
                          handle_substream_is_readable,
                          handle_substream_read_shutdown,
                          transfer_stream,
                          NULL);
  else
    gsk_io_untrap_readable (transfer_stream->substream);
}

static gboolean
gsk_url_transfer_stream_shutdown_read   (GskIO      *io,
                                        GError    **error)
{
  GskUrlTransferStream *transfer_stream = GSK_URL_TRANSFER_STREAM (io);
  if (transfer_stream->transfer != NULL)
    gsk_url_transfer_cancel (transfer_stream->transfer);
  if (transfer_stream->substream != NULL)
    gsk_io_read_shutdown (GSK_IO (transfer_stream->substream), NULL);
  return TRUE;
}

static guint
gsk_url_transfer_stream_raw_read (GskStream     *stream,
			 	 gpointer       data,
			 	 guint          length,
			 	 GError       **error)
{
  GskUrlTransferStream *transfer_stream = GSK_URL_TRANSFER_STREAM (stream);
  if (transfer_stream->substream == NULL)
    return 0;
  return gsk_stream_read (transfer_stream->substream, data, length, error);
}

static guint
gsk_url_transfer_stream_raw_read_buffer (GskStream     *stream,
				        GskBuffer     *buffer,
				        GError       **error)
{
  GskUrlTransferStream *transfer_stream = GSK_URL_TRANSFER_STREAM (stream);
  if (transfer_stream->substream == NULL)
    return 0;
  return gsk_stream_read_buffer (transfer_stream->substream, buffer, error);
}

static void
gsk_url_transfer_stream_class_init (GskUrlTransferStreamClass *class)
{
  GskIOClass *io_class = GSK_IO_CLASS (class);
  GskStreamClass *stream_class = GSK_STREAM_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  stream_class->raw_read = gsk_url_transfer_stream_raw_read;
  stream_class->raw_read_buffer = gsk_url_transfer_stream_raw_read_buffer;
  io_class->set_poll_read = gsk_url_transfer_stream_set_poll_read;
  io_class->shutdown_read = gsk_url_transfer_stream_shutdown_read;
  object_class->finalize = gsk_url_transfer_stream_finalize;
}

static void
gsk_url_transfer_stream_init (GskUrlTransferStream *transfer_stream)
{
  gsk_stream_mark_is_readable (transfer_stream);
}

static void
handle_transfer_done (GskUrlTransfer *transfer,
                      gpointer        data)
{
  GskUrlTransferStream *transfer_stream = GSK_URL_TRANSFER_STREAM (data);
  g_assert (transfer_stream->transfer == transfer);
  transfer_stream->transfer = NULL;

  if (transfer->error != NULL)
    gsk_io_set_gerror (GSK_IO (transfer_stream), GSK_IO_ERROR_CONNECT,
                       g_error_copy (transfer->error));
  if (transfer->content != NULL)
    {
      transfer_stream->substream = g_object_ref (transfer->content);
      if (gsk_io_is_polling_for_read (transfer_stream))
        gsk_io_trap_readable (transfer_stream->substream,
                              handle_substream_is_readable,
                              handle_substream_read_shutdown,
                              g_object_ref (transfer_stream),
                              g_object_unref);
    }
  else
    {
      gsk_io_notify_read_shutdown (GSK_IO (transfer_stream));
    }
}

/**
 * gsk_url_transfer_stream_new:
 * @transfer: the transfer.  must not be started.
 * @error: optional location to store the #GError if there is a problem.
 *
 * This code will start the transfer,
 * and return a stream that you can trap immediately.
 *
 * returns: the new stream, or NULL if an error occurred.
 */
GskStream *
gsk_url_transfer_stream_new (GskUrlTransfer *transfer,
                             GError        **error)
{
  GskUrlTransferStream *transfer_stream = g_object_new (GSK_TYPE_URL_TRANSFER_STREAM, NULL);
  transfer_stream->transfer = transfer;
  gsk_url_transfer_set_handler (transfer,
                                handle_transfer_done,
                                g_object_ref (transfer_stream),
                                g_object_unref);
  if (!gsk_url_transfer_start (transfer, error))
    {
      transfer_stream->transfer = NULL;
      g_object_unref (transfer_stream);
      return NULL;
    }
  return GSK_STREAM (transfer_stream);
}
