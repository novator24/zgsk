#define G_IMPLEMENT_INLINES 1
#define __GSK_REQUEST_C__
#include "gskrequest.h"
#include "gskdebug.h"
#include "debug.h"

#if defined (GSK_DEBUG)
static void gsk_request_debug (GskRequest *request,
			       const char *format,
			       ...) G_GNUC_PRINTF(2,3);
#define DEBUG(args) \
  G_STMT_START{ \
    if (GSK_IS_DEBUGGING (REQUEST)) \
      gsk_request_debug args; \
  }G_STMT_END
#else
#define DEBUG(args)
#endif

static guint done_signal = 0;
static guint cancelled_signal = 0;

/* Default signal handler. */
static void
gsk_request_default_cancelled (GskRequest *request)
{
  gsk_request_mark_is_cancelled (request);
}

/**
 * gsk_request_start:
 * @request: the #GskRequest to start.
 *
 * Start a #GskRequest.
 */
void
gsk_request_start (gpointer request)
{
  GskRequestClass *request_class;

  g_return_if_fail (request);
  g_return_if_fail (GSK_IS_REQUEST (request));
  request_class = GSK_REQUEST_GET_CLASS (request);
  g_return_if_fail (request_class);
  g_return_if_fail (request_class->start);

  g_return_if_fail (!gsk_request_get_is_running (request));
  g_return_if_fail (!gsk_request_get_is_cancelled (request));
  g_return_if_fail (!gsk_request_get_is_done (request));

  DEBUG ((request, "starting"));

  (*request_class->start) (GSK_REQUEST (request));
}

/**
 * gsk_request_cancel:
 * @request: the #GskRequest to cancel.
 *
 * Cancel a running #GskRequest.
 */
void
gsk_request_cancel (gpointer request)
{
  g_return_if_fail (gsk_request_get_is_cancellable (request));
  g_return_if_fail (!gsk_request_get_is_cancelled (request));
  DEBUG ((request, "cancelling"));
  g_signal_emit (request, cancelled_signal, 0);
}

/**
 * gsk_request_set_error:
 * @request: the #GskRequest to set the error for.
 * @error: the #GError.
 *
 * Set the error member of a #GskRequest.
 *
 * Protected; this function should only be used by subclasses of
 * #GskRequest.
 */
void
gsk_request_set_error (gpointer ptr, GError *error)
{
  GskRequest *request = GSK_REQUEST (ptr);

  g_return_if_fail (request);
  g_return_if_fail (GSK_IS_REQUEST (request));
  g_return_if_fail (error);

  DEBUG ((request, "setting error: %s", error ? error->message : "(NULL)"));
  if (request->error)
    g_error_free (request->error);
  request->error = error;
}

/**
 * gsk_request_done:
 * @request: the #GskRequest which has completed.
 *
 * Mark the request as done; emit the "done" signal to notify clients.
 *
 * Protected; this function should only be used by subclasses of
 * #GskRequest.
 */
void
gsk_request_done (gpointer request)
{
  g_return_if_fail (GSK_IS_REQUEST (request));
  g_return_if_fail (!gsk_request_get_is_cancelled (request));
  g_return_if_fail (!gsk_request_get_is_done (request));
  gsk_request_clear_is_running (request);
  gsk_request_mark_is_done (request);
  DEBUG ((request, "done"));
  g_signal_emit (request, done_signal, 0);
}

static void
gsk_request_class_init (GskRequestClass *request_class)
{
  GType type = G_OBJECT_CLASS_TYPE (request_class);

  /* Default signal handler. */
  request_class->cancelled = gsk_request_default_cancelled;

  done_signal =
    g_signal_new ("done",
		  type,
		  G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
		  G_STRUCT_OFFSET (GskRequestClass, done),
		  NULL,		/* accumulator */
		  NULL,		/* accu_data */
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE,
		  0);
  cancelled_signal =
    g_signal_new ("cancelled",
		  type,
		  G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
		  G_STRUCT_OFFSET (GskRequestClass, cancelled),
		  NULL,		/* accumulator */
		  NULL,		/* accu_data */
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE,
		  0);
}

GType
gsk_request_get_type (void)
{
  static GType type = 0;
  if (G_UNLIKELY (type == 0))
    {
      static const GTypeInfo type_info =
	{
	  sizeof (GskRequestClass),
	  (GBaseInitFunc) NULL,
	  (GBaseFinalizeFunc) NULL,
	  (GClassInitFunc) gsk_request_class_init,
	  NULL,		/* class_finalize */
	  NULL,		/* class_data */
	  sizeof (GskRequest),
	  0,		/* n_preallocs */
	  (GInstanceInitFunc) NULL,
	  NULL		/* value_table */
	};
      type = g_type_register_static (G_TYPE_OBJECT,
				     "GskRequest",
				     &type_info,
				     G_TYPE_FLAG_ABSTRACT);
    }
  return type;
}

#if defined (GSK_DEBUG)
#include <stdio.h>
static void
gsk_request_debug (GskRequest *request,
		   const char *format,
		   ...)
{
  va_list args;

  fprintf (stderr, "debug: request [%s/%p]: ",
	   g_type_name (G_OBJECT_TYPE (request)),
	   request);
  va_start (args, format);
  vfprintf (stderr, format, args);
  va_end (args);
  fprintf (stderr, ".\n");
}
#endif
