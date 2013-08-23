#ifndef __GSK_HTTP_CONTENT_H_
#define __GSK_HTTP_CONTENT_H_

#include "gskhttpserver.h"
#include "../mime/gskmimemultipartpiece.h"
#include "../gsksocketaddress.h"

G_BEGIN_DECLS

/* TODO:
     - provide error page handler functions (eg something
       which takes a server,request,response_status,error_message
       and responds to the user with it.  one variant
       registered with a particular error code,
       one that handles any error code.
       (provide suitable defaults)

     - likewise, redirect handlers.

     - ...

     - make a GskTableTree type for managing hierarchical data 
       like this, to clean it up (pretty hard!)
 */

/* class to help run an http server */

/* opaque class */
typedef struct _GskHttpContentHandler GskHttpContentHandler;
typedef struct _GskHttpContent GskHttpContent;
typedef struct _GskHttpContentId GskHttpContentId;

typedef enum
{
  GSK_HTTP_CONTENT_OK,
  GSK_HTTP_CONTENT_CHAIN,
  GSK_HTTP_CONTENT_ERROR        /* causes a 500 error to result */
} GskHttpContentResult;

typedef GskHttpContentResult (*GskHttpContentFunc)    (GskHttpContent   *content,
                                                       GskHttpContentHandler *handler,
                                                       GskHttpServer  *server,
                                                       GskHttpRequest *request,
                                                       GskStream      *post_data,
                                                       gpointer        data);


/* note: CGIs cannot chain */
typedef void                 (*GskHttpContentCGIFunc) (GskHttpContent   *content,
                                                       GskHttpContentHandler *handler,
                                                       GskHttpServer  *server,
                                                       GskHttpRequest *request,
					               guint           n_vars,
					               GskMimeMultipartPiece **vars,
                                                       gpointer        data);


struct _GskHttpContentId
{
  const char *host;
  const char *user_agent_prefix;
  const char *path;
  const char *path_prefix;
  const char *path_suffix;
};
#define GSK_HTTP_CONTENT_ID_INIT {NULL,NULL,NULL,NULL,NULL}

/* the order hook tables are tried is:
      with user_agent, with path and vhost
      with user_agent, with vhost
      with user_agent, with path
      with user_agent, no path or host
      without user_agent, with path and vhost
      without user_agent, with vhost
      without user_agent, with path
      without user_agent, no path or host */

GskHttpContent *gsk_http_content_new     (void);
GskHttpContentHandler *
gsk_http_content_handler_new     (GskHttpContentFunc    func,
                                  gpointer              data,
                                  GDestroyNotify        destroy);

GskHttpContentHandler *
gsk_http_content_handler_new_cgi (GskHttpContentCGIFunc func,
                                  gpointer              data,
                                  GDestroyNotify        destroy);

void gsk_http_content_handler_ref  (GskHttpContentHandler *handler);
void gsk_http_content_handler_unref(GskHttpContentHandler *handler);


typedef enum
{
  GSK_HTTP_CONTENT_PREPEND,
  GSK_HTTP_CONTENT_APPEND,
  GSK_HTTP_CONTENT_REPLACE
} GskHttpContentAction;

void           gsk_http_content_add_handler  (GskHttpContent          *content,
                                              const GskHttpContentId  *id,
                                              GskHttpContentHandler   *handler,
                                              GskHttpContentAction     action);

void           gsk_http_content_set_mime_type(GskHttpContent *content,
                                              const char     *prefix,
                                              const char     *suffix,
					      const char     *type,
					      const char     *subtype);
void           gsk_http_content_set_default_mime_type
                                             (GskHttpContent *content,
					      const char     *type,
					      const char     *subtype);
gboolean       gsk_http_content_get_mime_type(GskHttpContent *content,
                                              const char     *path,
					      const char    **type_out,
					      const char    **subtype_out);
void       gsk_http_content_set_idle_timeout (GskHttpContent *content);

typedef void (*GskHttpContentErrorHandler)(GskHttpContent          *content,
                                           GError                  *error,
                                           GskHttpServer           *server,
                                           GskHttpRequest          *request,
                                           GskHttpStatus            code,
                                           gpointer                 data);

void gsk_http_content_set_error_handler  (GskHttpContent          *content,
                                          GskHttpContentErrorHandler handler,
                                          gpointer                 data,
                                          GDestroyNotify           destroy);

/* helpers */
void           gsk_http_content_add_data (GskHttpContent          *content,
                                          const GskHttpContentId  *id,
                                          gconstpointer            data,
					  guint                    data_len,
					  gpointer                 destroy_data,
				          GDestroyNotify           destroy);
void           gsk_http_content_add_data_by_path
                                         (GskHttpContent          *content,
                                          const char              *path,
                                          gconstpointer            data,
					  guint                    data_len,
					  gpointer                 destroy_data,
				          GDestroyNotify           destroy);
typedef enum
{
  GSK_HTTP_CONTENT_FILE_EXACT,
  GSK_HTTP_CONTENT_FILE_DIR,
  GSK_HTTP_CONTENT_FILE_DIR_TREE
} GskHttpContentFileType;
void           gsk_http_content_add_file (GskHttpContent          *content,
                                          const char              *path,
					  const char              *fs_path,
					  GskHttpContentFileType   type);

/* note: id must include a path_prefix */
void           gsk_http_content_add_file_by_id
                                         (GskHttpContent          *content,
                                          const GskHttpContentId  *id,
					  const char              *fs_path,
					  GskHttpContentFileType   type);


/* --- serving pages --- */
gboolean gsk_http_content_listen (GskHttpContent *content,
                                  GskSocketAddress *address,
                                  GError          **error);
void gsk_http_content_respond    (GskHttpContent *content,
                                  GskHttpServer  *server,
                                  GskHttpRequest *request,
			          GskStream      *post_data);
void gsk_http_content_manage_server (GskHttpContent *content,
                                     GskHttpServer  *server);

G_END_DECLS

#endif
