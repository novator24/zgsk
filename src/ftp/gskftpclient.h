/* Insert header here. */
#ifndef __GSK_FTP_CLIENT_H_
#define __GSK_FTP_CLIENT_H_

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskFtpClient GskFtpClient;
typedef struct _GskFtpClientClass GskFtpClientClass;
typedef struct _GskFtpClientRequest GskFtpClientRequest;	/* opaque */
/* --- type macros --- */
GType gsk_ftp_client_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_FTP_CLIENT			(gsk_ftp_client_get_type ())
#define GSK_FTP_CLIENT(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_FTP_CLIENT, GskFtpClient))
#define GSK_FTP_CLIENT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_FTP_CLIENT, GskFtpClientClass))
#define GSK_FTP_CLIENT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_FTP_CLIENT, GskFtpClientClass))
#define GSK_IS_FTP_CLIENT(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_FTP_CLIENT))
#define GSK_IS_FTP_CLIENT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_FTP_CLIENT))

/* --- structures --- */
struct _GskFtpClientClass 
{
  GskStreamClass stream_class;
};

struct _GskFtpClient 
{
  GskStream      stream;
  GskBuffer      outgoing;
  GskBuffer      incoming;

  /* a queue of requests and associated handlers */
  GskFtpClientRequest *first_request;
  GskFtpClientRequest *last_request;
};

/* --- prototypes --- */
typedef gboolean (*GskFtpClientHandleResponse) (GskFtpClient   *client,
					        GskFtpResponse *response,
						gpointer        data);
GskFtpClient *gsk_ftp_client_new (void);

void gsk_ftp_client_issue_request (GskFtpClient    *client,
				   GskFtpRequest   *request,
				   GskFtpClientHandleResponse handler,
				   gpointer         data,
				   GDestroyNotify   destroy);



G_END_DECLS

#endif
