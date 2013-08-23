#include "gskhttpheader.h"
#include "gskhttprequest.h"
#include "../gskmacros.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include "../gskerror.h"

static GObjectClass *parent_class = NULL;

enum
{
  PROP_REQUEST_0,
  PROP_REQUEST_VERB,
  PROP_REQUEST_IF_MODIFIED_SINCE,
  PROP_REQUEST_USER_AGENT,
  PROP_REQUEST_PATH,
  PROP_REQUEST_REFERRER,
  PROP_REQUEST_HOST,
  PROP_REQUEST_MAX_FORWARDS,
  PROP_REQUEST_FROM
};


/* --- GskHttpRequest implementation --- */
static void
gsk_http_request_set_property   (GObject        *object,
                                 guint           property_id,
                                 const GValue   *value,
                                 GParamSpec     *pspec)
{
  GskHttpRequest *request = GSK_HTTP_REQUEST (object);
  switch (property_id)
    {
    case PROP_REQUEST_VERB:
      request->verb = g_value_get_enum (value);
      break;
    case PROP_REQUEST_IF_MODIFIED_SINCE:
      request->if_modified_since = g_value_get_long (value);
      break;
    case PROP_REQUEST_USER_AGENT:
      gsk_http_header_set_string_val (request, &request->user_agent, value);
      break;
    case PROP_REQUEST_PATH:
      gsk_http_header_set_string_val (request, &request->path, value);
      break;
    case PROP_REQUEST_REFERRER:
      gsk_http_header_set_string_val (request, &request->referrer, value);
      break;
    case PROP_REQUEST_HOST:
      gsk_http_header_set_string_val (request, &request->host, value);
      break;
    case PROP_REQUEST_MAX_FORWARDS:
      request->max_forwards = g_value_get_int (value);
      break;
    case PROP_REQUEST_FROM:
      gsk_http_header_set_string_val (request, &request->from, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gsk_http_request_get_property   (GObject        *object,
                                 guint           property_id,
                                 GValue         *value,
                                 GParamSpec     *pspec)
{
  GskHttpRequest *request = GSK_HTTP_REQUEST (object);
  switch (property_id)
    {
    case PROP_REQUEST_VERB:
      g_value_set_enum (value, request->verb);
      break;
    case PROP_REQUEST_IF_MODIFIED_SINCE:
      g_value_set_long (value, request->if_modified_since);
      break;
    case PROP_REQUEST_USER_AGENT:
      g_value_set_string (value, request->user_agent);
      break;
    case PROP_REQUEST_REFERRER:
      g_value_set_string (value, request->referrer);
      break;
    case PROP_REQUEST_PATH:
      g_value_set_string (value, request->path);
      break;
    case PROP_REQUEST_HOST:
      g_value_set_string (value, request->host);
      break;
    case PROP_REQUEST_MAX_FORWARDS:
      g_value_set_int (value, request->max_forwards);
      break;
    case PROP_REQUEST_FROM:
      g_value_set_string (value, request->from);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gsk_http_request_finalize (GObject *object)
{
  GskHttpRequest *request = GSK_HTTP_REQUEST (object);

#define FREE_LIST(Class, free_func, member)                             \
  G_STMT_START{                                                         \
    Class *at, *next;                                                   \
    for (at = request->member; at != NULL; at = next)                   \
      {                                                                 \
        next = at->next;                                                \
        free_func (at);                                                 \
      }                                                                 \
  }G_STMT_END
  FREE_LIST (GskHttpCharSet, gsk_http_char_set_free, accept_charsets);
  FREE_LIST (GskHttpContentEncodingSet, gsk_http_content_encoding_set_free, accept_content_encodings);
  FREE_LIST (GskHttpTransferEncodingSet, gsk_http_transfer_encoding_set_free, accept_transfer_encodings);
  FREE_LIST (GskHttpMediaTypeSet, gsk_http_media_type_set_free, accept_media_types);
  FREE_LIST (GskHttpLanguageSet, gsk_http_language_set_free, accept_languages);
#undef FREE_LIST

  g_free (request->path);
  g_free (request->host);
  if (request->had_if_match)
    g_strfreev (request->if_match);
  gsk_http_header_free_string (request, request->user_agent);
  gsk_http_header_free_string (request, request->referrer);
  gsk_http_header_free_string (request, request->from);
  if (request->authorization)
    gsk_http_authorization_unref (request->authorization);
  if (request->proxy_authorization)
    gsk_http_authorization_unref (request->proxy_authorization);
  if (NULL != request->cache_control)
    {
      gsk_http_request_cache_directive_free (request->cache_control);
    }
  g_free (request->ua_color);
  g_free (request->ua_os);
  g_free (request->ua_cpu);
  g_free (request->ua_language);
  g_slist_foreach (request->cookies, (GFunc) gsk_http_cookie_free, NULL);
  g_slist_free (request->cookies);

  parent_class->finalize (object);
}

static void
gsk_http_request_init (GskHttpRequest *request)
{
  request->verb = GSK_HTTP_VERB_GET;
  request->if_modified_since = (time_t) -1;
  request->max_forwards = -1;
  request->keep_alive_seconds = -1;
}

static void
gsk_http_request_class_init (GObjectClass *class)
{
  parent_class = g_type_class_peek_parent (class);
  class->finalize = gsk_http_request_finalize;
  class->get_property = gsk_http_request_get_property;
  class->set_property = gsk_http_request_set_property;
  g_object_class_install_property (class,
                                   PROP_REQUEST_VERB,
                                   g_param_spec_enum ("verb",
                                                      _("Verb"),
                                                      _("verb"),
                                                      GSK_TYPE_HTTP_VERB,
                                                      GSK_HTTP_VERB_GET,
                                                      G_PARAM_READWRITE));
  g_object_class_install_property (class,
                                   PROP_REQUEST_IF_MODIFIED_SINCE,
                                   g_param_spec_long ("if-modified-since",
                                                      _("If-Modified-Since"),
                                                      _("IMS tag"),
                                                      -1, G_MAXLONG, -1,
                                                      G_PARAM_READWRITE));
  g_object_class_install_property (class,
                                   PROP_REQUEST_USER_AGENT,
                                   g_param_spec_string ("user-agent",
                                                        _("User-Agent"),
                                                        _("User Agent"),
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (class,
                                   PROP_REQUEST_PATH,
                                   g_param_spec_string ("path",
                                                        _("Path"),
                                                        _("Path"),
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (class,
                                   PROP_REQUEST_REFERRER,
                                   g_param_spec_string ("referrer",
                                                        _("Referrer"),
                                                        _("Referrer"),
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (class,
                                   PROP_REQUEST_HOST,
                                   g_param_spec_string ("host",
                                                        _("Host"),
                                                        _("Hostname"),
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (class,
                                   PROP_REQUEST_MAX_FORWARDS,
                                   g_param_spec_long ("max-forwards",
                                                      _("Max-Forwards"),
                                                      _("IMax-Forwards"),
                                                      -1, 32, -1,
                                                      G_PARAM_READWRITE));
}

GType gsk_http_request_get_type()
{
  static GType http_request_type = 0;
  if (!http_request_type)
    {
      static const GTypeInfo http_request_info =
      {
        sizeof(GskHttpRequestClass),
        (GBaseInitFunc) NULL,
        (GBaseFinalizeFunc) NULL,
        (GClassInitFunc) gsk_http_request_class_init,
        NULL,           /* class_finalize */
        NULL,           /* class_data */
        sizeof (GskHttpRequest),
        8,              /* n_preallocs */
        (GInstanceInitFunc) gsk_http_request_init,
        NULL            /* value_table */
      };
      http_request_type = g_type_register_static (GSK_TYPE_HTTP_HEADER,
                                                  "GskHttpRequest",
                                                  &http_request_info,
                                                  0);
    }
  return http_request_type;
}

/* --- standard constructors --- */
/* Requests. */
/**
 * gsk_http_request_new_blank:
 * 
 * Create a new empty HTTP request.
 *
 * returns: the new request.
 */
GskHttpRequest *gsk_http_request_new_blank    (void)
{
  return g_object_new (GSK_TYPE_HTTP_REQUEST, NULL);
}

/**
 * gsk_http_request_new:
 * @verb: what type of request to make.
 * @path: path requested.
 * 
 * Create a new simple HTTP request.
 *
 * returns: the new request.
 */
GskHttpRequest *
gsk_http_request_new           (GskHttpVerb  verb,
			        const char  *path)
{
  return g_object_new (GSK_TYPE_HTTP_REQUEST,
		       "verb", verb,
		       "path", path, NULL);
}

/* GskHttpRequest public methods */

/**
 * gsk_http_request_set_cache_control:
 * @request: the HTTP request header to affect.
 * @directive: the new cache control directive, stolen by the HTTP header.
 * 
 * Set the cache-control flags in the header.
 * Note that @directive will be freed by the HTTP header when
 * it is destroyed.
 */
void
gsk_http_request_set_cache_control (GskHttpRequest *request,
				    GskHttpRequestCacheDirective *directive)
{
  if (NULL != request->cache_control) 
    {
      gsk_http_request_cache_directive_free (request->cache_control);
    }
  request->cache_control = directive;
}

/**
 * gsk_http_request_has_content_body:
 * @request: the request to query.
 *
 * Get whether this header should be accompanied by
 * a content-body.
 */
gboolean
gsk_http_request_has_content_body (GskHttpRequest *request)
{
  switch (request->verb)
    {
    case GSK_HTTP_VERB_GET:
    case GSK_HTTP_VERB_HEAD:
    case GSK_HTTP_VERB_OPTIONS:
    case GSK_HTTP_VERB_DELETE:
    case GSK_HTTP_VERB_CONNECT:	//???
    case GSK_HTTP_VERB_TRACE:	//???
      return FALSE;
    case GSK_HTTP_VERB_POST:
    case GSK_HTTP_VERB_PUT:
      return TRUE;
      break;
    }
  g_warning ("unrecognized HTTP verb %u", request->verb);
  return FALSE;
}

/**
 * gsk_http_request_add_charsets:
 * @header: the request to affect.
 * @char_sets: list of a #GskHttpCharSet's to indicate are accepted.
 *
 * Add Accept-CharSet headers to the header.
 * The char-sets will be freed when @header
 * is destroyed.
 *
 * A CharSet is a string representing a Character Set,
 * and an optional real quality factor for that particular Character Set.
 * (The default is 1.0)
 *
 * The CharSet "*" matches all other Character Sets.
 * If no "*" is given, then it is as though all other character sets
 * were given a quality of 0.0.
 *
 * If no Accept-CharSet header is given, then all character sets
 * are equally acceptable.
 *
 * If a server cannot meet character set requirements,
 * it SHOULD response with an error (#GSK_HTTP_STATUS_NOT_ACCEPTABLE)
 * but it may also ignore it, and send output in a character-set
 * the client has not suggested.
 *
 * See RFC 2616, Section 14.2.
 */
void
gsk_http_request_add_charsets   (GskHttpRequest *header,
				 GskHttpCharSet *char_sets)
{
  GskHttpCharSet *last = header->accept_charsets;
  if (last == NULL)
    {
      header->accept_charsets = char_sets;
      return;
    }
  while (last->next != NULL)
    last = last->next;
  last->next = char_sets;
}

/**
 * gsk_http_request_clear_charsets:
 * @header: the request to affect.
 *
 * Delete all accepted char-sets from the HTTP request.
 *
 * This has the effect of leaving a server free to use any
 * character set.
 */
void
gsk_http_request_clear_charsets (GskHttpRequest *header)
{
  GskHttpCharSet *set = header->accept_charsets;
  header->accept_charsets = NULL;
  while (set != NULL)
    {
      GskHttpCharSet *next = set->next;
      gsk_http_char_set_free (set);
      set = next;
    }
}

/**
 * gsk_http_request_add_content_encodings:
 * @header: the request to affect.
 * @set: list of a #GskHttpContentEncodingSet's to indicate are acceptable.
 *   The list is taken over by the header;
 *   you must not free it or use it further.
 *
 * Add Accept-Encoding lines to the header.
 * Each GskHttpContentEncodingSet represents a single
 * possible encoding and an optional associated
 * quality factor.
 *
 * The rules for conduct are the same as for character set,
 * with the exception that if no Accept-Encoding line
 * is given then the 'identity' encoding should be preferred.
 *
 * Note that the GSK http server and client handle content
 * encoding automatically, and will do the correct thing
 * without your intervention.
 *
 * See RFC 2616, Section 14.3.
 */
void
gsk_http_request_add_content_encodings  (GskHttpRequest *header,
				         GskHttpContentEncodingSet *set)
{
  GskHttpContentEncodingSet *last = header->accept_content_encodings;
  if (last == NULL)
    {
      header->accept_content_encodings = set;
      return;
    }
  while (last->next != NULL)
    last = last->next;
  last->next = set;
}

/**
 * gsk_http_request_clear_content_encodings:
 * @header: the request to affect.
 *
 * Delete all accepted encodings from the HTTP request.
 *
 * This has the effect of leaving a server free to use any
 * content encoding.
 */
void
gsk_http_request_clear_content_encodings(GskHttpRequest *header)
{
  GskHttpContentEncodingSet *set = header->accept_content_encodings;
  header->accept_content_encodings = NULL;
  while (set != NULL)
    {
      GskHttpContentEncodingSet *next = set->next;
      gsk_http_content_encoding_set_free (set);
      set = next;
    }
}

/**
 * gsk_http_request_add_transfer_encodings:
 * @header: the request to affect.
 * @set: list of a #GskHttpTransferEncodingSet's to indicate are acceptable.
 *   The list is taken over by the header;
 *   you must not free it or use it further.
 *
 * The rules for conduct are the same as for character set,
 * with the exception that the defaults are
 * 'none' for HTTP 1.0 clients,
 * and 'none' and 'chunked' for HTTP 1.1 clients.
 *
 * Note that the GSK http server and client handle content
 * encoding automatically, and will do the correct thing
 * without your intervention.
 *
 * This corresponds to the TE: header.
 * See RFC 2616, Section 14.39.
 */
void
gsk_http_request_add_transfer_encodings  (GskHttpRequest *header,
				          GskHttpTransferEncodingSet *set)
{
  GskHttpTransferEncodingSet *last = header->accept_transfer_encodings;
  if (last == NULL)
    {
      header->accept_transfer_encodings = set;
      return;
    }
  while (last->next != NULL)
    last = last->next;
  last->next = set;
}

/**
 * gsk_http_request_clear_transfer_encodings:
 * @header: the request to affect.
 *
 * Delete all accepted transfer encodings from the HTTP request.
 *
 * This has the effect of leaving a server free to use just
 * no encoding for HTTP 1.0 clients and also 'chunked' for HTTP 1.1 clients.
 */
void
gsk_http_request_clear_transfer_encodings(GskHttpRequest *header)
{
  GskHttpTransferEncodingSet *set = header->accept_transfer_encodings;
  header->accept_transfer_encodings = NULL;
  while (set != NULL)
    {
      GskHttpTransferEncodingSet *next = set->next;
      gsk_http_transfer_encoding_set_free (set);
      set = next;
    }
}


/**
 * gsk_http_request_add_media:
 * @header: the request to affect.
 * @set: list of a #GskHttpMediaTypeSet's to indicate are acceptable.
 *
 * Add Accept: headers to the header.
 * The media-type-sets will be freed when @header
 * is destroyed.
 *
 * A MediaSet is a range of media accepted,
 * with quality factors as for gsk_http_request_add_charsets().
 *
 * Note that '*' in a subtype applies to all
 * media with that major type, but
 * if a specific subtype matches, then it's
 * quality is given priority.
 *
 * XXX: Also, there is an accept-extension 'level='...
 * find out what it does!
 *
 * See RFC 2616, Section 14.1.
 */
void
gsk_http_request_add_media      (GskHttpRequest *header,
				 GskHttpMediaTypeSet *set)
{
  GskHttpMediaTypeSet *last = header->accept_media_types;
  if (last == NULL)
    {
      header->accept_media_types = set;
      return;
    }
  while (last->next != NULL)
    last = last->next;
  last->next = set;
}

/**
 * gsk_http_request_clear_media:
 * @header: the request to affect.
 *
 * Delete all accepted media-type-sets from the HTTP request.
 */
void
gsk_http_request_clear_media    (GskHttpRequest *header)
{
  GskHttpMediaTypeSet *set = header->accept_media_types;
  header->accept_media_types = NULL;
  while (set != NULL)
    {
      GskHttpMediaTypeSet *next = set->next;
      gsk_http_media_type_set_free (set);
      set = next;
    }
}

/**
 * gsk_http_request_set_authorization:
 * @request: the request to adjust the authorization for.
 * @is_proxy_auth: whether to set the Proxy-Authorization or the Authorization field.
 * @auth: the new authorization to use in this request.
 *
 * Set the authorization for this request.
 * This is like a key to get access to certain entities.
 *
 * Proxy-Authorization is intended to provide access control to the proxy.
 * Normal Authorization is passed through a proxy.
 * See sections 14.8 for normal Authorization and 14.34.
 */
void
gsk_http_request_set_authorization       (GskHttpRequest  *request,
					  gboolean         is_proxy_auth,
					  GskHttpAuthorization *auth)
{
  GskHttpAuthorization **dst_auth = is_proxy_auth ? &request->proxy_authorization : &request->authorization;
  if (auth)
    gsk_http_authorization_ref (auth);
  if (*dst_auth)
    gsk_http_authorization_unref (*dst_auth);
  *dst_auth = auth;
}

/**
 * gsk_http_request_peek_authorization:
 * @request: the request to query.
 * @is_proxy_auth: whether to query information about proxy authorization,
 * or normal server authorization.
 *
 * Get the requested authorization information.
 *
 * returns: the authorization information, or NULL if none exists (default).
 */
GskHttpAuthorization *
gsk_http_request_peek_authorization      (GskHttpRequest  *request,
					  gboolean    is_proxy_auth)
{
  return is_proxy_auth ? request->proxy_authorization : request->authorization;
}

/**
 * gsk_http_request_add_cookie:
 * @header: the request to affect.
 * @cookie: the cookie to add to the request.
 * It will be freed when the request is freed.
 *
 * Add a Cookie header line to a request.
 *
 * Cookies are defined in RFC 2965, a draft standard.
 *
 * [Section 3.3.4]  Sending Cookies to the Origin Server.
 * When [the user-agent] sends a request
 * to an origin server, the user agent includes a Cookie request header
 * if it has stored cookies that are applicable to the request, based on
 * (1) the request-host and request-port;
 * (2) the request-URI;
 * (3) the cookie's age.
 */
void
gsk_http_request_add_cookie      (GskHttpRequest *header,
				  GskHttpCookie  *cookie)
{
  header->cookies = g_slist_append (header->cookies, cookie);
}

/**
 * gsk_http_request_remove_cookie:
 * @header: the request to affect.
 * @cookie: the cookie to remove from the request.
 *
 * Remove a cookie from the request's list and delete it.
 */
void
gsk_http_request_remove_cookie   (GskHttpRequest *header,
				  GskHttpCookie  *cookie)
{
  g_return_if_fail (g_slist_find (header->cookies, cookie) != NULL);
  header->cookies = g_slist_remove (header->cookies, cookie);
  gsk_http_cookie_free (cookie);
}

/**
 * gsk_http_request_find_cookie:
 * @header: the request to query.
 * @key: the key field of the cookie to return.
 *
 * Find a cookie provided in the request by key.
 *
 * returns: a pointer to the cookie, or NULL if not found.
 */
GskHttpCookie  *
gsk_http_request_find_cookie     (GskHttpRequest *header,
				  const char     *key)
{
  GSList *at;
  for (at = header->cookies; at != NULL; at = at->next)
    {
      GskHttpCookie *cookie = at->data;
      if (strcmp (cookie->key, key) == 0)
	return cookie;
    }
  return NULL;
}

/* TODO: make more efficient */
static char *unescape_cgi (const char *q, GError **error)
{
  GString *str = g_string_new ("");
  while (*q)
    {
      if (*q == '%' && q[1] != 0 && q[2] != 0)
        {
          char hex[3] = { q[1], q[2], 0 };
          guint c = strtoul (hex, NULL, 16);
          g_string_append_c (str, c);
          q  += 3;
        }
      else if (*q == '+')
        {
          g_string_append_c (str, ' ');
          q++;
        }
      else
        g_string_append_c (str, *q++);
    }
  return g_string_free (str, FALSE);
}

static char *
unescape_cgi_n (const char *q, guint len, GError **error)
{
  char *rv = g_malloc (len + 1);
  char *at = rv;
  while (len > 0)
    {
      if (*q == '%')
        {
          char hex[3];
          if (len < 3)
            {
              g_set_error (error, GSK_G_ERROR_DOMAIN,
                           GSK_ERROR_HTTP_PARSE,
                           "'%%' string was too short in query value");
              g_free (rv);
              return NULL;
            }
          hex[0] = q[1];
          hex[1] = q[2];
          hex[2] = 0;
          *at++ = strtoul (hex, NULL, 16);
          q += 3;
          len -= 3;
        }
      else if (*q == '+')
        {
          *at++ = ' ';
          q++;
          len--;
        }
      else
        {
          *at++ = *q++;
          len--;
        }
    }
  *at = 0;
  return rv;
}


GHashTable *
gsk_http_request_parse_cgi_query_string  (const char     *query_string)
{
  char **cgi_vars = gsk_http_parse_cgi_query_string (query_string, NULL);
  GHashTable *table;
  guint i;
  if (cgi_vars == NULL)
    return NULL;
  table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  for (i = 0; cgi_vars[2*i] != NULL; i++)
    g_hash_table_insert (table, cgi_vars[2*i+0], cgi_vars[2*i+1]);
  g_free (cgi_vars);
  return table;
}

/**
 * gsk_http_parse_cgi_query_string:
 * @query_string: the full path from the HttpRequest.
 * @error: the place to put the error if something goes wrong.
 * returns: the key-value pairs of CGI data, NULL-terminated.
 *
 * Parse the CGI key-value pairs from a query.
 * The keys are normal alphanumeric strings;
 * the values are de-escaped.
 */
char **
gsk_http_parse_cgi_query_string  (const char     *query_string,
                                  GError        **error)
{
  const char *q = strchr (query_string, '?');
  const char *at;
  guint amp_count;
  guint i;
  guint n_pieces;
  char **rv;
  guint n_out = 0;
  if (q == NULL)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_HTTP_PARSE,
                   "no '?' found in CGI query string");
      return NULL;
    }

  amp_count = 0;
  at = q + 1;
  while (at)
    {
      at = strchr (at, '&');

      if (at == NULL)
        break;

      /* skip extra ampersands */
      while (*(at+1) == '&')
	at++;

      amp_count++;
      at++;
    }

  n_pieces = amp_count + 1;
  rv = g_new (char *, n_pieces * 2 + 1);
  at = q + 1;
  for (i = 0; i < n_pieces; i++)
    {
      const char *equalsat = at;
      const char *amp;
      equalsat = at;
      while (*equalsat != '=')
        {
          if (*equalsat == '&' || *equalsat == 0)
            {
              rv[2*n_out] = NULL;
              g_strfreev (rv);
              g_set_error (error, GSK_G_ERROR_DOMAIN,
                           GSK_ERROR_HTTP_PARSE,
                           "error parsing '=' query string cgi pairs");
              return FALSE;
            }
          equalsat++;
        }
      amp = strchr (equalsat + 1, '&');

      rv[n_out*2+0] = g_strndup (at, equalsat - at);
      if (amp != NULL)
        rv[n_out*2+1] = unescape_cgi_n (equalsat + 1, amp - (equalsat + 1), error);
      else
        rv[n_out*2+1] = unescape_cgi (equalsat + 1, error);
      if (rv[n_out*2+1] == NULL)
        {
          g_strfreev (rv);
          return NULL;
        }
      if (amp != NULL) 
	{
	  /* skip extra ampersands */
	  while (*(amp+1) == '&')
	    amp++;
	}
      at = amp ? amp + 1 : NULL;
      n_out++;
    }
  rv[2*n_out+0] = NULL;
  return rv;
}
