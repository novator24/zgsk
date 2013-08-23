/* TODO: HTTP proxy support */

#include "gskurltransferhttp.h"
#include "../gsknameresolver.h"
#include "../gskstreamclient.h"
#include "../ssl/gskstreamssl.h"
#include "../http/gskhttpclient.h"
#include <string.h>

G_DEFINE_TYPE(GskUrlTransferHttp, gsk_url_transfer_http, GSK_TYPE_URL_TRANSFER);

/* used to restart a transfer on a new URL (a redirect) */
static void start_name_resolution (GskUrlTransferHttp *http);

struct _GskUrlTransferHttpModifierNode
{
  GskUrlTransferHttpRequestModifierFunc modifier;
  gpointer data;
  GDestroyNotify destroy;
  GskUrlTransferHttpModifierNode *next;
};

static void
handle_http_response (GskHttpRequest  *request,
                      GskHttpResponse *response,
                      GskStream       *input,
                      gpointer         hook_data)
{
  GskUrlTransfer *transfer = GSK_URL_TRANSFER (hook_data);
  GskUrlTransferHttp *http = GSK_URL_TRANSFER_HTTP (hook_data);

  ++(http->response_count);
  if (gsk_url_transfer_is_done (transfer))
    return;

  /* For exposition we retain copy
     the entire list of GskHttpStatusCodes.

     However, the error case is the most common,
     which we handle in the 'default' case.

     Therefore, most of the error status-codes
     are commented out:  they will use the default case. */
  switch (response->status_code)
    {
      /* errors i don't expect should ever happen, given the request
       * we've issued (handled with the default case) */
    /*case GSK_HTTP_STATUS_CONTINUE:*/
    /*case GSK_HTTP_STATUS_SWITCHING_PROTOCOLS:*/
    /*case GSK_HTTP_STATUS_NOT_MODIFIED:*/
    /*case GSK_HTTP_STATUS_BAD_REQUEST:*/
    /*case GSK_HTTP_STATUS_CREATED:*/
    /*case GSK_HTTP_STATUS_ACCEPTED:*/
    /*case GSK_HTTP_STATUS_NONAUTHORITATIVE_INFO:*/
    /*case GSK_HTTP_STATUS_RESET_CONTENT:*/
    /*case GSK_HTTP_STATUS_PARTIAL_CONTENT:*/
    /*case GSK_HTTP_STATUS_CONFLICT:*/
    /*case GSK_HTTP_STATUS_BAD_RANGE:*/
    /*case GSK_HTTP_STATUS_BAD_GATEWAY:*/
    /*case GSK_HTTP_STATUS_GATEWAY_TIMEOUT:*/

      /* XXX: errors that i'm not sure about
              (currently handled with the default case) */
    /*case GSK_HTTP_STATUS_MULTIPLE_CHOICES:*/
    /*case GSK_HTTP_STATUS_USE_PROXY:*/
    /*case GSK_HTTP_STATUS_PROXY_AUTH_REQUIRED:*/

    case GSK_HTTP_STATUS_NO_CONTENT:
    case GSK_HTTP_STATUS_OK:
      gsk_url_transfer_set_response (transfer, G_OBJECT (response));
      if (input)
        gsk_url_transfer_set_download (transfer, input);
      gsk_url_transfer_notify_done (transfer, GSK_URL_TRANSFER_SUCCESS);
      break;

      /* errors that really mean the download failed,
       * in a protologically valid way.
       * (some of these indicate a seriously broken server)
       * (handled with the default case)
       */
    /*case GSK_HTTP_STATUS_UNAUTHORIZED:*/
    /*case GSK_HTTP_STATUS_PAYMENT_REQUIRED:*/
    /*case GSK_HTTP_STATUS_FORBIDDEN:*/
    /*case GSK_HTTP_STATUS_NOT_FOUND:*/
    /*case GSK_HTTP_STATUS_METHOD_NOT_ALLOWED:*/
    /*case GSK_HTTP_STATUS_NOT_ACCEPTABLE:*/
    /*case GSK_HTTP_STATUS_REQUEST_TIMEOUT:*/
    /*case GSK_HTTP_STATUS_GONE:*/
    /*case GSK_HTTP_STATUS_LENGTH_REQUIRED:*/
    /*case GSK_HTTP_STATUS_PRECONDITION_FAILED:*/
    /*case GSK_HTTP_STATUS_ENTITY_TOO_LARGE:*/
    /*case GSK_HTTP_STATUS_URI_TOO_LARGE:*/
    /*case GSK_HTTP_STATUS_EXPECTATION_FAILED:*/
    /*case GSK_HTTP_STATUS_UNSUPPORTED_MEDIA:*/
    /*case GSK_HTTP_STATUS_INTERNAL_SERVER_ERROR:*/
    /*case GSK_HTTP_STATUS_NOT_IMPLEMENTED:*/
    /*case GSK_HTTP_STATUS_SERVICE_UNAVAILABLE:*/
      /* XXX: if we get this, we should try again at HTTP/1.0... whatever */
    /*case GSK_HTTP_STATUS_UNSUPPORTED_VERSION:*/

      /* redirections */
    case GSK_HTTP_STATUS_MOVED_PERMANENTLY:
    case GSK_HTTP_STATUS_FOUND:
    case GSK_HTTP_STATUS_SEE_OTHER:
    case GSK_HTTP_STATUS_TEMPORARY_REDIRECT:
      {
	GskUrl *new_url = NULL;
        gboolean is_permanent = (response->status_code == GSK_HTTP_STATUS_MOVED_PERMANENTLY);
	if (response->location != NULL)
	  {
	    GskUrl *old_url = transfer->url;
	    GError *error = NULL;
	    new_url = gsk_url_new_relative (old_url, response->location, &error);
	    if (new_url == NULL)
	      {
                GskUrlTransferResult result = GSK_URL_TRANSFER_ERROR_UNSUPPORTED;       /* XXX: not especially correct */
                g_warning ("redirect to invalid Location: %s: %s", 
                           response->location,
                           error ? error->message : "unknown error");
                gsk_url_transfer_take_error (transfer, error);
		gsk_url_transfer_notify_done (transfer, result);
		if (input)
                  gsk_io_read_shutdown (input, NULL);
		return;
	      }
            if (new_url != NULL)
              {
                if (!gsk_url_transfer_add_redirect (transfer,
                                                    NULL,
                                                    G_OBJECT (response),
                                                    is_permanent,
                                                    new_url))
                  {
                    if (input)
                      gsk_io_read_shutdown (input, NULL);
                    g_object_unref (new_url);
                    return;
                  }

                g_object_unref (new_url);

                if (transfer->follow_redirects)
                  /* restart at name-lookup */
                  start_name_resolution (http);
                else
                  gsk_url_transfer_notify_done (transfer, GSK_URL_TRANSFER_REDIRECT);
		if (input)
                  gsk_io_read_shutdown (input, NULL);

                return;
              }
	  }
      }

      /* default case indicates an error occurred */
    default:
      {
	GEnumClass *eclass = g_type_class_ref (GSK_TYPE_HTTP_STATUS);
	GEnumValue *evalue = g_enum_get_value (eclass, response->status_code);
	const char *error_code_name = evalue ? evalue->value_nick : "**unknown status**";
        GskUrlTransferResult result;
	gsk_url_transfer_take_error (transfer,
                                     g_error_new (GSK_G_ERROR_DOMAIN, 
				                  GSK_ERROR_HTTP_NOT_FOUND,
				                  "error downloading via http: error %d [%s]",
				                  response->status_code, error_code_name));
	g_type_class_unref (eclass);

        if (response->status_code / 100 == 4)   /* 400s are not found type errors */
          result = GSK_URL_TRANSFER_ERROR_NOT_FOUND;
        else if (response->status_code / 100 == 5)   /* 400s are server errors */
          result = GSK_URL_TRANSFER_ERROR_SERVER_ERROR;
        else
          result = GSK_URL_TRANSFER_ERROR_UNSUPPORTED; /* who knows? */
        gsk_url_transfer_notify_done (transfer, result);
        if (input)
          gsk_io_read_shutdown (input, NULL);
	break;
      }
    }
}

static void
http_client_request_destroyed (gpointer data)
{
  GskUrlTransfer *transfer = GSK_URL_TRANSFER (data);
  GskUrlTransferHttp *http = GSK_URL_TRANSFER_HTTP (data);

  /* http invariant */
  g_assert (http->response_count <= http->request_count);

  g_assert (http->undestroyed_requests > 0);
  --(http->undestroyed_requests);

  if (!transfer->timed_out
   && !gsk_url_transfer_is_done (transfer)
   && http->undestroyed_requests == 0
   && http->response_count < http->request_count)
    {
      /* transfer has been aborted for mysterious reasons */
      gsk_url_transfer_take_error (transfer,
                                   g_error_new (GSK_G_ERROR_DOMAIN, 
                                                GSK_ERROR_BAD_FORMAT,
                                                "unable to get HTTP response from server"));
      gsk_url_transfer_notify_done (transfer, GSK_URL_TRANSFER_ERROR_SERVER_ERROR);
    }
  g_object_unref (transfer);
}

static void
handle_name_resolution_succeeded (GskSocketAddress *address,
                                  gpointer          data)
{
  GskUrlTransfer *transfer = GSK_URL_TRANSFER (data);
  GskUrlTransferHttp *http = GSK_URL_TRANSFER_HTTP (data);
  GError *error = NULL;
  GskStream *transport;
  GskHttpClient *http_client;
  GskHttpRequest *http_request;
  GskStream *upload_stream;
  GskUrlTransferHttpModifierNode *modifier;
  GskUrl *url = transfer->redirect_url ? transfer->redirect_url : transfer->url;

  if (gsk_url_transfer_is_done (transfer))
    return;

  /* Create actual address (with correct port) */
  {
    GskSocketAddressIpv4 *found = GSK_SOCKET_ADDRESS_IPV4 (address);
    GskSocketAddress *addr;
    guint url_port = gsk_url_get_port (url);
    if (http->is_proxy || found->ip_port == url_port)
      addr = g_object_ref (address);
    else
      addr = gsk_socket_address_ipv4_new (found->ip_address, url_port);
    gsk_url_transfer_set_address (transfer, addr);

    /* Create a TCP connection to that address. */
    if (http->raw_transport != NULL)
      {
        /* from a redirect */
        g_object_unref (http->raw_transport);
      }
    http->raw_transport = gsk_stream_new_connecting (addr, &error);
    if (http->raw_transport == NULL)
      {
        gsk_url_transfer_take_error (transfer, error);
        gsk_url_transfer_notify_done (transfer, GSK_URL_TRANSFER_ERROR_NO_SERVER);
        return;
      }
    g_object_unref (addr);
    addr = NULL;
  }

  /* For SSL streams, create the ssl-transport */
  if (url->scheme == GSK_URL_SCHEME_HTTPS)
    {
      transport = gsk_stream_ssl_new_client (http->ssl_cert,
                                             http->ssl_key,
                                             http->ssl_password,
                                             http->raw_transport,
                                             &error);
      if (transport == NULL)
        {
          gsk_url_transfer_take_error (transfer, error);
          gsk_url_transfer_notify_done (transfer, GSK_URL_TRANSFER_ERROR_BAD_REQUEST);
          return;
        }
    }
  else
    {
      /* otherwise, use the raw_transport directly. */
      transport = g_object_ref (http->raw_transport);
    }

  /* setup path */
  {
    char *to_free = NULL;
    const char *path;
    GskHttpVerb verb;
    if (http->is_proxy)
      path = to_free = gsk_url_to_string (url);
    else if (url->query)
      path = to_free = g_strdup_printf ("%s?%s", url->path, url->query);
    else
      path = url->path;
    verb = gsk_url_transfer_has_upload (transfer) ? GSK_HTTP_VERB_POST : GSK_HTTP_VERB_GET;

    /* make basic request object */
    http_request = gsk_http_request_new (verb, path);
    g_free (to_free);
    if (http->is_proxy)
      {
        /* TODO: 
           gsk_http_request_set_proxy_host?
         */
      }
    else if (url->port == 0
     || url->port == 80)
      gsk_http_request_set_host (http_request, url->host);
    else
      {
        guint hostlen = strlen (url->host);
        guint hostport_len = hostlen + 20;
        char *hostport = g_alloca (hostport_len);
        g_snprintf (hostport, hostport_len,
                    "%s:%u",
                    url->host, url->port);
        gsk_http_request_set_host (http_request, hostport);
      }
  }

  /* run modifiers */
  for (modifier = http->first_modifier; modifier != NULL; modifier = modifier->next)
    modifier->modifier (http_request, modifier->data);

  gsk_url_transfer_set_request (transfer, G_OBJECT (http_request));

  if (gsk_url_transfer_has_upload (transfer))
    {
      gssize size;
      upload_stream = gsk_url_transfer_create_upload (transfer, &size, &error);
      if (upload_stream == NULL)
        {
          g_object_unref (transport);
          g_object_unref (http_request);
          gsk_url_transfer_take_error (transfer, error);
          gsk_url_transfer_notify_done (transfer, GSK_URL_TRANSFER_ERROR_BAD_REQUEST);
          return;
        }
      if (size >= 0)
        gsk_http_header_set_content_length (GSK_HTTP_HEADER (http_request), size);
    }
  else
    {
      upload_stream = NULL;
    }

  http_client = gsk_http_client_new ();
  ++(http->request_count);
  ++(http->undestroyed_requests);
  gsk_http_client_propagate_content_read_shutdown (http_client);
  gsk_http_client_request (http_client, http_request, upload_stream,
                           handle_http_response,
                           g_object_ref (transfer),
                           http_client_request_destroyed);
  gsk_http_client_shutdown_when_done (http_client);
  if (!gsk_stream_attach_pair (transport, GSK_STREAM (http_client), &error))
    {
      g_warning ("gsk_stream_attach_pair: transport/http-client: %s", error->message);
      g_clear_error (&error);
    }
  if (upload_stream)
    g_object_unref (upload_stream);
  g_object_unref (transport);
  g_object_unref (http_request);
  g_object_unref (http_client);
}

static void
handle_name_resolution_failed (GError       *error,
                               gpointer      data)
{
  GskUrlTransfer *transfer = GSK_URL_TRANSFER (data);
  gsk_url_transfer_set_error (transfer, error);
  if (!gsk_url_transfer_is_done (transfer))
    gsk_url_transfer_notify_done (transfer, GSK_URL_TRANSFER_ERROR_BAD_NAME);
}

static void
set_name_lookup_NULL_and_unref (gpointer data)
{
  GskUrlTransferHttp *http = GSK_URL_TRANSFER_HTTP (data);
  http->name_lookup = NULL;
  g_object_unref (http);
}

static void
start_name_resolution (GskUrlTransferHttp *http)
{
  GskUrlTransfer *transfer = GSK_URL_TRANSFER (http);
  GskUrl *url = transfer->redirect_url ? transfer->redirect_url : transfer->url;
  g_return_if_fail (GSK_IS_URL (url));
  g_return_if_fail (url->host != NULL);
  gsk_name_resolver_lookup (GSK_NAME_RESOLVER_FAMILY_IPV4,
                            url->host,
                            handle_name_resolution_succeeded,
                            handle_name_resolution_failed,
                            g_object_ref (transfer),
                            set_name_lookup_NULL_and_unref);
}

static gboolean
gsk_url_transfer_http_start (GskUrlTransfer *transfer,
                             GError        **error)
{
  GskUrlTransferHttp *http = GSK_URL_TRANSFER_HTTP (transfer);
  GskUrl *url = transfer->url;
  if (url->host == NULL)
    {
      g_set_error (error,
                   GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_BAD_FORMAT,
                   "HTTP urls must have hosts");
      return FALSE;
    }

  if (transfer->address_hint)
    handle_name_resolution_succeeded (transfer->address_hint, transfer);
  else
    start_name_resolution (http);
  return TRUE;
}

static void
cancel_internal (GskUrlTransferHttp *http)
{
  if (http->name_lookup)
    {
      /* we were doing name resolution */
      gsk_name_resolver_task_cancel (http->name_lookup);
    }
  else if (http->raw_transport)
    {
      /* who knows where we are... just cancel now. */
      GError *error = NULL;
      gsk_io_shutdown (GSK_IO (http->raw_transport), &error);
      if (error)
        {
          g_message ("GskUrlTransferHttp: error shutting down for cancellation: %s", error->message);
          g_error_free (error);
        }
    }
}

static void
gsk_url_transfer_http_cancel (GskUrlTransfer *transfer)
{
  g_object_ref (transfer);
  cancel_internal (GSK_URL_TRANSFER_HTTP (transfer));
  if (!gsk_url_transfer_is_done (transfer))
    gsk_url_transfer_notify_done (transfer, GSK_URL_TRANSFER_CANCELLED);
  g_object_unref (transfer);
}

static void
gsk_url_transfer_http_timed_out (GskUrlTransfer *transfer)
{
  cancel_internal (GSK_URL_TRANSFER_HTTP (transfer));
  GSK_URL_TRANSFER_CLASS (gsk_url_transfer_http_parent_class)->timed_out (transfer);
}

static char *
gsk_url_transfer_http_get_running_state (GskUrlTransfer *transfer)
{
  GString *str = g_string_new ("RUNNING: ");
  GskUrlTransferHttp *http = GSK_URL_TRANSFER_HTTP (transfer);
  if (transfer->url)
    {
      char *url_str = gsk_url_to_string (transfer->url);
      g_string_append (str, url_str);
      g_free (url_str);
    }
  else
    g_string_append (str, "(no url!?!)");

  if (http->name_lookup)
    g_string_append (str, ": doing name lookup");
  else if (http->raw_transport == NULL)
    g_string_append (str, ": no raw transport");
  else if (gsk_io_get_is_connecting (http->raw_transport))
    g_string_append (str, ": connecting");
  return g_string_free (str, FALSE);
}

static void
gsk_url_transfer_http_finalize (GObject *object)
{
  GskUrlTransferHttp *http = GSK_URL_TRANSFER_HTTP (object);
  GskUrlTransferHttpModifierNode *mod;

  g_free (http->ssl_cert);
  g_free (http->ssl_key);
  g_free (http->ssl_password);

  g_assert (http->name_lookup == NULL);
  if (http->raw_transport)
    g_object_unref (http->raw_transport);

  for (mod = http->first_modifier; mod; )
    {
      GskUrlTransferHttpModifierNode *next = mod->next;
      if (mod->destroy)
        mod->destroy (mod->data);
      g_free (mod);
      mod = next;
    }

  G_OBJECT_CLASS (gsk_url_transfer_http_parent_class)->finalize (object);
}

static void
gsk_url_transfer_http_init (GskUrlTransferHttp *url_transfer_http)
{
}

static void
gsk_url_transfer_http_class_init (GskUrlTransferHttpClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GskUrlTransferClass *transfer_class = GSK_URL_TRANSFER_CLASS (class);
  object_class->finalize = gsk_url_transfer_http_finalize;
  transfer_class->start = gsk_url_transfer_http_start;
  transfer_class->cancel = gsk_url_transfer_http_cancel;
  transfer_class->get_running_state = gsk_url_transfer_http_get_running_state;
  transfer_class->timed_out = gsk_url_transfer_http_timed_out;
}

/**
 * gsk_url_transfer_http_set_ssl_cert:
 * @http: the transfer to affect.
 * @cert_fname: the certificate filename.
 *
 * Set the SSL certificate file for this connection.
 */
void
gsk_url_transfer_http_set_ssl_cert    (GskUrlTransferHttp *http,
                                       const char         *cert_fname)
{
  char *str = g_strdup (cert_fname);
  g_free (http->ssl_cert);
  http->ssl_cert = str;
}

void
gsk_url_transfer_http_set_ssl_key     (GskUrlTransferHttp *http,
                                       const char         *key_fname)
{
  char *str = g_strdup (key_fname);
  g_free (http->ssl_key);
  http->ssl_key = str;
}

void
gsk_url_transfer_http_set_ssl_password(GskUrlTransferHttp *http,
                                       const char         *password)
{
  char *str = g_strdup (password);
  g_free (http->ssl_password);
  http->ssl_password = str;
}

static void
transfer_modifier_set_user_agent (GskHttpRequest *request,
                                  gpointer        mod_data)
{
  gsk_http_request_set_user_agent (request, (const char*)mod_data);
}

/**
 * gsk_url_transfer_http_set_user_agent:
 * @http: the transfer to affect.
 * @user_agent: the User-Agent: header's value for this transfer.
 *
 * Set the User-Agent to use for this HTTP transaction.
 */
void
gsk_url_transfer_http_set_user_agent  (GskUrlTransferHttp *http,
                                       const char         *user_agent)
{
  gsk_url_transfer_http_add_modifier (http,
                                      transfer_modifier_set_user_agent,
                                      g_strdup (user_agent),
                                      g_free);
}

/**
 * gsk_url_transfer_http_set_proxy_address:
 * @http: the transfer to affect.
 * @proxy_address: the socket-address to 
 * really connect to.
 *
 * Set an HTTP proxy for this transfer.
 */
void
gsk_url_transfer_http_set_proxy_address  (GskUrlTransferHttp *http,
                                          GskSocketAddress   *proxy_address)
{
  gsk_url_transfer_set_address_hint (GSK_URL_TRANSFER (http), proxy_address);
  http->is_proxy = TRUE;
}

static void
transfer_modifier_set_misc_header  (GskHttpRequest *request,
                                    gpointer        mod_data)
{
  char *key = mod_data;
  char *value = strchr (key, 0) + 1;
  gsk_http_header_add_misc (GSK_HTTP_HEADER (request), key, value);
}

/**
 * gsk_url_transfer_http_add_extra_header:
 * @http: the transfer to affect.
 * @key: a HTTP header name
 * @value: the value of that HTTP header.
 *
 * Add an arbitrary header to the HTTP request.
 */
void
gsk_url_transfer_http_add_extra_header(GskUrlTransferHttp *http,
                                       const char         *key,
                                       const char         *value)
{
  guint key_len, value_len;
  char *kv;
  g_return_if_fail (key != NULL && value != NULL);
  key_len = strlen (key);
  value_len = strlen (value);
  kv = g_malloc (key_len + 1 + value_len + 1);
  strcpy (kv, key);
  strcpy (kv + key_len + 1, value);
  gsk_url_transfer_http_add_modifier (http,
                                      transfer_modifier_set_misc_header,
                                      kv,
                                      g_free);
}

/**
 * gsk_url_transfer_http_add_modifier:
 * @http: the transfer to affect.
 * @modifier: function to call to modify the HTTP request header.
 * @data: data to pass to modifier.
 * @destroy: called with data when the modifier is destroyed.
 *
 * Add a generic transformation to do to the HTTP request header.
 */
void
gsk_url_transfer_http_add_modifier (GskUrlTransferHttp *http,
                                    GskUrlTransferHttpRequestModifierFunc modifier,
                                    gpointer data,
                                    GDestroyNotify destroy)
{
  GskUrlTransferHttpModifierNode *node = g_new (GskUrlTransferHttpModifierNode, 1);
  node->modifier = modifier;
  node->data = data;
  node->destroy = destroy;
  node->next = NULL;
  if (http->first_modifier == NULL)
    http->first_modifier = node;
  else
    http->last_modifier->next = node;
  http->last_modifier = node;
}
