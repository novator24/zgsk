
/* GskCgiServer handles the parsing of CGI GET/POST requests,
   and delivers them to the user are GskCgiRequest objects.
   The user must use trap/get_request/respond to
   properly handle these requests. */


#ifndef __GSK_CGI_SERVER_H_
#define __GSK_CGI_SERVER_H_

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskCgiServer GskCgiServer;
typedef struct _GskCgiServerClass GskCgiServerClass;
/* --- type macros --- */
GType gsk_cgi_server_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_CGI_SERVER              (gsk_cgi_server_get_type ())
#define GSK_CGI_SERVER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_CGI_SERVER, GskCgiServer))
#define GSK_CGI_SERVER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_CGI_SERVER, GskCgiServerClass))
#define GSK_CGI_SERVER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_CGI_SERVER, GskCgiServerClass))
#define GSK_IS_CGI_SERVER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_CGI_SERVER))
#define GSK_IS_CGI_SERVER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_CGI_SERVER))

#define GSK_CGI_SERVER_HOOK(server)      (&(GSK_CGI_SERVER (server)->cgi_hook))

/* --- structures --- */
struct _GskCgiServerClass 
{
  GskHttpServerClass base_class;
  void         (*set_poll_cgi) (GskCgiServer *server,
                                gboolean      do_poll);
  void         (*shutdown_cgi) (GskCgiServer *server);
};
struct _GskCgiServer 
{
  GskHttpServer      base_instance;

  GskHook            cgi_hook;
  /* queue of requests that have been removed with get_request()
     but for whom respond() has not been called,
     (or if any request before them is pending) */
  GQueue            *pending_requests;

  /* queue of requests that are done, but for whom the
     data is not available */
  GQueue            *waiting_requests;

  GskMimeMultipartDecoder *decoder;
};

typedef gboolean (*GskCgiServerReadyFunc) (GskCgiServer *server,
                                           gpointer      data);


/* --- prototypes --- */
GskCgiServer * gsk_cgi_server_new         (void);

void           gsk_cgi_server_trap        (GskCgiServer *server,
                                           GskCgiServerReadyFunc handler,
                                           gpointer data);

/* get_request/response methods:  generally to be used by the trap */
GskCgiRequest *gsk_cgi_server_get_request (GskCgiServer *server);
void           gsk_cgi_server_respond     (GskCgiServer *server,
                                           GskCgiRequest *request,
                                           GskCgiResponse *response);


/* helper function, occasionally useful */
GskCgiRequest *gsk_cgi_request_from_mime_multipart_piece (GskMimeMultipartPiece *);

G_END_DECLS

#endif
