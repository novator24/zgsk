/* NB: the transfer isn't *really* done until you're finishing uploading and/or downloading,
   but we actually ignore that;  not ignoring makes a slightly irritating circular reference danger.
 */
#ifndef __GSK_URL_TRANSFER_H_
#define __GSK_URL_TRANSFER_H_

#include "gskurl.h"
#include "../gsksocketaddress.h"
#include "../gskmainloop.h"
#include "../gskpacket.h"

G_BEGIN_DECLS

typedef struct _GskUrlTransferClass GskUrlTransferClass;
typedef struct _GskUrlTransferRedirect GskUrlTransferRedirect;
typedef struct _GskUrlTransfer GskUrlTransfer;

GType gsk_url_transfer_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_URL_TRANSFER			(gsk_url_transfer_get_type ())
#define GSK_URL_TRANSFER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_URL_TRANSFER, GskUrlTransfer))
#define GSK_URL_TRANSFER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_URL_TRANSFER, GskUrlTransferClass))
#define GSK_URL_TRANSFER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_URL_TRANSFER, GskUrlTransferClass))
#define GSK_IS_URL_TRANSFER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_URL_TRANSFER))
#define GSK_IS_URL_TRANSFER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_URL_TRANSFER))

typedef enum
{
  GSK_URL_TRANSFER_ERROR_BAD_REQUEST,
  GSK_URL_TRANSFER_ERROR_BAD_NAME,
  GSK_URL_TRANSFER_ERROR_NO_SERVER,
  GSK_URL_TRANSFER_ERROR_NOT_FOUND,
  GSK_URL_TRANSFER_ERROR_SERVER_ERROR,
  GSK_URL_TRANSFER_ERROR_UNSUPPORTED,
  GSK_URL_TRANSFER_ERROR_TIMED_OUT,
  GSK_URL_TRANSFER_ERROR_REDIRECT_LOOP,
  GSK_URL_TRANSFER_REDIRECT,
  GSK_URL_TRANSFER_CANCELLED,
  GSK_URL_TRANSFER_SUCCESS
} GskUrlTransferResult;

#define GSK_URL_TRANSFER_N_RESULTS      (GSK_URL_TRANSFER_SUCCESS+1)

const char *gsk_url_transfer_result_name (GskUrlTransferResult result);

typedef void (*GskUrlTransferFunc)    (GskUrlTransfer *info,
                                       gpointer        user_data);
typedef GskStream *(*GskUrlUploadFunc)(gpointer        upload_data,
                                       gssize         *size_out,
                                       GError        **error);

struct _GskUrlTransferClass
{
  GObjectClass base_class;

  /* The test() method is called before the instance
     is constructed, to give the class an opportunity to
     see if it can handle the url. */
  gboolean (*test)  (GskUrlTransferClass *transfer_class,
                     const GskUrl        *url);

  /* The start() method is called on an instance which
     has been configured.  It returns TRUE if the transfer is started.
     If it returns FALSE, an error has occurred. */
  gboolean (*start) (GskUrlTransfer      *transfer,
                     GError             **error);

  void     (*cancel)(GskUrlTransfer     *transfer);

  char    *(*get_constructing_state) (GskUrlTransfer *transfer);
  char    *(*get_running_state) (GskUrlTransfer *transfer);
  char    *(*get_done_state) (GskUrlTransfer *transfer);

  /* default impl calls gsk_url_transfer_task_notify_done(ERROR_TIMEOUT) */
  void     (*timed_out)(GskUrlTransfer     *transfer);
};

struct _GskUrlTransferRedirect
{
  gboolean is_permanent;
  GskUrl *url;
  GObject *request;
  GObject *response;
  GskUrlTransferRedirect *next;
};



struct _GskUrlTransfer
{
  GObject base_instance;

  /*< public >*/
  /* --- information prepared for the handler --- */
  GskUrlTransferResult result;
  GskUrl *url;
  GSList *redirect_urls;        // XXX: unused
  GskUrlTransferRedirect *first_redirect, *last_redirect;
  GskSocketAddress *address;

  /* may be available: protocol-specific headers */
  GObject *request;     /* a GskHttpRequest probably */
  GObject *response;    /* a GskHttpResponse probably */

  GskStream *content;   /* the downloading content */

  /* the last redirect (if any) */
  GskUrl *redirect_url; /* [just a peeked version of last_redirect->url, not a ref] */
  gboolean redirect_is_permanent;

  /* ERROR status codes */
  GError *error;

  /*< protected >*/
  GskSocketAddress   *address_hint;
  guint               follow_redirects : 1;
  guint               has_timeout : 1;
  guint               timed_out : 1;

  /*< private >*/
  GskSource *timeout_source;
  guint timeout_ms;

  GskUrlTransferFunc handler;
  gpointer handler_data;
  GDestroyNotify handler_data_destroy;

  GskUrlUploadFunc    upload_func;
  gpointer            upload_data;
  GDestroyNotify      upload_destroy;

  guint transfer_state;
};

gboolean        gsk_url_transfer            (GskUrl            *url,
                                             GskUrlUploadFunc   upload_func,
                                             gpointer           upload_data,
                                             GDestroyNotify     upload_destroy,
                                             GskUrlTransferFunc handler,
                                             gpointer           data,
                                             GDestroyNotify     destroy,
                                             GError           **error);

GskUrlTransfer *gsk_url_transfer_new        (GskUrl             *url);

void            gsk_url_transfer_set_handler(GskUrlTransfer     *transfer,
                                             GskUrlTransferFunc  handler,
                                             gpointer            data,
                                             GDestroyNotify      destroy);
void            gsk_url_transfer_set_url    (GskUrlTransfer     *transfer,
                                             GskUrl             *url);
void            gsk_url_transfer_set_upload (GskUrlTransfer     *transfer,
                                             GskUrlUploadFunc    func,
                                             gpointer            data,
                                             GDestroyNotify      destroy);
void      gsk_url_transfer_set_upload_packet(GskUrlTransfer     *transfer,
                                             GskPacket          *packet);
void     gsk_url_transfer_set_oneshot_upload(GskUrlTransfer     *transfer,
                                             GskStream          *stream,
                                             gssize              size);

void            gsk_url_transfer_set_timeout(GskUrlTransfer     *transfer,
                                             guint               millis);
void          gsk_url_transfer_clear_timeout(GskUrlTransfer     *transfer);

void   gsk_url_transfer_set_follow_redirects(GskUrlTransfer     *transfer,
                                             gboolean            follow_redirs);
void            gsk_url_transfer_set_address_hint(GskUrlTransfer     *transfer,
                                                  GskSocketAddress   *address);

/* Starting a transfer */
gboolean        gsk_url_transfer_start      (GskUrlTransfer     *transfer,
                                             GError            **error);

/* Cancelling a started transfer */
void            gsk_url_transfer_cancel     (GskUrlTransfer     *transfer);


char *          gsk_url_transfer_get_state_string (GskUrlTransfer *transfer);


/* --- Treating a Transfer as a Stream --- */
GskStream     * gsk_url_transfer_stream_new      (GskUrlTransfer *transfer,
                                                  GError        **error);

/* --- Protected API --- */
gboolean        gsk_url_transfer_has_upload      (GskUrlTransfer     *transfer);
GskStream      *gsk_url_transfer_create_upload   (GskUrlTransfer     *transfer,
                                                  gssize             *size_out,
                                                  GError            **error);
gboolean        gsk_url_transfer_peek_expects_download_stream (GskUrlTransfer *transfer);

/* whether gsk_url_transfer_notify_done() has been called */
gboolean        gsk_url_transfer_is_done         (GskUrlTransfer     *transfer);

/* invoke the notification callbacks as needed */
void            gsk_url_transfer_set_address     (GskUrlTransfer     *transfer,
                                                  GskSocketAddress   *addr);
gboolean        gsk_url_transfer_add_redirect    (GskUrlTransfer     *transfer,
                                                  GObject            *request,
                                                  GObject            *response,
                                                  gboolean            is_permanent,
                                                  GskUrl             *dest_url);
void            gsk_url_transfer_set_download    (GskUrlTransfer     *transfer,
                                                  GskStream          *content);
void            gsk_url_transfer_set_request     (GskUrlTransfer     *transfer,
                                                  GObject            *request);
void            gsk_url_transfer_set_response    (GskUrlTransfer     *transfer,
                                                  GObject            *response);

void            gsk_url_transfer_set_error       (GskUrlTransfer     *transfer,
                                                  const GError       *error);
void            gsk_url_transfer_take_error      (GskUrlTransfer     *transfer,
                                                  GError             *error);

/* NOTE: does a g_object_unref() (undoing the one in gsk_url_transfer_start),
   so the transfer may be destroyed by this function. */
void            gsk_url_transfer_notify_done     (GskUrlTransfer     *transfer,
                                                  GskUrlTransferResult result);

/* Registering a transfer type */
void            gsk_url_transfer_class_register  (GskUrlScheme            scheme,
                                                  GskUrlTransferClass    *transfer_class);

G_END_DECLS

#endif
