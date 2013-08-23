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


#ifndef __GSK_URL_H_
#define __GSK_URL_H_

#include "../gskstream.h"

G_BEGIN_DECLS

typedef struct _GskUrl GskUrl;
typedef struct _GskUrlClass GskUrlClass;

/* --- type macros --- */
GType gsk_url_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_URL			(gsk_url_get_type ())
#define GSK_URL(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_URL, GskUrl))
#define GSK_URL_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_URL, GskUrlClass))
#define GSK_URL_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_URL, GskUrlClass))
#define GSK_IS_URL(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_URL))
#define GSK_IS_URL_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_URL))

/* --- enums --- */
typedef enum 
{
  GSK_URL_SCHEME_FILE,
  GSK_URL_SCHEME_HTTP,
  GSK_URL_SCHEME_HTTPS,
  GSK_URL_SCHEME_FTP,
  GSK_URL_SCHEME_OTHER = 0x10000,
} GskUrlScheme;

/* --- structures --- */
struct _GskUrlClass
{
  GObjectClass                  parent_class;
};

struct _GskUrl
{
  GObject                       parent_instance;

  GskUrlScheme			scheme;
  char                         *scheme_name;

  /* The place where the resource can be obtained.
   * (The RFC refers to this as an `authority', which is more general, sect 3.2)
   */
  char                         *host;

  /* XXX: refer to RFC again for if this is general... */
  char                         *password;

  /* For server-based urls, the port where the resource may be obtained,
   * or 0, indicating that none was specified. (Section 3.2.2)
   */
  int				port;

  /* The username or NULL if none was given. */
  char			       *user_name;

  /* The scheme-specific data (we separate the section and query pieces though)
   * This string usually begins with a slash (/).
   * RFC 2396, Section 3.3.
   */
  char                         *path;

  /* The query portion of the URL. */
  char                         *query;

  /* The portion after a `#' character. (Section 4.1) */
  char			       *fragment;
};

/* --- character sets --- */
/* Characters having specific meaning within a url; 
 * these should be escaped to be passed on to the underlying scheme.
 * From RFC 2396, section 2.2.
 */
#define GSK_URL_RESERVED_CHARSET			\
	";/?:@&=+$,"

/* Characters which do not normally need escaping within a URL.
 * From RFC 2396, section 2.3.
 */
#define GSK_URL_UNRESERVED_CHARSET			\
	"abcdefghijklmnopqrstuvwxyz"			\
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"			\
	"0123456789"					\
	"-_.!~*'()"

/* --- public methods --- */
GskUrl         *gsk_url_new                 (const char      *spec,
					     GError         **error);
GskUrl         *gsk_url_new_in_context      (const char      *spec,
                                             GskUrlScheme     context,
					     GError         **error);
GskUrl         *gsk_url_new_from_parts      (GskUrlScheme     scheme,
					     const char      *host,
					     int              port,
					     const char      *user_name,
					     const char      *password,
					     const char      *path,
					     const char      *query,
					     const char      *fragment);
GskUrl         *gsk_url_new_relative        (GskUrl          *base_url,
					     const char      *location,
					     GError         **error);

char           *gsk_url_get_relative_path   (GskUrl          *url);

guint           gsk_url_get_port            (const GskUrl    *url);

/* Url-encoding helper functions. */
char           *gsk_url_decode              (const char      *encoded);
char           *gsk_url_encode              (const char      *decoded);


char           *gsk_url_to_string           (const GskUrl    *url);

guint           gsk_url_hash                (const GskUrl    *url);
gboolean        gsk_url_equal               (const GskUrl    *a,
                                             const GskUrl    *b);

/* These do what is typically thought of
 * as "url encoding" in http-land... namely SPACE maps to '+'
 * and funny characters are encoded
 * as %xx where 'x' denotes a single hex-digit.
 * 
 * This is the encoding applied to field names and values in HTML
 * forms when using the default "application/x-www-form-urlencoded"
 * encoding. See RFC 1866, section 8.2.1.
 */
char           *gsk_url_decode_http         (const char      *encoded);
char           *gsk_url_encode_http         (const char      *decoded);
char           *gsk_url_encode_http_binary  (const guint8    *decoded,
                                             guint            length);

/* Split an "application/x-www-form-urlencoded" format query string into
 * a null-terminated array of strings: key, value, ...
 * Caller must free result with g_strfreev.
 *
 * XXX: this is a duplicate of gsk_http_parse_cgi_query_string()
 */
char **         gsk_url_split_form_urlencoded (const char *encoded_query);


gboolean gsk_url_is_valid_hostname          (const char *str,
                                             char *bad_char_out);
#define gsk_url_is_valid_path(str, bad_char_out)     \
	gsk_url_is_valid_generic_component(str, bad_char_out)
#define gsk_url_is_valid_query(str, bad_char_out)    \
	gsk_url_is_valid_generic_component(str, bad_char_out)
#define gsk_url_is_valid_fragment(str, bad_char_out) \
	gsk_url_is_valid_generic_component(str, bad_char_out)

/* Downloading URLs */
typedef void (*GskUrlSuccess) (GskStream        *stream,
			       gpointer          user_data);
typedef void (*GskUrlFailure) (GError           *error,
			       gpointer          user_data);
void           gsk_url_download             (GskUrl          *url,
					     GskUrlSuccess    success_func,
					     GskUrlFailure    failure_func,
					     gpointer         user_data);

/* --- protected methods --- */
/* Allocating new url schemes. */
typedef GskUrl *(*GskUrlParser) (GskUrlScheme     scheme,
				 const char      *url,
				 gpointer         data);
typedef char   *(*GskUrlToString) (GskUrl        *url,
				   gpointer       data);
GskUrlScheme    gsk_url_scheme_get_unique   (const char      *url_scheme,
					     guint            default_port,
					     GskUrlParser     parse_func,
					     GskUrlToString   print_func,
					     gpointer         data);

/* registering new download methods */
typedef struct _GskUrlDownload GskUrlDownload;
typedef void  (*GskUrlDownloadMethod)       (GskUrlDownload  *download,
					     gpointer         download_data);
void            gsk_url_scheme_add_dl_method(GskUrlScheme     scheme,
					     GskUrlDownloadMethod download_method,
					     gpointer         download_data);

/* protected: (called (eventually) by DownloadMethod) */
void            gsk_url_download_success    (GskUrlDownload  *download,
			                     GskStream       *stream);
void            gsk_url_download_fail       (GskUrlDownload  *download,
			                     GError          *error);
GskUrl         *gsk_url_download_peek_url   (GskUrlDownload  *download);
void            gsk_url_download_redirect   (GskUrlDownload  *download,
					     GskUrl          *new_url);

/*< private >*/
gboolean gsk_url_is_valid_generic_component (const char *str,
                                             char *bad_char_out);
G_END_DECLS

#endif
