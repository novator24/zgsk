#ifndef __GSK_REQUEST_H_
#define __GSK_REQUEST_H_

/*
 * GskRequest: abstract base class for asynchronous, cancellable requests.
 */

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GskRequestClass GskRequestClass;
typedef struct _GskRequest      GskRequest;

GType gsk_request_get_type (void) G_GNUC_CONST;

#define GSK_TYPE_REQUEST (gsk_request_get_type ())
#define GSK_REQUEST(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_REQUEST, GskRequest))
#define GSK_REQUEST_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_REQUEST, GskRequestClass))
#define GSK_REQUEST_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_REQUEST, GskRequestClass))
#define GSK_IS_REQUEST(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_REQUEST))
#define GSK_IS_REQUEST_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_REQUEST))

struct _GskRequestClass 
{
  GObjectClass object_class;

  /* --- signals --- */

  /* Emitted when request is done (whether successful or not). */
  void  (*done)      (GskRequest *request);

  /* Emitted when request is cancelled. */
  void  (*cancelled) (GskRequest *request);

  /* --- virtuals --- */

  void  (*start)     (GskRequest *request);
};

struct _GskRequest 
{
  GObject object;

  GError *error;

  guint is_cancellable : 1;
  guint is_running : 1;
  guint is_cancelled : 1;
  guint is_done : 1;
};

void                    gsk_request_start     (gpointer  request);
void                    gsk_request_cancel    (gpointer  request);

G_INLINE_FUNC
G_CONST_RETURN GError * gsk_request_get_error (gpointer  request);

#define gsk_request_had_error(req)		(gsk_request_get_error (req) != NULL)

/* flags */
#define gsk_request_get_is_cancellable(req)	_GSK_REQUEST_TEST_FIELD (req, is_cancellable)
#define gsk_request_get_is_running(req)		_GSK_REQUEST_TEST_FIELD (req, is_running)
#define gsk_request_get_is_cancelled(req)	_GSK_REQUEST_TEST_FIELD (req, is_cancelled)
#define gsk_request_get_is_done(req)		_GSK_REQUEST_TEST_FIELD (req, is_done)


/* protected */
void                    gsk_request_set_error (gpointer  request,
					       GError   *error);
void                    gsk_request_done      (gpointer  request);

#define gsk_request_mark_is_cancellable(req)	_GSK_REQUEST_MARK_FIELD (req, is_cancellable)
#define gsk_request_mark_is_running(req)	_GSK_REQUEST_MARK_FIELD (req, is_running)
#define gsk_request_mark_is_cancelled(req)	_GSK_REQUEST_MARK_FIELD (req, is_cancelled)
#define gsk_request_mark_is_done(req)		_GSK_REQUEST_MARK_FIELD (req, is_done)
#define gsk_request_clear_is_cancellable(req)	_GSK_REQUEST_CLEAR_FIELD (req, is_cancellable)
#define gsk_request_clear_is_running(req)	_GSK_REQUEST_CLEAR_FIELD (req, is_running)
#define gsk_request_clear_is_cancelled(req)	_GSK_REQUEST_CLEAR_FIELD (req, is_cancelled)
#define gsk_request_clear_is_done(req)		_GSK_REQUEST_CLEAR_FIELD (req, is_done)

#define _GSK_REQUEST_TEST_FIELD(req, field)	(GSK_REQUEST (req)->field != 0)
#define _GSK_REQUEST_MARK_FIELD(req, field)	G_STMT_START{ GSK_REQUEST (req)->field = 1; }G_STMT_END
#define _GSK_REQUEST_CLEAR_FIELD(req, field)	G_STMT_START{ GSK_REQUEST (req)->field = 0; }G_STMT_END


/* Inline functions. */

#if defined (G_CAN_INLINE) || defined (__GSK_REQUEST_C__)

/**
 * gsk_request_get_error:
 * @request: the #GskRequest to get the error for.
 *
 * Returns the error set for a #GskRequest, or NULL if none has been set.
 */
G_INLINE_FUNC G_CONST_RETURN GError *
gsk_request_get_error (gpointer request)
{
  g_return_val_if_fail (GSK_IS_REQUEST (request), NULL);
  return GSK_REQUEST (request)->error;
}

#endif /* defined (G_CAN_INLINE) || defined (__GSK_REQUEST_C__) */

G_END_DECLS

#endif
