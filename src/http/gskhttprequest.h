#ifndef __GSK_HTTP_REQUEST_H_
#define __GSK_HTTP_REQUEST_H_

#ifndef __GSK_HTTP_HEADER_H_
#include "gskhttpheader.h"
#endif

G_BEGIN_DECLS

typedef struct _GskHttpRequestClass GskHttpRequestClass;
typedef struct _GskHttpRequest GskHttpRequest;

#define GSK_TYPE_HTTP_REQUEST              (gsk_http_request_get_type ())
#define GSK_HTTP_REQUEST(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_HTTP_REQUEST, GskHttpRequest))
#define GSK_HTTP_REQUEST_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_HTTP_REQUEST, GskHttpRequestClass))
#define GSK_HTTP_REQUEST_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_HTTP_REQUEST, GskHttpRequestClass))
#define GSK_IS_HTTP_REQUEST(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_HTTP_REQUEST))
#define GSK_IS_HTTP_REQUEST_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_HTTP_REQUEST))

struct _GskHttpRequestClass
{
  GskHttpHeaderClass base_class;
};
struct _GskHttpRequest
{
  GskHttpHeader base_instance;
  
  /*< public >*/
  /* the command: GET, PUT, POST, HEAD, etc */
  GskHttpVerb                   verb;

  /* Note that HTTP/1.1 servers must accept the entire
   * URL being included in `path'! (maybe including http:// ... */
  char                         *path;

  GskHttpCharSet           *accept_charsets;              /* Accept-CharSet */
  GskHttpContentEncodingSet*accept_content_encodings;     /* Accept-Encoding */
  GskHttpTransferEncodingSet*accept_transfer_encodings;   /* TE */
  GskHttpMediaTypeSet      *accept_media_types;           /* Accept */
  GskHttpAuthorization     *authorization;                /* Authorization */
  GskHttpLanguageSet       *accept_languages;             /* Accept-Languages */
  char                     *host;                         /* Host */

  gboolean                  had_if_match;
  char                    **if_match;             /* If-Match */
  glong                     if_modified_since;    /* If-Modified-Since */
  char                     *user_agent;           /* User-Agent */

  char                     *referrer;             /* Referer */

  char                     *from;      /* The From: header (sect 14.22) */

  /* List of Cookie: headers. */
  GSList                   *cookies;

  GskHttpAuthorization     *proxy_authorization;

  int                       keep_alive_seconds;   /* -1 if not used */

  /* rarely used: */
  int                       max_forwards;         /* -1 if not used */

  /* Nonstandard User-Agent information.
     Many browsers provide this data to allow
     dynamic content to take advantage of the
     client configuration.  (0 indicated "not supplied").  */
  unsigned                  ua_width, ua_height;
  char                     *ua_color;
  char                     *ua_os;
  char                     *ua_cpu;
  char                     *ua_language;

  GskHttpRequestCacheDirective *cache_control;        /* Cache-Control */
};

/* --- cookies --- */
void            gsk_http_request_add_cookie      (GskHttpRequest *header,
                                                  GskHttpCookie  *cookie);
void            gsk_http_request_remove_cookie   (GskHttpRequest *header,
                                                  GskHttpCookie  *cookie);
GskHttpCookie  *gsk_http_request_find_cookie     (GskHttpRequest *header,
                                                  const char     *key);

void gsk_http_request_set_cache_control (GskHttpRequest               *request,
                                         GskHttpRequestCacheDirective *directive);

gboolean        gsk_http_request_has_content_body (GskHttpRequest *request);

/* request specific functions */
/* unhandled: if_match */
void            gsk_http_request_add_charsets            (GskHttpRequest *header,
                                                          GskHttpCharSet *char_sets);
void            gsk_http_request_clear_charsets          (GskHttpRequest *header);
void            gsk_http_request_add_content_encodings   (GskHttpRequest *header,
                                                          GskHttpContentEncodingSet *set);
void            gsk_http_request_clear_content_encodings (GskHttpRequest *header);
void            gsk_http_request_add_transfer_encodings  (GskHttpRequest *header,
                                                          GskHttpTransferEncodingSet *set);
void            gsk_http_request_clear_transfer_encodings(GskHttpRequest *header);
void            gsk_http_request_add_media               (GskHttpRequest *header,
                                                          GskHttpMediaTypeSet *set);
void            gsk_http_request_clear_media             (GskHttpRequest *header);
void            gsk_http_request_set_authorization       (GskHttpRequest  *request,
					                  gboolean         is_proxy_auth,
					                  GskHttpAuthorization *auth);
GskHttpAuthorization*
                gsk_http_request_peek_authorization      (GskHttpRequest  *request,
				                          gboolean    is_proxy_auth);
/* Requests */
GskHttpRequest *gsk_http_request_new_blank               (void);
GskHttpRequest *gsk_http_request_new                     (GskHttpVerb  verb,
						          const char  *path);

typedef enum
{
  GSK_HTTP_REQUEST_FIRST_LINE_ERROR,
  GSK_HTTP_REQUEST_FIRST_LINE_SIMPLE,
  GSK_HTTP_REQUEST_FIRST_LINE_FULL
} GskHttpRequestFirstLineStatus;

GskHttpRequestFirstLineStatus
                gsk_http_request_parse_first_line        (GskHttpRequest *request,
				                          const char     *line,
                                                          GError        **error);

GHashTable     *gsk_http_request_parse_cgi_query_string  (const char     *query_string);

/* XXX: duplicates gsk_url_split_form_urlencoded! */
char **         gsk_http_parse_cgi_query_string          (const char     *query_string,
                                                          GError        **error);

/* macros to get/set properties.  */
#define gsk_http_request_set_verb(request, verb)	\
    g_object_set(GSK_HTTP_REQUEST(request), "verb", (GskHttpVerb)(verb), NULL)
#define gsk_http_request_get_verb(request) (GSK_HTTP_REQUEST(request)->verb)
#define gsk_http_request_set_if_modified_since(request, t) \
    g_object_set(GSK_HTTP_REQUEST(request), "if-modified-since", (long)(t), NULL)
#define gsk_http_request_get_if_modified_since(request) \
    (GSK_HTTP_REQUEST(request)->if_modified_since)
#define gsk_http_request_set_from(request, from)			      \
  g_object_set (GSK_HTTP_REQUEST(request), "from", (const char *) (from), NULL)
#define gsk_http_request_set_host(request, host)			      \
  g_object_set (GSK_HTTP_REQUEST(request), "host", (const char *) (host), NULL)
#define gsk_http_request_peek_from(request)				      \
  (GSK_HTTP_REQUEST(request)->from)
#define gsk_http_request_set_user_agent(request, user_agent)	              \
  g_object_set (GSK_HTTP_REQUEST(request), "user-agent", (const char *) (user_agent), NULL)
#define gsk_http_request_peek_user_agent(request)		              \
  (GSK_HTTP_REQUEST(request)->user_agent)

G_END_DECLS

#endif
