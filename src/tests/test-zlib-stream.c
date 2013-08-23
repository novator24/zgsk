#include <string.h>
#include "../gsk.h"
#include "../zlib/gskzlibinflator.h"
#include "../zlib/gskzlibdeflator.h"


static gboolean use_gzip = FALSE;
static gboolean compress = FALSE;
static int flush_millis = -1;
static gint compress_level = -1;


static GOptionEntry entries[] =
{
  { "gzip", 'g', 0, G_OPTION_ARG_NONE, &use_gzip, "use gzip format (as opposed to deflate format)", NULL },
  { "compress", 'c', 0, G_OPTION_ARG_NONE, &compress, "compress (instead of decompressing)", NULL },
  { "flush-millis", 'f', 0, G_OPTION_ARG_INT, &flush_millis, "flush timeout", "MS" },
  { "compress-level", 0, 0, G_OPTION_ARG_INT, &compress_level, "compression level", NULL },
  { NULL, 0, 0, 0, NULL, NULL, NULL },
};

int
main (int argc, char **argv)
{
  GskStream *in, *zlib, *out;
  GOptionContext *context;
  GError *error = NULL;
  gsk_init_without_threads (&argc, &argv);

  context = g_option_context_new ("test-zlib-stream");
  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    g_error ("error parsing options: %s", error->message);

  in = gsk_stream_fd_new_auto (0);
  if (compress)
    zlib = gsk_zlib_deflator_new2 (compress_level,
                                   flush_millis,
                                   use_gzip);
  else
    zlib = gsk_zlib_inflator_new2 (use_gzip);
  out = gsk_stream_fd_new_auto (1);
  gsk_stream_attach (in, zlib, NULL);
  gsk_stream_attach (zlib, out, NULL);
  g_object_weak_ref (G_OBJECT (zlib), (GWeakNotify) gsk_main_quit, NULL);
  g_object_unref (zlib);
  return gsk_main_run ();
}
