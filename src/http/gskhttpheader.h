/*
    GSK - a library to write servers
    Copyright (C) 1999-2000 Dave Benson

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA

    Contact:
        daveb@ffem.org <Dave Benson>
*/

/*
 * As per the HTTP 1.1 Specification, RFC 2616.
 *
 * TODO:  Compliance notes.
 */

#ifndef __GSK_HTTP_HEADER_H_
#define __GSK_HTTP_HEADER_H_

#include <glib-object.h>
#include "../gskbuffer.h"

G_BEGIN_DECLS

typedef struct _GskHttpHeaderClass GskHttpHeaderClass;
typedef struct _GskHttpHeader GskHttpHeader;
typedef struct _GskHttpAuthorization GskHttpAuthorization;
typedef struct _GskHttpAuthenticate GskHttpAuthenticate;
typedef struct _GskHttpCharSet GskHttpCharSet;
typedef struct _GskHttpResponseCacheDirective GskHttpResponseCacheDirective;
typedef struct _GskHttpRequestCacheDirective GskHttpRequestCacheDirective;
typedef struct _GskHttpCookie GskHttpCookie;
typedef struct _GskHttpLanguageSet GskHttpLanguageSet;
typedef struct _GskHttpMediaTypeSet GskHttpMediaTypeSet;
typedef struct _GskHttpContentEncodingSet GskHttpContentEncodingSet;
typedef struct _GskHttpTransferEncodingSet GskHttpTransferEncodingSet;
typedef struct _GskHttpRangeSet GskHttpRangeSet;

/* enums */
GType gsk_http_status_get_type (void) G_GNUC_CONST;
GType gsk_http_verb_get_type (void) G_GNUC_CONST;
GType gsk_http_content_encoding_get_type (void) G_GNUC_CONST;
GType gsk_http_transfer_encoding_get_type (void) G_GNUC_CONST;
GType gsk_http_range_get_type (void) G_GNUC_CONST;
GType gsk_http_connection_get_type (void) G_GNUC_CONST;

/* boxed types */
GType gsk_http_cookie_get_type (void) G_GNUC_CONST;
GType gsk_http_authorization_get_type (void) G_GNUC_CONST;
GType gsk_http_authenticate_get_type (void) G_GNUC_CONST;
GType gsk_http_char_set_get_type (void) G_GNUC_CONST;
GType gsk_http_cache_directive_get_type (void) G_GNUC_CONST;
GType gsk_http_language_set_get_type (void) G_GNUC_CONST;
GType gsk_http_media_type_set_get_type (void) G_GNUC_CONST;
GType gsk_http_content_encoding_set_get_type (void) G_GNUC_CONST;
GType gsk_http_transfer_encoding_set_get_type (void) G_GNUC_CONST;
GType gsk_http_range_set_get_type (void) G_GNUC_CONST;

/* object types */
GType gsk_http_header_get_type (void) G_GNUC_CONST;
GType gsk_http_request_get_type (void) G_GNUC_CONST;
GType gsk_http_response_get_type (void) G_GNUC_CONST;

/* type macros */
#define GSK_TYPE_HTTP_STATUS       (gsk_http_status_get_type ())
#define GSK_TYPE_HTTP_VERB         (gsk_http_verb_get_type ())
#define GSK_TYPE_HTTP_TRANSFER_ENCODING     (gsk_http_transfer_encoding_get_type ())
#define GSK_TYPE_HTTP_CONTENT_ENCODING     (gsk_http_content_encoding_get_type ())
#define GSK_TYPE_HTTP_RANGE        (gsk_http_range_get_type ())
#define GSK_TYPE_HTTP_CONNECTION   (gsk_http_connection_get_type ())
#define GSK_TYPE_HTTP_MEDIA_SET    (gsk_http_media_set_get_type ())

/* type-casting macros */
#define GSK_TYPE_HTTP_HEADER               (gsk_http_header_get_type ())
#define GSK_HTTP_HEADER(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_HTTP_HEADER, GskHttpHeader))
#define GSK_HTTP_HEADER_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_HTTP_HEADER, GskHttpHeaderClass))
#define GSK_HTTP_HEADER_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_HTTP_HEADER, GskHttpHeaderClass))
#define GSK_IS_HTTP_HEADER(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_HTTP_HEADER))
#define GSK_IS_HTTP_HEADER_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_HTTP_HEADER))


typedef enum
{
  GSK_HTTP_STATUS_CONTINUE                      = 100,
  GSK_HTTP_STATUS_SWITCHING_PROTOCOLS           = 101,
  GSK_HTTP_STATUS_OK                            = 200,
  GSK_HTTP_STATUS_CREATED                       = 201,
  GSK_HTTP_STATUS_ACCEPTED                      = 202,
  GSK_HTTP_STATUS_NONAUTHORITATIVE_INFO         = 203,
  GSK_HTTP_STATUS_NO_CONTENT                    = 204,
  GSK_HTTP_STATUS_RESET_CONTENT                 = 205,
  GSK_HTTP_STATUS_PARTIAL_CONTENT               = 206,
  GSK_HTTP_STATUS_MULTIPLE_CHOICES              = 300,
  GSK_HTTP_STATUS_MOVED_PERMANENTLY             = 301,
  GSK_HTTP_STATUS_FOUND                         = 302,
  GSK_HTTP_STATUS_SEE_OTHER                     = 303,
  GSK_HTTP_STATUS_NOT_MODIFIED                  = 304,
  GSK_HTTP_STATUS_USE_PROXY                     = 305,
  GSK_HTTP_STATUS_TEMPORARY_REDIRECT            = 306,
  GSK_HTTP_STATUS_BAD_REQUEST                   = 400,
  GSK_HTTP_STATUS_UNAUTHORIZED                  = 401,
  GSK_HTTP_STATUS_PAYMENT_REQUIRED              = 402,
  GSK_HTTP_STATUS_FORBIDDEN                     = 403,
  GSK_HTTP_STATUS_NOT_FOUND                     = 404,
  GSK_HTTP_STATUS_METHOD_NOT_ALLOWED            = 405,
  GSK_HTTP_STATUS_NOT_ACCEPTABLE                = 406,
  GSK_HTTP_STATUS_PROXY_AUTH_REQUIRED           = 407,
  GSK_HTTP_STATUS_REQUEST_TIMEOUT               = 408,
  GSK_HTTP_STATUS_CONFLICT                      = 409,
  GSK_HTTP_STATUS_GONE                          = 410,
  GSK_HTTP_STATUS_LENGTH_REQUIRED               = 411,
  GSK_HTTP_STATUS_PRECONDITION_FAILED           = 412,
  GSK_HTTP_STATUS_ENTITY_TOO_LARGE              = 413,
  GSK_HTTP_STATUS_URI_TOO_LARGE                 = 414,
  GSK_HTTP_STATUS_UNSUPPORTED_MEDIA             = 415,
  GSK_HTTP_STATUS_BAD_RANGE                     = 416,
  GSK_HTTP_STATUS_EXPECTATION_FAILED            = 417,
  GSK_HTTP_STATUS_INTERNAL_SERVER_ERROR         = 500,
  GSK_HTTP_STATUS_NOT_IMPLEMENTED               = 501,
  GSK_HTTP_STATUS_BAD_GATEWAY                   = 502,
  GSK_HTTP_STATUS_SERVICE_UNAVAILABLE           = 503,
  GSK_HTTP_STATUS_GATEWAY_TIMEOUT               = 504,
  GSK_HTTP_STATUS_UNSUPPORTED_VERSION           = 505
} GskHttpStatus;

/*
 * The Verb is the first text transmitted
 * (from the user-agent to the server).
 */
typedef enum
{
  GSK_HTTP_VERB_GET,
  GSK_HTTP_VERB_POST,
  GSK_HTTP_VERB_PUT,
  GSK_HTTP_VERB_HEAD,
  GSK_HTTP_VERB_OPTIONS,
  GSK_HTTP_VERB_DELETE,
  GSK_HTTP_VERB_TRACE,
  GSK_HTTP_VERB_CONNECT
} GskHttpVerb;

/* A GskHttpRange is a unit in which partial content ranges
 * may be specified and transferred.
 */
typedef enum
{
  GSK_HTTP_RANGE_BYTES
} GskHttpRange;

typedef enum {
  GSK_HTTP_CONTENT_ENCODING_IDENTITY,
  GSK_HTTP_CONTENT_ENCODING_GZIP,
  GSK_HTTP_CONTENT_ENCODING_COMPRESS,
  GSK_HTTP_CONTENT_ENCODING_UNRECOGNIZED = 0x100
} GskHttpContentEncoding;

/*
 * The Transfer-Encoding field of HTTP/1.1.
 *
 * In particular, HTTP/1.1 compliant clients and proxies
 * MUST support `chunked'.  The compressed Transfer-Encodings
 * are rarely (if ever) used.
 *
 * Note that:
 *   - we interpret this field, even for HTTP/1.0 clients.
 */
typedef enum {
  GSK_HTTP_TRANSFER_ENCODING_NONE    = 0,
  GSK_HTTP_TRANSFER_ENCODING_CHUNKED = 1,
  GSK_HTTP_TRANSFER_ENCODING_UNRECOGNIZED = 0x100
} GskHttpTransferEncoding;

/*
 * The Connection: header enables or disables http-keepalive.
 *
 * For HTTP/1.0, NONE should be treated like CLOSE.
 * For HTTP/1.1, NONE should be treated like KEEPALIVE.
 *
 * Use gsk_http_header_get_connection () to do this automatically -- it
 * always returns GSK_HTTP_CONNECTION_CLOSE or GSK_HTTP_CONNECTION_KEEPALIVE.
 *
 * See RFC 2616, Section 14.10.
 */
typedef enum
{
  GSK_HTTP_CONNECTION_NONE       = 0,
  GSK_HTTP_CONNECTION_CLOSE      = 1,
  GSK_HTTP_CONNECTION_KEEPALIVE  = 2,
} GskHttpConnection;

/*
 * The Cache-Control response directive.
 * See RFC 2616, Section 14.9 (cache-response-directive)
 */
struct _GskHttpResponseCacheDirective
{
  /*< public (read/write) >*/
  /* the http is `public' and `private'; is_ is added
   * for C++ users.
   */
  guint   is_public : 1;
  guint   is_private : 1;

  guint   no_cache : 1;
  guint   no_store : 1;
  guint   no_transform : 1;

  guint   must_revalidate : 1;
  guint   proxy_revalidate : 1;
  guint   max_age;
  guint   s_max_age;

  /*< public (read-only) >*/
  char   *private_name;
  char   *no_cache_name;

  /* TODO: what about cache-extensions? */
};

/*
 * The Cache-Control request directive.
 * See RFC 2616, Section 14.9 (cache-request-directive)
 */
struct _GskHttpRequestCacheDirective
{
  guint no_cache : 1;
  guint no_store : 1;
  guint no_transform : 1;
  guint only_if_cached : 1;

  guint max_age;
  guint min_fresh;

 /* 
  * We need to be able to indicate that max_stale is set without the 
  * optional argument. So:
  *		      0 not set
  *		     -1 set no arg
  *		     >0 set with arg.	  
  */
  gint  max_stale;

  /* TODO: what about cache-extensions? */
};


GskHttpResponseCacheDirective *gsk_http_response_cache_directive_new (void);
void gsk_http_response_cache_directive_set_private_name (
				     GskHttpResponseCacheDirective *directive,
				     const char            *name,
				     gsize                  name_len);
void gsk_http_response_cache_directive_set_no_cache_name (
				     GskHttpResponseCacheDirective *directive,
				     const char            *name,
				     gsize                  name_len);
void gsk_http_response_cache_directive_free(
				    GskHttpResponseCacheDirective *directive);


GskHttpRequestCacheDirective *gsk_http_request_cache_directive_new (void);
void gsk_http_request_cache_directive_free(
				     GskHttpRequestCacheDirective *directive);


/*
 * The Accept: request-header.
 *
 * See RFC 2616, Section 14.1.
 *
 * TODO: support level= accept-extension.
 */
struct _GskHttpMediaTypeSet
{
  /*< public: read-only >*/
  char                 *type;
  char                 *subtype;
  gfloat                quality;                /* -1 if not present */

  /*< private >*/
  GskHttpMediaTypeSet  *next;
};
GskHttpMediaTypeSet *gsk_http_media_type_set_new (const char *type,
                                                  const char *subtype,
                                                  gfloat      quality);
void                 gsk_http_media_type_set_free(GskHttpMediaTypeSet *set);


/*
 * The Accept-Charset: request-header.
 *
 * See RFC 2616, Section 14.2.
 */
struct _GskHttpCharSet
{
  /*< public: read-only >*/
  char                 *charset_name;
  gfloat                quality;                /* -1 if not present */

  /*< private >*/
  GskHttpCharSet       *next;
};
GskHttpCharSet       *gsk_http_char_set_new (const char *charset_name,
                                             gfloat      quality);
void                  gsk_http_char_set_free(GskHttpCharSet *char_set);

/*
 * The Accept-Encoding: request-header.
 *
 * See RFC 2616, Section 14.3.
 */
struct _GskHttpContentEncodingSet
{
  /*< public: read-only >*/
  GskHttpContentEncoding       encoding;
  gfloat                       quality;       /* -1 if not present */

  /*< private >*/
  GskHttpContentEncodingSet   *next;
};
GskHttpContentEncodingSet *
gsk_http_content_encoding_set_new (GskHttpContentEncoding encoding,
				   gfloat                 quality);
void
gsk_http_content_encoding_set_free(GskHttpContentEncodingSet *encoding_set);

/*
 * for the TE: request-header.
 *
 * See RFC 2616, Section 14.39.
 */
struct _GskHttpTransferEncodingSet
{
  /*< public: read-only >*/
  GskHttpTransferEncoding      encoding;
  gfloat                       quality;       /* -1 if not present */

  /*< private >*/
  GskHttpTransferEncodingSet   *next;
};
GskHttpTransferEncodingSet *
gsk_http_transfer_encoding_set_new (GskHttpTransferEncoding     encoding,
				    gfloat                      quality);
void
gsk_http_transfer_encoding_set_free(GskHttpTransferEncodingSet *encoding_set);

struct _GskHttpRangeSet
{
  /*< public: read-only >*/
  GskHttpRange          range_type;

  /*< private >*/
  GskHttpRangeSet   *next;
};
GskHttpRangeSet *gsk_http_range_set_new (GskHttpRange range_type);
void             gsk_http_range_set_free(GskHttpRangeSet *set);


/*
 * The Accept-Language: request-header.
 *
 * See RFC 2616, Section 14.4.
 */
struct _GskHttpLanguageSet
{
  /*< public: read-only >*/

  /* these give a language (with optional dialect specifier) */
  char                 *language;
  gfloat                quality;                /* -1 if not present */

  /*< private >*/
  GskHttpLanguageSet   *next;
};
GskHttpLanguageSet *gsk_http_language_set_new (const char *language,
                                               gfloat      quality);
void                gsk_http_language_set_free(GskHttpLanguageSet *set);

typedef enum
{
  GSK_HTTP_AUTH_MODE_UNKNOWN,
  GSK_HTTP_AUTH_MODE_BASIC,
  GSK_HTTP_AUTH_MODE_DIGEST
} GskHttpAuthMode;

/* HTTP Authentication.
   
   These structures give map to the WWW-Authenticate/Authorization headers,
   see RFC 2617.

   The outline is:
     If you get a 401 (Unauthorized) header, the server will
     accompany that with information about how to authenticate,
     in the WWW-Authenticate header.
     
     The user-agent should prompt the user for a username/password.

     Then the client tries again: but this time with an appropriate Authorization.
     If the server is satified, it will presumably give you the content.
 */
struct _GskHttpAuthenticate
{
  GskHttpAuthMode mode;
  char           *auth_scheme_name;
  char           *realm;
  union
  {
    struct {
      char                   *options;
    } unknown;
    /* no members:
    struct {
    } basic;
    */
    struct {
      char                   *domain;
      char                   *nonce;
      char                   *opaque;
      gboolean                is_stale;
      char                   *algorithm;
    } digest;
  } info;
  guint           ref_count;            /*< private >*/
};
GskHttpAuthenticate *gsk_http_authenticate_new_unknown (const char          *auth_scheme_name,
                                                        const char          *realm,
                                                        const char          *options);
GskHttpAuthenticate *gsk_http_authenticate_new_basic   (const char          *realm);
GskHttpAuthenticate *gsk_http_authenticate_new_digest  (const char          *realm,
                                                        const char          *domain,
                                                        const char          *nonce,
                                                        const char          *opaque,
                                                        const char          *algorithm);
GskHttpAuthenticate  *gsk_http_authenticate_ref        (GskHttpAuthenticate *auth);
void                  gsk_http_authenticate_unref      (GskHttpAuthenticate *auth);

struct _GskHttpAuthorization
{
  GskHttpAuthMode mode;
  char           *auth_scheme_name;
  union
  {
    struct {
      char                   *response;
    } unknown;
    struct {
      char                   *user;
      char                   *password;
    } basic;
    struct {
      char                   *realm;
      char                   *domain;
      char                   *nonce;
      char                   *opaque;
      char                   *algorithm;
      char                   *user;
      char                   *password;
      char                   *response_digest;
      char                   *entity_digest;
    } digest;
  } info;
  guint           ref_count;            /*< private >*/
};
GskHttpAuthorization *gsk_http_authorization_new_unknown (const char *auth_scheme_name,
                                                          const char *response);
GskHttpAuthorization *gsk_http_authorization_new_basic   (const char *user,
                                                          const char *password);
GskHttpAuthorization *gsk_http_authorization_new_digest  (const char *realm,
                                                          const char *domain,
                                                          const char *nonce,
                                                          const char *opaque,
                                                          const char *algorithm,
                                                          const char *user,
                                                          const char *password,
                                                          const char *response_digest,
                                                          const char *entity_digest);
GskHttpAuthorization *gsk_http_authorization_new_respond (const GskHttpAuthenticate *,
                                                          const char *user,
                                                          const char *password,
                                                          GError    **error);
GskHttpAuthorization *gsk_http_authorization_new_respond_post(const GskHttpAuthenticate *,
                                                          const char *user,
                                                          const char *password,
                                                          guint       post_data_len,
                                                          gconstpointer post_data,
                                                          GError    **error);
const char           *gsk_http_authorization_peek_response_digest (GskHttpAuthorization *);
GskHttpAuthorization *gsk_http_authorization_copy        (const GskHttpAuthorization *);
void                  gsk_http_authorization_set_nonce   (GskHttpAuthorization *,
                                                          const char *nonce);
GskHttpAuthorization *gsk_http_authorization_ref         (GskHttpAuthorization *);
void                  gsk_http_authorization_unref       (GskHttpAuthorization *);

/* an update to an existing authentication,
   to verify that we're talking to the same host. */
struct _GskHttpAuthenticateInfo
{
  char *next_nonce;
  char *response_digest;
  char *cnonce;
  guint has_nonce_count;
  guint32 nonce_count;
};

/* A single `Cookie' or `Set-Cookie' header.
 *
 * See RFC 2109, HTTP State Management Mechanism 
 */
struct _GskHttpCookie
{
  /*< public: read-only >*/
  char                   *key;
  char                   *value;
  char                   *domain;
  char                   *path;
  char                   *expire_date;
  char                   *comment;
  int                     max_age;
  gboolean                secure;               /* default is FALSE */
  guint                   version;              /* default is 0, unspecified */
};
GskHttpCookie  *gsk_http_cookie_new              (const char     *key,
                                                  const char     *value,
                                                  const char     *path,
                                                  const char     *domain,
                                                  const char     *expire_date,
                                                  const char     *comment,
                                                  int             max_age);
GskHttpCookie  *gsk_http_cookie_copy             (const GskHttpCookie *orig);
void            gsk_http_cookie_free             (GskHttpCookie *orig);


/*
 *                 GskHttpHeader
 *
 * A structure embodying an http header
 * (as in a request or a response).
 */
struct _GskHttpHeaderClass
{
  GObjectClass                  base_class;
};

struct _GskHttpHeader
{
  GObject                       base_instance;

  /*< public >*/  /* read-write */
  guint16                       http_major_version;             /* always 1 */
  guint16                       http_minor_version;

  GskHttpConnection             connection_type;

  GskHttpTransferEncoding       transfer_encoding_type;
  GskHttpContentEncoding        content_encoding_type;
  GskHttpRangeSet              *accepted_range_units;  /* Accept-Ranges */

  /*< public >*/
  char                         *unrecognized_transfer_encoding;
  char                         *unrecognized_content_encoding;

  char                     *content_encoding;     /* Content-Encoding */

  unsigned                  has_content_type : 1;

  char                     *content_type;             /* Content-Type */
  char                     *content_subtype;
  char                     *content_charset;
  GSList                   *content_additional;

  /* NULL-terminated array of language tags from the Content-Language
   * header, or NULL if header not present. */
  char                    **content_languages;

  /* Bytes ranges. Both with be == -1 if there is no Range tag. */
  int                           range_start;
  int                           range_end;

  /* may be set to ((time_t) -1) to omit them. */
  glong                         date;

  /* From the Content-Length header. */
  gint64                        content_length;

  /*< public >*/

  /* Key/value searchable header lines. */
  GHashTable                   *header_lines;

  /* Error messages.  */
  GSList                       *errors;		      /* list of char* Error: directives */

  /* General headers.  */
  GSList                       *pragmas;

  /* and actual accumulated parse error (a bit of a hack) */
  GError                       *g_error;
};


/* Public methods to parse/write a header. */
typedef enum
{
  GSK_HTTP_PARSE_STRICT = (1 << 0),

  /* instead of giving up on unknown headers, just 
   * add them as misc-headers */
  GSK_HTTP_PARSE_SAVE_ERRORS = (1 << 1)
} GskHttpParseFlags;

GskHttpHeader  *gsk_http_header_from_buffer      (GskBuffer     *input,
						  gboolean       is_request,
                                                  GskHttpParseFlags flags,
                                                  GError        **error);
void            gsk_http_header_to_buffer        (GskHttpHeader *header,
                                                  GskBuffer     *output);


/* response specific functions */
/* unhandled: content-type and friends */
void             gsk_http_header_set_content_encoding(GskHttpHeader *header,
                                                     const char      *encoding);

/*content_type; content_subtype; content_charset; content_additional;*/

typedef struct _GskHttpContentTypeInfo GskHttpContentTypeInfo;
struct _GskHttpContentTypeInfo
{
  const char *type_start;
  guint type_len;
  const char *subtype_start;
  guint subtype_len;
  const char *charset_start;
  guint charset_len;
  guint max_additional;         /* unimplemented */
  guint n_additional;           /* unimplemented */
  const char **additional_starts; /* unimplemented */
  guint *additional_lens;         /* unimplemented */
};
typedef enum
{
  GSK_HTTP_CONTENT_TYPE_PARSE_ADDL = (1<<0) /* unimplemented */
} GskHttpContentTypeParseFlags;

gboolean gsk_http_content_type_parse (const char *content_type_header,
                                      GskHttpContentTypeParseFlags flags,
                                      GskHttpContentTypeInfo *out,
                                      GError                **error);



/* --- miscellaneous key/value pairs --- */
void             gsk_http_header_add_misc     (GskHttpHeader *header,
                                               const char    *key,
                                               const char    *value);
void             gsk_http_header_remove_misc  (GskHttpHeader *header,
                                               const char    *key);
const char      *gsk_http_header_lookup_misc  (GskHttpHeader *header,
                                               const char    *key);

/* XXX: need to figure out the clean way to replace this one
 * (probably some generic gsk_make_randomness (CHAR-SET, LENGTH) trick)
 */
/*char                *gsk_http_header_gen_random_cookie();*/


typedef struct _GskHttpHeaderLineParser GskHttpHeaderLineParser;
typedef gboolean (*GskHttpHeaderLineParserFunc) (GskHttpHeader *header,
						 const char    *text,
						 gpointer       data);
struct _GskHttpHeaderLineParser
{
  const char *name;
  GskHttpHeaderLineParserFunc func;
  gpointer data;
};
  
/* The returned table is a map from g_str_hash(lowercase(header)) to
   GskHttpHeaderLineParser. */
GHashTable        *gsk_http_header_get_parser_table(gboolean       is_request);

/* Standard header constructions... */


/* --- outputting an http header --- */
typedef void (*GskHttpHeaderPrintFunc) (const char       *text,
					gpointer          data);
void              gsk_http_header_print (GskHttpHeader          *http_header,
		                         GskHttpHeaderPrintFunc  print_func,
		                         gpointer                print_data);


GskHttpConnection    gsk_http_header_get_connection (GskHttpHeader *header);
void                 gsk_http_header_set_version    (GskHttpHeader *header,
						     gint           major,
						     gint           minor);
void                 gsk_http_header_add_pragma     (GskHttpHeader *header,
                                                     const char    *pragma);
void             gsk_http_header_add_accepted_range (GskHttpHeader *header,
                                                     GskHttpRange   range);


#define gsk_http_header_set_connection_type(header, connection_type)	      \
  g_object_set (GSK_HTTP_HEADER(header), "connection", (GskHttpConnection) (connection_type), NULL)
#define gsk_http_header_get_connection_type(header)			      \
  (GSK_HTTP_HEADER(header)->connection_type)
#define gsk_http_header_set_transfer_encoding(header, enc)		      \
  g_object_set (GSK_HTTP_HEADER(header), "transfer-encoding", (GskHttpTransferEncoding) (enc), NULL)
#define gsk_http_header_get_transfer_encoding(header)			      \
  (GSK_HTTP_HEADER(header)->transfer_encoding_type)
#define gsk_http_header_set_content_encoding(header, enc)		      \
  g_object_set (GSK_HTTP_HEADER(header), "content-encoding", (GskHttpContentEncoding) (enc), NULL)
#define gsk_http_header_get_content_encoding(header)			      \
  (GSK_HTTP_HEADER(header)->content_encoding_type)
#define gsk_http_header_set_content_length(header, len)		              \
  g_object_set (GSK_HTTP_HEADER(header), "content-length", (gint64) (len), NULL)
#define gsk_http_header_get_content_length(header)			      \
  (GSK_HTTP_HEADER(header)->content_length)
#define gsk_http_header_set_range(header, start, end)		              \
  g_object_set (GSK_HTTP_HEADER(header), "range-start", (gint) (start), "range-end", (gint) (end), NULL)
#define gsk_http_header_get_range_start(header)			              \
  (GSK_HTTP_HEADER(header)->range_start)
#define gsk_http_header_get_range_end(header)			              \
  (GSK_HTTP_HEADER(header)->range_end)
#define gsk_http_header_set_date(header, date)			      	      \
  g_object_set (GSK_HTTP_HEADER(header), "date", (long) (date), NULL)
#define gsk_http_header_get_date(header)				      \
  (GSK_HTTP_HEADER(header)->date)

/*< private >*/
void gsk_http_header_set_string (gpointer         http_header,
                                 char           **p_str,
                                 const char      *str);
void gsk_http_header_set_string_len (gpointer         http_header,
                                     char           **p_str,
                                     const char      *str,
                                     guint            len);

void gsk_http_header_set_string_val (gpointer         http_header,
                                     char           **p_str,
                                     const GValue    *value);

char * gsk_http_header_cut_string (gpointer    http_header,
                                   const char *start,
                                   const char *end);

void gsk_http_header_free_string (gpointer http_header,
			          char    *str);
void gsk_http_header_set_connection_string (GskHttpHeader *header,
                                            const char    *str);
void gsk_http_header_set_content_encoding_string  (GskHttpHeader *header,
                                                   const char    *str);
void gsk_http_header_set_transfer_encoding_string (GskHttpHeader *header,
                                                   const char    *str);

#define gsk_http_header_set_content_type(header, content_type)	      \
  g_object_set (GSK_HTTP_HEADER(header), "content-type", (const char *)(content_type), NULL)
#define gsk_http_header_get_content_type(header)			      \
  (GSK_HTTP_HEADER(header)->content_type)
#define gsk_http_header_set_content_subtype(header, content_type)	      \
  g_object_set (GSK_HTTP_HEADER(header), "content-subtype", (const char *)(content_type), NULL)
#define gsk_http_header_get_content_subtype(header)			      \
  (GSK_HTTP_HEADER(header)->content_subtype)
#define gsk_http_header_set_content_charset(header, content_type)	      \
  g_object_set (GSK_HTTP_HEADER(header), "content-charset", (const char *)(content_type), NULL)
#define gsk_http_header_get_content_charset(header)			      \
  (GSK_HTTP_HEADER(header)->content_charset)

G_END_DECLS

#endif
