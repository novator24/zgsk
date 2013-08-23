#ifndef __GSK_IMAP_CLIENT_H_
#define __GSK_IMAP_CLIENT_H_

#include "gskimapcommon.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskImapClient GskImapClient;
typedef struct _GskImapClientClass GskImapClientClass;

/* --- type macros --- */
GType gsk_imap_client_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_IMAP_CLIENT			(gsk_imap_client_get_type ())
#define GSK_IMAP_CLIENT(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_IMAP_CLIENT, GskImapClient))
#define GSK_IMAP_CLIENT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_IMAP_CLIENT, GskImapClientClass))
#define GSK_IMAP_CLIENT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_IMAP_CLIENT, GskImapClientClass))
#define GSK_IS_IMAP_CLIENT(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_IMAP_CLIENT))
#define GSK_IS_IMAP_CLIENT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_IMAP_CLIENT))

/* --- structures --- */
typedef enum
{
  GSK_IMAP_CLIENT_STATE_WAITING_FOR_GREETING,
  GSK_IMAP_CLIENT_STATE_NONAUTHENTICATED,
  GSK_IMAP_CLIENT_STATE_AUTHENTICATING,
  GSK_IMAP_CLIENT_STATE_AUTHENTICATED,
  GSK_IMAP_CLIENT_STATE_LOGGING_OUT,
  GSK_IMAP_CLIENT_STATE_LOGGED_OUT
} GskImapClientState;

struct _GskImapClientClass 
{
  GskStreamClass stream_class;
  void (*handle_response) (GskImapClient *client,
			   GskImapRequest *request,
			   GSList         *messages,
			   GskImapResponse *response);
  void (*handle_event)    (GskImapClient *client,
			   GskImapServerEvent *event);
};
struct _GskImapClient 
{
  GskStream      stream;
  GskImapClientState state;
  GskBuffer incoming;
  GskBuffer outgoing;
  GskImapClientRequest *first_request;
  GskImapClientRequest *last_request;
};

/* --- prototypes --- */
void gsk_imap_client_request_capability (GskImapClient   *client,


G_END_DECLS

#endif
