#include "../gskutils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage ()
{
  g_printerr ("usage: gsk-escape MODE\n\n"
              "Escape or unescape standard-input to standard-output.\n"
	      "MODE may be one of:\n"
	      "   escape         C quote\n"
	      "   unescape       Un-C quote\n"
	      "   escape-hex     C quote\n"
	      "   unescape-hex   Un-C quote\n"
	      );
  exit (1);
}

int main(int argc, char **argv)
{
  GByteArray *array;
  guint8 buf[2048];
  gsize nread;
  if (argc != 2 || strcmp (argv[1], "--help") == 0)
    usage ();
  array = g_byte_array_new ();

  while ((nread=fread (buf, 1, sizeof (buf), stdin)) != 0)
    {
      g_byte_array_append (array, buf, nread);
    }
  if (strcmp (argv[1], "escape") == 0)
    {
      char *out = gsk_escape_memory (array->data, array->len);
      fputs (out, stdout);
      fputc ('\n', stdout);
      g_free (out);
    }
  else if (strcmp (argv[1], "unescape") == 0)
    {
      guint8 *out;
      guint len;
      guint8 nul = 0;
      GError *error = NULL;
      if (array->len > 0 && array->data[array->len - 1] == '\n')
        g_byte_array_set_size (array, array->len - 1);
      g_byte_array_append (array, &nul, 1);
      out = gsk_unescape_memory ((char *) array->data, FALSE, NULL, &len, &error);
      if (out == NULL)
        g_error ("error unescaping: %s", error->message);
      fwrite (out, len, 1, stdout);
      g_free (out);
    }
  else if (strcmp (argv[1], "escape-hex") == 0)
    {
      char *out = gsk_escape_memory_hex (array->data, array->len);
      fputs (out, stdout);
      fputc ('\n', stdout);
      g_free (out);
    }
  else if (strcmp (argv[1], "unescape-hex") == 0)
    {
      guint8 *out;
      guint len;
      GError *error = NULL;
      if (array->len > 0 && array->data[array->len - 1] == '\n')
        g_byte_array_set_size (array, array->len - 1);
      out = gsk_unescape_memory_hex ((char*)array->data, array->len, &len, &error);
      if (out == NULL)
        g_error ("error unescaping: %s", error->message);
      fwrite (out, len, 1, stdout);
      g_free (out);
    }
  else
    g_error ("unknown mode %s: try --help", argv[1]);
  return 0;
}
