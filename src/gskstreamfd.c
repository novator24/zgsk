#include "config.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include "gskghelpers.h"
#include "gskutils.h"
#include "gskstreamfd.h"
#include "gskerror.h"
#include "gsktypes.h"
#include "gskerrno.h"
#include "gskfork.h"
#include "gskdebug.h"
#include "debug.h"
#include "gskmacros.h"

/* Hmm: should be removed.  required for gsk_socket_address_finish_fd() */
#include "gsksocketaddress.h"

#if 0		/* is debugging on? */
#define DEBUG	g_message
#else
#define DEBUG(args...)
#endif

#define USE_GLIB_MAIN_LOOP	GSK_STREAM_FD_USE_GLIB_MAIN_LOOP

#define G_IO_CONNECT		(G_IO_IN | G_IO_OUT)


enum
{
  PROP_0,

  /* construct-only properties */
  PROP_FILE_DESCRIPTOR,
  PROP_IS_POLLABLE,
  PROP_IS_READABLE,
  PROP_IS_WRITABLE
};

typedef struct _GskStreamFdPrivate GskStreamFdPrivate;
struct _GskStreamFdPrivate
{
  GskSocketAddressSymbolic *symbolic;
  gpointer name_resolver;
};
#define GET_PRIVATE(stream_fd) \
  G_TYPE_INSTANCE_GET_PRIVATE(stream_fd, GSK_TYPE_STREAM_FD, GskStreamFdPrivate)


static GObjectClass *parent_class = NULL;

static void
set_events (GskStreamFd *stream_fd, GIOCondition events)
{
#if USE_GLIB_MAIN_LOOP
  stream_fd->poll_fd.events = events;
#else
  if (stream_fd->source != NULL)
    gsk_source_adjust_io (stream_fd->source, events);
#endif
}

static void
handle_stream_fd_events (GskStreamFd *stream_fd,
			 GIOCondition events)
{
  if (gsk_stream_get_is_connecting (stream_fd))
    {
      GError *error = NULL;
      DEBUG ("gsk_stream_fd_source_dispatch: gsk_stream=IS-CONNECTING");
      if (events == 0)
	return;
      /* XXX: this function should be renamed or virtualized or something */
      if (!gsk_socket_address_finish_fd (stream_fd->fd, &error))
	{
	  if (error)
	    {
	      DEBUG ("handle_stream_fd_events: %s", error->message);
	      set_events (stream_fd, 0);
	      gsk_io_set_gerror (GSK_IO (stream_fd), GSK_IO_ERROR_CONNECT, error);

	      gsk_io_notify_shutdown (GSK_IO (stream_fd));
	      return;
	    }
	  /* not done connecting yet: keep trying */
	  return;
	}
      DEBUG ("handle_stream_fd_events: connected successfully");
      set_events (stream_fd, stream_fd->post_connecting_events);
      gsk_io_notify_connected (GSK_IO (stream_fd));
      return;
    }
  DEBUG ("gsk_stream_fd_source_dispatch: revents=%d", events);
  if ((events & G_IO_IN) != 0
   && gsk_io_get_is_readable (stream_fd))
    gsk_io_notify_ready_to_read (GSK_IO (stream_fd));
  if ((events & G_IO_OUT) == G_IO_OUT
   && gsk_io_get_is_writable (stream_fd))
    gsk_io_notify_ready_to_write (GSK_IO (stream_fd));
  if ((events & G_IO_HUP) == G_IO_HUP)
    {
      if (gsk_io_get_is_readable (stream_fd))
	gsk_io_notify_read_shutdown (stream_fd);
      if (gsk_io_get_is_writable (stream_fd))
	gsk_io_notify_write_shutdown (stream_fd);
    }
  else if ((events & G_IO_ERR) == G_IO_ERR)
    {
      int e = gsk_errno_from_fd (stream_fd->fd);
      GskErrorCode code = gsk_error_code_from_errno (e);
      gsk_io_set_error (GSK_IO (stream_fd),
			GSK_IO_ERROR_POLL,
			code,
			"error polling file description %d: %s",
			stream_fd->fd,
			g_strerror (e));
    }
}


#if USE_GLIB_MAIN_LOOP
typedef struct _GskStreamFdSource GskStreamFdSource;
struct _GskStreamFdSource
{
  GSource base;
  GskStreamFd *stream_fd;
};

static gboolean
gsk_stream_fd_source_prepare (GSource    *source,
			      gint       *timeout)
{
  DEBUG ("gsk_stream_fd_source_prepare: events=%d", ((GskStreamFdSource*)source)->stream_fd->poll_fd.events);
  return FALSE;
}

static gboolean
gsk_stream_fd_source_check    (GSource    *source)
{
  GskStreamFdSource *fd_source = (GskStreamFdSource *) source;
  DEBUG ("gsk_stream_fd_source_check: events=%d, revents=%d", fd_source->stream_fd->poll_fd.events,fd_source->stream_fd->poll_fd.revents);
  return fd_source->stream_fd->poll_fd.revents != 0;
}

static gboolean
gsk_stream_fd_source_dispatch (GSource    *source,
			       GSourceFunc callback,
			       gpointer    user_data)
{
  GskStreamFdSource *fd_source = (GskStreamFdSource *) source;
  GskStreamFd *stream_fd = fd_source->stream_fd;
  g_object_ref (stream_fd);	/* XXX: maybe unnecessary (for destroy-in-destroy) */
  handle_stream_fd_events (stream_fd, stream_fd->poll_fd.revents);
  g_object_unref (stream_fd);
  return TRUE;
}

static GSourceFuncs gsk_stream_fd_source_funcs =
{
  gsk_stream_fd_source_prepare,
  gsk_stream_fd_source_check,
  gsk_stream_fd_source_dispatch,
  NULL,					/* finalize */
  NULL,					/* closure-callback (reserved) */
  NULL					/* closure-marshal (reserved) */
};

static gboolean
add_poll (GskStreamFd *stream_fd)
{
  g_return_val_if_fail (stream_fd->source == NULL, FALSE);

  stream_fd->source = g_source_new (&gsk_stream_fd_source_funcs,
				    sizeof (GskStreamFdSource));
  fd_source = (GskStreamFdSource *) stream_fd->source;
  fd_source->stream_fd = stream_fd;
  stream_fd->poll_fd.fd = stream_fd->fd;
  stream_fd->poll_fd.events = G_IO_HUP;
  g_source_add_poll (stream_fd->source, &stream_fd->poll_fd);
  g_source_attach (stream_fd->source, g_main_context_default ());
  return TRUE;
}

static void
remove_poll (GskStreamFd *stream_fd)
{
  if (stream_fd->source != NULL)
    {
      g_source_destroy (stream_fd->source);
      g_source_unref (stream_fd->source);
      stream_fd->source = NULL;
    }
}
#else  /* !USE_GLIB_MAIN_LOOP */
static gboolean
handle_io_event (int fd, GIOCondition events, gpointer user_data)
{
  GskStreamFd *stream_fd = GSK_STREAM_FD (user_data);
  g_return_val_if_fail (stream_fd->fd == fd, TRUE);
  g_object_ref (stream_fd);	/* XXX: maybe unnecessary (for destroy-in-destroy) */
  handle_stream_fd_events (stream_fd, events);
  g_object_unref (stream_fd);
  return TRUE;
}

static gboolean
add_poll (GskStreamFd *stream_fd)
{
  if (stream_fd->is_pollable)
    {
      stream_fd->source = gsk_main_loop_add_io (gsk_main_loop_default (),
						stream_fd->fd,
						G_IO_HUP,	/* events */
						handle_io_event,
						stream_fd,
						NULL);
    }
  else
    {
      GskIO *io = GSK_IO (stream_fd);
      if (gsk_io_get_is_readable (io))
	gsk_io_mark_idle_notify_read (io);
      if (gsk_io_get_is_writable (io))
	gsk_io_mark_idle_notify_write (io);
    }
  return TRUE;
}

static void
remove_poll (GskStreamFd *stream_fd)
{
  if (stream_fd->is_pollable)
    {
      if (stream_fd->source != NULL)
	{
	  gsk_source_remove (stream_fd->source);
	  stream_fd->source = NULL;
	}
    }
  else
    {
      GskIO *io = GSK_IO (stream_fd);
      gsk_io_clear_idle_notify_read (io);
      gsk_io_clear_idle_notify_write (io);
    }
}

#endif  /* !USE_GLIB_MAIN_LOOP */
/* Note: don't bother checking 'is_readable/is_writable':
 *       gskstream does that itself.
 */

static inline void
gsk_stream_fd_set_poll_event  (GskStreamFd   *stream_fd,
			       gushort        event_mask,
			       gboolean       do_poll)
{
  if (stream_fd->is_resolving_name
   || gsk_io_get_is_connecting (stream_fd))
    {
      if (do_poll)
	stream_fd->post_connecting_events |= event_mask;
      else
	stream_fd->post_connecting_events &= ~event_mask;
    }
  else if (stream_fd->failed_name_resolution)
    {
      /* do nothing, about to shutdown */
    }
  else
    {
#if USE_GLIB_MAIN_LOOP
      if (do_poll)
	stream_fd->poll_fd.events |= event_mask;
      else
	stream_fd->poll_fd.events &= ~event_mask;
#else
      if (do_poll)
        gsk_source_add_io_events (stream_fd->source, event_mask);
      else
        gsk_source_remove_io_events (stream_fd->source, event_mask);
#endif
    }
}

static void
gsk_stream_fd_set_poll_read   (GskIO         *io,
			       gboolean       do_poll)
{
  GskStreamFd *stream_fd = GSK_STREAM_FD (io);
  if (stream_fd->is_pollable)
    gsk_stream_fd_set_poll_event (stream_fd, G_IO_IN, do_poll);
}

static void
gsk_stream_fd_set_poll_write  (GskIO         *io,
			       gboolean       do_poll)
{
  GskStreamFd *stream_fd = GSK_STREAM_FD (io);
  if (stream_fd->is_pollable)
    gsk_stream_fd_set_poll_event (stream_fd, G_IO_OUT, do_poll);
}

/* --- reading and writing --- */
static guint
gsk_stream_fd_raw_read        (GskStream     *stream,
			       gpointer       data,
			       guint          length,
			       GError       **error)
{
  GskStreamFd *stream_fd = GSK_STREAM_FD (stream);
  int rv = read (stream_fd->fd, data, length);
  if (rv < 0)
    {
      gint e = errno;
      if (gsk_errno_is_ignorable (e))
	return 0;
      if (e == ECONNRESET)
        {
          gsk_io_notify_shutdown (GSK_IO (stream));
          return 0;
        }
      g_set_error (error, GSK_G_ERROR_DOMAIN,
		   gsk_error_code_from_errno (e),
		   "error reading from fd %d: %s",
		   stream_fd->fd,
		   g_strerror (e));
      return 0;
    }
  if (rv == 0)
    {
      gsk_io_notify_read_shutdown (GSK_IO (stream));
    }
  return rv;
}

static guint
gsk_stream_fd_raw_write       (GskStream     *stream,
			       gconstpointer  data,
			       guint          length,
			       GError       **error)
{
  GskStreamFd *stream_fd = GSK_STREAM_FD (stream);
  int rv;
  if (stream_fd->fd == -1)
    return 0;
  rv = write (stream_fd->fd, data, length);
  if (rv < 0)
    {
      gint e = errno;
      if (gsk_errno_is_ignorable (e))
	return 0;
      if (e == ECONNRESET)
        {
          gsk_io_notify_shutdown (GSK_IO (stream));
          return 0;
        }
      g_set_error (error, GSK_G_ERROR_DOMAIN,
		   gsk_error_code_from_errno (e),
		   "error writing to fd %d: %s",
		   stream_fd->fd,
		   g_strerror (e));
      //{char*t=gsk_escape_memory(data,length); g_message ("errorr writing : data=%s",t);g_free(t);}
      gsk_io_notify_shutdown (GSK_IO (stream_fd));
      return 0;
    }
  return rv;
}

static guint
gsk_stream_fd_raw_read_buffer(GskStream    *stream,
			      GskBuffer     *buffer,
			      GError       **error)
{
  GskStreamFd *stream_fd = GSK_STREAM_FD (stream);
  int rv;
  if (stream_fd->fd == -1)
    return 0;
  rv = gsk_buffer_read_in_fd (buffer, stream_fd->fd);
  if (rv < 0)
    {
      gint e = errno;
      if (gsk_errno_is_ignorable (e))
	return 0;
      g_set_error (error, GSK_G_ERROR_DOMAIN,
		   gsk_error_code_from_errno (e),
		   "error reading into buffer from fd %d: %s",
		   stream_fd->fd,
		   g_strerror (e));
      gsk_io_notify_shutdown (GSK_IO (stream));
      return 0;
    }
  if (rv == 0)
    gsk_io_notify_read_shutdown (GSK_IO (stream));
  return (guint) rv;
}

static guint
gsk_stream_fd_raw_write_buffer (GskStream     *stream,
				GskBuffer     *buffer,
			        GError       **error)
{
  GskStreamFd *stream_fd = GSK_STREAM_FD (stream);
  int rv;
  if (stream_fd->fd == -1)
    return 0;
  rv = gsk_buffer_writev (buffer, stream_fd->fd);
  if (rv < 0)
    {
      gint e = errno;
      if (gsk_errno_is_ignorable (e))
	return 0;
      g_set_error (error, GSK_G_ERROR_DOMAIN,
		   gsk_error_code_from_errno (e),
		   "error writing from buffer to fd %d: %s",
		   stream_fd->fd,
		   g_strerror (e));
      gsk_io_notify_shutdown (GSK_IO (stream_fd));
      return 0;
    }
  return (guint) rv;
}

/* --- shutting-down and closing --- */
static void
gsk_stream_fd_close (GskIO         *io)
{
  GskStreamFd *stream_fd = GSK_STREAM_FD (io);
  remove_poll (stream_fd);
  if (stream_fd->fd >= 0)
    {
      if (GSK_IS_DEBUGGING (FD))
        g_message ("debug-fd: close(%d): was a gsk-stream", stream_fd->fd);
      close (stream_fd->fd);
      gsk_fork_remove_cleanup_fd (stream_fd->fd);
      stream_fd->fd = -1;
      stream_fd->is_pollable = 0;
    }
}

static gboolean
gsk_stream_fd_shutdown_read   (GskIO         *io,
			       GError       **error)
{
  GskStreamFd *stream_fd = GSK_STREAM_FD (io);
  if (stream_fd->is_resolving_name)
    {
      if (!gsk_io_get_is_writable (io))
        {
          GskStreamFdPrivate *priv = GET_PRIVATE (stream_fd);
          gsk_socket_address_symbolic_cancel_resolution (priv->symbolic,
                                                         priv->name_resolver);
        }
    }
  else if (stream_fd->is_shutdownable)
    {
      if (shutdown (stream_fd->fd, SHUT_RD) < 0)
	{
	  int e = errno;

	  /* NOTE: there seems to be evidence that the other side
	     can close the connection before giving us
	     any HUP notification.  In this case, we get
	     ENOTCONN when we attempt the shutdown.
	     Just ignore that condition.

             TODO: find a real discussion of ENOTCONN */
	  if (e != ENOTCONN)
	    {
	      g_set_error (error, GSK_G_ERROR_DOMAIN,
			   gsk_error_code_from_errno (e),
			   "error shutting down fd %d for reading: %s",
			   stream_fd->fd,
			   g_strerror (e));
	      return FALSE;
	    }
	}
    }
  else
    {
      if (!gsk_io_get_is_writable (io))
	gsk_stream_fd_close (io);
    }
  return TRUE;
}

static gboolean
gsk_stream_fd_shutdown_write  (GskIO         *io,
			       GError       **error)
{
  GskStreamFd *stream_fd = GSK_STREAM_FD (io);
  if (stream_fd->is_resolving_name)
    {
      if (!gsk_io_get_is_readable (io))
        {
          GskStreamFdPrivate *priv = GET_PRIVATE (stream_fd);
          gsk_socket_address_symbolic_cancel_resolution (priv->symbolic,
                                                         priv->name_resolver);
        }
    }
  else if (stream_fd->is_shutdownable)
    {
      if (shutdown (stream_fd->fd, SHUT_WR) < 0)
	{
	  int e = errno;
	  /* NOTE: there seems to be evidence that the other side
	     can close the connection before giving us
	     any HUP notification.  In this case, we get
	     ENOTCONN when we attempt the shutdown.
	     Just ignore that condition.

             TODO: find a real discussion of ENOTCONN */
	  if (e != ENOTCONN)
	    {
	      g_set_error (error, GSK_G_ERROR_DOMAIN,
			   gsk_error_code_from_errno (e),
			   "error shutting down fd %d for writing: %s",
			   stream_fd->fd,
			   g_strerror (e));
	      return FALSE;
	    }
	}
    }
  else
    {
      if (!gsk_io_get_is_readable (io))
	gsk_stream_fd_close (io);
    }
  return TRUE;
}

/* --- arguments --- */
static void
gsk_stream_fd_get_property (GObject        *object,
			    guint           property_id,
			    GValue         *value,
			    GParamSpec     *pspec)
{
  switch (property_id)
    {
    case PROP_FILE_DESCRIPTOR:
      g_value_set_int (value, GSK_STREAM_FD (object)->fd);
      break;
    case PROP_IS_POLLABLE:
      g_value_set_boolean (value, GSK_STREAM_FD (object)->is_pollable);
      break;
    case PROP_IS_READABLE:
      g_value_set_boolean (value, gsk_io_get_is_readable (object));
      break;
    case PROP_IS_WRITABLE:
      g_value_set_boolean (value, gsk_io_get_is_writable (object));
      break;
    }
}

static void
gsk_stream_fd_set_property (GObject        *object,
			    guint           property_id,
			    const GValue   *value,
			    GParamSpec     *pspec)
{
  switch (property_id)
    {
    case PROP_FILE_DESCRIPTOR:
      {
	int fd = g_value_get_int (value);
	GskStreamFd *stream_fd = GSK_STREAM_FD (object);
	if (stream_fd->fd >= 0)
	  gsk_fork_remove_cleanup_fd (fd);
	if (fd >= 0)
	  gsk_fork_add_cleanup_fd (fd);
	stream_fd->fd = fd;
	break;
      }
    case PROP_IS_POLLABLE:
      {
	GSK_STREAM_FD (object)->is_pollable = g_value_get_boolean (value);
	break;
      }
    case PROP_IS_WRITABLE:
      { 
	if (g_value_get_boolean (value))
	  gsk_io_mark_is_writable (object);
	else
	  gsk_io_clear_is_writable (object);
	break;
      }
    case PROP_IS_READABLE:
      {
	if (g_value_get_boolean (value))
	  gsk_io_mark_is_readable (object);
	else
	  gsk_io_clear_is_readable (object);
	break;
      }
    }
}

static void
gsk_stream_fd_finalize (GObject *object)
{
  GskStreamFd *stream_fd = GSK_STREAM_FD (object);
  if (stream_fd->is_resolving_name)
    {
      GskStreamFdPrivate *priv = GET_PRIVATE (stream_fd);
      if (priv->name_resolver != NULL)
        {
          gsk_socket_address_symbolic_cancel_resolution (priv->symbolic, priv->name_resolver);
          priv->name_resolver = NULL;
        }
      stream_fd->is_resolving_name = 0;
      g_object_unref (priv->symbolic);
      priv->symbolic = NULL;
    }
  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static gboolean
gsk_stream_fd_open (GskIO     *io,
		    GError   **error)
{
  GskStreamFd *stream_fd = GSK_STREAM_FD (io);
  if (stream_fd->fd < 0)
    return TRUE;                /* permit postponed fd assignment */
  return add_poll (stream_fd);
}

/* --- functions --- */
static void
gsk_stream_fd_init (GskStreamFd *stream_fd)
{
  stream_fd->fd = -1;
}

static void
gsk_stream_fd_class_init (GskStreamClass *class)
{
  GParamSpec *pspec;
  GskIOClass *io_class = GSK_IO_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  parent_class = g_type_class_peek_parent (class);
  class->raw_read = gsk_stream_fd_raw_read;
  class->raw_read_buffer = gsk_stream_fd_raw_read_buffer;
  class->raw_write = gsk_stream_fd_raw_write;
  class->raw_write_buffer = gsk_stream_fd_raw_write_buffer;
  io_class->set_poll_read = gsk_stream_fd_set_poll_read;
  io_class->set_poll_write = gsk_stream_fd_set_poll_write;
  io_class->shutdown_read = gsk_stream_fd_shutdown_read;
  io_class->shutdown_write = gsk_stream_fd_shutdown_write;
  io_class->open = gsk_stream_fd_open;
  io_class->close = gsk_stream_fd_close;
  object_class->get_property = gsk_stream_fd_get_property;
  object_class->set_property = gsk_stream_fd_set_property;
  object_class->finalize = gsk_stream_fd_finalize;

  g_type_class_add_private (class, sizeof (GskStreamFdPrivate));

  pspec = gsk_param_spec_fd ("file-descriptor",
			     _("File Descriptor"),
			     _("for reading and/or writing"),
                             G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_FILE_DESCRIPTOR, pspec);

  pspec = g_param_spec_boolean ("is-pollable",
			        _("Is Pollable"),
			        _("whether the file descriptor is pollable"),
				FALSE,
                                G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_IS_POLLABLE, pspec);
  pspec = g_param_spec_boolean ("is-readable",
				_("Is Readable"),
				_("is the FD readable"),
				FALSE,
				G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_IS_READABLE, pspec);
  pspec = g_param_spec_boolean ("is-writable",
				_("Is Writable"),
				_("is the FD writable"),
				FALSE,
				G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_IS_WRITABLE, pspec);
}

GType gsk_stream_fd_get_type()
{
  static GType stream_fd_type = 0;
  if (!stream_fd_type)
    {
      static const GTypeInfo stream_fd_info =
      {
	sizeof(GskStreamFdClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_stream_fd_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskStreamFd),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_stream_fd_init,
	NULL		/* value_table */
      };
      GType parent = GSK_TYPE_STREAM;
      stream_fd_type = g_type_register_static (parent,
					       "GskStreamFd",
					       &stream_fd_info, 0);
    }
  return stream_fd_type;
}

GskStreamFdFlags gsk_stream_fd_flags_guess (gint            fd)
{
  struct stat stat_buf;
  GskStreamFdFlags rv = 0;
  guint flags;
  if (fstat (fd, &stat_buf) < 0)
    {
      g_warning ("gsk_stream_fd_flags_guess failed: fd=%d: %s",
		 fd, g_strerror (errno));
      return 0;
    }
  if (S_ISFIFO (stat_buf.st_mode)
   || S_ISSOCK (stat_buf.st_mode)
   || S_ISCHR (stat_buf.st_mode)
   || isatty (fd))
    rv |= GSK_STREAM_FD_IS_POLLABLE;
  if (S_ISSOCK (stat_buf.st_mode))
    rv |= GSK_STREAM_FD_IS_SHUTDOWNABLE;
  flags = fcntl (fd, F_GETFL);
  if ((flags & O_ACCMODE) == O_RDONLY)
    rv |= GSK_STREAM_FD_IS_READABLE;
  if ((flags & O_ACCMODE) == O_WRONLY)
    rv |= GSK_STREAM_FD_IS_WRITABLE;
  if ((flags & O_ACCMODE) == O_RDWR)
    rv |= GSK_STREAM_FD_IS_READABLE | GSK_STREAM_FD_IS_WRITABLE;
#if 0
  g_message ("gsk_stream_fd_flags_guess: fd=%d: pollable=%s, shutdownable=%s, readable=%s, writeable=%s",
	     fd, 
	     rv & GSK_STREAM_FD_IS_POLLABLE ? "yes" : "no",
	     rv & GSK_STREAM_FD_IS_SHUTDOWNABLE ? "yes" : "no",
	     rv & GSK_STREAM_FD_IS_READABLE ? "yes" : "no",
	     rv & GSK_STREAM_FD_IS_WRITABLE ? "yes" : "no");
#endif
  return rv;
}

/**
 * gsk_stream_fd_new_auto:
 * @fd: the file-descriptor to use as the basis for a stream.
 *
 * Try to guess the nature of the file-descriptor using fstat(),
 * isatty().
 *
 * returns: a new GskStream which will free @fd when it is closed.
 */
GskStream   *gsk_stream_fd_new_auto        (gint            fd)
{
  GskStreamFdFlags flags = gsk_stream_fd_flags_guess (fd);
  if (flags == 0)
    return NULL;
  return gsk_stream_fd_new (fd, flags);
}

/**
 * gsk_stream_fd_new:
 * @fd: the raw file descriptor.
 * @flags: information about how to use the file descriptor.
 *
 * Create a new GskStream based on an already open file descriptor.
 *
 * returns: a new GskStream
 */
GskStream *
gsk_stream_fd_new (gint fd,
		   GskStreamFdFlags flags)
{
  GskStream *rv;
  GskStreamFd *rv_fd;
  g_return_val_if_fail (fd >= 0, NULL);
  rv = g_object_new (GSK_TYPE_STREAM_FD, "file-descriptor", fd,
		     "is-pollable",
		     (flags & GSK_STREAM_FD_IS_POLLABLE) == GSK_STREAM_FD_IS_POLLABLE,
		     "is-readable",
		     (flags & GSK_STREAM_FD_IS_READABLE) == GSK_STREAM_FD_IS_READABLE,
		     "is-writable",
		     (flags & GSK_STREAM_FD_IS_WRITABLE) == GSK_STREAM_FD_IS_WRITABLE,
		     NULL);
  rv_fd = GSK_STREAM_FD (rv);
  if ((flags & GSK_STREAM_FD_IS_READABLE) == GSK_STREAM_FD_IS_READABLE)
    gsk_stream_mark_is_readable (rv);
  if ((flags & GSK_STREAM_FD_IS_WRITABLE) == GSK_STREAM_FD_IS_WRITABLE)
    gsk_stream_mark_is_writable (rv);
  rv_fd->is_shutdownable = (flags & GSK_STREAM_FD_IS_SHUTDOWNABLE) ? 1 : 0;

  return rv;
}

/**
 * gsk_stream_fd_new_connecting:
 * @fd: the raw file descriptor.
 *
 * Create a new GskStream based on a socket which is still in the process of connecting.
 *
 * returns: a new GskStream
 */
GskStream *
gsk_stream_fd_new_connecting (gint fd)
{
  GskStream *rv;
  GskStreamFd *stream_fd;
  g_return_val_if_fail (fd >= 0, NULL);
  rv = g_object_new (GSK_TYPE_STREAM_FD,
		     "file-descriptor", fd,
		     "is-pollable", TRUE,
		     NULL);
  gsk_stream_mark_is_connecting (rv);

  gsk_stream_mark_is_readable (rv);
  gsk_stream_mark_is_writable (rv);

  stream_fd = GSK_STREAM_FD (rv);
  stream_fd->is_shutdownable = 1;
  stream_fd->is_pollable = 1;
  set_events (stream_fd, G_IO_CONNECT);

  return rv;
}

/* address which requires a name resolution */
/**
 * gsk_stream_fd_new_from_symbolic_address:
 * @symbolic: a symbolic address.  name resolution will begin immediately.
 * @error: optional error return value.
 *
 * Create a stream connecting to a symbolic socket-address.
 *
 * returns: a new GskStream
 */
static void
done_resolving_name (GskStreamFd *stream_fd)
{
  GskStreamFdPrivate *priv = GET_PRIVATE (stream_fd);
  stream_fd->is_resolving_name = 0;
  priv->name_resolver = NULL;
  g_object_unref (priv->symbolic);
  priv->symbolic = NULL;
}

static void
handle_name_lookup_success  (GskSocketAddressSymbolic *orig,
                             GskSocketAddress         *resolved,
                             gpointer                  user_data)
{
  GskStreamFd *stream_fd = GSK_STREAM_FD (user_data);
  GError *error = NULL;
  gboolean is_connected;

  g_object_ref (stream_fd);

  done_resolving_name (stream_fd);
  stream_fd->fd = gsk_socket_address_connect_fd (resolved, &is_connected, &error);
  if (stream_fd->fd < 0)
    {
      gsk_io_set_gerror (GSK_IO (stream_fd), GSK_IO_ERROR_CONNECT, error);
      gsk_io_notify_shutdown (GSK_IO (stream_fd));
      goto cleanup;
    }
  stream_fd->is_shutdownable = 1;
  gsk_fork_add_cleanup_fd (stream_fd->fd);
  add_poll (stream_fd);
  if (is_connected)
    {
      set_events (stream_fd, stream_fd->post_connecting_events);
    }
  else
    {
      set_events (stream_fd, G_IO_CONNECT);
      gsk_stream_mark_is_connecting (stream_fd);
    }

cleanup:
  g_object_unref (stream_fd);
}

static void
handle_name_lookup_failure  (GskSocketAddressSymbolic *orig,
                             const GError             *error,
                             gpointer                  user_data)
{
  GskStreamFd *stream_fd = GSK_STREAM_FD (user_data);

  stream_fd->failed_name_resolution = 1;

  g_object_ref (stream_fd);
  done_resolving_name (stream_fd);
  gsk_io_set_gerror (GSK_IO (stream_fd), GSK_IO_ERROR_CONNECT, g_error_copy (error));
  gsk_io_notify_shutdown (GSK_IO (stream_fd));
  g_object_unref (stream_fd);
}

GskStream *
gsk_stream_fd_new_from_symbolic_address (GskSocketAddressSymbolic *symbolic,
                                         GError                  **error)
{
  GskStreamFd *stream_fd = g_object_new (GSK_TYPE_STREAM_FD, NULL);
  GskStreamFdPrivate *priv = GET_PRIVATE (stream_fd);
  stream_fd->is_resolving_name = 1;
  stream_fd->is_pollable = 1;
  gsk_stream_mark_is_readable (stream_fd);
  gsk_stream_mark_is_writable (stream_fd);
  priv->symbolic = g_object_ref (symbolic);
  priv->name_resolver = gsk_socket_address_symbolic_create_name_resolver (symbolic);
  gsk_socket_address_symbolic_start_resolution (symbolic,
                                                priv->name_resolver,
                                                handle_name_lookup_success,
                                                handle_name_lookup_failure,
                                                stream_fd,
                                                NULL);
  return GSK_STREAM (stream_fd);
}


/* more constructors */

/**
 * gsk_stream_fd_new_open:
 * @filename: file to open or create (depending on @open_flags)
 * @open_flags: same as the second argument to open(2).
 * @permission: permissions if creating a new file.
 * @error: optional error return value.
 *
 * Open a file as a #GskStream; this interface strongly
 * reflects its underlying open(2) implementation.
 * Using gsk_stream_fd_new_read_file()
 * and gsk_stream_fd_new_write_file() may be more portable ultimately.
 *
 * returns: a new GskStream
 */
GskStream *
gsk_stream_fd_new_open (const char     *filename,
			guint           open_flags,
			guint           permission,
			GError        **error)
{
  int fd = open (filename, open_flags, permission);
  GskStream *rv;
  if (fd < 0)
    {
      int e = errno;
      gsk_errno_fd_creation_failed_errno (e);
      g_set_error (error, GSK_G_ERROR_DOMAIN,
		   gsk_error_code_from_errno (e),
		   _("error opening %s: %s"),
		   filename, g_strerror (e));
      return NULL;
    }
  gsk_fd_set_close_on_exec (fd, TRUE);
  rv = gsk_stream_fd_new_auto (fd);

  return rv;
}

/**
 * gsk_stream_fd_new_read_file:
 * @filename: file to open readonly.
 * @error: optional error return value.
 *
 * Open a file for reading as a #GskStream.
 * The stream is not writable.
 *
 * returns: a new GskStream
 */
GskStream *
gsk_stream_fd_new_read_file   (const char     *filename,
			       GError        **error)
{
  return gsk_stream_fd_new_open (filename, O_RDONLY, 0, error);
}

/**
 * gsk_stream_fd_new_write_file:
 * @filename: file to open write-only.
 * @may_create: whether creating the filename is acceptable.
 * @should_truncate: whether an existing filename should be truncated.
 * @error: optional error return value.
 *
 * Open a file for writing as a #GskStream.
 * The stream is not readable.
 *
 * returns: a new GskStream
 */
GskStream  *
gsk_stream_fd_new_write_file  (const char     *filename,
			       gboolean        may_create,
			       gboolean        should_truncate,
			       GError        **error)
{
  guint flags = O_WRONLY | (may_create ? O_CREAT : 0)
              | (should_truncate ? O_TRUNC : 0);
  return gsk_stream_fd_new_open (filename, flags, 0660, error);
}

/**
 * gsk_stream_fd_new_create_file:
 * @filename: file to open write-only.
 * @may_exist: whether file may exist.
 * @error: optional error return value.
 *
 * Create a file for writing as a #GskStream.
 * The stream is not readable.
 *
 * returns: a new GskStream
 */
GskStream *
gsk_stream_fd_new_create_file (const char     *filename,
			       gboolean        may_exist,
			       GError        **error)
{
  guint flags = O_WRONLY | O_CREAT | (may_exist ? 0 : O_EXCL);
  return gsk_stream_fd_new_open (filename, flags, 0660, error);
}

/**
 * gsk_stream_fd_pipe:
 * @read_side_out: place to store a reference to a newly allocated readable stream-fd.
 * @write_side_out: place to store a reference to a newly allocated writable stream-fd.
 * @error: optional error return value.
 *
 * Call the pipe(2) system call to make a half-duplex connection
 * between two streams.
 * The newly allocated streams are returned.
 *
 * returns: whether the streams were allocated successfully.
 */
gboolean    gsk_stream_fd_pipe     (GskStream     **read_side_out,
                                    GskStream     **write_side_out,
			            GError        **error)
{
  int pipe_fds[2];
retry:
  if (pipe (pipe_fds) < 0)
    {
      if (gsk_errno_is_ignorable (errno))
        goto retry;
      gsk_errno_fd_creation_failed ();
      g_set_error (error, GSK_G_ERROR_DOMAIN,
		   gsk_error_code_from_errno (errno),
		   "error allocating pipe: %s", g_strerror (errno));
      return FALSE;
    }
  gsk_fd_set_close_on_exec (pipe_fds[0], TRUE);
  gsk_fd_set_close_on_exec (pipe_fds[1], TRUE);
  gsk_fd_set_nonblocking (pipe_fds[0]);
  gsk_fd_set_nonblocking (pipe_fds[1]);
  *read_side_out = gsk_stream_fd_new (pipe_fds[0], GSK_STREAM_FD_IS_READABLE | GSK_STREAM_FD_IS_POLLABLE);
  *write_side_out = gsk_stream_fd_new (pipe_fds[1], GSK_STREAM_FD_IS_WRITABLE | GSK_STREAM_FD_IS_POLLABLE);
  return TRUE;
}

static gboolean
do_socketpair (int fds[2],
               GError **error)
{
retry:
  if (socketpair (AF_UNIX, SOCK_STREAM, 0, fds) < 0)
    {
      if (gsk_errno_is_ignorable (errno))
        goto retry;
      gsk_errno_fd_creation_failed ();
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   gsk_error_code_from_errno (errno),
                   "error allocating duplex pipe: %s",
                   g_strerror (errno));
      return FALSE;
    }
  gsk_fd_set_close_on_exec (fds[0], TRUE);
  gsk_fd_set_close_on_exec (fds[1], TRUE);
  return TRUE;
}

/**
 * gsk_stream_fd_duplex_pipe:
 * @side_a_out: place to store a reference to a newly allocated stream-fd.
 * @side_b_out: place to store a reference to a newly allocated stream-fd.
 * @error: optional error return value.
 *
 * Create a pair of file-descriptors that 
 * are connected to eachother, and make GskStreamFds around them.
 *
 * returns: whether the streams were allocated successfully.
 */
gboolean    gsk_stream_fd_duplex_pipe (GskStream     **side_a_out,
                                       GskStream     **side_b_out,
			               GError        **error)
{
  int fds[2];
  guint flags = GSK_STREAM_FD_IS_READABLE
              | GSK_STREAM_FD_IS_WRITABLE
              | GSK_STREAM_FD_IS_POLLABLE;
  if (!do_socketpair (fds, error))
    return FALSE;
  gsk_fd_set_nonblocking (fds[0]);
  gsk_fd_set_nonblocking (fds[1]);
  *side_a_out = gsk_stream_fd_new (fds[0], flags);
  *side_b_out = gsk_stream_fd_new (fds[1], flags);
  return TRUE;
}

/**
 * gsk_stream_fd_duplex_pipe_fd:
 * @side_a_out: place to store a reference to a newly allocated stream-fd.
 * @side_b_fd_out: place to store a new file-descriptor.
 * @error: optional error return value.
 *
 * Create a pair of file-descriptors that 
 * are connected to eachother, and make a GskStreamFd around one of them.
 *
 * returns: whether the file-descriptors were allocated successfully.
 */
gboolean    gsk_stream_fd_duplex_pipe_fd (GskStream     **side_a_out,
                                          int            *side_b_fd_out,
			                  GError        **error)
{
  int fds[2];
  guint flags = GSK_STREAM_FD_IS_READABLE
              | GSK_STREAM_FD_IS_WRITABLE
              | GSK_STREAM_FD_IS_POLLABLE;
  if (!do_socketpair (fds, error))
    return FALSE;
  gsk_fd_set_nonblocking (fds[0]);
  *side_a_out = gsk_stream_fd_new (fds[0], flags);
  *side_b_fd_out = fds[1];
  return TRUE;
}
