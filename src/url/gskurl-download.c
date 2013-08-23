#include "gskurl.h"
#include "../http/gskhttpclient.h"
#include "../gskstreamclient.h"
#include "../gsknameresolver.h"
#include "../gskmacros.h"
#include "../gskdebug.h"
#include "../debug.h"

/* XXX: thread-safety */

typedef struct _DownloadInfo DownloadInfo;
struct _DownloadInfo
{
  GskUrlScheme scheme;
  GskUrlDownloadMethod download;
  gpointer download_data;
  DownloadInfo *next;
};

struct _GskUrlDownload
{
  GskUrl          *url;
  GskUrlSuccess    success_func;
  GskUrlFailure    failure_func;
  gpointer         user_data;
};

/* global list of download methods */
static DownloadInfo *first_dl_info = NULL;

/* whether we've initialized the download-method system */
static gboolean has_initialized = FALSE;
static void initialize_url_download_system (void);
#define CHECK_HAS_INITIALIZED()				\
  G_STMT_START{						\
    if (!has_initialized)				\
      initialize_url_download_system ();		\
  }G_STMT_END

static DownloadInfo *
find_download_info (GskUrlScheme scheme)
{
  DownloadInfo *at;
  for (at = first_dl_info; at != NULL; at = at->next)
    if (at->scheme == scheme)
      return at;
  return NULL;
}


/**
 * gsk_url_scheme_add_dl_method:
 * @scheme: the URL scheme which this download method can handle.
 * @download_method: function to call to initiate the URL download.
 * @download_data: data to be passed to download_method.
 *
 * Register a new method for downloading a URL of a particular scheme.
 *
 * The callback @download_method will be run with each new requested URL.
 * Each call to @download_method must cause it to
 * eventually call gsk_url_download_success()
 * or gsk_url_download_fail(); furthermore, it may
 * call those functions before returning.
 */
void
gsk_url_scheme_add_dl_method (GskUrlScheme     scheme,
			      GskUrlDownloadMethod  download_method,
			      gpointer         download_data)
{
  DownloadInfo *info;
  g_return_if_fail (find_download_info (scheme) == NULL);
  CHECK_HAS_INITIALIZED ();
  info = g_new (DownloadInfo, 1);

  info->scheme = scheme;
  info->download = download_method;
  info->download_data = download_data;
  info->next = first_dl_info;

  first_dl_info = info;
}

/**
 * gsk_url_download:
 * @url: the URL to attempt to retrieve.
 * @success_func: a function to call with a stream corresponding to the
 * content of the page.
 * @failure_func: a function to call with an error message if the
 * url cannot be retrieved.
 * @user_data: data to pass to @success_func or @failure_func.
 *
 * Initiate a URL download.
 *
 * A caller-supplied function will be invoked when
 * the first content is available or a different
 * function will be called if there is a problem.
 * Only exactly one of these functions will be called.
 *
 * Either callback may be invoked before this function returns:
 * the caller must deal with it.
 */
void
gsk_url_download            (GskUrl          *url,
			     GskUrlSuccess    success_func,
			     GskUrlFailure    failure_func,
			     gpointer         user_data)
{
  DownloadInfo *info;
  CHECK_HAS_INITIALIZED ();
  info = find_download_info (url->scheme);
  if (info == NULL)
    {
      GError *error = g_error_new (GSK_G_ERROR_DOMAIN,
				   GSK_ERROR_PROTOCOL_NOT_AVAILABLE,
				   _("couldn't find url-download method for %s"),
				   url->scheme_name);
      if (failure_func)
	failure_func (error, user_data);
      g_error_free (error);
    }
  else
    {
      GskUrlDownload *download = g_new (GskUrlDownload, 1);
      download->success_func = success_func;
      download->url = g_object_ref (url);
      download->failure_func = failure_func;
      download->user_data = user_data;
      (*info->download) (download, info->download_data);
    }
}

/* --- protected methods --- */
static inline void
gsk_url_download_destroy (GskUrlDownload *download)
{
  g_object_unref (download->url);
  g_free (download);
}

/**
 * gsk_url_download_success:
 * @download: the download object.
 * @stream: the content stream to return to the caller
 * who requested the download.
 *
 * Give a stream to the user which requested it
 * via gsk_url_download().
 *
 * This function should only be used for implementing
 * handlers for new URL schemes.
 */
void
gsk_url_download_success    (GskUrlDownload  *download,
			     GskStream       *stream)
{
  if (download->success_func)
    download->success_func (stream, download->user_data);
  gsk_url_download_destroy (download);
}

/**
 * gsk_url_download_fail:
 * @download: the download object.
 * @error: an error to return.
 *
 * Give a failure notice to the user which requested a url download
 * via gsk_url_download().
 *
 * This function should only be used for implementing
 * handlers for new URL schemes.
 */
void
gsk_url_download_fail       (GskUrlDownload  *download,
			     GError          *error)
{
  if (download->failure_func)
    download->failure_func (error, download->user_data);
  gsk_url_download_destroy (download);
}

/**
 * gsk_url_download_peek_url:
 * @download: the download object to query.
 *
 * Get the URL that you are supposed to download.
 *
 * returns: the URL for downloading.
 */
GskUrl *
gsk_url_download_peek_url (GskUrlDownload *download)
{
  return download->url;
}

/**
 * gsk_url_download_redirect:
 * @download: the download whose URL has been redirected.
 * @new_url: the new URL to download.
 *
 * Indicate that retrieving one URL lead to a message
 * saying that the content is at a different URL.
 * The new URL should be retrieved, just like if it has
 * been the original requestor.
 *
 * This function should only be used for implementing
 * handlers for new URL schemes.
 */
void
gsk_url_download_redirect   (GskUrlDownload  *download,
			     GskUrl          *new_url)
{
  DownloadInfo *info = find_download_info (new_url->scheme);
  if (info == NULL)
    {
      GError *error = g_error_new (GSK_G_ERROR_DOMAIN,
				   GSK_ERROR_PROTOCOL_NOT_AVAILABLE,
				   _("couldn't find url-download method for %s"),
				   new_url->scheme_name);
      gsk_url_download_fail (download, error);
      g_error_free (error);
    }
  else
    {
      GskUrl *old_url = download->url;
      download->url = g_object_ref (new_url);
      g_object_unref (old_url);
      (*info->download) (download, info->download_data);
    }
}

/* --- http implementation --- */
typedef struct _HttpDownloadInfo HttpDownloadInfo;
struct _HttpDownloadInfo
{
  GskUrlDownload *download;
  gboolean has_notified;
};

static void
http_handle_response  (GskHttpRequest  *request,
		       GskHttpResponse *response,
		       GskStream       *input,
		       gpointer         hook_data)
{
  HttpDownloadInfo *info = hook_data;
  GskUrlDownload *download = info->download;
  switch (response->status_code)
    {
      /* errors i don't expect should ever happen, given the request
       * we've issued */
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
    /*case GSK_HTTP_STATUS_LENGTH_REQUIRED:*/
    /*case GSK_HTTP_STATUS_BAD_RANGE:*/
    /*case GSK_HTTP_STATUS_BAD_GATEWAY:*/
    /*case GSK_HTTP_STATUS_GATEWAY_TIMEOUT:*/

      /* XXX: errors that i'm not sure about */
    /*case GSK_HTTP_STATUS_NO_CONTENT:*/
    /*case GSK_HTTP_STATUS_MULTIPLE_CHOICES:*/
    /*case GSK_HTTP_STATUS_USE_PROXY:*/
    /*case GSK_HTTP_STATUS_PROXY_AUTH_REQUIRED:*/

      /* XXX: if we get this, we should try again at HTTP/1.0... whatever */
    /*case GSK_HTTP_STATUS_UNSUPPORTED_VERSION:*/

      /* errors that really mean the download failed,
       * in a protologically valid way.
       * (some of these indicate a seriously broken server)
       */
    /*case GSK_HTTP_STATUS_UNAUTHORIZED:*/
    /*case GSK_HTTP_STATUS_PAYMENT_REQUIRED:*/
    /*case GSK_HTTP_STATUS_FORBIDDEN:*/
    /*case GSK_HTTP_STATUS_NOT_FOUND:*/
    /*case GSK_HTTP_STATUS_METHOD_NOT_ALLOWED:*/
    /*case GSK_HTTP_STATUS_NOT_ACCEPTABLE:*/
    /*case GSK_HTTP_STATUS_REQUEST_TIMEOUT:*/
    /*case GSK_HTTP_STATUS_GONE:*/
    /*case GSK_HTTP_STATUS_PRECONDITION_FAILED:*/
    /*case GSK_HTTP_STATUS_ENTITY_TOO_LARGE:*/
    /*case GSK_HTTP_STATUS_URI_TOO_LARGE:*/
    /*case GSK_HTTP_STATUS_EXPECTATION_FAILED:*/
    /*case GSK_HTTP_STATUS_UNSUPPORTED_MEDIA:*/
    /*case GSK_HTTP_STATUS_INTERNAL_SERVER_ERROR:*/
    /*case GSK_HTTP_STATUS_NOT_IMPLEMENTED:*/
    /*case GSK_HTTP_STATUS_SERVICE_UNAVAILABLE:*/

    case GSK_HTTP_STATUS_OK:
      info->has_notified = TRUE;
      gsk_url_download_success (download, input);
      break;

      /* redirections */
    case GSK_HTTP_STATUS_MOVED_PERMANENTLY:
    case GSK_HTTP_STATUS_FOUND:
    case GSK_HTTP_STATUS_SEE_OTHER:
    case GSK_HTTP_STATUS_TEMPORARY_REDIRECT:
      {
	GskUrl *new_url = NULL;
	if (response->location != NULL)
	  {
	    GskUrl *old_url = gsk_url_download_peek_url (download);
	    GError *error = NULL;
	    new_url = gsk_url_new_relative (old_url, response->location, &error);
	    if (new_url == NULL)
	      {
		gsk_url_download_fail (download, error);
		info->has_notified = TRUE;
		g_error_free (error);
		return;
	      }
	  }
	if (new_url != NULL)
	  {
	    gsk_url_download_redirect (download, new_url);
	    info->has_notified = TRUE;
	    g_object_unref (new_url);
	    return;
	  }
      }

      /* Fall through to the error case:  all of the above error conditions
       *     should be accompanied by a Location: header.
       */

      /* default case indicates an error occurred */
    default:
      {
	GEnumClass *eclass = g_type_class_ref (GSK_TYPE_HTTP_STATUS);
	GEnumValue *evalue = g_enum_get_value (eclass, response->status_code);
	const char *error_code_name = evalue ? evalue->value_nick : "**unknown status**";
	GError *error = g_error_new (GSK_G_ERROR_DOMAIN, 
				     GSK_ERROR_HTTP_NOT_FOUND,
				     _("error downloading via http: error %d [%s]"),
				     response->status_code, error_code_name);
	gsk_url_download_fail (download, error);
	info->has_notified = TRUE;
	g_error_free (error);
	g_type_class_unref (eclass);
	break;
      }
    }
}

static void
destroy_http_download  (gpointer         hook_data)
{
  HttpDownloadInfo *info = hook_data;
  if (!info->has_notified)
    {
      GError *error = g_error_new (GSK_G_ERROR_DOMAIN, 
				   GSK_ERROR_HTTP_NOT_FOUND,
				   _("problem getting response from HTTP server"));
      gsk_url_download_fail (info->download, error);
      g_error_free (error);
      info->has_notified = TRUE;
    }
  g_free (info);
}

static void
http_name_lookup_success (GskSocketAddress *address,
			  gpointer          func_data)
{
  GskUrlDownload *download = func_data;
  GskUrl *url = gsk_url_download_peek_url (download);
  GError *error = NULL;
  GskStream *istream;

  /* XXX: most likely this should be moved into the generic resolver code */
  if (GSK_IS_SOCKET_ADDRESS_IPV4 (address))
    {
      GskSocketAddressIpv4 *ipv4 = GSK_SOCKET_ADDRESS_IPV4 (address);
      
      /* HACK: we need to get the port from the GskUrl 
       */
      ipv4->ip_port = url->port;

      /* HACK: what if someone else is using 'address', an ostensibly
       *       immutable class.
       */
      
      if (ipv4->ip_port == 0)
	ipv4->ip_port = 80;
    }

  istream = gsk_stream_new_connecting (address, &error);
  if (istream == NULL)
    gsk_url_download_fail (download, error);
  else
    {
      GskHttpClient *client = gsk_http_client_new ();
      GskStream *cstream;
      GskHttpRequest *request;
      HttpDownloadInfo *download_info = g_new (HttpDownloadInfo, 1);
      char *full_path;

      if (GSK_IS_DEBUGGING (FD))
        {
          g_message ("debug-fd: open(%d): url->scheme,host=%s,%s",
                     GSK_STREAM_FD (istream)->fd,
                     url->scheme_name,
                     url->host);
        }

      download_info->has_notified = FALSE;
      download_info->download = download;

      full_path = gsk_url_get_relative_path (url);
      request = gsk_http_request_new (GSK_HTTP_VERB_GET, full_path);
      g_free (full_path);

      g_object_set (request, "host", url->host, NULL);
      gsk_http_client_request (client,
			       request,
			       NULL,
			       http_handle_response,
			       download_info,
			       destroy_http_download);
      cstream = GSK_STREAM (client);
      gsk_stream_attach (istream, cstream, NULL);
      gsk_stream_attach (cstream, istream, NULL);
      g_object_unref (istream);
      gsk_http_client_shutdown_when_done (client);
      g_object_unref (cstream);
    }
}

static void
http_name_lookup_failure (GError           *error,
			  gpointer          func_data)
{
  GskUrlDownload *download = func_data;
  gsk_url_download_fail (download, error);
}

static void
download_http (GskUrlDownload  *download,
	       gpointer         data)
{
  GskUrl *url = gsk_url_download_peek_url (download);
  GskNameResolverTask *task = gsk_name_resolve (GSK_NAME_RESOLVER_FAMILY_IPV4,
		                                url->host,
		                                http_name_lookup_success,
		                                http_name_lookup_failure,
		                                download,
		                                NULL);
  gsk_name_resolver_task_unref (task);
}

/* --- initialization --- */
static void
initialize_url_download_system (void)
{
  has_initialized = TRUE;
  gsk_url_scheme_add_dl_method (GSK_URL_SCHEME_HTTP, download_http, NULL);
}
