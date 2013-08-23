#include "gskhttpheader.h"
#include "gskhttprequest.h"
#include "gskhttpresponse.h"
#include "../gskmacros.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static GObjectClass *parent_class = NULL;

enum
{
  PROP_RESPONSE_0,
  PROP_RESPONSE_STATUS_CODE,
  PROP_RESPONSE_AGE,
  PROP_RESPONSE_LOCATION,
  PROP_RESPONSE_EXPIRES,
  PROP_RESPONSE_ETAG,
  PROP_RESPONSE_LAST_MODIFIED,
  PROP_RESPONSE_SERVER
};

/* --- GskHttpResponse implementation --- */
/**
 * gsk_http_response_has_content_body:
 * @response: the response to query
 * @request: the request that provoked the response.
 *
 * Find out whether a HTTP Response should be
 * accompanied by a body.
 *
 * It also depends on the request, in particular,
 * HEAD requests do not retrieve bodies.
 *
 * returns: whether the response should be accompanied
 * by a body of data.
 */
gboolean
gsk_http_response_has_content_body (GskHttpResponse *response,
                                    GskHttpRequest  *request)
{
  if (request->verb == GSK_HTTP_VERB_HEAD)
    return FALSE;
  switch (response->status_code)
    {
	/* Status-codes that don't generally include entity body. */
	case GSK_HTTP_STATUS_CONTINUE:
	case GSK_HTTP_STATUS_SWITCHING_PROTOCOLS:
	case GSK_HTTP_STATUS_CREATED:
	case GSK_HTTP_STATUS_ACCEPTED:
	case GSK_HTTP_STATUS_NO_CONTENT:
	case GSK_HTTP_STATUS_RESET_CONTENT:
	case GSK_HTTP_STATUS_NOT_MODIFIED:
          return FALSE;

	case GSK_HTTP_STATUS_OK:
	case GSK_HTTP_STATUS_NONAUTHORITATIVE_INFO:
	case GSK_HTTP_STATUS_MULTIPLE_CHOICES:
          if (request->verb == GSK_HTTP_VERB_PUT
           || request->verb == GSK_HTTP_VERB_DELETE) /* HEAD handled above */
            return FALSE;
          else
            return TRUE;
	  switch (request->verb)
	    {
	    case GSK_HTTP_VERB_HEAD:
	    case GSK_HTTP_VERB_PUT:
	    case GSK_HTTP_VERB_DELETE:
	      return FALSE;
	    default:
	      return TRUE;
	    }
	  break;

	  /* Must contain entity for all verbs except HEAD. */
	case GSK_HTTP_STATUS_PARTIAL_CONTENT:
	case GSK_HTTP_STATUS_MOVED_PERMANENTLY:
	case GSK_HTTP_STATUS_FOUND:
	case GSK_HTTP_STATUS_SEE_OTHER:
	case GSK_HTTP_STATUS_USE_PROXY:
	case GSK_HTTP_STATUS_TEMPORARY_REDIRECT:
	case GSK_HTTP_STATUS_BAD_REQUEST:
	case GSK_HTTP_STATUS_UNAUTHORIZED:
	case GSK_HTTP_STATUS_PAYMENT_REQUIRED:
	case GSK_HTTP_STATUS_FORBIDDEN:
	case GSK_HTTP_STATUS_NOT_FOUND:
	case GSK_HTTP_STATUS_METHOD_NOT_ALLOWED:
	case GSK_HTTP_STATUS_NOT_ACCEPTABLE:
	case GSK_HTTP_STATUS_PROXY_AUTH_REQUIRED:
	case GSK_HTTP_STATUS_REQUEST_TIMEOUT:
	case GSK_HTTP_STATUS_CONFLICT:
	case GSK_HTTP_STATUS_GONE:
	case GSK_HTTP_STATUS_LENGTH_REQUIRED:
	case GSK_HTTP_STATUS_PRECONDITION_FAILED:
	case GSK_HTTP_STATUS_ENTITY_TOO_LARGE:
	case GSK_HTTP_STATUS_URI_TOO_LARGE:
	case GSK_HTTP_STATUS_UNSUPPORTED_MEDIA:
	case GSK_HTTP_STATUS_BAD_RANGE:
	case GSK_HTTP_STATUS_EXPECTATION_FAILED:
	case GSK_HTTP_STATUS_INTERNAL_SERVER_ERROR:
	case GSK_HTTP_STATUS_NOT_IMPLEMENTED:
	case GSK_HTTP_STATUS_BAD_GATEWAY:
	case GSK_HTTP_STATUS_SERVICE_UNAVAILABLE:
	case GSK_HTTP_STATUS_GATEWAY_TIMEOUT:
	case GSK_HTTP_STATUS_UNSUPPORTED_VERSION:
	  return TRUE;  /* HEAD handled above */
    }
  g_warning ("gsk_http_response_has_content_body: unknown status code %u",
             response->status_code);
  return FALSE;
}

static void
gsk_http_response_set_property  (GObject        *object,
                                 guint           property_id,
                                 const GValue   *value,
                                 GParamSpec     *pspec)
{
  GskHttpResponse *response = GSK_HTTP_RESPONSE (object);
  switch (property_id)
    {
    case PROP_RESPONSE_STATUS_CODE:
      response->status_code = g_value_get_enum (value);
      break;
    case PROP_RESPONSE_AGE:
      response->age = g_value_get_long (value);
      break;
    case PROP_RESPONSE_LOCATION:
      gsk_http_header_set_string_val (response, &response->location, value);
      break;
    case PROP_RESPONSE_EXPIRES:
      response->expires = g_value_get_long (value);
      break;
    case PROP_RESPONSE_ETAG:
      gsk_http_header_set_string_val (response, &response->etag, value);
      break;
    case PROP_RESPONSE_LAST_MODIFIED:
      response->last_modified = g_value_get_long (value);
      break;
    case PROP_RESPONSE_SERVER:
      gsk_http_header_set_string_val (response, &response->server, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gsk_http_response_get_property  (GObject        *object,
                                 guint           property_id,
                                 GValue         *value,
                                 GParamSpec     *pspec)
{
  GskHttpResponse *response = GSK_HTTP_RESPONSE (object);
  switch (property_id)
    {
    case PROP_RESPONSE_STATUS_CODE:
      g_value_set_enum (value, response->status_code);
      break;
    case PROP_RESPONSE_AGE:
      g_value_set_long (value, response->age);
      break;
#if 0
    case PROP_RESPONSE_CONTENT_ENCODING:
      g_value_set_string (value, response->content_encoding);
      break;
#endif
    case PROP_RESPONSE_LOCATION:
      g_value_set_string (value, response->location);
      break;
    case PROP_RESPONSE_EXPIRES:
      g_value_set_long (value, response->expires);
      break;
    case PROP_RESPONSE_ETAG:
      g_value_set_string (value, response->etag);
      break;
    case PROP_RESPONSE_LAST_MODIFIED:
      g_value_set_long (value, response->last_modified);
      break;
    case PROP_RESPONSE_SERVER:
      g_value_set_string (value, response->server);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gsk_http_response_finalize (GObject *object)
{
  GskHttpResponse *response = GSK_HTTP_RESPONSE (object);
  gsk_http_header_free_string (response, response->location);
  gsk_http_header_free_string (response, response->etag);
  gsk_http_header_free_string (response, response->server);
  gsk_http_header_free_string (response, response->expires_str);
  if (response->cache_control)
    {
      gsk_http_response_cache_directive_free (response->cache_control);
    }
  if (response->set_cookies)
    {
      g_slist_foreach (response->set_cookies, (GFunc) gsk_http_cookie_free, NULL);
      g_slist_free (response->set_cookies);
    }
  parent_class->finalize (object);
}

static void
gsk_http_response_init (GskHttpResponse *response)
{
  response->status_code = GSK_HTTP_STATUS_OK;
  response->age = -1;
  response->expires = (time_t) -1;
  response->last_modified = (time_t) -1;
}

static void
gsk_http_response_class_init (GObjectClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  parent_class = g_type_class_peek_parent (class);
  class->finalize = gsk_http_response_finalize;
  class->get_property = gsk_http_response_get_property;
  class->set_property = gsk_http_response_set_property;

  g_object_class_install_property (object_class,
                                   PROP_RESPONSE_STATUS_CODE,
                                   g_param_spec_enum ("status-code",
						      _("Status Code"),
						      _("HTTP status code"),
						      GSK_TYPE_HTTP_STATUS,
						      GSK_HTTP_STATUS_OK,
						      G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_RESPONSE_AGE,
                                   g_param_spec_long ("age",
						      _("Age"),
						      _("Age of content"),
						      -1, G_MAXLONG,
						      -1,
						      G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_RESPONSE_LOCATION,
                                   g_param_spec_string ("location",
						      _("Location of the content"),
						      _("The URL of the content, for redirect responses"),
						      NULL,
						      G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_RESPONSE_EXPIRES,
                                   g_param_spec_long ("expires",
						      _("expires"),
						      _("Length of time to wait for content to expire"),
						      -1, G_MAXLONG,
						      -1,
						      G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_RESPONSE_ETAG,
                                   g_param_spec_string ("e-tag",
						      _("E-Tag"),
						      _("Unique identifier for this content"),
						      NULL,
						      G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_RESPONSE_LAST_MODIFIED,
                                   g_param_spec_long ("last-modified",
						      _("Last Modified"),
						      _("Time this content was last changed"),
						      -1, G_MAXLONG,
						      -1,
						      G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_RESPONSE_SERVER,
                                   g_param_spec_string ("server",
						      _("Server"),
						      _("Name of this webserver program"),
						      NULL,
						      G_PARAM_READWRITE));
}


GType gsk_http_response_get_type()
{
  static GType http_response_type = 0;
  if (!http_response_type)
    {
      static const GTypeInfo http_response_info =
      {
        sizeof(GskHttpResponseClass),
        (GBaseInitFunc) NULL,
        (GBaseFinalizeFunc) NULL,
        (GClassInitFunc) gsk_http_response_class_init,
        NULL,           /* class_finalize */
        NULL,           /* class_data */
        sizeof (GskHttpResponse),
        8,              /* n_preallocs */
        (GInstanceInitFunc) gsk_http_response_init,
        NULL            /* value_table */
      };
      http_response_type = g_type_register_static (GSK_TYPE_HTTP_HEADER,
                                                   "GskHttpResponse",
                                                   &http_response_info,
                                                   0);
    }
  return http_response_type;
}

/* --- public methods --- */
/**
 * gsk_http_response_set_cache_control:
 * @response: the HTTP response header to affect.
 * @directive: the new cache control directive, stolen by the HTTP header.
 * 
 * Set the cache-control flags in the header.
 * Note that @directive will be freed by the HTTP header when
 * it is destroyed.
 */
void
gsk_http_response_set_cache_control (GskHttpResponse *response,
				     GskHttpResponseCacheDirective *directive)
{
  if (response->cache_control)
    gsk_http_response_cache_directive_free (response->cache_control);
  response->cache_control = directive;
}

/**
 * gsk_http_response_set_md5:
 * @response: the HTTP response whose content MD5 sum is being given.
 * @md5sum: the 16-byte binary value of the MD5 hash of the content.
 * If this is NULL, unset the MD5 field.
 *
 * Set the MD5 field of the HTTP response to the given @md5sum.
 * GSK does not attempt to verify the MD5 sum;
 * the caller should be sure they have the right MD5 sum.
 */
void
gsk_http_response_set_md5      (GskHttpResponse *response,
			        const guint8    *md5sum)
{
  if (md5sum == NULL)
    {
      response->has_md5sum = 0;
    }
  else
    {
      memcpy (response->md5sum, md5sum, 16);
      response->has_md5sum = 1;
    }
}

/**
 * gsk_http_response_peek_md5:
 * @response: the response to query.
 * 
 * Get the MD5 sum associated with this response.
 * (It should be the MD5 sum of the content stream).
 *
 * It will return NULL if there is no associated content MD5 sum.
 * This is the default state.
 *
 * returns: the MD5 checksum (as 16 binary bytes) or NULL.
 */
const guint8 *
gsk_http_response_peek_md5     (GskHttpResponse *response)
{
  return response->has_md5sum ? response->md5sum : NULL;
}

/**
 * gsk_http_response_set_retry_after:
 * @response: the response to set a Retry-After line in.
 * @is_relative: whether the time should be relative to the current time (in which case it is
 * printed as a raw integer), or whether it is an absolute time (in which case it is printed
 * in the standard date format.)
 * @time: the time in seconds.  If is_relative, then its the number of seconds after
 * the current time; otherwise, it's unix time (ie seconds after Jan 1 1970, GMT).
 *
 * Set the Retry-After header for this response.
 * 
 * [From RFC 2616, Section 14.37]
 * The Retry-After response-header field can be used with a 503 (Service
 * Unavailable) response to indicate how long the service is expected to
 * be unavailable to the requesting client. This field MAY also be used
 * with any 3xx (Redirection) response to indicate the minimum time the
 * user-agent is asked wait before issuing the redirected request.
 */
void       gsk_http_response_set_retry_after   (GskHttpResponse *response,
                                                gboolean         is_relative,
                                                glong            time)
{
  response->has_retry_after = 1;
  response->retry_after_relative = is_relative;
  response->retry_after = time;
}

/**
 * gsk_http_response_set_no_retry_after:
 * @response: the response to clear the Retry-After line from.
 *
 * Clear the Retry-After header for this response.
 */
void       gsk_http_response_set_no_retry_after(GskHttpResponse *response)
{
  response->has_retry_after = 0;
}

/**
 * gsk_http_response_set_authenticate:
 * @response: the response to adjust the authentication for.
 * @is_proxy_auth: whether to set the Proxy-Authorization or the Authorization field.
 * @auth: the new authentication to use in this response.
 *
 * Set the authentication for this response.
 * This is like a key to get access to certain entities.
 *
 * Proxy-Authorization is intended to provide access control to the proxy.
 * Normal Authorization is passed through a proxy.
 * See sections 14.8 for normal Authorization and 14.34.
 */
void
gsk_http_response_set_authenticate       (GskHttpResponse  *response,
					  gboolean         is_proxy_auth,
					  GskHttpAuthenticate *auth)
{
  GskHttpAuthenticate **dst_auth = is_proxy_auth
                                 ? &response->proxy_authenticate
                                 : &response->authenticate;

  if (auth)
    gsk_http_authenticate_ref (auth);
  if (*dst_auth != NULL)
    gsk_http_authenticate_unref (*dst_auth);

  *dst_auth = auth;
}

/**
 * gsk_http_response_peek_authenticate:
 * @response: the response to query.
 * @is_proxy_auth: whether to query information about proxy authentication,
 * or normal server authentication.
 *
 * Get the responseed authentication information.
 *
 * returns: the authentication information, or NULL if none exists (default).
 */
GskHttpAuthenticate *
gsk_http_response_peek_authenticate      (GskHttpResponse  *response,
					  gboolean    is_proxy_auth)
{
  return is_proxy_auth ? response->proxy_authenticate : response->authenticate;
}

/**
 * gsk_http_response_set_allowed_verbs:
 * @response: the response to affect.
 * @allowed: bits telling which type of requests this server allows.
 * The bit for a verb 'Q' is (1 &lt;&lt; GSK_HTTP_VERB_Q);
 * the default allowed bits are (1&lt;&lt;GSK_HTTP_VERB_GET) | (1&lt;&lt;GSK_HTTP_VERB_HEAD).
 *
 * Set the Allow: header to indicate a particular set of HTTP verbs
 * are allowed from the client.  If you give a request with
 * a verb which is not allowed, you should get a GSK_HTTP_STATUS_METHOD_NOT_ALLOWED response.
 * That response MUST have an Allow: header.
 */
void       gsk_http_response_set_allowed_verbs  (GskHttpResponse *response,
                                                 guint            allowed)
{
  response->allowed_verbs = allowed;
}

/* --- standard header constructors --- */

/* Responses. */
/**
 * gsk_http_response_new_blank:
 * 
 * Create a new, empty default response.
 *
 * returns: the newly allocated response.
 */
GskHttpResponse *gsk_http_response_new_blank    (void)
{
  return g_object_new (GSK_TYPE_HTTP_RESPONSE, NULL);
}

/**
 * gsk_http_response_new_redirect:
 * @location: the URL to redirect the client to, as a string.
 *
 * Create a redirection HTTP response header.
 *
 * We use the 302 ("Found") status code.
 *
 * returns: the newly allocated header.
 */
GskHttpResponse *
gsk_http_response_new_redirect (const char    *location)
{
  GskHttpResponse *response;
  response = gsk_http_response_new_blank ();
  response->status_code = GSK_HTTP_STATUS_FOUND;	/* 302 */
  gsk_http_response_set_location (response, location);
  return response;
}

/* --- responding to a request (made easy) --- */
/**
 * gsk_http_response_from_request:
 * @request: the client's request to match.
 * @status_code: the return status of the request.
 * @length: the length of the message, or -1 for unknown.
 *
 * Create a response which macthes the request,
 * with a caller-supplied status code and optional length.
 *
 * returns: a new, matching response.
 */
GskHttpResponse  *
gsk_http_response_from_request (GskHttpRequest *request,
			        GskHttpStatus   status_code,
				gint64          length)
{
  GskHttpHeader *header_request = request ? GSK_HTTP_HEADER (request) : NULL;
  GskHttpResponse *response = gsk_http_response_new_blank ();
  GskHttpHeader *header_response = GSK_HTTP_HEADER (response);
  response->status_code = status_code;
  header_response->content_length = length;

  if (request == NULL)
    {
      gsk_http_header_set_version (header_response, 1, 0);
    }
  else
    {
      header_response->connection_type = header_request->connection_type;
      gsk_http_header_set_version (header_response,
				   header_request->http_major_version,
				   header_request->http_minor_version);
    }

  if (length < 0)
    {
      if (   request != NULL
          && header_request->http_minor_version >= 1
          && status_code == GSK_HTTP_STATUS_OK)
        header_response->transfer_encoding_type = GSK_HTTP_TRANSFER_ENCODING_CHUNKED;
      else
        header_response->connection_type = GSK_HTTP_CONNECTION_CLOSE;
    }

  return response;
}
