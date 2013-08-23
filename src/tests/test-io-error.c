#include "../gskinit.h"
#include "../gskio.h"
#include "../gskbufferstream.h"
#include <string.h>

static guint error_count = 0;
static void
increment_error_counter (void)
{
  error_count++;
}

int main (int argc, char **argv)
{
  GError *error = NULL;
  GskBufferStream *stream;
  gsk_init (&argc, &argv, NULL);

  stream = gsk_buffer_stream_new ();
  g_signal_connect (stream, "on-error", increment_error_counter, NULL);
  g_assert (gsk_io_get_is_readable (stream));
  g_assert (gsk_io_get_is_writable (stream));
  error_count = 0;
  gsk_io_set_error (GSK_IO (stream), GSK_IO_ERROR_WRITE,
		    GSK_ERROR_BAD_FORMAT,
		    "hello, this is an error, %d", 16);
  g_assert (error_count == 1);
  error = GSK_IO (stream)->error;
  g_assert (error != NULL);
  g_assert (strstr (error->message, "error, 16") != NULL);
  g_assert (GSK_IO (stream)->error_cause == GSK_IO_ERROR_WRITE);
  g_assert (!gsk_io_get_is_readable (stream));
  g_assert (!gsk_io_get_is_writable (stream));
  g_object_unref (stream);

  stream = gsk_buffer_stream_new ();
  gsk_io_clear_shutdown_on_error (stream);
  g_signal_connect (stream, "on-error", increment_error_counter, NULL);
  g_assert (gsk_io_get_is_readable (stream));
  g_assert (gsk_io_get_is_writable (stream));
  error_count = 0;
  error = g_error_new (GSK_G_ERROR_DOMAIN,
		       GSK_ERROR_BAD_FORMAT,
		       "whatever");
  gsk_io_set_gerror (GSK_IO (stream), GSK_IO_ERROR_WRITE, error);
  g_assert (error_count == 1);
  g_assert (GSK_IO (stream)->error == error);
  g_assert (strcmp (error->message, "whatever") == 0);
  g_assert (GSK_IO (stream)->error_cause == GSK_IO_ERROR_WRITE);
  g_assert (gsk_io_get_is_readable (stream));
  g_assert (gsk_io_get_is_writable (stream));
  g_object_unref (stream);

  return 0;
}
