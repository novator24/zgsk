#include "gskhttpheader.h"
#include "gskhttprequest.h"
#include "gskhttpresponse.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../common/gskdate.h"
#include "../common/gskbase64.h"

/* === output: writing a header to a buffer === */

static GEnumClass *gsk_http_connection_class = NULL;
static GEnumClass *gsk_http_content_encoding_class = NULL;
static GEnumClass *gsk_http_transfer_encoding_class = NULL;
static GEnumClass *gsk_http_verb_class = NULL;

static inline void
init_classes (void)
{
  gsk_http_connection_class = g_type_class_ref (GSK_TYPE_HTTP_CONNECTION);
  gsk_http_content_encoding_class = g_type_class_ref (GSK_TYPE_HTTP_CONTENT_ENCODING);
  gsk_http_transfer_encoding_class = g_type_class_ref (GSK_TYPE_HTTP_TRANSFER_ENCODING);
  gsk_http_verb_class = g_type_class_ref (GSK_TYPE_HTTP_VERB);
}

/* http status code descriptions */
typedef struct
{
  int         status_code;
  const char *description;
} GskHttpStatusDescription;

static gint
status_description_comparator(const GskHttpStatusDescription *desc_a,
                              const GskHttpStatusDescription *desc_b)
{
  int a = desc_a->status_code;
  int b = desc_b->status_code;
  if (a < b)
    return -1;
  if (a > b)
    return +1;
  return 0;
}

static const char* 
get_http_status_description(int id)
{
  static GskHttpStatusDescription descriptions[] = {
    {GSK_HTTP_STATUS_CONTINUE, "Continue"},
    {GSK_HTTP_STATUS_SWITCHING_PROTOCOLS, "Switching Protocols"},
    {200, "OK"},
    {201, "Created"},
    {202, "Accepted"},
    {203, "Non-Authoritative Information"},
    {204, "No Content"},
    {205, "Reset Content"},
    {206, "Partial Content"},
    {300, "Multiple Choices"},
    {301, "Moved Permanently"},
    {302, "Found"},
    {303, "See Other"},
    {304, "Not Modified"},
    {305, "Use Proxy"},
    {307, "Temporary Redirect"},
    {400, "Bad Request"},
    {401, "Unauthorized"},
    {402, "Payment Required"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {406, "Not Acceptable"},
    {407, "Proxy Authentication Required"},
    {408, "Request Time-out"},
    {409, "Conflict"},
    {410, "Gone"},
    {411, "Length Required"},
    {412, "Precondition Failed"},
    {413, "Request Entity Too Large"},
    {414, "Request-URI Too Large"},
    {415, "Unsupported Media Type"},
    {416, "Requested range not satisfiable"},
    {417, "Expectation Failed"},
    {500, "Internal Server Error"},
    {501, "Not Implemented"},
    {502, "Bad Gateway"},
    {503, "Service Unavailable"},
    {504, "Gateway Time-out"},
    {505, "HTTP Version not supported"}
  };
  GskHttpStatusDescription *description;

  description = bsearch (&id,
			 descriptions,
			 G_N_ELEMENTS (descriptions),
			 sizeof (descriptions[0]),
			 (GCompareFunc) status_description_comparator);
  if (description)
    return description->description;
  return "Unknown HTTP Status Code being proxied";
}


/* TODO: make this more efficient */
static void
print_request_first_line(GskHttpVerb            verb,
                         const char            *path,
			 int                    http_minor_version,
			 GskHttpHeaderPrintFunc print_func,
			 gpointer               data)
{
  guint len = strlen (path) + 100;
  char *tmp = g_alloca (len);
  GEnumValue *enum_value = g_enum_get_value (gsk_http_verb_class, verb);
  const char *verb_name = enum_value ? enum_value->value_nick : "unknown";
  char *at;
  g_snprintf (tmp, len, "%s %s HTTP/1.%d", verb_name, path, http_minor_version);
  for (at = tmp; *at != 0 && isalpha (*at); at++)
    *at = toupper (*at);
  (*print_func) (tmp, data);
}


static void
print_response_first_line(int                    code,
			  int                    http_minor_version,
			  GskHttpHeaderPrintFunc print_func,
			  gpointer               data)
{
  /* Assert: all status strings must be short enough!!!! */
  char buf[256];
  g_snprintf (buf, sizeof (buf),
	      "HTTP/1.%d %d %s",
	      http_minor_version,
	      code,
	      get_http_status_description (code));
  (*print_func) (buf, data);
}

/* NOTE: must sync with cookie_to_string */
static guint
cookie_max_length (GskHttpCookie *cookie)
{
  guint rv = 0;
  if (!cookie->key || !cookie->value)
    return 0;
  rv += strlen (cookie->key) + strlen (cookie->value) + 3;
  if (cookie->domain)
    rv += strlen (cookie->domain) + 9;
  if (cookie->expire_date)
    rv += strlen (cookie->expire_date) + 10;
  if (cookie->max_age >= 0)
    rv += 30;
  if (cookie->secure)
    rv += 10;
  if (cookie->version)
    rv += 12;
  if (cookie->path)
    rv += strlen (cookie->path) + 7;
  return rv;
}


static guint
cookie_to_string (GskHttpCookie *cookie,
		  char          *buf_at,
		  guint          remaining)

{
  char *start = buf_at;
  if (!cookie->key || !cookie->value)
    return 0;
  g_snprintf (buf_at, remaining, "%s=%s;", cookie->key, cookie->value);
  buf_at = strchr (buf_at, 0);
  if (cookie->domain)
    {
      g_snprintf (buf_at, remaining - (buf_at - start),
		  " Domain=%s;", cookie->domain);
      buf_at = strchr (buf_at, 0);
    }
  if (cookie->max_age >= 0)
    {
      g_snprintf (buf_at, remaining - (buf_at - start),
		  " Max-Age=%ld;", (long) cookie->max_age);
      buf_at = strchr (buf_at, 0);
    }
  if (cookie->expire_date)
    {
      g_snprintf (buf_at, remaining - (buf_at - start),
		  " Expires=%s;", cookie->expire_date);
      buf_at = strchr (buf_at, 0);
    }
  if (cookie->path)
    {
      g_snprintf (buf_at, remaining - (buf_at - start),
                  " Path=%s;", cookie->path);
      buf_at = strchr (buf_at, 0);
    }
  if (cookie->version)
    {
      g_snprintf (buf_at, remaining - (buf_at - start),
                  " Version=%u;",
                  cookie->version);
      buf_at = strchr (buf_at, 0);
    }
  if (cookie->secure)
    {
      g_snprintf (buf_at, remaining - (buf_at - start), " Secure;");
      buf_at = strchr (buf_at, 0);
    }

  return buf_at - start;
}

static void
print_cookielist    (const char             *header,
		     GSList                 *cookie_list,
		     GskHttpHeaderPrintFunc  print_func,
		     gpointer                data)
{
  if (cookie_list)
    {
      char *buf;
      guint index = 0;
      guint len = 0;
      GSList *tmp;
      for (tmp = cookie_list; tmp != NULL; tmp = tmp->next)
	len += cookie_max_length (tmp->data) + strlen (header) + 4;
      buf = g_alloca (len + 1);

      index = 0;
      for (tmp = cookie_list; tmp != NULL; tmp = tmp->next)
	{
          strcpy (buf + index, header);
          index += strlen (header);
          strcpy (buf + index, ": ");
          index += 2;
	  index += cookie_to_string (tmp->data, buf + index, len - index);
	  if (tmp->next != NULL)
	    {
	      strcpy (buf + index, "\r\n");
	      index += 2;
	    }
	}
      print_func (buf, data);
    }
}

static void
gsk_http_char_set_append_list (GskHttpCharSet        *set,
			       GskHttpHeaderPrintFunc print_func,
			       gpointer               print_data)
{
  GskHttpCharSet *at;
  guint approx_len = 20;
  guint cur_len;
  char *buf;
  for (at = set; at != NULL; at = at->next)
    approx_len += strlen (at->charset_name) + 50 + 5;
  buf = g_alloca (approx_len + 1);
  strcpy (buf, "Accept-CharSet: ");
  cur_len = 16;
  for (at = set; at != NULL; at = at->next)
    {
      strcpy (buf + cur_len, at->charset_name);
      cur_len += strlen (at->charset_name);
      if (set->quality >= 0.0)
	{
	  g_snprintf (buf, approx_len - cur_len, ";q=%.1g", set->quality);
	  cur_len += strlen (buf + cur_len);
	}
      if (at->next != NULL)
	{
	  strcpy (buf + cur_len, ", ");
	  cur_len += 2;
	}
    }
  buf[cur_len] = 0;
  print_func (buf, print_data);
}


static void
gsk_http_content_encoding_set_append_list (GskHttpContentEncodingSet     *set,
				           GskHttpHeaderPrintFunc  print_func,
				           gpointer                print_data)
{
  GskHttpContentEncodingSet *at;
  guint approx_len = 30;
  guint cur_len;
  char *buf;
  for (at = set; at != NULL; at = at->next)
    approx_len += 50 + 30;
  buf = g_alloca (approx_len + 1);
  strcpy (buf, "Accept-Encoding: ");
  cur_len = 17;

  for (at = set; at != NULL; at = at->next)
    {
      switch (at->encoding)
        {
	  case GSK_HTTP_CONTENT_ENCODING_UNRECOGNIZED:
	    /* XXX: error handling */
	    continue;
	  case GSK_HTTP_CONTENT_ENCODING_IDENTITY:
	    strcpy (buf + cur_len, "identity");
	    cur_len += 8;
	    break;
	  case GSK_HTTP_CONTENT_ENCODING_GZIP:
	    strcpy (buf + cur_len, "gzip");
	    cur_len += 4;
	    break;
	  case GSK_HTTP_CONTENT_ENCODING_COMPRESS:
	    strcpy (buf + cur_len, "compress");
	    cur_len += 7;
	    break;
	  default:
	    /* XXX: error handling */
	    g_warning ("gsk_http_content_encoding_set_append_list: "
	    		"unknown encoding %d", at->encoding);
	    break;
	}
      if (at->quality >= 0.0)
	{
	  g_snprintf (buf + cur_len, approx_len - cur_len,
		      ";q=%.1g", at->quality);
	  cur_len += strlen (buf + cur_len);
	}
      if (at->next != NULL)
	{
	  strcpy (buf + cur_len, ", ");
	  cur_len += 2;
	}
    }
  buf[cur_len] = 0;
  print_func (buf, print_data);
}

static void
gsk_http_transfer_encoding_set_append_list (GskHttpTransferEncodingSet *set,
				            GskHttpHeaderPrintFunc  print_func,
				            gpointer                print_data)
{
  GskHttpTransferEncodingSet *at;
  guint approx_len = 30;
  guint cur_len;
  char *buf;
  for (at = set; at != NULL; at = at->next)
    approx_len += 50 + 30;
  buf = g_alloca (approx_len + 1);
  strcpy (buf, "TE: ");
  cur_len = 17;

  for (at = set; at != NULL; at = at->next)
    {
      switch (at->encoding)
        {
	  case GSK_HTTP_TRANSFER_ENCODING_UNRECOGNIZED:
	    /* XXX: error handling */
	    continue;
	  case GSK_HTTP_TRANSFER_ENCODING_NONE:
	    strcpy (buf + cur_len, "none");
	    cur_len += 4;
	    break;
	  case GSK_HTTP_TRANSFER_ENCODING_CHUNKED:
	    strcpy (buf + cur_len, "chunked");
	    cur_len += 7;
	    break;
	  default:
	    /* XXX: error handling */
	    g_warning ("gsk_http_transfer_encoding_set_append_list: "
	    		"unknown encoding %d", at->encoding);
	    break;
	}
      if (at->quality >= 0.0)
	{
	  g_snprintf (buf + cur_len, approx_len - cur_len,
		      ";q=%.1g", at->quality);
	  cur_len += strlen (buf + cur_len);
	}
      if (at->next != NULL)
	{
	  strcpy (buf + cur_len, ", ");
	  cur_len += 2;
	}
    }
  buf[cur_len] = 0;
  print_func (buf, print_data);
}

static void
gsk_http_range_set_append_list    (GskHttpRangeSet    *list,
				   GskHttpHeaderPrintFunc  print_func,
				   gpointer                print_data)
{
  /* XXX: implement this!!! */
  //gsk_buffer_append (buffer, "Accept-Ranges: ", 15);
}

/* uh, standardize this */
#define QUALITY_MAX_LEN		64

static void
gsk_http_language_set_append_list    (GskHttpLanguageSet     *list,
				      GskHttpHeaderPrintFunc  print_func,
				      gpointer                print_data)
{
  GskHttpLanguageSet *at;
  guint max_len = strlen ("Accept-Language: ");
  char *line, *line_at;

  /* estimate length */
  for (at = list; at != NULL; at = at->next)
    {
      if (at->quality != -1.0)
	max_len += QUALITY_MAX_LEN + 4;	/* for our double. */
      max_len += strlen(at->language);
      max_len += 20;
    }

  /* produce line */
  line = g_alloca (max_len);
  line_at = line;

  strcpy (line_at, "Accept-Language: ");
  line_at = strchr (line_at, 0);

  for (at = list; at != NULL; at = at->next)
    {
      strcpy (line_at, at->language);
      line_at = strchr (line_at, 0);
      if (at->quality != -1)
	{
	  char qual_buf[QUALITY_MAX_LEN];
	  g_snprintf(qual_buf, QUALITY_MAX_LEN, ";q=%.6f", at->quality);
	  strcpy (line_at, qual_buf);
	  line_at = strchr (line_at, 0);;
	}
      if (at->next)
	*line_at++ = ',';
    }

  (*print_func) (line, print_data);
}

static void
gsk_http_media_type_set_append_list    (GskHttpMediaTypeSet    *list,
				        GskHttpHeaderPrintFunc  print_func,
				        gpointer                print_data)
{
  /* XXX: implement this!!! */
  // gsk_buffer_append (buffer, "Accept: ", 8);
}

static void
gsk_http_append_if_matches (char                   **matches,
			    GskHttpHeaderPrintFunc   print_func,
			    gpointer                 print_data)
{
  guint i;
  guint approx_len = 20;
  guint cur_len;
  char *buf;
  for (i = 0; matches[i] != NULL; i++)
    approx_len += strlen (matches[i]) + 5;
  buf = g_alloca (approx_len);
  strcpy (buf, "If-Match: ");
  cur_len = 10;
  for (i = 0; matches[i] != NULL; i++)
    {
      strcpy (buf + cur_len, matches[i]);
      cur_len += strlen (matches[i]);
      if (matches[i + 1] != NULL)
	{
	  strcpy (buf + cur_len, ", ");
	  cur_len += 2;
	}
    }
  print_func (buf, print_data);
}

static void
print_allowed_verb (guint                    allowed,
		    GskHttpHeaderPrintFunc   print_func,
		    gpointer                 print_data)

{
  char tmp[256];
  guint len = 7;
  strcpy (tmp, "Allow: ");
#define MAYBE_ADD_VERB(verb)				\
  G_STMT_START{						\
    if ((allowed & (1 << GSK_HTTP_VERB_ ## verb)) != 0)	\
      {							\
	if (len > 7)					\
	  {						\
	    strcpy (tmp + len, ", ");			\
	    len += 2;					\
	  }						\
	strcpy (tmp, #verb);				\
	len += strlen (#verb);				\
      }							\
  }G_STMT_END
  MAYBE_ADD_VERB (GET);
  MAYBE_ADD_VERB (POST);
  MAYBE_ADD_VERB (PUT);
  MAYBE_ADD_VERB (HEAD);
  MAYBE_ADD_VERB (OPTIONS);
  MAYBE_ADD_VERB (DELETE);
  MAYBE_ADD_VERB (TRACE);
#undef MAYBE_ADD_VERB
  (*print_func) (tmp, print_data);
}

static void
print_content_type (const char      *type,
		    const char      *subtype,
		    const char      *charset,
		    GSList          *additional,
		    GskHttpHeaderPrintFunc print_func,
		    gpointer print_data)
{
  guint approx_len = 20
                   + (type ? strlen (type) : 5) + 3
                   + (subtype ? strlen (subtype) : 5) + 3
                   + (charset ? strlen (charset) + 20 : 5) + 3;
  GSList *at;
  guint cur_len;
  char *buf;
  for (at = additional; at != NULL; at = at->next)
    approx_len += strlen (additional->data) + 5;
  buf = g_alloca (approx_len + 1);
  strcpy (buf, "Content-Type: ");
  cur_len = 14;

  if (type == NULL)
    buf[cur_len++] = '*';
  else
    {
      strcpy (buf + cur_len, type);
      cur_len += strlen (buf + cur_len);
    }
  buf[cur_len++] = '/';
  if (subtype == NULL)
    {
      buf[cur_len++] = '*';
      buf[cur_len] = 0;
    }
  else
    {
      strcpy (buf + cur_len, subtype);
      cur_len += strlen (buf + cur_len);
    }
  if (charset != NULL)
    {
      strcpy (buf + cur_len, "; charset=");
      cur_len += 10;
      strcpy (buf + cur_len, charset);
      cur_len += strlen (buf + cur_len);
    }
  for (at = additional; at != NULL; at = at->next)
    {
      buf[cur_len++] = ';';
      buf[cur_len++] = ' ';
      strcpy (buf + cur_len, additional->data);
      cur_len += strlen (additional->data);
    }
  g_assert (buf[cur_len] == 0);
  print_func (buf, print_data);
}

static void
print_date_line (const char            *tag,
		 time_t                 date,
		 GskHttpHeaderPrintFunc print_func,
		 gpointer               print_data)
{
  char tmp[256];
  guint len = strlen (tag);
  memcpy (tmp, tag, len);
  strcpy (tmp + len, ": ");
  len += 2;
  g_assert (len < sizeof (tmp));
  gsk_date_print_timet (date, tmp + len, sizeof (tmp) - len, GSK_DATE_FORMAT_1123);
  (*print_func) (tmp, print_data);
}

static void
print_retry_after (gboolean                is_relative,
		   long                    t,
		   GskHttpHeaderPrintFunc  print_func,
		   gpointer                print_data)
{
  if (is_relative)
    {
      char tmp[128];
      g_snprintf (tmp, sizeof (tmp), "Retry-After: %ld", t);
      (*print_func) (tmp, print_data);
    }
  else
    print_date_line ("Retry-After", t, print_func, print_data);
}

static void
print_response_cache_control(GskHttpResponseCacheDirective  *cache_dir,
                             GskHttpHeaderPrintFunc          print_func,
                             gpointer                        print_data)
{
  char numbuf[64];
  char buf[2048];
  char *end;
  strcpy (buf, "Cache-Control:");
  end = strchr (buf, 0);
#define APPEND(str) G_STMT_START{strcpy(end,str);end=strchr(end,0);}G_STMT_END
#define APPEND_UINT(ui) G_STMT_START{g_snprintf(numbuf,sizeof(numbuf),"%u",ui);APPEND(numbuf);}G_STMT_END
 
  if (cache_dir->no_cache)
    {
      APPEND (" no-cache");
      if (cache_dir->no_cache_name)
        {
          APPEND ("=");
          APPEND (cache_dir->no_cache_name);
        }
      APPEND (",");
    }
  if (cache_dir->no_store)
    {
      APPEND (" no-store,");
    }
  if (cache_dir->no_transform)
    {
      APPEND (" no-transform,");
    }
  if (cache_dir->is_public)
    {
      APPEND (" public,");
    }
  if (cache_dir->is_private)
    {
      APPEND (" private");
      if (cache_dir->private_name)
        {
          APPEND ("=");
          APPEND (cache_dir->private_name);
        }
        APPEND (",");
    }
  if (cache_dir->must_revalidate)
    {
      APPEND (" must-revalidate,");
    }
  if (cache_dir->proxy_revalidate)
    {
      APPEND (" proxy-revalidate,");
    }
  if (cache_dir->max_age > 0)
    {
      APPEND (" max-age=");
      APPEND_UINT (cache_dir->max_age);
      APPEND (",");
    }
  if (cache_dir->s_max_age > 0)
    {
      APPEND (" s-maxage=");
      APPEND_UINT (cache_dir->s_max_age);
      APPEND (",");
    }
  print_func (buf, print_data);

#undef APPEND_UINT
#undef APPEND
}


static void
print_request_cache_control (GskHttpRequestCacheDirective *cache_dir,
                             GskHttpHeaderPrintFunc        print_func,
                             gpointer                      print_data)
{
  char numbuf[64];
  char buf[2048];
  char *end;
  strcpy (buf, "Cache-Control:");
  end = strchr (buf, 0);
#define APPEND(str) G_STMT_START{strcpy(end,str);end=strchr(end,0);}G_STMT_END
#define APPEND_UINT(ui) G_STMT_START{g_snprintf(numbuf,sizeof(numbuf),"%u",ui);APPEND(numbuf);}G_STMT_END
 
  if (cache_dir->no_cache) 
    {
      APPEND (" no-cache,");
    }
  if (cache_dir->no_store)
    {
      APPEND (" no-store,");
    }
  if (cache_dir->max_age > 0)
    {
      APPEND (" max-age=");
      APPEND_UINT (cache_dir->max_age);
      APPEND (",");
    }
  if (cache_dir->max_stale != 0) 
    {
      if (cache_dir->max_stale > 0) /* set with arg */
    {
      APPEND (" max-stale=");
      APPEND_UINT (cache_dir->max_stale);
    }
      else /* set with no argument */
	{
	  APPEND (" max-stale");
	}
      APPEND (",");
    }
  if (cache_dir->min_fresh > 0)
    {
      APPEND (" min-fresh=");
      APPEND_UINT (cache_dir->min_fresh);
      APPEND (",");
    }
  if (cache_dir->no_transform)
    {
      APPEND (" no-transform,");
    }
  if (cache_dir->only_if_cached)
    {
      APPEND (" only-if-cached,");
    }
  print_func (buf, print_data);

#undef APPEND_UINT
#undef APPEND
}

static void
print_header_line (const char             *tag,
		   const char             *value,
		   GskHttpHeaderPrintFunc  print_func,
		   gpointer                print_data)
{
  guint len_tag = strlen (tag);
  guint len = 3 + len_tag + strlen (value);
  char *line = g_alloca (len + 1);
  strcpy (line, tag);
  line[len_tag] = ':';
  line[len_tag + 1] = ' ';
  strcpy (line + len_tag + 2, value);
  (*print_func) (line, print_data);
}

typedef struct _PrintInfo PrintInfo;
struct _PrintInfo
{
  GskHttpHeaderPrintFunc print_func;
  gpointer               print_data;
};

static void
append_key_value_to_print_info (gpointer         key,
			        gpointer         value,
			        gpointer         data)
{
  PrintInfo *info = data;
  guint key_len = strlen (key);
  guint value_len = strlen (value);
  guint total_len = key_len + 2 + value_len + 1;
  char *buf = g_alloca (total_len + 1);
  g_snprintf (buf, total_len, "%s: %s", (char *) key, (char *) value);
  info->print_func (buf, info->print_data);
}

/**
 * gsk_http_header_print:
 * @http_header: the header to output, line-by-line.
 * @print_func: a function to call on each line of output.
 * @print_data: data to pass to @print_func.
 *
 * Print the HTTP header line-by-line, through a generic
 * printing function.  This could conceivable save memory
 * and allow better streaming.
 */
void
gsk_http_header_print(GskHttpHeader          *http_header,
		      GskHttpHeaderPrintFunc  print_func,
		      gpointer                print_data)
{
  const char *type;
  GEnumValue *enum_value;
  GskHttpRequest *request = NULL;
  GskHttpResponse *response = NULL;
  GSList *list;
  if (gsk_http_verb_class == NULL)
    init_classes ();
  if (GSK_IS_HTTP_REQUEST (http_header))
    request = GSK_HTTP_REQUEST (http_header);
  if (GSK_IS_HTTP_RESPONSE (http_header))
    response = GSK_HTTP_RESPONSE (http_header);

  /*
   * print the first line.
   */
  if (request)
    {
      print_request_first_line (request->verb,
				request->path,
				http_header->http_minor_version,
				print_func, print_data);
    }
  else
    {
      print_response_first_line (response->status_code,
				 http_header->http_minor_version,
				 print_func, print_data);
    }

#define MAYBE_PRINT_TAG(string, tag)					\
  G_STMT_START{								\
    if ((string) != NULL)						\
      print_header_line (tag, (string), print_func, print_data);	\
  }G_STMT_END
#define MAYBE_PRINT_STRING(object, member, tag)				\
  G_STMT_START{								\
    if ((object) != NULL)						\
      MAYBE_PRINT_TAG ((object)->member, tag);				\
  }G_STMT_END
#define MAYBE_PRINT_DATE(object, member, tag)				\
  G_STMT_START{								\
    if ((object)->member != (time_t)-1)					\
      print_date_line (tag, (object)->member, print_func, print_data);	\
  }G_STMT_END

  /* Host: */
  MAYBE_PRINT_STRING (request, host, "Host");

  /*
   * Content-Length:
   */
  if (http_header->content_length >= 0)
    {
      char content_length[128];
      g_snprintf (content_length, sizeof (content_length),
		  "Content-Length: %"G_GUINT64_FORMAT,
                  (guint64) http_header->content_length);
      print_func (content_length, print_data);
    }

  /* 
   * Connection:
   */
  if (http_header->http_minor_version < 1)
    {
      if (http_header->connection_type == GSK_HTTP_CONNECTION_KEEPALIVE)
	{
	  /* XXX: TODO: we should support this.  see RFC 2068 for HTTP/1.0 things... */
	  g_warning ("!! WE DON'T SUPPORT KEEPALIVE FOR HTTP/1.0");
	}
      else if (http_header->connection_type == GSK_HTTP_CONNECTION_CLOSE)
	print_func ("Connection: close", print_data);
    }
  else
    {
      if (http_header->connection_type == GSK_HTTP_CONNECTION_KEEPALIVE
       || http_header->connection_type == GSK_HTTP_CONNECTION_NONE)
	/* Default: nothing to do. */
	;
      else if (http_header->connection_type == GSK_HTTP_CONNECTION_CLOSE)
	print_func ("Connection: close", print_data);
      else
	g_warning ("unknown connection type");
    }

  /*
   * Content-Encoding:
   */
  if (http_header->content_encoding_type == GSK_HTTP_CONTENT_ENCODING_IDENTITY)
    type = NULL;
  else if (http_header->content_encoding_type == GSK_HTTP_CONTENT_ENCODING_UNRECOGNIZED)
    type = http_header->content_encoding;
  else
    {
      enum_value = g_enum_get_value (gsk_http_content_encoding_class, http_header->content_encoding_type);
      type = enum_value ? enum_value->value_nick : NULL;
    }
  MAYBE_PRINT_TAG (type, "Content-Encoding");

  /*
   * Content-Language:
   */
  if (http_header->content_languages != NULL)
    {
      char *value;
      /* TODO: avoid allocation? */
      value = g_strjoinv (", ", http_header->content_languages);
      MAYBE_PRINT_TAG (value, "Content-Language");
      g_free (value);
    }

  /*
   * Transfer-Encoding:
   */
  if (http_header->transfer_encoding_type == GSK_HTTP_TRANSFER_ENCODING_NONE)
    type = NULL;
  else if (http_header->transfer_encoding_type == GSK_HTTP_TRANSFER_ENCODING_UNRECOGNIZED)
    type = http_header->unrecognized_transfer_encoding;
  else
    {
      enum_value = g_enum_get_value (gsk_http_transfer_encoding_class, http_header->transfer_encoding_type);
      type = enum_value ? enum_value->value_nick : NULL;
    }
  MAYBE_PRINT_TAG (type, "Transfer-Encoding");

  /* Add `Date' */
  MAYBE_PRINT_DATE (http_header, date, "Date");

  /* Add `Pragma's */
  for (list = http_header->pragmas; list; list = list->next)
    print_header_line ("Pragma", (char*)(list->data), print_func, print_data);

  /*
   * Add all the cookies.
   */
  if (request)
    print_cookielist ("Cookie", request->cookies, print_func, print_data);
  else
    print_cookielist ("Set-Cookie", response->set_cookies, print_func, print_data);

  /* Add `Content-Type' */
  if (http_header->has_content_type)
    {
      print_content_type (http_header->content_type, http_header->content_subtype,
			  http_header->content_charset, http_header->content_additional,
			  print_func, print_data);
    }

  /* Add `Accept-Ranges' */
  if (http_header->accepted_range_units != NULL)
    gsk_http_range_set_append_list (http_header->accepted_range_units,
                                    print_func, print_data);


  /*
   * Add the various fields from `info'.
   */
  if (request)
    {
      /* Add `Accept-CharSet' */
      if (request->accept_charsets)
	gsk_http_char_set_append_list (request->accept_charsets,
				       print_func, print_data);
      /* Add `Accept-Encoding' */
      if (request->accept_content_encodings != NULL)
	gsk_http_content_encoding_set_append_list (request->accept_content_encodings,
					            print_func, print_data);
      /* Add `Accept-Encoding' */
      if (request->accept_transfer_encodings != NULL)
	gsk_http_transfer_encoding_set_append_list (request->accept_transfer_encodings,
					            print_func, print_data);

      /* Add `Accept-Languages' */
      if (request->accept_languages != NULL)
	gsk_http_language_set_append_list (request->accept_languages,
					   print_func, print_data);

      /* Add `Accept' */
      if (request->accept_media_types != NULL)
	gsk_http_media_type_set_append_list (request->accept_media_types,
					     print_func, print_data);
      /* Add `If-Match' */
      if (request->had_if_match)
	gsk_http_append_if_matches (request->if_match, print_func, print_data);

      /* Add `If-Modified-Since' */
      MAYBE_PRINT_DATE (request, if_modified_since, "If-Modified-Since");

      /* Add `User-Agent' */
      MAYBE_PRINT_STRING (request, user_agent, "User-Agent");

      /* Add `Referer' */
      MAYBE_PRINT_STRING (request, referrer, "Referer");

      /* Add `From' */
      MAYBE_PRINT_STRING (request, from, "From");

      ///* Add `Proxy-Authorization' */
      //MAYBE_PRINT_STRING (request, proxy_auth.credentials, "Proxy-Authorization");

      /* Add `Keep-Alive' */
      if (request->keep_alive_seconds >= 0)
        {
	  char tmp[128];
	  g_snprintf (tmp, sizeof (tmp), "Keep-Alive: %d", request->keep_alive_seconds);
	  (*print_func) (tmp, print_data);
	}

      /* Add `Max-Forwards' */
      if (request->max_forwards >= 0)
        {
	  char tmp[128];
	  g_snprintf (tmp, sizeof (tmp), "Max-Forwards: %d", request->max_forwards);
	  (*print_func) (tmp, print_data);
	}

      /* Add cache-controle directives */
      if (request->cache_control)
	{
	  print_request_cache_control (request->cache_control,
					print_func, print_data);
	}
    }
  else
    {
      /*
       * Add the response-specific headers.
       */

      /* Add `Age' */
      if (response->age >= 0)
        {
	  char tmp[64];
	  g_snprintf (tmp, sizeof (tmp), "Age: %d", response->age);
	  (*print_func) (tmp, print_data);
	}

      /* Add `Allow' */
      if (response->allowed_verbs)
	print_allowed_verb (response->allowed_verbs, print_func, print_data);

      /* Add base64-encoded MD5 checksum (Content-MD5) */
      if (response->has_md5sum)
        {
	  char encoded[50 + GSK_BASE64_GET_ENCODED_LEN (16)];
	  strcpy (encoded, "Content-MD5: ");
	  gsk_base64_encode (encoded + 13, (char*)response->md5sum, 16);
	  encoded[13 + GSK_BASE64_GET_ENCODED_LEN (16)] = 0;
	  (*print_func) (encoded, print_data);
	}

      /* Add `Expires' */
      if (response->expires != (time_t)(-1))
        {
          MAYBE_PRINT_DATE (response, expires, "Expires");
        }
      else
        {
          MAYBE_PRINT_STRING (response, expires_str, "Expires");
        }

      /* Add `ETag' */
      MAYBE_PRINT_STRING (response, etag, "ETag");

      /* Location: */
      MAYBE_PRINT_STRING (response, location, "Location");

      ///* Add `Proxy-Authenticate' */
      //MAYBE_PRINT_STRING (response, proxy_auth.challenge, "Proxy-Authenticate");

      /* Add `Retry-After' */
      if (response->has_retry_after)
	print_retry_after (response->retry_after_relative,
			   response->retry_after,
			   print_func, print_data);

      /* Add `Last-Modified' */
      MAYBE_PRINT_DATE (response, last_modified, "Last-Modified");

      /* Add `Server' */
      MAYBE_PRINT_STRING (response, server, "Server");

      /* Add cache-controle directives */
      if (response->cache_control)
        print_response_cache_control (response->cache_control,
                             print_func, print_data);
    }

  /*
   * And the miscellaneous headers.
   */
  if (http_header->header_lines)
    {
      PrintInfo info;
      info.print_func = print_func;
      info.print_data = print_data;
      g_hash_table_foreach (http_header->header_lines,
			    append_key_value_to_print_info, &info);
    }
}

/* convenience api */
/**
 * gsk_http_header_to_buffer:
 * @header: the HTTP header to write into the buffer.
 * @output: the buffer to store the header in, as text.
 *
 * Appends an HTTP header into a buffer.
 */
static inline void
add_newline_to_buffer (GskBuffer *buffer)
{
  gsk_buffer_append (buffer, "\r\n", 2);
}

static void
write_header_line_to_buffer_print_func (const char      *text,
					gpointer         buffer_ptr)
{
  GskBuffer *buffer = buffer_ptr;
  gsk_buffer_append_string (buffer, text);
  add_newline_to_buffer (buffer);
}

void
gsk_http_header_to_buffer (GskHttpHeader *header,
                           GskBuffer     *output)
{
  gsk_http_header_print (header, write_header_line_to_buffer_print_func, output);
  add_newline_to_buffer (output);
}

