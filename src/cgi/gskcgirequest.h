#ifndef __GSK_CGI_REQUEST_H_
#define __GSK_CGI_REQUEST_H_

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskCgiRequest GskCgiRequest;
typedef struct _GskCgiRequestClass GskCgiRequestClass;
/* --- type macros --- */
GType gsk_cgi_request_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_CGI_REQUEST			(gsk_cgi_request_get_type ())
#define GSK_CGI_REQUEST(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_CGI_REQUEST, GskCgiRequest))
#define GSK_CGI_REQUEST_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_CGI_REQUEST, GskCgiRequestClass))
#define GSK_CGI_REQUEST_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_CGI_REQUEST, GskCgiRequestClass))
#define GSK_IS_CGI_REQUEST(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_CGI_REQUEST))
#define GSK_IS_CGI_REQUEST_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_CGI_REQUEST))

/* --- structures --- */
struct _GskCgiRequestClass 
{
  GObjectClass base_class;
};


struct _GskCgiRequest 
{
  GObject          base_instance;
  GHashTable      *content_parts_by_name;
  GskCgiContent   *content;
};

struct _GskCgiResponse
{
  GskHttpResponse *response;

/* --- prototypes --- */
GskCgiRequest *gsk_cgi_request_new (void);

/* NOTE: either stored by name or stored in 'content' member.
 * In either event, the destruction of 'request' causes the
 * destruction of 'content'. */
void           gsk_cgi_request_set_content (GskCgiRequest *request,
					    GskCgiContent *content);

/* NOTE: 'name' may be NULL, to access the central content. */
G_GNUC_CONST GskCgiContent *
               gsk_cgi_request_peek_content(GskCgiContent *request,
					    const char    *name);

G_END_DECLS

#endif
