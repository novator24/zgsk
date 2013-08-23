#include "gskhttpheader.h"
#include "gskhttprequest.h"
#include "gskhttpresponse.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>		/* for sscanf */
#include "../gskmacros.h"
#include "../gskerror.h"
#include "../common/gskdate.h"
#include "../common/gskbase64.h"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN    "Gsk-Http-Parser"

/* TODO: unified set of parsing macros for http-header-ish stuff,
   similar to those used in the authenticate/authorization code
   (which is in this file, search for CUT_TOKEN) */
/* TODO: GError-style error handling in line-handlers */
/* TODO: multiline header-lines */

/* === input: reading a header from an output source === */

/* command: handle first line of Http Request */

/**
 * gsk_http_request_parse_first_line:
 * @request: request to initialize
 * @line: first line of request header, e.g. GET / HTTP/1.0
 *
 * Parse the first line of an HTTP request.
 *
 * returns: whether the line was the start of a valid HTTP-request.
 */
GskHttpRequestFirstLineStatus
gsk_http_request_parse_first_line (GskHttpRequest *request,
				   const char     *line,
                                   GError        **error)
{
  GskHttpHeader *header = GSK_HTTP_HEADER (request);
  int verb_length = 0;
  int request_start, request_length;
  int at;
  while (line[verb_length] != 0 && isalpha (line[verb_length]))
    verb_length++;
  /* The verbs are `PUT', `POST', `GET', `HEAD'. */
  if (verb_length < 3 || verb_length > 4)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_HTTP_PARSE,
                   "request first-line: verb length is bad (%d) (line=\"%s\")", verb_length, line);
      return GSK_HTTP_REQUEST_FIRST_LINE_ERROR;
    }
  if (verb_length == 3 && g_strncasecmp (line, "PUT", 3) == 0)
    request->verb = GSK_HTTP_VERB_PUT;
  else if (verb_length == 3 && g_strncasecmp (line, "GET", 3) == 0)
    request->verb = GSK_HTTP_VERB_GET;
  else if (verb_length == 4 && g_strncasecmp (line, "POST", 4) == 0)
    request->verb = GSK_HTTP_VERB_POST;
  else if (verb_length == 4 && g_strncasecmp (line, "HEAD", 4) == 0)
    request->verb = GSK_HTTP_VERB_HEAD;
  else
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_HTTP_PARSE,
                   "parsing HTTP header: bad verb: `%s'", line);
      return GSK_HTTP_REQUEST_FIRST_LINE_ERROR;
    }

  /* Parse the request_path. */
  at = verb_length;
  while (line[at] && isspace (line[at]))
    at++;
  request_start = at;

  if (at == verb_length)
    {
      if (line[at] == 0)
        g_set_error (error, GSK_G_ERROR_DOMAIN,
                     GSK_ERROR_HTTP_PARSE,
                     "parsing HTTP header: no request path: `%s'", line);
      else
        g_set_error (error, GSK_G_ERROR_DOMAIN,
                     GSK_ERROR_HTTP_PARSE,
                    "parsing HTTP header: garbage between HTTP VERB and request path: `%s'",
                    line);
      return GSK_HTTP_REQUEST_FIRST_LINE_ERROR;
    }

  /* Assumption:  the request path is the longest possible block
   *              of non-whitespace characters. */
  while (line[at] && !isspace (line[at]))
    at++;

  request_length = at - request_start;
  if (request_length == 0)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_HTTP_PARSE,
                   "warning parsing HTTP header: empty request path: `%s'", line);
      return GSK_HTTP_REQUEST_FIRST_LINE_ERROR;
    }

  g_free (request->path);
  request->path = g_new (char, request_length + 1);
  memcpy (request->path, line + request_start, request_length);
  request->path[request_length] = '\0';

  header->http_minor_version = 0;
  while (line[at] && (line[at] == ' ' || line[at] == '\t'))
    at++;

  /* TODO: return error if there is trailing garbage */

  if (g_ascii_strncasecmp (line + at, "HTTP/", 5) == 0 && isdigit (line[at + 5]))
    {
      const char *dot = strchr (line + at + 5, '.');
      header->http_major_version = atoi (line + at + 5);
      if (dot)
        header->http_minor_version = atoi (dot + 1);
      return GSK_HTTP_REQUEST_FIRST_LINE_FULL;
    }
  else
    {
      return GSK_HTTP_REQUEST_FIRST_LINE_SIMPLE;
    }
}

/**
 * gsk_http_response_process_first_line:
 * @response: response to initialize
 * @line: first line of response header.
 *
 * Parse the first line of an HTTP response.
 *
 * returns: whether the line was the start of a valid HTTP-response.
 */
gboolean
gsk_http_response_process_first_line (GskHttpResponse *response,
                                      const char      *line)
{
  GskHttpHeader *header = GSK_HTTP_HEADER (response);
  while (*line && isspace (*line))
    line++;
  if (g_strncasecmp (line, "http/", 5) != 0)
    {
      g_set_error (&header->g_error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_HTTP_PARSE,
                   "from server: response `%s' didn't begin with http/", line);
      return FALSE;
    }
  line += 5;
  if (*line != '1')
    {
      g_set_error (&header->g_error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_HTTP_PARSE,
                   "from server: got header starting with `http/%c'", *line);
      return FALSE;
    }
  line++;
  if (*line != '.')
    {
      g_set_error (&header->g_error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_HTTP_PARSE,
                   "from server: got header starting with `http/1%c'", *line);
      return FALSE;
    }
  line++;
  header->http_minor_version = atoi (line);
  while (*line && isdigit (*line))
    line++;
  while (*line && isspace (*line))
    line++;
  if (!isdigit (*line))
    {
      g_set_error (&header->g_error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_HTTP_PARSE,
                   "from server: got header without status code");
      return FALSE;
    }
  response->status_code = atoi (line);


  /*
   * Ignore the textual description of the status code.
   */
  return TRUE;
}

static gboolean
parse_uint (GskHttpHeader *header,
	    const char    *value,
            gpointer       data)
{
  guint offset = GPOINTER_TO_UINT (data);
  G_STRUCT_MEMBER (guint, header, offset) = strtoul (value, NULL, 10);
  return TRUE;
}

static gboolean
parse_uint64 (GskHttpHeader *header,
	      const char    *value,
              gpointer       data)
{
  guint offset = GPOINTER_TO_UINT (data);
  G_STRUCT_MEMBER (guint64, header, offset) = g_ascii_strtoull (value, NULL, 10);
  return TRUE;
}

static gboolean
parse_date   (GskHttpHeader *header,
              const char    *value,
              gpointer       data)
{
  guint offset = GPOINTER_TO_UINT (data);
  time_t t;
  if (!gsk_date_parse_timet (value, &t, GSK_DATE_FORMAT_HTTP))
    {
      return FALSE;
    }
  G_STRUCT_MEMBER (time_t, header, offset) = t;
  return TRUE;
}


static gboolean
parse_string (GskHttpHeader *header,
	      const char    *value,
              gpointer       data)
{
  guint offset = GPOINTER_TO_UINT (data);
  char **p_str = (char **) G_STRUCT_MEMBER_P (header, offset);
  gsk_http_header_set_string (header, p_str, value);
  return TRUE;
}

static inline char *
str0cpy (char *dst,
         const char *src,
         guint len)
{
  memcpy (dst, src, len);
  dst[len] = 0;
  return dst;
}

#define CUT_ONTO_STACK(start, end)                              \
        ((start) ? (str0cpy (g_alloca (end - start + 1), start, (end) - (start))) : NULL)

/* general header handlers */
static gboolean
handle_transfer_encoding (GskHttpHeader *header,
			  const char    *value,
			  gpointer data)
{
  gsk_http_header_set_transfer_encoding_string (header, value);
  return TRUE;
}

static gboolean
handle_content_encoding  (GskHttpHeader *header,
			  const char    *value,
			  gpointer data)
{
  gsk_http_header_set_content_encoding_string (header, value);
  return TRUE;
}

static gboolean
handle_connection (GskHttpHeader *header,
		   const char *value,
		   gpointer data)
{
  gsk_http_header_set_connection_string (header, value);
  return TRUE;
}

static gboolean
handle_response_cache_control (GskHttpHeader *header,
                               const char    *value,
                               gpointer       data)
{
  const char                    *at = value;
  guint                          length = 0;
  GskHttpResponseCacheDirective *control;

  control = gsk_http_response_cache_directive_new ();
  control->is_public = 0;
  while (*at != '\0')
    {
      const char *start;
      const char *arg;
      GSK_SKIP_WHITESPACE (at);
      if (*at == '\0')
	{
          break;
	}
      start = at;
      while (*at != '\0' && *at != ',')
	{
          at++;
	}
      arg = memchr (start, '=', at - start);
      if (arg != NULL)
        {
	  arg++;
        }
      length = at - start;

      if (8 == length
	  && strncasecmp (start, "no-store", length) == 0)
        {
          control->no_store = 1;
        }
      else if (6 == length
	       && strncasecmp (start, "public", length) == 0)
        {
          control->is_public = 1;
        }
      else if (12 == length
	       && strncasecmp (start, "no-transform", length) == 0)
        {
          control->no_transform = 1;
        }
      else if (15 == length
	       && strncasecmp (start, "must-revalidate", length) == 0)
        {
          control->must_revalidate = 1;
        }
      else if (16 == length
	       && strncasecmp (start, "proxy-revalidate", length) == 0)
        {
          control->proxy_revalidate = 1;
        }
      else if (strncasecmp (start, "max-age", 7) == 0)
        {
          if (arg != NULL)
            control->max_age = atoi (arg);
        }
      else if (strncasecmp (start, "s-maxage", 8) == 0)
        {
          if (arg != NULL)
            control->s_max_age = atoi (arg);
        }
      else if (strncasecmp (start, "no-cache", 8) == 0)
        {
          control->no_cache = 1;
          if (arg != NULL)
	    {
	      gsk_http_response_cache_directive_set_no_cache_name(
						      control, arg, (at-arg));
	    }
        }
      else if (strncasecmp (start, "private", 7) == 0)
        {
          control->is_private = 1;
          if (arg != NULL)
	    {
	      gsk_http_response_cache_directive_set_private_name(
						      control, arg, (at-arg));
	    }
        }
      if (*at == ',')
        at++;
    }
  if (!control->is_public && !control->is_private)
    control->is_public = 1;

  gsk_http_response_set_cache_control (GSK_HTTP_RESPONSE (header), control);
  return TRUE;
        }

static gboolean
handle_request_cache_control (GskHttpHeader *header,
                              const char    *value,
                              gpointer       data)
{
  const char *at = value;
  guint length = 0;
  GskHttpRequestCacheDirective *control;

  control = gsk_http_request_cache_directive_new ();
  while (*at != '\0')
    {
      const char *start;
      const char *arg;
      GSK_SKIP_WHITESPACE (at);
      if (*at == '\0')
	{
	  break;
	}
      start = at;
      while (*at != '\0' && *at != ',')
	{
	  at++;
	}
      arg = memchr (start, '=', at - start);
          if (arg != NULL)
	{
	  arg++;
	}
      length = at - start;
      if (8 == length
	  && strncasecmp (start, "no-cache", length) == 0)
        {
          control->no_cache = 1;
        }
      else if (8 == length
	       && strncasecmp (start, "no-store", length) == 0)
        {
          control->no_store = 1;
        }
      else if (12 == length
	       && strncasecmp (start, "no-transform", length) == 0)
        {
          control->no_transform = 1;
        }
      else if (14 == length
	       && strncasecmp (start, "only-if-cached", length) == 0)
        {
          control->only_if_cached = 1;
        }
      else if (strncasecmp (start, "max-age", 7) == 0)
        {
          if (arg != NULL)
            {
	      control->max_age = atoi (arg);
            }
        }
      else if (strncasecmp (start, "max-stale", 9) == 0)
        {
	  if (arg != NULL)
	    {
	      control->max_stale = atoi (arg);
            }
	  else
            {
	      control->max_stale = -1;
	    }
        }
      else if (strncasecmp (start, "min-fresh", 9) == 0)
        {
          if (arg != NULL)
            {
              control->min_fresh = atoi (arg);
            }
        }

      if (*at == ',')
        at++;
    }

  gsk_http_request_set_cache_control (GSK_HTTP_REQUEST (header), control);
  return TRUE;
}

/* 14.32 Pragma: miscellaneous instructions.
   
   The only one specially noted in the RFC
   is the 'no-cache' pragma, which we don't
   have any special support for. */
static gboolean
handle_pragma    (GskHttpHeader *header,
                  const char    *value,
                  gpointer       data)
{
  gsk_http_header_add_pragma (header, value);
  return TRUE;
}

/* request handlers */
static gboolean
handle_ua_pixels (GskHttpHeader *header,
	          const char *value,
	          gpointer data)
{
  GskHttpRequest *request = GSK_HTTP_REQUEST (header);
  return sscanf (value, "%ux%u", &request->ua_width, &request->ua_height) == 2;
}

static inline gboolean
is_cookie_key_name_char (char c)
{
  return isalnum (c)
      || c == '.' || c == ':'
      || c == '_' || c == '-'
      || c == '~' || c == '%';
}

static GSList *
parse_cookies (const char *value)
{
  const char *at = value;
  GSList *cookie_list = NULL;

  while (TRUE)
    {
      const char *key_start = NULL;
      const char *key_end;
      const char *value_start = NULL;
      const char *value_end;
      const char *domain_start = NULL;
      const char *domain_end;
      const char *expire_date_string_start = NULL;
      const char *expire_date_string_end;
      const char *comment_start = NULL;
      const char *comment_end;
      const char *path_start = NULL;
      const char *path_end;
      int max_age = -1;
      guint version = 0;
      gboolean secure = FALSE;


      GSK_SKIP_WHITESPACE (at);
      if (*at == '\0')
        break;
      
      /* Find start and end pointers for the `key'. */
      key_start = at;
      while (*at != '\0' && *at != '=')
        at++;
      if (*at != '=')
        break;
      key_end = at;
      at++;

      /* Find start and end pointers for the `value'.
       * (may or may not be quoted).
       */
      if (*at == '"')
        {
          value_start = at + 1;
          value_end = strchr (value_start, '"');
          if (value_end == NULL)
            break;
          at = value_end + 1;
        }
      else
        {
          value_start = at;
          at = strchr (value_start, ';');
          if (at == NULL)
            at = strchr (value_start, 0);
          value_end = at;
        }
      at++;

      /* Parse `domain=', `comment=', `date=', `maxage=', `path='.
       */
      for (;;)
        {
          const char *this_key_start;
          const char *this_key_end;
          const char *this_value_start;
          const char *this_value_end;
          const char *equal;

          GSK_SKIP_WHITESPACE (at);
          if (*at == 0)
            break;
          this_key_start = at;
          GSK_SKIP_CHAR_TYPE (at, is_cookie_key_name_char);
          this_key_end = at;
          if (this_key_start == this_key_end && *this_key_end != '=')
            {
              g_warning ("misc error parsing cookie (key start %s)",this_key_start);
              goto error;
            }
          equal = strchr (this_key_end, '=');
          if (equal == NULL)
            {
              g_warning ("malformed cookie line");
              goto error;
            }
          this_value_start = equal + 1;
          GSK_SKIP_WHITESPACE (this_value_start);
          if (*this_value_start == '"')
            {
              this_value_start++;
              this_value_end = strchr (this_value_start, '"');
              if (this_value_end == NULL)
                {
                  g_warning ("users' cookie contained unmatched \"");
                  goto error;
                }
              at = this_value_end + 1;
            }
          else
            {
              this_value_end = strchr (this_value_start, ';');
              if (this_value_end == NULL)
                this_value_end = strchr (this_value_start, 0);
              at = this_value_end;
            }

          GSK_SKIP_WHITESPACE (at);
          if (*at == ';')
            {
              at++;
              GSK_SKIP_WHITESPACE (at);
            }

          /* various parameters...
           *     Comment   Domain   Max-Age   Path
           *     Secure    Version  Expires
           */
          if (strncasecmp (this_key_start, "Comment", 7) == 0)
            {
              if (comment_start != NULL)
                goto error;
              comment_start = this_value_start;
              comment_end = this_value_end;
            }
          else if (strncasecmp (this_key_start, "Domain", 6) == 0)
            {
              if (domain_start != NULL)
                goto error;
              domain_start = this_value_start;
              domain_end = this_value_end;
            }
          else if (strncasecmp (this_key_start, "Expires", 7) == 0)
            {
              if (expire_date_string_start != NULL)
                goto error;
              expire_date_string_start = this_value_start;
              expire_date_string_end = this_value_end;
            }
          else if (strncasecmp (this_key_start, "Max-Age", 7) == 0)
            {
              if (this_value_start != NULL)
                {
                  max_age = atoi (this_value_start);
                }
            }
          else if (strncasecmp (this_key_start, "Path", 4) == 0)
            {
              if (path_start != NULL)
                goto error;
              path_start = this_value_start;
              path_end = this_value_end;
            }
          else if (strncasecmp (this_key_start, "Secure", 6) == 0)
            {
              secure = TRUE;
            }
          else if (strncasecmp (this_key_start, "Version", 7) == 0)
            {
              version = atoi (this_key_start);
            }
          else
            {
              /* um, yuck: rewind to last key. */
              at = this_key_start;
              break;
            }
        }

      /* TODO: check that these should be! */
      g_return_val_if_fail (key_start != NULL, NULL);
      g_return_val_if_fail (value_start != NULL, NULL);

      {
        GskHttpCookie *cookie;
        const char *key = CUT_ONTO_STACK (key_start, key_end);
        const char *value = CUT_ONTO_STACK (value_start, value_end);
        const char *path = CUT_ONTO_STACK (path_start, path_end);
        const char *domain = CUT_ONTO_STACK (domain_start, domain_end);
        const char *expire_date = CUT_ONTO_STACK (expire_date_string_start,
                                                  expire_date_string_end);
        const char *comment = CUT_ONTO_STACK (comment_start, comment_end);

        cookie = gsk_http_cookie_new (key,
                                      value,
                                      path,
                                      domain,
                                      expire_date,
                                      comment,
                                      max_age);
        cookie->version = version;
        cookie->secure = secure;
        cookie_list = g_slist_prepend (cookie_list, cookie);
      }
    }
  return g_slist_reverse (cookie_list);

error:
  /* TODO: error handling needs improvement! */
  g_warning ("error parsing Set-Cookie");
  return g_slist_reverse (cookie_list);;
}

static gboolean
handle_cookie (GskHttpHeader *header,
	       const char *value,
	       gpointer data)
{
  GSList *cookie_list = parse_cookies (value);
  GskHttpRequest *request = GSK_HTTP_REQUEST (header);
  request->cookies = g_slist_concat (request->cookies, cookie_list);
  return cookie_list ? TRUE : FALSE;
}

#define HEADER_HANDLER_FAIL(arglist) \
  G_STMT_START{ g_warning arglist; return FALSE; }G_STMT_END

static gboolean
handle_range (GskHttpHeader *header,
	      const char *value,
	      gpointer data)
{
  int start = -1;
  int end = -1;

  if (strncasecmp (value, "bytes", 5) == 0)
    value += 5;
  else
    HEADER_HANDLER_FAIL(("Range must begin with `bytes'"));
  while (*value != '\0' && isspace (*value))
    value++;
  if (*value != '-')
    start = atoi (value);
  value = strchr (value, '-');
  if (value != NULL)
    {
      value++;
      end = atoi (value);
    }
  header->range_start = start;
  header->range_end = end;
  return TRUE;
}

static gboolean
parse_str_quality (const char **pstr,
                   char       **word_out,
                   gfloat      *quality_out)
{
  char *word;
  const char *str = *pstr;
  const char *start;
  const char *end;
  const char *comma;
  gfloat quality = -1;
  while (*str != '\0' && isspace (*str))
    str++;
  if (*str == '\0')
    HEADER_HANDLER_FAIL(("quality empty"));
  start = str;
  end = start;
  while (*end != '\0' && (!isspace (*end) && *end != ',' && *end != ';'))
    end++;
  if (start == end)
    HEADER_HANDLER_FAIL(("bad character in quality spec (end=%c)",*end));
  word = g_new (char, end - start + 1);
  memcpy (word, start, end - start);
  word[end - start] = '\0';
  
  str = end;
  
  if (*str == ';')
    {
      str++;  /* skip semicolon */
      while (*str != '\0' && isspace (*str))
        str++;
      if (*str == 'q' && str[1] == '=')
        {
          char *endp;
          str += 2;
          quality = (int) strtod (str, &endp);
          str = endp;
        }
    }
  while (*str != '\0' && isspace (*str))
    str++;
  *word_out = word;
  *quality_out = quality;
  comma = strchr (str, ',');
  if (comma != NULL)
    str = comma + 1;
  *pstr = str;
  return TRUE;
}

/* Charsets.
 *
 * See RFC 2616, Section 3.4
 */
static GskHttpCharSet *
parse_charset (const char **pstr)
{
  char *charset;
  GskHttpCharSet *rv;
  gfloat quality = -1;
  if (!parse_str_quality (pstr, &charset, &quality))
    return NULL;
  rv = gsk_http_char_set_new (charset, quality);
  g_free (charset);
  return rv;
}


static gboolean
handle_accept_charset (GskHttpHeader *header,
		       const char *value,
		       gpointer data)
{
  while (*value != '\0')
    {
      GskHttpCharSet *set;
      const char *start;
      while (*value != '\0' && (isspace (*value) || *value == ','))
        value++;
      start = value;
      set = parse_charset (&value);
      if (set == NULL)
        HEADER_HANDLER_FAIL(("error parsing charset from %s", value));
      gsk_http_request_add_charsets (GSK_HTTP_REQUEST (header), set);
    }
  return TRUE;
}

static GskHttpContentEncodingSet *
parse_content_encoding (const char **pstr)
{
  gfloat quality = -1;
  GskHttpContentEncoding encoding;
  const char *str = *pstr;

  while (*str != '\0' && isspace (*str))
    str++;

  if (strncasecmp (str, "identity", 8) == 0)
    {
      encoding = GSK_HTTP_CONTENT_ENCODING_IDENTITY;
    }
  else if (strncasecmp (str, "gzip", 4) == 0)
    {
      encoding = GSK_HTTP_CONTENT_ENCODING_GZIP;
    }
  else if (strncasecmp (str, "compress", 8) == 0)
    {
      encoding = GSK_HTTP_CONTENT_ENCODING_COMPRESS;
    }
  else
    {
      encoding = GSK_HTTP_CONTENT_ENCODING_UNRECOGNIZED;
    }

  while (TRUE)
    {
      while (*str && *str != ',' && *str != ';')
        str++;
      if (*str != ';')
        break;
      str++;
      while (*str != '\0' && isspace (*str))
        str++;
      if (*str == 'q' && (isspace (str[1]) || str[1] == '='))
        {
          const char *equal = strchr (str, '=');
          if (equal != NULL)
            quality = strtod (equal + 1, NULL);
        }
    }
  *pstr = str;
  return gsk_http_content_encoding_set_new (encoding, quality);
}

/* handle Accept-Encoding:
   which should really be Accept-Content-Encoding[s]: */
static gboolean
handle_accept_encoding (GskHttpHeader *header,
			const char *value,
			gpointer data)
{
  while (*value != '\0')
    {
      GskHttpContentEncodingSet *encoding;
      const char *start;
      while (*value != '\0' && (isspace (*value) || *value == ','))
        value++;
      start = value;
      encoding = parse_content_encoding (&value);
      if (encoding == NULL)
        HEADER_HANDLER_FAIL(("error parsing encoding from %s", value));
      gsk_http_request_add_content_encodings (GSK_HTTP_REQUEST (header), encoding);
    }
  return TRUE;
}

static GskHttpTransferEncodingSet *
parse_transfer_encoding (const char **pstr)
{
  gfloat quality = -1;
  GskHttpTransferEncoding encoding;
  const char *str = *pstr;

  while (*str != '\0' && isspace (*str))
    str++;

  if (strncasecmp (str, "none", 8) == 0)
    {
      encoding = GSK_HTTP_TRANSFER_ENCODING_NONE;
    }
  else if (strncasecmp (str, "chunked", 7) == 0)
    {
      encoding = GSK_HTTP_TRANSFER_ENCODING_CHUNKED;
    }
  else
    {
      encoding = GSK_HTTP_TRANSFER_ENCODING_UNRECOGNIZED;
    }

  while (TRUE)
    {
      while (*str && *str != ',' && *str != ';')
        str++;
      if (*str != ';')
        break;
      str++;
      while (*str != '\0' && isspace (*str))
        str++;
      if (*str == 'q' && (isspace (str[1]) || str[1] == '='))
        {
          const char *equal = strchr (str, '=');
          if (equal != NULL)
            quality = strtod (equal + 1, NULL);
        }
    }
  *pstr = str;
  return gsk_http_transfer_encoding_set_new (encoding, quality);
}

/* TE:  is an ugly header-line, that should really been Accept-Transfer-Encoding;
   (and Accept-Encoding should probably be Accept-Content-Encoding, but.. too late!) */
static gboolean
handle_te               (GskHttpHeader *header,
			 const char *value,
			 gpointer data)
{
  while (*value != '\0')
    {
      GskHttpTransferEncodingSet *encoding;
      const char *start;
      while (*value != '\0' && (isspace (*value) || *value == ','))
        value++;
      start = value;
      encoding = parse_transfer_encoding (&value);
      if (encoding == NULL)
        HEADER_HANDLER_FAIL(("error parsing encoding from %s", value));
      gsk_http_request_add_transfer_encodings (GSK_HTTP_REQUEST (header), encoding);
    }
  return TRUE;
}

static gboolean
handle_accept_ranges (GskHttpHeader *header,
		      const char *value,
		      gpointer data)
{
  if (strcmp (value, "none") == 0
   || strcmp (value, "") == 0)
    return TRUE;
  else if (strcmp (value, "bytes") == 0)
    {
      gsk_http_header_add_accepted_range (header, GSK_HTTP_RANGE_BYTES);
      return TRUE;
    }
  else
    return FALSE;
}

static GskHttpLanguageSet *
parse_language_set_list (const char *value)
{
  /* Parse
   *      LANGUAGE-RANGE [ ';' 'q' '=' VALUE ] ','
   * where
   *      LANGUAGE-RANGE ::= ( ( 1*8ALPHA *( "-" 1*8ALPHA ) ) | "*" )
   * See RFC 2616, Section 14.4.
   */
#define IS_8ALPHA	isalnum	/* XXX: lookup 8ALPHA */
  GskHttpLanguageSet *rv = NULL;	/* initially maintained in reverse order */

#define ERROR(str)	G_STMT_START{ g_warning("parse language-sets: %s", str); goto error; }G_STMT_END

  for (;;)
    {
      const char *language_start, *language_end;
      char *language;
      double quality = -1.0;
      GskHttpLanguageSet *set;

      GSK_SKIP_WHITESPACE (value);
      language_start = value;
      GSK_SKIP_CHAR_TYPE (value, IS_8ALPHA);
      if (value[0] == '-')
	{
	  value++;
	  GSK_SKIP_CHAR_TYPE (value, IS_8ALPHA);
	  language_end = value;
	}
      else
        language_end = value;
      language = g_alloca (language_end - language_start + 1);
      memcpy (language, language_start, language_end - language_start);
      language[language_end - language_start] = 0;

      GSK_SKIP_WHITESPACE (value);
      if (*value == ';')
	{
	  char *end_quality;
	  value++;
	  GSK_SKIP_WHITESPACE (value);
	  if (*value != 'q')
	    ERROR("expected 'q'");
	  value++;
	  GSK_SKIP_WHITESPACE (value);
	  if (*value != '=')
	    ERROR("expected '='");
	  value++;
	  GSK_SKIP_WHITESPACE (value);

	  quality = strtod (value, &end_quality);
	  if (value == end_quality)
	    ERROR("error parsing quality");
          value = end_quality;
          GSK_SKIP_WHITESPACE (value);
	}

      set = gsk_http_language_set_new (language, quality);
      if (rv == NULL)
	rv = set;
      else
	{
	  set->next = rv;
	  rv = set;
	}
      if (*value == 0)
	break;
      if (*value != ',')
	ERROR ("missing ','");
      value++;
      GSK_SKIP_WHITESPACE (value);
    }

#undef ERROR
#undef IS_8ALPHA

  /* reverse the order of rv */
  {
    GskHttpLanguageSet *last = NULL;
    while (rv)
      {
	GskHttpLanguageSet *next = rv->next;
	rv->next = last;
	last = rv;
	rv = next;
      }
    rv = last;
  }

  return rv;

error:
  while (rv)
    {
      GskHttpLanguageSet *next = rv->next;
      gsk_http_language_set_free (rv);
      rv = next;
    }
  return NULL;
}

static gboolean
handle_accept_language (GskHttpHeader *header,
		        const char *value,
		        gpointer data)
{
  GskHttpLanguageSet *set = parse_language_set_list (value);
  GskHttpLanguageSet *last;
  GskHttpRequest *request = GSK_HTTP_REQUEST (header);
  if (set == NULL)
    HEADER_HANDLER_FAIL(("error language-set from %s", value));

  /* append to the header */
  last = request->accept_languages;
  if (last)
    {
      while (last->next)
	last = last->next;
      last->next = set;
    }
  else
    request->accept_languages = set;

  return TRUE;
}

static GskHttpMediaTypeSet *
parse_media_type (const char **pstr)
{
  /* Parse
   *      { TYPE | '*' } / { SUBTYPE | '*' } [ ';' 'q' '=' VALUE ] ','
   * but * / SUBTYPE is not allowed.
   */
  char expr[512];
  char *slash;
  char *semi;
  const char *str;
  char *type;
  char *subtype;
  const char *end_str;
  char *subtype_ptr;
  char *options_ptr;
  gfloat quality = -1;

  str = *pstr;
  end_str = str;
  while (*end_str != 0 && *end_str != ',')
    end_str++;

  if (end_str - str + 1 > (int) sizeof (expr))
    return NULL;

  memcpy (expr, str, end_str - str);
  expr[end_str - str] = '\0';

  slash = strchr (expr, '/');
  if (slash == NULL)
    return NULL;
  *slash = '\0';
  semi = strchr (slash + 1, ';');
  subtype_ptr = slash + 1;
  if (semi != NULL)
    {
      *semi = '\0';
      options_ptr = semi + 1;
    }
  else
    options_ptr = NULL;

  /* perhaps this should be inlined. */
  g_strstrip (expr);
  g_strstrip (subtype_ptr);
  if (options_ptr != NULL)
    g_strstrip (options_ptr);

  if (strcmp (expr, "*") == 0)
    {
      if (strcmp (subtype_ptr, "*") != 0)
        return NULL;
      type = subtype = NULL;
    }
  else
    {
      type = expr;
      if (strcmp (subtype_ptr, "*") == 0)
        subtype = NULL;
      else
        subtype = subtype_ptr;
    }

  if (options_ptr != NULL && *options_ptr == 'q')
    {
      options_ptr++;
      GSK_SKIP_WHITESPACE (options_ptr);
      if (*options_ptr == '=')
        {
          options_ptr++;
          quality = strtod (options_ptr, NULL);
        }
    }
  *pstr = end_str;
  return gsk_http_media_type_set_new (type, subtype, quality);
}

static gboolean
handle_accept (GskHttpHeader *header,
	       const char *value,
	       gpointer data)
{
  while (*value != '\0')
    {
      GskHttpMediaTypeSet *media_type;
      const char *start;
      while (*value != '\0' && (isspace (*value) || *value == ','))
        value++;
      start = value;
      media_type = parse_media_type (&value);
      if (media_type == NULL)
        {
          /* XXX: error handling */
          g_warning ("error parsing media_type from %s", value);
          return FALSE;
        }
      gsk_http_request_add_media (GSK_HTTP_REQUEST (header), media_type);
    }
  return TRUE;
}

static void
strip_double_quotes (char *str)
{
  char *init = str;
  GSK_SKIP_WHITESPACE (str);
  if (*str != '"')
    {
      const char *start = str;
      GSK_SKIP_NONWHITESPACE (str);
      memmove (init, start, str - start);
      init[str - start] = 0;
    }
  else
    {
      const char *end = strchr (str + 1, '"');
      if (end == NULL)
        end = strchr (str, '\0');
      memmove (init, str, end - str);
      init[end - str] = 0;
    }
}

static gboolean
handle_if_match (GskHttpHeader *header,
		 const char *value,
		 gpointer data)
{
  GskHttpRequest *request = GSK_HTTP_REQUEST (header);
  char **at;
  char **old_if_match = request->if_match;
  request->if_match = g_strsplit (value, ",", 0);
  for (at = request->if_match; *at != NULL; at++)
    strip_double_quotes (*at);
  g_strfreev (old_if_match);
  return TRUE;
}


/* responses */
static gboolean
handle_set_cookie (GskHttpHeader *header,
		   const char *value,
		   gpointer    data)
{
  GSList *cookie_list = parse_cookies (value);
  GskHttpResponse *response = GSK_HTTP_RESPONSE (header);
  response->set_cookies = g_slist_concat (response->set_cookies, cookie_list);
  return cookie_list ? TRUE : FALSE;
}

static gboolean
handle_age (GskHttpHeader *header,
	    const char *value,
	    gpointer    data)
{
  GSK_SKIP_WHITESPACE (value);
  if (!isdigit (*value))
    return FALSE;
  GSK_HTTP_RESPONSE (header)->age = atoi (value);
  return TRUE;
}

static gboolean
handle_allow (GskHttpHeader *header,
	      const char    *value,
	      gpointer       data)
{
  guint val = 0;
  while (TRUE)
    {
      int len;
      char buf[15];
      int i;
      GSK_SKIP_WHITESPACE (value);
      if (*value == '\0')
        break;
      len = 0;
      while (value[len] != '\0' && value[len] != ',' && !isspace (value[len]))
        len++;
      
      if (len > 14)
        {
          g_warning ("unrecognized method, at %s", value);
          return FALSE;
        }
      memcpy (buf, value, len);
      for (i = 0; i < len; i++)
        buf[i] = toupper (buf[i]);
      buf[i] = 0;
      if (strcmp (buf, "GET") == 0)
        val |= (1 << GSK_HTTP_VERB_GET);
      else if (strcmp (buf, "POST") == 0)
        val |= (1 << GSK_HTTP_VERB_POST);
      else if (strcmp (buf, "PUT") == 0)
        val |= (1 << GSK_HTTP_VERB_PUT);
      else if (strcmp (buf, "HEAD") == 0)
        val |= (1 << GSK_HTTP_VERB_HEAD);
      else if (strcmp (buf, "OPTIONS") == 0)
        val |= (1 << GSK_HTTP_VERB_OPTIONS);
      else if (strcmp (buf, "DELETE") == 0)
        val |= (1 << GSK_HTTP_VERB_DELETE);
      else if (strcmp (buf, "TRACE") == 0)
        val |= (1 << GSK_HTTP_VERB_TRACE);
      else if (strcmp (buf, "CONNECT") == 0)
        val |= (1 << GSK_HTTP_VERB_CONNECT);
      else
        {
          /* XXX: need better error handling! */
          g_warning ("unrecognized verb: %s", buf);
          return FALSE;
        }
      value += len;
      while (*value != '\0' && (isspace (*value) || *value == ','))
        value++;
    }
  GSK_HTTP_RESPONSE (header)->allowed_verbs = val;
  return TRUE;
}

static gboolean
handle_expires        (GskHttpHeader *header,
		       const char *value,
		       gpointer data)
{
  GskHttpResponse *response = GSK_HTTP_RESPONSE (header);
  if (!gsk_date_parse_timet (value, &response->expires, GSK_DATE_FORMAT_HTTP))
    {
      response->expires = (time_t)-1;
      gsk_http_header_set_string (header, &response->expires_str, value);
    }
  return TRUE;
}

static gboolean
handle_content_md5sum (GskHttpHeader *header,
		       const char *value,
		       gpointer data)
{
  GskHttpResponse *response = GSK_HTTP_RESPONSE (header);

  if (response->has_md5sum)
    return FALSE;

  /* XXX: gsk_base64_decode() should be guint8 on the output, sigh */
  if (gsk_base64_decode ((char*)response->md5sum, 16, value, -1) == 16)
    response->has_md5sum = 1;
  else
    {
      g_warning ("got invalid base64-encoded MD5-checksum");
      return FALSE;
    }
  return TRUE;
}

/* XXX: rename this stupid function */
static void
content_type_parse_token(const char **start_out,
                         guint      *len_out,
                         const char    **pstr)
{
  const char *str = *pstr;
  GSK_SKIP_WHITESPACE (str);
  *start_out = str;
  while (*str != 0
       && !g_ascii_isspace (*str)
       && *str != ';' && *str != '/' && *str != ',')
    str++;
  *len_out = str - (*start_out);
  *pstr = str;
}

gboolean gsk_http_content_type_parse (const char *content_type_header,
                                      GskHttpContentTypeParseFlags flags,
                                      GskHttpContentTypeInfo *out,
                                      GError                **error)
{
  const char *value = content_type_header;
  /* TYPE / SUBTYPE [;charset="charset][...] */
  GSK_SKIP_WHITESPACE (value);
  if (*value == '*')
    {
      out->type_start = NULL;
      out->type_len = 0;
      value++;
    }
  else
    {
      /* parse a token */
      content_type_parse_token (&out->type_start, &out->type_len, &value);
    }
  GSK_SKIP_WHITESPACE (value);

  /* parse a slash */
  if (*value != '/')
    {
      g_set_error (error,
                   GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_BAD_FORMAT,
                   "value begins %s", value);
      return FALSE;
    }
  value++;
  GSK_SKIP_WHITESPACE (value);

  /* parse a subtype */
  if (*value == '*')
    {
      out->subtype_start = NULL;
      out->subtype_len = 0;
      value++;
    }
  else
    {
      /* parse a token */
      content_type_parse_token (&out->subtype_start, &out->subtype_len, &value);
    }

  if ((flags & GSK_HTTP_CONTENT_TYPE_PARSE_ADDL) == 0)
    out->max_additional = 0;
  out->n_additional = 0;

  /* parse a list of attributes, treating `charset=' specially */
  out->charset_start = NULL;
  out->charset_len = 0;
  while (TRUE)
    {
      GSK_SKIP_WHITESPACE (value);
      if (*value == '\0')
        break;
      if (*value == ';')
        value++;
      GSK_SKIP_WHITESPACE (value);
      if (strncasecmp (value, "charset", 7) == 0)
        {
          const char *test = value + 7;
          GSK_SKIP_WHITESPACE (test);
          if (*test == '=')
            {
              const char *end;
              value = test + 1;
              GSK_SKIP_WHITESPACE (value);
              if (*value == '"' || *value == '\'')
                value++;
              end = value;
              while (*end != '\0'
                  && (!isspace (*end) && *end != ',' && *end != ';'))
                end++;
              out->charset_start = value;
              out->charset_len = end - value;
              if (end > value && (end[-1] == '\'' || end[-1] == '"'))
                out->charset_len--;
              GSK_SKIP_WHITESPACE (end);
              while (*end == ';' || *end == ',')
                end++;
              GSK_SKIP_WHITESPACE (end);
              value = end;
              continue;
            }
          else
            {
              g_set_error (error, GSK_G_ERROR_DOMAIN,
                           GSK_ERROR_BAD_FORMAT,
                           "missing '=' after charset");
              return FALSE;
            }
        }
      else
	{
	  const char *end = strchr (value, ';');
	  if (end == NULL)
	    end = strchr (value, 0);
          GSK_SKIP_WHITESPACE (value);
          if (out->n_additional < out->max_additional)
            {
              const char *start_value = value;
              const char *end_value = end;
              while (end_value > start_value
                  && g_ascii_isspace (end_value[-1]))
                end_value--;
              out->additional_starts[out->n_additional] = start_value;
              out->additional_lens[out->n_additional] = end_value - start_value;
              out->n_additional++;
            }
	  value = end;
	}
    }
  return TRUE;
}

static gboolean
handle_content_type (GskHttpHeader *header,
		     const char *value,
		     gpointer data)
{
  GskHttpContentTypeInfo type_info;
  GError *error = NULL;
  GSList *addl = NULL;
  guint i;
  if (header->has_content_type)
    {
      g_warning ("has_content_type already so Content-Type not allowed");
      return FALSE;
    }
  type_info.max_additional = 16;
  type_info.additional_starts = g_newa (const char *, type_info.max_additional);
  type_info.additional_lens = g_newa (guint, type_info.max_additional);
  if (!gsk_http_content_type_parse (value,
                                    GSK_HTTP_CONTENT_TYPE_PARSE_ADDL,
                                    &type_info,
                                    &error))
    {
      g_warning ("gsk_http_content_type_parse failed: %s",
                 error->message);
      g_error_free (error);
      return FALSE;
    }
  header->has_content_type = 1;
  gsk_http_header_set_string_len (header, &header->content_type, type_info.type_start, type_info.type_len);
  gsk_http_header_set_string_len (header, &header->content_subtype, type_info.subtype_start, type_info.subtype_len);
  gsk_http_header_set_string_len (header, &header->content_charset, type_info.charset_start, type_info.charset_len);
  for (i = 0; i < type_info.n_additional; i++)
    addl = g_slist_prepend (addl, g_strndup (type_info.additional_starts[i],
                                             type_info.additional_lens[i]));
  header->content_additional = g_slist_concat (header->content_additional,
                                               g_slist_reverse (addl));
  return TRUE;
}

static gboolean
handle_content_language (GskHttpHeader *header,
			 const char *value,
			 gpointer data)
{
  if (header->content_languages)
    g_strfreev (header->content_languages);
  header->content_languages = g_strsplit (value, ",", 0);
  return TRUE;
}

static gboolean
handle_retry_after (GskHttpHeader *header,
		    const char *value,
		    gpointer data)
{
  GskHttpResponse *response = GSK_HTTP_RESPONSE (header);
  if (response->has_retry_after)
    return FALSE;
  response->has_retry_after = 1;
  if (isdigit (*value))
    {
      response->retry_after_relative = TRUE;
      response->retry_after = atoi (value);
    }
  else
    {
      response->retry_after_relative = FALSE;
      if (!gsk_date_parse_timet (value, &response->retry_after, GSK_DATE_FORMAT_HTTP))
        {
          g_warning ("error parsing date for Retry-After");
          return FALSE;
        }
    }
  return TRUE;
}

/* --- Authenticate and Authorization ---*/

/* NOTES:
    - The macros CUT_TOKEN and CUT_TOKEN_MAYBE_QUOTED
      assume that there are local variables "start"
      and "at" that are being manipulated.
    - IS_NONQUOTED_ATTR is intended for use with GSK_SKIP_CHAR_TYPE().
 */

  /* This macro assigns 'var_name' a stack-allocated
     string which consists of the characters starting
     at the variable 'start' and with 'at' immediately past
     the last character. */
#define CUT_TOKEN(var_name)                     \
  G_STMT_START{                                 \
    var_name = g_alloca (at - start + 1);       \
    memcpy (var_name, start, at - start);       \
    var_name[at - start] = 0;                   \
  }G_STMT_END

/* TODO: optimize these */
#define IS_ATTR_NAME_CHAR(c)                    \
  ((c) == '-' || (c) == '_' || g_ascii_isalnum(c))
#define IS_NONQUOTED_ATTR(c)                    \
  (!g_ascii_isspace (c) && c != ';' && c != ',')

  /* Assign var_name the value of a string starting
     at 'at', at moving 'at' past the end of the string,
     past a ';' or ',' delimiter.
     and skipping any trailing whitespace */
#define CUT_TOKEN_MAYBE_QUOTED(var_name)             \
  G_STMT_START{                                      \
    if (*at == '"')                                  \
      {                                              \
        start = ++at;                                \
        at = strchr (at, '"');                       \
        if (at == NULL)                              \
          return FALSE;                              \
        CUT_TOKEN (var_name);                        \
        at++;                                        \
        GSK_SKIP_WHITESPACE (at);                    \
        if (*at == ',' || *at == ';')                \
          at++;                                      \
      }                                              \
    else                                             \
      {                                              \
        start = at;                                  \
        GSK_SKIP_CHAR_TYPE (at, IS_NONQUOTED_ATTR);  \
        CUT_TOKEN (var_name);                        \
        if (*at)                                     \
          at++; /* skip delimiter character */       \
      }                                              \
    GSK_SKIP_WHITESPACE (at);                        \
  }G_STMT_END

static inline gboolean
is_key (const char *start, guint len, const char *str)
{
  return memcmp (start, str, len) == 0 
     &&  str[len] == '\0';
}

static GskHttpAuthenticate *
gsk_http_authenticate_parse (const char *value)
{
  char *auth_scheme = NULL;
  char *realm = NULL;
  char *domain = NULL;
  char *nonce = NULL;
  char *opaque = NULL;
  char *algorithm = NULL;
  char *stale = NULL;
  char *qop = NULL;
  const char *at = value;
  const char *start;
  GskHttpAuthenticate *rv;

  GSK_SKIP_WHITESPACE (at);
  if (*at == 0)
    return FALSE;

  start = at;
  GSK_SKIP_NONWHITESPACE (at);
  CUT_TOKEN (auth_scheme);
  GSK_SKIP_WHITESPACE (at);

  while (*at)
    {
      const char *key_start = at;
      const char *key_end = key_start;
      GSK_SKIP_CHAR_TYPE (key_end, IS_ATTR_NAME_CHAR);
      at = key_end;
      GSK_SKIP_WHITESPACE (at);
      if (*at != '=')
        {
          /* is this ever ok? */
          return FALSE;
        }
      at++;
      GSK_SKIP_WHITESPACE (at);
#define IS_KEY(str) is_key (key_start, key_end - key_start, str)
      if (IS_KEY ("realm"))
        CUT_TOKEN_MAYBE_QUOTED (realm);
      else if (IS_KEY ("domain"))
        CUT_TOKEN_MAYBE_QUOTED (domain);
      else if (IS_KEY ("nonce"))
        CUT_TOKEN_MAYBE_QUOTED (nonce);
      else if (IS_KEY ("opaque"))
        CUT_TOKEN_MAYBE_QUOTED (opaque);
      else if (IS_KEY ("algorithm"))
        CUT_TOKEN_MAYBE_QUOTED (algorithm);
      else if (IS_KEY ("stale"))
        CUT_TOKEN_MAYBE_QUOTED (stale);
      else if (IS_KEY ("qop"))
        CUT_TOKEN_MAYBE_QUOTED (qop);
      else
        {
          char *unused;
          CUT_TOKEN_MAYBE_QUOTED (unused);      /* TODO: save these! */
        }
#undef IS_KEY
    }

  if (g_ascii_strcasecmp (auth_scheme, "Basic") == 0)
    {
      rv = gsk_http_authenticate_new_basic (realm);
    }
  else if (g_ascii_strcasecmp (auth_scheme, "Digest") == 0)
    {
      /* XXX: missing qop and stale ??? */
      rv = gsk_http_authenticate_new_digest (realm, domain, nonce,
                                             opaque, algorithm);
    }
  else
    {
      return NULL;
    }

  /* TODO: aux parameters */

  return rv;
}

static GskHttpAuthorization *
gsk_http_authorization_parse (const char *value)
{
  char *auth_scheme = NULL;
  char *realm = NULL;
  char *domain = NULL;
  char *nonce = NULL;
  char *opaque = NULL;
  char *algorithm = NULL;
  char *qop = NULL;
  char *user = NULL;
  char *password = NULL;
  char *response_digest = NULL;
  char *entity_digest = NULL;
  const char *at = value;
  const char *start;
  GskHttpAuthorization *rv;

  GSK_SKIP_WHITESPACE (at);
  if (*at == 0)
    return NULL;

  start = at;
  GSK_SKIP_NONWHITESPACE (at);
  CUT_TOKEN (auth_scheme);
  GSK_SKIP_WHITESPACE (at);

  if (g_ascii_strcasecmp (auth_scheme, "basic") == 0)
    {
      /* remainder is user:password, base-64 encoded */
      guint base64_encoded_len = strlen (at);
      guint decoded_max_len = GSK_BASE64_GET_MAX_DECODED_LEN (base64_encoded_len);
      char *decoded = g_alloca (decoded_max_len + 1);
      char *colon;
      guint decoded_len = gsk_base64_decode (decoded, decoded_max_len,
                                             at, base64_encoded_len);
      decoded[decoded_len] = 0;
      colon = strchr (decoded, ':');
      if (colon == NULL)
        {
          return NULL;         /* need ':' separating user/password */
        }
      *colon = 0;
      return gsk_http_authorization_new_basic (decoded, colon + 1);
    }
  if (g_ascii_strcasecmp (auth_scheme, "digest") != 0)
    {
      return gsk_http_authorization_new_unknown (auth_scheme, at);
    }

  /* -- digest authorization header -- */
  while (*at)
    {
      const char *key_start = at;
      const char *key_end = key_start;
      GSK_SKIP_CHAR_TYPE (key_end, IS_ATTR_NAME_CHAR);
      at = key_end;
      GSK_SKIP_WHITESPACE (at);
      if (*at != '=')
        {
          /* is this ever ok? */
          return FALSE;
        }
      at++;
      GSK_SKIP_WHITESPACE (at);
#define IS_KEY(str) is_key (key_start, key_end - key_start, str)
      if (IS_KEY ("realm"))
        CUT_TOKEN_MAYBE_QUOTED (realm);
      else if (IS_KEY ("domain"))
        CUT_TOKEN_MAYBE_QUOTED (domain);
      else if (IS_KEY ("nonce"))
        CUT_TOKEN_MAYBE_QUOTED (nonce);
      else if (IS_KEY ("opaque"))
        CUT_TOKEN_MAYBE_QUOTED (opaque);
      else if (IS_KEY ("algorithm"))
        CUT_TOKEN_MAYBE_QUOTED (algorithm);
      else if (IS_KEY ("qop"))
        CUT_TOKEN_MAYBE_QUOTED (qop);
      else if (IS_KEY ("user"))
        CUT_TOKEN_MAYBE_QUOTED (user);
      else if (IS_KEY ("password"))
        CUT_TOKEN_MAYBE_QUOTED (password);
      else if (IS_KEY ("response_digest"))
        CUT_TOKEN_MAYBE_QUOTED (response_digest);
      else if (IS_KEY ("entity_digest"))
        CUT_TOKEN_MAYBE_QUOTED (entity_digest);
      else
        {
          char *unused;
          CUT_TOKEN_MAYBE_QUOTED (unused);      /* TODO: save these! */
        }
#undef IS_KEY
    }

  rv = gsk_http_authorization_new_digest (realm, domain, nonce, opaque,
                                          algorithm, user, password,
                                          response_digest, entity_digest);

  /* TODO: aux parameters */

  return rv;
}

static gboolean
handle_www_authenticate (GskHttpHeader *header,
		         const char *value,
		         gpointer data)
{

  GskHttpAuthenticate *auth = gsk_http_authenticate_parse (value);
  if (auth == NULL)
    return FALSE;
  gsk_http_response_set_authenticate (GSK_HTTP_RESPONSE (header), FALSE, auth);
  gsk_http_authenticate_unref (auth);
  return TRUE;
}

static gboolean
handle_authorization (GskHttpHeader *header,
		      const char *value,
		      gpointer data)
{
  GskHttpAuthorization *auth = gsk_http_authorization_parse (value);
  if (auth == NULL)
    return FALSE;
  gsk_http_request_set_authorization (GSK_HTTP_REQUEST (header), FALSE, auth);
  gsk_http_authorization_unref (auth);
  return TRUE;
}

#undef IS_ATTR_NAME_CHAR
#undef CUT_TOKEN_MAYBE_QUOTED
#undef CUT_TOKEN


/* --- Parser Tables --- */
G_LOCK_DEFINE_STATIC (table_table);

/**
 * gsk_http_header_get_parser_table:
 * @is_request: whether the command-table should corresponse to a HTTP request.
 *
 * Get a #GHashTable which can handle HTTP header lines
 * for a request or a response.
 *
 * TODO: describe how to use it!
 *
 * returns: a reference to the command table.
 */

#define UINT_LINE_PARSER(name, struct, member)	\
 { name, parse_uint, GUINT_TO_POINTER (G_STRUCT_OFFSET(struct, member)) }
#define UINT64_LINE_PARSER(name, struct, member)	\
 { name, parse_uint64, GUINT_TO_POINTER (G_STRUCT_OFFSET(struct, member)) }
#define STRING_LINE_PARSER(name, struct, member)	\
 { name, parse_string, GUINT_TO_POINTER (G_STRUCT_OFFSET(struct, member)) }
#define DATE_LINE_PARSER(name, struct, member)	\
 { name, parse_date, GUINT_TO_POINTER (G_STRUCT_OFFSET(struct, member)) }
#define GENERIC_LINE_PARSER(name, func)		\
 { name, func, NULL }
static GskHttpHeaderLineParser common_parsers[] =
{
  GENERIC_LINE_PARSER ("accept-ranges", handle_accept_ranges),
  UINT64_LINE_PARSER ("content-length", GskHttpHeader, content_length),
  GENERIC_LINE_PARSER ("transfer-encoding", handle_transfer_encoding),
  GENERIC_LINE_PARSER ("content-encoding", handle_content_encoding),
  GENERIC_LINE_PARSER ("connection", handle_connection),
  DATE_LINE_PARSER ("date", GskHttpHeader, date),
  GENERIC_LINE_PARSER ("content-type", handle_content_type),
  GENERIC_LINE_PARSER ("content-language", handle_content_language),
  GENERIC_LINE_PARSER ("pragma", handle_pragma)
};

static GskHttpHeaderLineParser request_parsers[] =
{
  UINT_LINE_PARSER ("max-forwards", GskHttpRequest, max_forwards),
  UINT_LINE_PARSER ("keep-alive", GskHttpRequest, keep_alive_seconds),
  DATE_LINE_PARSER ("if-modified-since", GskHttpRequest, if_modified_since),
  STRING_LINE_PARSER ("host", GskHttpRequest, host),
  STRING_LINE_PARSER ("referer", GskHttpRequest, referrer),
  STRING_LINE_PARSER ("from", GskHttpRequest, from),
  STRING_LINE_PARSER ("user-agent", GskHttpRequest, user_agent),
  //STRING_LINE_PARSER ("authorization", GskHttpRequest, authorization.credentials),
  GENERIC_LINE_PARSER ("ua-pixels", handle_ua_pixels),
  STRING_LINE_PARSER ("ua-color", GskHttpRequest, ua_color),
  STRING_LINE_PARSER ("ua-language", GskHttpRequest, ua_language),
  STRING_LINE_PARSER ("ua-os", GskHttpRequest, ua_os),
  STRING_LINE_PARSER ("ua-cpu", GskHttpRequest, ua_cpu),
  GENERIC_LINE_PARSER ("cookie", handle_cookie),
  GENERIC_LINE_PARSER ("range", handle_range),
  GENERIC_LINE_PARSER ("accept-charset", handle_accept_charset),
  GENERIC_LINE_PARSER ("accept-encoding", handle_accept_encoding),
  GENERIC_LINE_PARSER ("accept-language", handle_accept_language),
  GENERIC_LINE_PARSER ("accept", handle_accept),
  GENERIC_LINE_PARSER ("if-match", handle_if_match),
  GENERIC_LINE_PARSER ("te", handle_te),
  GENERIC_LINE_PARSER ("cache-control", handle_request_cache_control),
  GENERIC_LINE_PARSER ("authorization", handle_authorization),
};

static GskHttpHeaderLineParser response_parsers[] =
{
  DATE_LINE_PARSER ("last-modified", GskHttpResponse, last_modified),
  STRING_LINE_PARSER ("e-tag", GskHttpResponse, etag),
  STRING_LINE_PARSER ("etag", GskHttpResponse, etag),/* encountered on the web */
  STRING_LINE_PARSER ("location", GskHttpResponse, location),
  STRING_LINE_PARSER ("server", GskHttpResponse, server),
  GENERIC_LINE_PARSER ("set-cookie", handle_set_cookie),
  GENERIC_LINE_PARSER ("content-range", handle_range),
  GENERIC_LINE_PARSER ("age", handle_age),
  GENERIC_LINE_PARSER ("allow", handle_allow),
  GENERIC_LINE_PARSER ("expires", handle_expires),
  GENERIC_LINE_PARSER ("content-md5", handle_content_md5sum),
  GENERIC_LINE_PARSER ("retry-after", handle_retry_after),
  GENERIC_LINE_PARSER ("cache-control", handle_response_cache_control),
  GENERIC_LINE_PARSER ("www-authenticate", handle_www_authenticate),
};

/**
 * gsk_http_header_get_parser_table:
 * @is_request: whether to get the parse table for request (versus for responses).
 *
 * Obtain a hash-table which maps lowercased HTTP header keys
 * to #GskHttpHeaderLineParser's.  There are different maps
 * for requests and responses.
 *
 * returns: the hash-table mapping, which is global and should not be freed.
 */
GHashTable *
gsk_http_header_get_parser_table (gboolean is_request)
{
  static GHashTable *table_table[2] = { NULL, NULL };
  guint index = is_request ? 1 : 0;
  G_LOCK (table_table);
  if (!table_table[index])
    {
      GHashTable *table = g_hash_table_new (g_str_hash, g_str_equal);
      unsigned i;
#define ADD_PARSERS(table_name)				                   \
      G_STMT_START{					                   \
	for (i = 0; i < G_N_ELEMENTS(table_name); i++)	                   \
	  g_hash_table_insert (table, (gpointer) table_name[i].name, &table_name[i]); \
      }G_STMT_END
      ADD_PARSERS(common_parsers);
      if (is_request)
	ADD_PARSERS(request_parsers);
      else
	ADD_PARSERS(response_parsers);
#undef ADD_PARSERS
      table_table[index] = table;
    }
  G_UNLOCK (table_table);
  return table_table[index];
}

static void
snip_between (GskBufferIterator *start,
	      GskBufferIterator *end,
              gsize *line_size,
	      char **line_mem,
	      gboolean *line_mem_from_stack)
{
  gboolean must_realloc = FALSE;
  guint len = end->offset - start->offset;
  while (len + 1 > *line_size)
    {
      *line_size += *line_size;
      must_realloc = TRUE;
    }
  if (must_realloc)
    {
      if (!*line_mem_from_stack)
	g_free (*line_mem);
      else
	*line_mem_from_stack = FALSE;
      *line_mem = g_malloc (*line_size);
    }
  gsk_buffer_iterator_peek (start, *line_mem, len);
  if (len > 0 && (*line_mem)[len - 1] == '\r')
    (*line_mem)[len-1] = '\0';
  else
    (*line_mem)[len] = '\0';
}

/**
 * gsk_http_header_from_buffer:
 * @input: the buffer from which to parse the request.
 * @is_request: whether this is a request; otherwise, it is a response.
 * @flags: preferences for parsing.
 *
 * Parse the HTTP header from the buffer.
 *
 * returns: a new reference to the header.
 */
GskHttpHeader  *
gsk_http_header_from_buffer (GskBuffer     *input,
			     gboolean       is_request,
			     GskHttpParseFlags flags,
                             GError        **error)
{
  GskBufferIterator iterator;
  GskBufferIterator newline;
  gsize line_size = 4096;
  char *line_mem = g_alloca (line_size);
  gboolean line_mem_from_stack = TRUE;
  GType header_type;
  GskHttpHeader *rv;
  GHashTable *parser_table;
  gboolean save_errors = ((flags & GSK_HTTP_PARSE_SAVE_ERRORS) != 0);

  gsk_buffer_iterator_construct (&iterator, input);
  newline = iterator;
  if (!gsk_buffer_iterator_find_char (&newline, '\n'))
    return NULL;
  snip_between (&iterator, &newline, &line_size, &line_mem, &line_mem_from_stack);
  header_type = is_request ? GSK_TYPE_HTTP_REQUEST : GSK_TYPE_HTTP_RESPONSE;
  rv = g_object_new (header_type, NULL);
#define ERROR_RETURN()                          \
  G_STMT_START{                                 \
    if (!line_mem_from_stack)                   \
      g_free (line_mem);                        \
    g_object_unref (rv);                        \
    return NULL;                                \
  }G_STMT_END
    
  if (is_request)
    {
      switch (gsk_http_request_parse_first_line (GSK_HTTP_REQUEST (rv),
                                                 line_mem, 
                                                 error))
        {
        case GSK_HTTP_REQUEST_FIRST_LINE_ERROR:
          g_object_unref (rv);
          if (!line_mem_from_stack)
            g_free (line_mem);
          return NULL;
        case GSK_HTTP_REQUEST_FIRST_LINE_SIMPLE:
          if (!line_mem_from_stack)
            g_free (line_mem);
          gsk_buffer_discard (input, gsk_buffer_iterator_offset (&newline) + 1);
          return rv;
        case GSK_HTTP_REQUEST_FIRST_LINE_FULL:
          break;
        default:
          g_assert_not_reached ();
        }
    }
  else
    {
      if (!gsk_http_response_process_first_line (GSK_HTTP_RESPONSE (rv), line_mem))
        {
          if (rv->g_error)
            {
              g_propagate_error (error, rv->g_error);
              rv->g_error = NULL;
            }
          ERROR_RETURN ();
        }
    }

  gsk_buffer_iterator_skip (&newline, 1);
  iterator = newline;

  parser_table = gsk_http_header_get_parser_table (is_request);
  for (;;)
    {
      /* Assert:  newline == iterator */
      char *at, *colon;
      GskHttpHeaderLineParser *parser;

      if (!gsk_buffer_iterator_find_char (&newline, '\n'))
	ERROR_RETURN ();
      snip_between (&iterator, &newline, &line_size, &line_mem, &line_mem_from_stack);
      if (line_mem[0] == '\0' || isspace (line_mem[0]))
	break;

      colon = strchr (line_mem, ':');
      if (colon == NULL)
	ERROR_RETURN ();
      at = line_mem;
      while (at < colon)
	{
	  *at = tolower (*at);
	  at++;
	}
      *at++ = 0;
      GSK_SKIP_WHITESPACE (at);
      parser = g_hash_table_lookup (parser_table, line_mem);
      if (parser == NULL)
	{
	  /* Add it as an additional field. */
	  gsk_http_header_add_misc (rv, line_mem, at);
	}
      else
	{
	  if (!(*parser->func)(rv, at, parser->data))
            {
              if (save_errors)
	        gsk_http_header_add_misc (rv, line_mem, at);
              else
	        ERROR_RETURN ();
            }
	}
      gsk_buffer_iterator_skip (&newline, 1);
      iterator = newline;
    }
#undef ERROR_RETURN

  gsk_buffer_discard (input, gsk_buffer_iterator_offset (&newline) + 1);

  if (!line_mem_from_stack)
    g_free (line_mem);

  return rv;
}
