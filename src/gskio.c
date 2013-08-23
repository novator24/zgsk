#include <stdlib.h>
#include "gskio.h"
#include "gskmacros.h"
#include "debug.h"

/* always use 'notify_connected' from the outside */
#define gsk_io_clear_is_connecting(io)           _GSK_IO_CLEAR_FIELD (io, is_connecting)

static GObjectClass *parent_class = NULL;

/* signals */
static guint on_connect_signal = 0;
static guint on_error_signal = 0;

/* ugh... */
#define DEBUG_PRINT_HEADER(flags, fctname)			\
	_GSK_DEBUG_PRINTF((flags), ("running %s on %s[%p]",	\
		           fctname, g_type_name(G_OBJECT_TYPE(io)), io))


/**
 * gsk_io_error_cause_to_string:
 * @cause: the cause code.
 *
 * Convert the GskIOErrorCause code into a human-readable lowercase string.
 *
 * returns: the error as a string.
 */
const char *
gsk_io_error_cause_to_string (GskIOErrorCause cause)
{
  switch (cause)
    {
    case GSK_IO_ERROR_NONE:		return "none";
    case GSK_IO_ERROR_OPEN:		return "open";
    case GSK_IO_ERROR_READ:		return "read";
    case GSK_IO_ERROR_WRITE:		return "write";
    case GSK_IO_ERROR_POLL_READ:	return "poll-read";
    case GSK_IO_ERROR_POLL_WRITE:	return "poll-write";
    case GSK_IO_ERROR_SHUTDOWN_READ:	return "shutdown-read";
    case GSK_IO_ERROR_SHUTDOWN_WRITE:	return "shutdown-write";
    case GSK_IO_ERROR_CLOSE:		return "close";
    case GSK_IO_ERROR_SYNC:		return "sync";
    case GSK_IO_ERROR_POLL:		return "poll";
    default:                            return "*unknown*";
    }
}

static gboolean default_print_errors = FALSE;
static gboolean has_default_print_errors = FALSE;

void
gsk_io_set_default_print_errors (gboolean print_errors)
{
  has_default_print_errors = TRUE;
  default_print_errors = print_errors;
}

static void
gsk_io_set_error_literal (GskIO           *io,
			  GskIOErrorCause  cause,
			  GError          *error)
{
  g_assert (error != NULL);
  if (io->error != NULL)
    g_error_free (io->error);
  io->error = error;
  io->error_cause = cause;
  if (GSK_IS_DEBUGGING (IO) || io->print_errors)
    g_message ("I/O Error [%s,%p]: cause=%s: %s",
               G_OBJECT_TYPE_NAME (io), io,
	       gsk_io_error_cause_to_string (cause),
	       error->message);

  g_signal_emit (io, on_error_signal, 0);

  if (io->error != NULL
   && gsk_io_get_shutdown_on_error (io))
    {
      gsk_io_shutdown (io, NULL);
    }
}

/**
 * gsk_io_set_error:
 * @io: the object whose GError member should be set.
 * @cause: what kind of situation triggered the error
 * @error_code: an error code.
 * @format: a printf-like format string.
 * @Varargs: values to be embedded in the format string.
 *
 * Set the error member of the #GskIO.
 */
void
gsk_io_set_error (GskIO             *io,
		  GskIOErrorCause    cause,
		  GskErrorCode       error_code,
		  const char        *format,
		  ...)
{
  va_list args;
  guint len;
  char *buf;
  GError *error;

  va_start (args, format);
  len = g_printf_string_upper_bound (format, args);
  buf = alloca (len + 1);
  g_vsnprintf (buf, len + 1, format, args);
  va_end (args);

  error = g_error_new_literal (GSK_G_ERROR_DOMAIN, error_code, buf);
  gsk_io_set_error_literal (io, cause, error);
}

/**
 * gsk_io_set_gerror:
 * @io: the IO whose error member should be set.
 * @cause: the operation which caused the error.
 * @error: gerror which will be freed by the IO automatically now.
 *
 * Set the IO's error member, taking ownership of
 * the @error parameter.
 */
void
gsk_io_set_gerror (GskIO             *io,
		   GskIOErrorCause    cause,
		   GError            *error)
{
  gsk_io_set_error_literal (io, cause, error);
}

/**
 * gsk_io_shutdown:
 * @io: the object which should be shut down.
 * @error: optional error to set upon failure.
 *
 * Shutdown the read and write ends of a #GskIO.
 */
void
gsk_io_shutdown (GskIO *io, GError **error)
{
  g_object_ref (io);
  gsk_io_read_shutdown (io, error);
  gsk_io_write_shutdown (io, error);
  g_object_unref (io);
}

/* --- implement poll-read/write for nonblocking ios --- */

/**
 * gsk_io_notify_shutdown:
 * @io: the object which is shut-down.
 *
 * This function is called by an implementation
 * when the read- and write- ends of the i/o object
 * have both shut-down.
 */
void
gsk_io_notify_shutdown (GskIO *io)
{
  g_object_ref (io);
  gsk_io_notify_read_shutdown (io);
  gsk_io_notify_write_shutdown (io);
  g_object_unref (io);
}

/**
 * gsk_io_notify_connected:
 * @io: the #GskIO that finished connecting to the remote side.
 *
 * Trigger an is-connected event.  This should only be
 * called by derived implementations.
 * Called to indicate that the connection has been made.
 */
void
gsk_io_notify_connected (GskIO *io)
{
  g_return_if_fail (gsk_io_get_is_connecting (io));
  DEBUG_PRINT_HEADER (GSK_DEBUG_IO, "gsk_io_notify_connected");
  gsk_io_clear_is_connecting (io);
  g_signal_emit (io, on_connect_signal, 0);
}

/**
 * gsk_io_close:
 * @io: the #GskIO to close.
 *
 * Close an open #GskIO.
 */
void
gsk_io_close (GskIO *io)
{
  GskIOClass *class = GSK_IO_GET_CLASS (io);
  g_return_if_fail (io->is_open);
  if (class->close != NULL)
    (*class->close) (io);
  io->is_open = FALSE;
}

static GObject *
gsk_io_constructor (GType                  type,
		    guint                  n_construct_properties,
		    GObjectConstructParam *construct_properties)
{
  GObject *rv = parent_class->constructor (type, n_construct_properties,
					   construct_properties);
  GskIO *io = GSK_IO (rv);
  GskIOClass *class = GSK_IO_GET_CLASS (io);
  _GSK_DEBUG_PRINTF(GSK_DEBUG_LIFETIME,
		    ("constructing %s [%p] [num_construct_properties=%d]",
		     g_type_name (type), rv, n_construct_properties));
  if (class->open != NULL)
    {
      GError *error = NULL;
      if (class->open (io, &error))
	{
	  io->is_open = 1;
	}
      else
	{
	  if (error == NULL)
	    gsk_io_set_error (io, GSK_IO_ERROR_OPEN,
				  GSK_ERROR_OPEN_FAILED,
			         _("open failed for %s (no explanation given)"),
				  g_type_name (G_OBJECT_CLASS_TYPE (class)));
	  else
	    gsk_io_set_error_literal (io, GSK_IO_ERROR_OPEN, error);
	  io->open_failed = 1;
	}
    }
  else
    {
      io->is_open = 1;
    }
  return rv;
}

static void
gsk_io_finalize (GObject *object)
{
  GskIO *io = GSK_IO (object);
  DEBUG_PRINT_HEADER(GSK_DEBUG_IO | GSK_DEBUG_LIFETIME, "gsk_io_finalize");

  /* TO CONSIDER:  should we close then destruct the hooks, instead? */
  gsk_hook_destruct (&io->read_hook);
  gsk_hook_destruct (&io->write_hook);
  if (io->error)
    g_error_free (io->error);
  gsk_io_close (io);
  (*parent_class->finalize) (object);
}


/* --- functions --- */
static void
gsk_io_init (GskIO *io)
{
  gsk_io_mark_shutdown_on_error (io);
  io->print_errors = default_print_errors;
  GSK_HOOK_INIT (io, GskIO, read_hook,
		 GSK_HOOK_CAN_HAVE_SHUTDOWN_ERROR,
		 set_poll_read, shutdown_read);
  GSK_HOOK_INIT (io, GskIO, write_hook,
		 GSK_HOOK_CAN_HAVE_SHUTDOWN_ERROR,
		 set_poll_write, shutdown_write);
}

static void
gsk_io_class_init (GskIOClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GType type = G_OBJECT_CLASS_TYPE (object_class);
  parent_class = g_type_class_peek_parent (class);
  object_class->constructor = gsk_io_constructor;
  object_class->finalize = gsk_io_finalize;
  GSK_HOOK_CLASS_INIT (object_class, "read", GskIO, read_hook);
  GSK_HOOK_CLASS_INIT (object_class, "write", GskIO, write_hook);
  on_connect_signal
    = g_signal_new ("on-connect",
		    type,
		    G_SIGNAL_NO_RECURSE,
		    G_STRUCT_OFFSET (GskIOClass, on_connect),
		    NULL,		/* accumulator */
		    NULL,		/* accu_data */
		    g_cclosure_marshal_VOID__VOID,
		    G_TYPE_NONE,
		    0);
  on_error_signal
    = g_signal_new ("on-error",
		    type,
		    G_SIGNAL_NO_RECURSE,
		    G_STRUCT_OFFSET (GskIOClass, on_error),
		    NULL,		/* accumulator */
		    NULL,		/* accu_data */
		    g_cclosure_marshal_VOID__VOID,
		    G_TYPE_NONE,
		    0);

  if (!has_default_print_errors)
    {
      const char *env = getenv ("GSK_PRINT_ERRORS");
      if (env)
        gsk_io_set_default_print_errors (atoi (env) ? 1 : 0);
    }
}

GType gsk_io_get_type()
{
  static GType io_type = 0;
  if (!io_type)
    {
      static const GTypeInfo io_info =
      {
	sizeof(GskIOClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_io_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskIO),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_io_init,
	NULL		/* value_table */
      };
      io_type = g_type_register_static (G_TYPE_OBJECT, "GskIO",
					&io_info, G_TYPE_FLAG_ABSTRACT);
    }
  return io_type;
}
