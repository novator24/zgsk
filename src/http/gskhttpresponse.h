#ifndef __GSK_HTTP_RESPONSE_H_
#define __GSK_HTTP_RESPONSE_H_

#ifndef __GSK_HTTP_HEADER_H_
#include "gskhttpheader.h"
#endif

G_BEGIN_DECLS

typedef struct _GskHttpResponseClass GskHttpResponseClass;
typedef struct _GskHttpResponse GskHttpResponse;

#define GSK_TYPE_HTTP_RESPONSE             (gsk_http_response_get_type ())
#define GSK_HTTP_RESPONSE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_HTTP_RESPONSE, GskHttpResponse))
#define GSK_HTTP_RESPONSE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_HTTP_RESPONSE, GskHttpResponseClass))
#define GSK_HTTP_RESPONSE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_HTTP_RESPONSE, GskHttpResponseClass))
#define GSK_IS_HTTP_RESPONSE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_HTTP_RESPONSE))
#define GSK_IS_HTTP_RESPONSE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_HTTP_RESPONSE))

struct _GskHttpResponseClass
{
  GskHttpHeaderClass base_class;
};
struct _GskHttpResponse
{
  GskHttpHeader base_instance;
  
  GskHttpStatus             status_code;
  int                       age;                  /* Age */

  /* initially allowed_verbs == 0;
   * since it is an error not to allow any verbs;
   * otherwise it is a bitwise-OR: (1 << GSK_HTTP_VERB_*)
   */
  guint                     allowed_verbs;

  GskHttpResponseCacheDirective *cache_control;        /* Cache-Control */

  unsigned                  has_md5sum : 1;
  unsigned char             md5sum[16];           /* Content-MD5 (14.15) */

  /* List of Set-Cookie: headers. */
  GSList                   *set_cookies;

  /* The `Location' to redirect to. */
  char                     *location;

  /* may be set to ((time_t) -1) to omit them. */
  glong                     expires;
  char                     *expires_str;

  /* The ``Entity-Tag'', cf RFC 2616, Sections 14.24, 14.26, 14.44. */
  char                     *etag;

  GskHttpAuthenticate      *proxy_authenticate;

  /* This is the WWW-Authenticate: header line. */
  GskHttpAuthenticate      *authenticate;

  /* If `retry_after_relative', the retry_after is the number 
   * of seconds to wait before retrying; otherwise,
   * it is a unix-time indicting when to retry.
   *
   * (Corresponding to the `Retry-After' header, cf RFC 2616, 14.37)
   */
  guint                     has_retry_after : 1;
  gboolean                  retry_after_relative;
  glong                     retry_after;

  /* The Last-Modified header.  If != -1, this is the unix-time
   * the message-body-contents were last modified. (RFC 2616, section 14.29)
   */
  glong                     last_modified;

  char                     *server;        /* The Server: header */
};

/* Responses. */
GskHttpResponse *gsk_http_response_new_blank    (void);

/* Redirects should be accompanied by an HTML body saying the URL. */
GskHttpResponse *gsk_http_response_new_redirect (const char    *location);

GskHttpResponse *gsk_http_response_from_request (GskHttpRequest *request,
						 GskHttpStatus   status_code,
						 gint64          length);

gboolean   gsk_http_response_process_first_line (GskHttpResponse *response,
				                 const char      *line);

void       gsk_http_response_set_retry_after   (GskHttpResponse *response,
                                                gboolean         is_relative,
                                                glong            time);
void       gsk_http_response_set_no_retry_after(GskHttpResponse *response);

void       gsk_http_response_set_authenticate (GskHttpResponse *response,
					       gboolean         is_proxy_auth,
					       GskHttpAuthenticate *auth);
GskHttpAuthenticate*
           gsk_http_response_peek_authenticate(GskHttpResponse *response,
				               gboolean         is_proxy_auth);

/* --- setting / getting --- */
gboolean   gsk_http_response_has_content_body   (GskHttpResponse *response,
                                                 GskHttpRequest  *request);
void       gsk_http_response_set_cache_control(
					    GskHttpResponse *response,
				            GskHttpResponseCacheDirective *directive);
#define    gsk_http_response_set_status_code(response, status)	\
  G_STMT_START{ GSK_HTTP_RESPONSE(response)->status_code = (status); G_STMT_END
#define    gsk_http_response_get_status_code(response) \
               (GSK_HTTP_RESPONSE(response)->status_code)
void       gsk_http_response_set_allowed_verbs  (GskHttpResponse *response,
                                                 guint            allowed);
#define gsk_http_response_get_allowed_verbs(header)		              \
  (GSK_HTTP_RESPONSE(header)->allowed_verbs)
/* md5sum may be NULL to unset it */
void             gsk_http_response_set_md5      (GskHttpResponse *response,
                                                 const guint8    *md5sum);
const guint8    *gsk_http_response_peek_md5     (GskHttpResponse *response);
#define gsk_http_response_set_location(response, location)		      \
  g_object_set (GSK_HTTP_RESPONSE(response), "location", (const char *) (location), NULL)
#define gsk_http_response_peek_location(response)		              \
  (GSK_HTTP_RESPONSE(response)->location)
#define gsk_http_response_set_etag(response, etag)			      \
  g_object_set (GSK_HTTP_RESPONSE(response), "etag", (const char *)(etag), NULL)
#define gsk_http_response_peek_etag(response)				      \
  (GSK_HTTP_RESPONSE(response)->etag)
#define gsk_http_response_set_server(response, server)			      \
  g_object_set (GSK_HTTP_RESPONSE(response), "server", (const char *) (server), NULL)
#define gsk_http_response_peek_server(response)				      \
  (GSK_HTTP_RESPONSE(response)->server)
#define gsk_http_response_set_expires(response, expires)	              \
  g_object_set (GSK_HTTP_RESPONSE(response), "expires", (long) (expires), NULL)
#define gsk_http_response_get_expires(response)				      \
  (GSK_HTTP_RESPONSE(response)->expires)
#define gsk_http_response_peek_cache_control(response)	                      \
	(GSK_HTTP_RESPONSE(response)->cache_control)
#define gsk_http_response_set_age(response, age)	              	      \
  g_object_set (GSK_HTTP_RESPONSE(response), "age", (long) (age), NULL)
#define gsk_http_response_get_age(response)		                      \
	(GSK_HTTP_RESPONSE(response)->age)
#define gsk_http_response_set_last_modified(response, last_modified)	      \
  g_object_set (GSK_HTTP_RESPONSE(response), "last-modified", (long) (last_modified), NULL)
#define gsk_http_response_get_last_modified(response)		              \
  (GSK_HTTP_RESPONSE(response)->last_modified)

#define gsk_http_response_set_content_type gsk_http_header_set_content_type
#define gsk_http_response_get_content_type gsk_http_header_get_content_type
#define gsk_http_response_set_content_subtype gsk_http_header_set_content_subtype
#define gsk_http_response_get_content_subtype gsk_http_header_get_content_subtype

G_END_DECLS

#endif
