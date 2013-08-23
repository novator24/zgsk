#include "../gskstreamconcat.h"
#include "../gskmemory.h"
#include "../gskinit.h"
#include <string.h>

int main(int argc, char **argv)
{
  GskStream *out;
  char buf[10];
  guint nread;
  GError *error = NULL;

  gsk_init_without_threads (&argc, &argv);
  out = gsk_streams_concat_and_unref (gsk_memory_source_static_string ("hi"),
                                      gsk_memory_source_static_string ("mom"),
                                      NULL);
  nread = gsk_stream_read (out, buf, 10, &error);
  g_assert (nread == 5);
  g_assert (gsk_io_get_is_readable (out));
  g_assert (error == NULL);
  g_assert (memcmp (buf, "himom", 5) == 0);
  g_assert (gsk_stream_read (out, buf, 10, &error) == 0);
  g_assert (!gsk_io_get_is_readable (out));
  g_assert (error == NULL);
  g_object_unref (out);

  return 0;
}
