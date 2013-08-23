#include "../gskbufferstream.h"
#include "../gskstreamfd.h"
#include "../gskinit.h"
#include <string.h>

int main (int argc, char **argv)
{
  GskStream *read_end;
  GskStream *write_end;
  GskBufferStream *memory_input;
  GskBufferStream *memory_output;
  GError *error = NULL;
  GskMainLoop *loop;
  GskBuffer *output_buffer;
  char buf[6];

  gsk_init_without_threads (&argc, &argv);
  loop = gsk_main_loop_default ();

  memory_input = gsk_buffer_stream_new ();
  memory_output = gsk_buffer_stream_new ();
  if (!gsk_stream_fd_pipe (&read_end, &write_end, &error))
    g_error("error creating pipe: %s", error->message);
  gsk_stream_attach (GSK_STREAM (memory_input), write_end, &error);
  g_assert (error == NULL);
  gsk_stream_attach (read_end, GSK_STREAM (memory_output), &error);
  g_assert (error == NULL);
  gsk_buffer_append_string (gsk_buffer_stream_peek_read_buffer (memory_input), "hi mom");
  gsk_buffer_stream_read_buffer_changed (memory_input);
  output_buffer = gsk_buffer_stream_peek_write_buffer (memory_output);

  while (output_buffer->size < 6)
    gsk_main_loop_run (loop, -1, NULL);
  g_assert (output_buffer->size == 6);
  gsk_buffer_read (output_buffer, buf, 6);
  g_assert (memcmp (buf, "hi mom", 6) == 0);
  return 0;
}
