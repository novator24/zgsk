#include "../gskinit.h"
#include "../gskstreamexternal.h"
#include "../gskbufferstream.h"
#include <string.h>

int
main (int argc, char **argv)
{
  GskBufferStream *input_buffer;
  GskBufferStream *output_buffer;
  GskStream *external;
  const char *args[10];
  const char *desired_output;
  char tmp[1024];

  gsk_init_without_threads (&argc, &argv);

  /* filter some text through 'grep cat' */
  input_buffer = gsk_buffer_stream_new();
  output_buffer = gsk_buffer_stream_new();
  gsk_buffer_append_string (gsk_buffer_stream_peek_read_buffer (input_buffer),
                            "cat\n"
			    "dog\n"
			    "kitty-cat\n"
			    "elephant\n");
  gsk_buffer_stream_read_buffer_changed (input_buffer);
  gsk_buffer_stream_read_shutdown (input_buffer);
  args[0] = "grep";
  args[1] = "cat";
  args[2] = NULL;
  external = gsk_stream_external_new (GSK_STREAM_EXTERNAL_SEARCH_PATH,
				      NULL, NULL,	/* in/out redirect */
				      NULL, NULL, NULL, /* termination */
				      "grep", args,	/* args */
				      NULL,		/* environment */
				      NULL		/* error */
				     );
  gsk_stream_attach (GSK_STREAM (input_buffer), external, NULL);
  gsk_stream_attach (external, GSK_STREAM (output_buffer), NULL);
  desired_output = "cat\nkitty-cat\n";
  while (gsk_buffer_stream_peek_write_buffer (output_buffer)->size < strlen (desired_output))
    gsk_main_loop_run (gsk_main_loop_default (), -1, NULL);
  g_assert (gsk_buffer_stream_peek_write_buffer (output_buffer)->size == strlen (desired_output));
  gsk_buffer_read (gsk_buffer_stream_peek_write_buffer (output_buffer), tmp, sizeof (tmp));
  g_assert (memcmp (tmp, desired_output, strlen (desired_output)) == 0);
  gsk_buffer_stream_write_buffer_changed (output_buffer);
  
  return 0;
}
