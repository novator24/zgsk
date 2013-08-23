#include "../gskstreamfd.h"
#include "../gskinit.h"
#include "../gskmainloop.h"


/* globals */
static gboolean done;
static gboolean timed_out;

static gboolean
handle_readable (GskStream *stream, gpointer data)
{
  char buf[1024];
  unsigned n_read;
  GError *error = 0;
  n_read = gsk_stream_read (stream, buf, sizeof (buf), &error);
  if (n_read == 0)
    {
      if (error)
	{
	  g_printerr ("got error: %s\n", error->message);
	  g_error_free (error);
	}
      return FALSE;
    }
  g_assert_not_reached ();
  return TRUE;
}

static gboolean
handle_read_shutdown (GskStream *stream, gpointer data)
{
  return FALSE;
}

static void
read_destroy_notify (gpointer data)
{
  done = TRUE;
}

static gboolean
handle_writable (GskStream *stream, gpointer data)
{
  //g_printerr ("handle_writable (after read-end hung up) ??\n");
  char buf = 0;
  g_message ("handle_writable");
  gsk_stream_write (stream, &buf, 1, NULL);
  return TRUE;
}

static gboolean
handle_write_shutdown (GskStream *stream, gpointer data)
{
  g_message ("handle_write_shutdown");
  return FALSE;
}

static void
write_destroy_notify (gpointer data)
{
  g_message ("write_destroy_notify\n");
  done = TRUE;
}

static gboolean
handle_main_loop_timeout (void *user_data)
{
  done = TRUE;
  timed_out = TRUE;
  return FALSE;
}

int main(int argc, char **argv)
{
  GskStream *read_side, *write_side;
  GError *error = 0;
  gsk_init_without_threads (&argc, &argv);
  if (!gsk_stream_fd_pipe (&read_side, &write_side, &error))
    g_error ("error creating pipe: %s", error->message);
  g_printerr ("Testing Write-end Shutdown... ");
  done = FALSE;
  gsk_stream_trap_readable(GSK_STREAM (read_side),
			   handle_readable, handle_read_shutdown,
			   NULL, read_destroy_notify);
  g_assert(done == FALSE);
  gsk_io_shutdown (GSK_IO(write_side), &error);
  if (error)
    g_error ("error running gsk_io_shutdown: %s", error->message);
  while (!done)
    gsk_main_loop_run (gsk_main_loop_default (), -1, NULL);
  g_printerr (" done.\n");
  g_object_unref (read_side);
  g_object_unref (write_side);

  if (!gsk_stream_fd_pipe (&read_side, &write_side, &error))
    g_error ("error creating pipe: %s", error->message);
  g_printerr ("Testing Read-end Shutdown... ");
  timed_out = done = FALSE;
  gsk_stream_trap_writable(GSK_STREAM (write_side),
			   handle_writable, handle_write_shutdown,
			   NULL, write_destroy_notify);
  g_assert(done == FALSE);
  gsk_io_shutdown (GSK_IO(read_side), &error);
  if (error)
    g_error ("error running gsk_io_shutdown: %s", error->message);
  gsk_main_loop_add_timer (gsk_main_loop_default(),
			   handle_main_loop_timeout, NULL, NULL,
			   1500, -1);
  while (!done)
    gsk_main_loop_run (gsk_main_loop_default (), -1, NULL);
  if (timed_out)
    g_printerr (" timed-out.\n");
  else
    g_printerr (" done.\n");

  return 0;
}
