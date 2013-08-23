#include "config.h"
#include "gskstreamexternal.h"
#include "gskmacros.h"
#include "gskerror.h"
#include "gskerrno.h"
#include "gskghelpers.h"
#include <fcntl.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static GObjectClass *parent_class = NULL;

#define DEFAULT_MAX_WRITE_BUFFER		4096
#define DEFAULT_MAX_READ_BUFFER			4096
#define DEFAULT_MAX_ERROR_LINE_LEN		2048

static guint
gsk_stream_external_raw_read  (GskStream     *stream,
			       gpointer       data,
			       guint          length,
			       GError       **error)
{
  GskStreamExternal *external = GSK_STREAM_EXTERNAL (stream);
  guint rv = gsk_buffer_read (&external->read_buffer, data, length);
  if (external->read_buffer.size == 0)
    gsk_io_clear_idle_notify_read (external);
  return rv;
}

static guint
gsk_stream_external_raw_write (GskStream     *stream,
			       gconstpointer  data,
			       guint          length,
			       GError       **error)
{
  GskStreamExternal *external = GSK_STREAM_EXTERNAL (stream);
  gsize immediate_write = 0;
  gsize to_buffer;

  if (external->write_buffer.size == 0)
    {
      /* Try to do a write immediately. */
      int rv = write (external->write_fd, data, length);
      if (rv < 0)
	{
	  if (gsk_errno_is_ignorable (errno))
	    {
	      to_buffer = length;
	    }
	  else
	    {
	      g_set_error (error, GSK_G_ERROR_DOMAIN,
			   gsk_error_code_from_errno (errno),
			   "error writing to external process: %s",
			   g_strerror (errno));
	      gsk_io_notify_shutdown (GSK_IO (stream));
	      return 0;
	    }
	}
      else
	{
	  data = (char*)data + rv;
	  to_buffer = length - rv;
	  immediate_write = rv;
	}
    }
  else
    to_buffer = length;
  
  if (to_buffer + external->write_buffer.size > external->max_write_buffer)
    {
      if (external->write_buffer.size < external->max_write_buffer)
	to_buffer = external->max_write_buffer - external->write_buffer.size;
      else
	to_buffer = 0;
      gsk_io_clear_idle_notify_write (external);
    }

  /* If the buffer is going from empty -> nonempty,
     request writable notification from the main-loop. */
  if (external->write_buffer.size == 0
   && to_buffer > 0)
    gsk_source_adjust_io (external->write_source, G_IO_OUT);

  gsk_buffer_append (&external->write_buffer, data, to_buffer);

  return immediate_write + to_buffer;
}

static guint
gsk_stream_external_raw_read_buffer (GskStream     *stream,
			             GskBuffer     *buffer,
			             GError       **error)
{
  GskStreamExternal *external = GSK_STREAM_EXTERNAL (stream);
  guint rv;
  (void) error;
  rv = gsk_buffer_drain (buffer, &external->read_buffer);
  gsk_io_clear_idle_notify_read (external);
  return rv;
}

static guint
gsk_stream_external_raw_write_buffer (GskStream    *stream,
			              GskBuffer     *buffer,
			              GError       **error)
{
  GskStreamExternal *external = GSK_STREAM_EXTERNAL (stream);
  guint sys_written = 0;
  if (external->write_buffer.size == 0)
    {
      /* try using writev immediately */
      int rv = gsk_buffer_writev (buffer, external->write_fd);
      if (rv < 0)
	{
	  if (gsk_errno_is_ignorable (errno))
	    return 0;
	  g_set_error (error, GSK_G_ERROR_DOMAIN,
		       gsk_error_code_from_errno (errno),
		       "error writing to external process: %s",
		       g_strerror (errno));
	  gsk_io_notify_shutdown (GSK_IO (stream));
	  return 0;
	}

      if (buffer->size > 0)
	gsk_source_adjust_io (external->write_source, G_IO_OUT);
    }
  if (external->write_buffer.size >= external->max_write_buffer)
    return sys_written;
  return sys_written + gsk_buffer_transfer (&external->write_buffer, buffer,
					    external->max_write_buffer - external->write_buffer.size);
}

static void
gsk_stream_external_set_poll_read   (GskIO      *io,
			             gboolean    do_poll)
{
  GskStreamExternal *external = GSK_STREAM_EXTERNAL (io);
  if (do_poll)
    {
      if (external->read_buffer.size < external->max_read_buffer
       && external->read_source != NULL)
	gsk_source_add_io_events (external->read_source, G_IO_IN);
    }
  else
    {
      if (external->read_source != NULL)
	gsk_source_remove_io_events (external->read_source, G_IO_IN);
    }
}

static void
gsk_stream_external_set_poll_write  (GskIO      *io,
			             gboolean    do_poll)
{
#if 0
  GskStreamExternal *external = GSK_STREAM_EXTERNAL (io);
  if (do_poll)
    {
      ???
    }
  else
    {
      ???
    }
#endif
}

static gboolean
gsk_stream_external_shutdown_read   (GskIO      *io,
			             GError    **error)
{
  GskStreamExternal *external = GSK_STREAM_EXTERNAL (io);
  if (external->read_source != NULL)
    {
      gsk_source_remove (external->read_source);
      external->read_source = NULL;
    }
  if (external->read_fd >= 0)
    {
      close (external->read_fd);
      external->read_fd = -1;
    }
  return TRUE;
}

static gboolean
gsk_stream_external_shutdown_write  (GskIO      *io,
			             GError    **error)
{
  GskStreamExternal *external = GSK_STREAM_EXTERNAL (io);
  if (external->write_source != NULL)
    {
      gsk_source_remove (external->write_source);
      external->write_source = NULL;
    }
  if (external->write_fd >= 0)
    {
      close (external->write_fd);
      external->write_fd = -1;
    }
  return TRUE;
}


static void
gsk_stream_external_finalize (GObject *object)
{
  GskStreamExternal *external = GSK_STREAM_EXTERNAL (object);
  g_assert (external->process_source == NULL);
  if (external->read_source != NULL)
    {
      gsk_source_remove (external->read_source);
      external->read_source = NULL;
    }
  if (external->write_source != NULL)
    {
      gsk_source_remove (external->write_source);
      external->write_source = NULL;
    }
  if (external->read_fd >= 0)
    {
      close (external->read_fd);
      external->read_fd = -1;
    }
  if (external->write_fd >= 0)
    {
      close (external->write_fd);
      external->write_fd = -1;
    }
  if (external->read_err_source != NULL)
    {
      gsk_source_remove (external->read_err_source);
      external->read_err_source = NULL;
    }
  if (external->read_err_fd >= 0)
    {
      close (external->read_err_fd);
      external->read_err_fd = -1;
    }
  gsk_buffer_destruct (&external->read_buffer);
  gsk_buffer_destruct (&external->read_err_buffer);
  gsk_buffer_destruct (&external->write_buffer);

  (*parent_class->finalize) (object);
}

static gboolean
handle_read_fd_ready (int                   fd,
		      GIOCondition          condition,
		      gpointer              user_data)
{
  GskStreamExternal *external = GSK_STREAM_EXTERNAL (user_data);
  int rv;
  gboolean had_data = external->read_buffer.size > 0;
  g_assert (external->read_fd == fd);
  if ((condition & G_IO_ERR) == G_IO_ERR)
    {
      int err = gsk_errno_from_fd (fd);
      gsk_io_set_error (GSK_IO (external), GSK_IO_ERROR_READ,
			GSK_ERROR_IO, "error flag on %d: %s", fd, 
			g_strerror (err));
      gsk_source_remove (external->read_source);
      close (fd);
      external->read_fd = -1;
      external->read_source = NULL;
      gsk_io_notify_read_shutdown (external);
      return FALSE;
    }

  if ((condition & G_IO_HUP) == G_IO_HUP)
    {
      gsk_source_remove (external->read_source);
      close (fd);
      external->read_fd = -1;
      external->read_source = NULL;
      gsk_io_notify_read_shutdown (external);
      return FALSE;
    }

  if ((condition & G_IO_IN) != G_IO_IN)
    return TRUE;

  rv = gsk_buffer_read_in_fd (&external->read_buffer, fd);
  if (rv < 0)
    {
      if (gsk_errno_is_ignorable (errno))
	{
	  return TRUE;
	}
      gsk_source_remove (external->read_source);
      close (fd);
      external->read_fd = -1;
      external->read_source = NULL;
      gsk_io_set_error (GSK_IO (external), GSK_IO_ERROR_READ,
			gsk_error_code_from_errno (errno),
			"error reading to low-level stream: %s",
			g_strerror (errno));
      gsk_io_notify_read_shutdown (external);
      return FALSE;
    }
  if (rv == 0)
    {
      gsk_source_remove (external->read_source);
      close (fd);
      external->read_fd = -1;
      external->read_source = NULL;
      gsk_io_notify_read_shutdown (external);
      return FALSE;
    }
  if (!had_data)
    gsk_io_mark_idle_notify_read (external);
  return TRUE;
}

static gboolean
handle_write_fd_ready (int                   fd,
		       GIOCondition          condition,
		       gpointer              user_data)
{
  GskStreamExternal *external = GSK_STREAM_EXTERNAL (user_data);
  int rv;

  if ((condition & G_IO_ERR) == G_IO_ERR)
    {
      int err = gsk_errno_from_fd (fd);
      gsk_io_set_error (GSK_IO (external), GSK_IO_ERROR_WRITE,
			GSK_ERROR_IO, "error flagged writingon %d: %s", fd, 
			g_strerror (err));
      gsk_source_remove (external->write_source);
      close (fd);
      external->write_fd = -1;
      external->write_source = NULL;
      gsk_io_notify_write_shutdown (external);
      return FALSE;
    }

  if ((condition & G_IO_OUT) != G_IO_OUT)
    return TRUE;

  rv = gsk_buffer_writev (&external->write_buffer, fd);
  if (rv < 0)
    {
      if (gsk_errno_is_ignorable (errno))
	{
	  return TRUE;
	}
      gsk_io_set_error (GSK_IO (external), GSK_IO_ERROR_WRITE,
			gsk_error_code_from_errno (errno),
			"error writing to low-level stream: %s",
			g_strerror (errno));
      gsk_source_remove (external->write_source);
      external->write_fd = -1;
      external->write_source = NULL;
      close (fd);
      gsk_io_notify_write_shutdown (external);
      return FALSE;
    }

  if (external->write_buffer.size == 0)
    gsk_source_adjust_io (external->write_source, 0);
  if (external->write_buffer.size < external->max_write_buffer)
    gsk_io_mark_idle_notify_write (GSK_IO (external));
  return TRUE;
}

static gboolean
handle_read_err_fd_ready (int                   fd,
		          GIOCondition          condition,
		          gpointer              user_data)
{
  GskStreamExternal *external = GSK_STREAM_EXTERNAL (user_data);
  int rv;
  char *line;

  g_assert (external->read_err_fd == fd);

  if ((condition & G_IO_ERR) == G_IO_ERR)
    {
      int err = gsk_errno_from_fd (fd);
      gsk_io_set_error (GSK_IO (external), GSK_IO_ERROR_READ,
			GSK_ERROR_IO, "error flag reading error from process %ld: %s",
			external->pid, g_strerror (err));
      gsk_source_remove (external->read_err_source);
      close (fd);
      external->read_err_fd = -1;
      external->read_err_source = NULL;
      return FALSE;
    }

  if ((condition & G_IO_HUP) == G_IO_HUP)
    {
      gsk_source_remove (external->read_err_source);
      close (fd);
      external->read_err_fd = -1;
      external->read_err_source = NULL;
      return FALSE;
    }

  if ((condition & G_IO_IN) != G_IO_IN)
    return TRUE;

  rv = gsk_buffer_read_in_fd (&external->read_err_buffer, fd);
  if (rv < 0)
    {
      if (gsk_errno_is_ignorable (errno))
	{
	  return TRUE;
	}
      gsk_io_set_error (GSK_IO (external), GSK_IO_ERROR_WRITE,
			gsk_error_code_from_errno (errno),
			"error reading error messages low-level stream: %s",
			g_strerror (errno));
      gsk_source_remove (external->read_err_source);
      external->read_err_fd = -1;
      external->read_err_source = NULL;
      close (fd);
      return FALSE;
    }

  while ((line = gsk_buffer_read_line (&external->read_err_buffer)) != NULL)
    {
      (*external->err_func) (external, line, external->user_data);
      g_free (line);
    }

  return TRUE;
}

static void
handle_process_terminated (GskMainLoopWaitInfo  *info,
			   gpointer              user_data)
{
  GskStreamExternal *external = GSK_STREAM_EXTERNAL (user_data);
  if (external->term_func != NULL)
    (*external->term_func) (external, info, external->user_data);
  external->process_source = NULL;
}

/* --- functions --- */
static void
gsk_stream_external_init (GskStreamExternal *stream_external)
{
  stream_external->max_write_buffer = DEFAULT_MAX_WRITE_BUFFER;
  stream_external->max_read_buffer = DEFAULT_MAX_READ_BUFFER;
  stream_external->max_err_line_length = DEFAULT_MAX_ERROR_LINE_LEN;
}

static void
gsk_stream_external_class_init (GskStreamExternalClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GskStreamClass *stream_class = GSK_STREAM_CLASS (class);
  GskIOClass *io_class = GSK_IO_CLASS (class);
  parent_class = g_type_class_peek_parent (class);

  stream_class->raw_read = gsk_stream_external_raw_read;
  stream_class->raw_read_buffer = gsk_stream_external_raw_read_buffer;
  stream_class->raw_write = gsk_stream_external_raw_write;
  stream_class->raw_write_buffer = gsk_stream_external_raw_write_buffer;
  io_class->shutdown_read = gsk_stream_external_shutdown_read;
  io_class->shutdown_write = gsk_stream_external_shutdown_write;
  io_class->set_poll_read = gsk_stream_external_set_poll_read;
  io_class->set_poll_write = gsk_stream_external_set_poll_write;
  object_class->finalize = gsk_stream_external_finalize;
}

GType gsk_stream_external_get_type()
{
  static GType stream_external_type = 0;
  if (!stream_external_type)
    {
      static const GTypeInfo stream_external_info =
      {
	sizeof(GskStreamExternalClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_stream_external_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskStreamExternal),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_stream_external_init,
	NULL		/* value_table */
      };
      stream_external_type = g_type_register_static (GSK_TYPE_STREAM,
                                                  "GskStreamExternal",
						  &stream_external_info, 0);
    }
  return stream_external_type;
}

static inline int
nb_pipe (int pipe_fds[2])
{
retry:
  if (pipe (pipe_fds) < 0)
    {
      if (gsk_errno_is_ignorable (errno))
        goto retry;
      gsk_errno_fd_creation_failed ();
      return -1;
    }
  gsk_fd_set_nonblocking (pipe_fds[0]);
  gsk_fd_set_nonblocking (pipe_fds[1]);
  return 0;
}

/**
 * gsk_stream_external_new:
 * @flags: whether to allocate a pseudo-tty and/or use $PATH.
 * @stdin_filename: file to redirect as standard input into the process.
 * If NULL, then the returned stream will be writable, and data
 * written in will appear as standard-input to the process.
 * @stdout_filename: file to redirect output from the process's standard-input.
 * If NULL, then the returned stream will be readable; the data read
 * will be the process's standard-output.
 * @term_func: function to call with the process's exit status.
 * @err_func: function to call with standard error output from the process,
 * line-by-line.
 * @user_data: data to pass to term_func and err_func.
 * @path: name of the executable.
 * @argv: arguments to pass to the executable.
 * @env: environment variables, as a NULL-terminated key=value list of strings.
 * @error: optional error return location.
 *
 * Allocates a stream which points to standard input and/or output
 * of a newly forked process.
 *
 * This forks, redirects the standard input from a file if @stdin_filename
 * is non-NULL, otherwise it uses a pipe to communicate
 * the data to the main-loop.
 * It redirects the standard output to a file if @stdout_filename
 * is non-NULL, otherwise it uses a pipe to communicate
 * the data from the main-loop.
 *
 * When the process terminates, @term_func will be called.
 * Until @term_func is called, @err_func may be called with lines
 * of data from the standard-error of the process.
 *
 * If @env is non-NULL, then the environment for the subprocess
 * will consist of nothing but the list given as @env.
 * If @env is NULL, then the environment will be the same as
 * the parent process's environment.
 *
 * If GSK_STREAM_EXTERNAL_SEARCH_PATH is set, then the executable
 * will be saught in the colon-separated list of paths
 * in the $PATH environment-variable.  Otherwise, @path must be
 * the exact path to the executable.
 *
 * If the executable is not found, or exec otherwise fails,
 * then @term_func will be called with an exit status of 127.
 *
 * returns: the new stream.
 */
GskStream *
gsk_stream_external_new       (GskStreamExternalFlags      flags,
			       const char                 *stdin_filename,
			       const char                 *stdout_filename,
                               GskStreamExternalTerminated term_func,
                               GskStreamExternalStderr     err_func,
                               gpointer                    user_data,
                               const char                 *path,
                               const char                 *argv[],
                               const char                 *env[],
			       GError                    **error)
{
  GskStreamExternal *external = g_object_new (GSK_TYPE_STREAM_EXTERNAL, NULL);
  GskMainLoop *main_loop = gsk_main_loop_default ();
  int rpipe[2], wpipe[2], epipe[2];
  int fork_rv;
  if (stdout_filename == NULL)
    nb_pipe (rpipe);
  if (stdin_filename == NULL)
    nb_pipe (wpipe);
  if (err_func != NULL)
    nb_pipe (epipe);

  for (;;)
    {
      fork_rv = fork ();
      if (fork_rv < 0)
	{
	  int e = errno;
	  if (gsk_errno_is_ignorable (e))
	    continue;

	  /* error: cannot fork, set error, clean up, and return. */
	  g_set_error (error, GSK_G_ERROR_DOMAIN,
		       gsk_error_code_from_errno (e),
		       "fork: %s",
		       g_strerror (errno));

	  if (stdout_filename == NULL)
	    {
	      close (rpipe[0]);
	      close (rpipe[1]);
	    }

	  if (stdin_filename == NULL)
	    {
	      close (wpipe[0]);
	      close (wpipe[1]);
	    }

	  if (err_func != NULL)
	    {
	      close (epipe[0]);
	      close (epipe[1]);
	    }
	  return NULL;
	}
      else		/* fork >= 0 */
	{
	  break;	/* success: child and parent get out of this loop */
	}
    }

  /* Child process */
  if (fork_rv == 0)
    {
      const char *search_path;
      /* Deal with standard output. */
      if (stdout_filename == NULL)
	{
	  dup2 (rpipe[1], STDOUT_FILENO);
	  close (rpipe[0]);
	  close (rpipe[1]);
	}
      else
	{
	   int fd = open (stdout_filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	   if (fd < 0)
	     {
               gsk_errno_fd_creation_failed ();
	       g_warning ("error opening %s", stdout_filename);
	       _exit (126);
	     }
	   dup2 (fd, STDOUT_FILENO);
	   close (fd);
	}

      /* Deal with standard input. */
      if (stdin_filename == NULL)
	{
	  dup2 (wpipe[0], STDIN_FILENO);
	  close (wpipe[0]);
	  close (wpipe[1]);
	}
      else
	{
	   int fd = open (stdin_filename, O_RDONLY);
	   if (fd < 0)
	     {
               gsk_errno_fd_creation_failed ();
	       g_warning ("error opening %s", stdin_filename);
	       _exit (126);
	     }
	   dup2 (fd, STDIN_FILENO);
	   close (fd);
	}

      /* deal with standard error. */
      if (err_func != NULL)
	{
	  dup2 (epipe[1], STDERR_FILENO);
	  close (epipe[0]);
	  close (epipe[1]);
	}
      else
	{
	   int fd = open ("/dev/null", O_WRONLY);
	   if (fd < 0)
	     {
               gsk_errno_fd_creation_failed ();
	       g_warning ("error opening %s", "/dev/null");
	       _exit (126);
	     }
	   dup2 (fd, STDERR_FILENO);
	   close (fd);
	}


#define PATH_SEPARATOR			':'
#define IS_PATH_SEPARATOR(ch)		((ch) == PATH_SEPARATOR)
#define IS_NOT_PATH_SEPARATOR(ch)	((ch) != PATH_SEPARATOR)

      gsk_fd_clear_nonblocking (STDIN_FILENO);
      gsk_fd_clear_nonblocking (STDOUT_FILENO);
      gsk_fd_clear_nonblocking (STDERR_FILENO);

      if (strchr (path, '/') == NULL
       && (flags & GSK_STREAM_EXTERNAL_SEARCH_PATH) == GSK_STREAM_EXTERNAL_SEARCH_PATH
       && (search_path = g_getenv("PATH")) != NULL)
	{
	  const char *start, *end;
	  int sp_len = strlen (search_path) + 1 + strlen (path) + 1;
	  char *scratch = sp_len > 4096 ? g_malloc (sp_len) : g_alloca (sp_len);
	  /* search through each component of $PATH. */
	  GSK_SKIP_CHAR_TYPE (search_path, IS_PATH_SEPARATOR);
	  start = search_path;
	  while (*start)
	    {
	      end = start;
	      GSK_SKIP_CHAR_TYPE (end, IS_NOT_PATH_SEPARATOR);
	      if (end > start)
		{
		  memcpy (scratch, start, end - start);
		  scratch[end - start] = G_DIR_SEPARATOR;
		  strcpy (scratch + (end - start) + 1, path);

		  if (env)
		    execve (scratch, (char **) argv, (char **) env);
		  else
		    execv (scratch, (char **) argv);

		  /* if not found, continue to the next path component. */
		}
	      start = end;
	      GSK_SKIP_CHAR_TYPE (start, IS_PATH_SEPARATOR);
	    }
	}
      else
	{
	  /* Just exec, no path searching required. */
	  if (env)
	    execve (path, (char **) argv, (char **) env);
	  else
	    execv (path, (char **) argv);
	}
      _exit (127);
    }

  /* Parent process */
  if (stdout_filename == NULL)
    {
      external->read_fd = rpipe[0];
      close (rpipe[1]);
      external->read_source = gsk_main_loop_add_io (main_loop, external->read_fd, G_IO_IN,
						    handle_read_fd_ready, external, NULL);
      gsk_io_mark_is_readable (external);
    }
  else
    {
      external->read_fd = -1;
    }
  if (stdin_filename == NULL)
    {
      external->write_fd = wpipe[1];
      close (wpipe[0]);
      external->write_source = gsk_main_loop_add_io (main_loop, external->write_fd, 0,
						     handle_write_fd_ready, external, NULL);
      gsk_io_mark_is_writable (external);
      gsk_io_mark_idle_notify_write (GSK_IO (external));
    }
  else
    {
      external->write_fd = -1;
    }
  if (err_func != NULL)
    {
      external->read_err_fd = epipe[0];
      close (epipe[1]);
      external->read_err_source = gsk_main_loop_add_io (main_loop, external->read_err_fd, G_IO_IN,
						        handle_read_err_fd_ready, external, NULL);
    }
  else
    external->read_err_fd = -1;

  external->pid = fork_rv;
  external->process_source = gsk_main_loop_add_waitpid (main_loop, external->pid,
							handle_process_terminated, 
							g_object_ref (external), 
							g_object_unref);

  external->term_func = term_func;
  external->err_func = err_func;
  external->user_data = user_data;

  return GSK_STREAM (external);
}
