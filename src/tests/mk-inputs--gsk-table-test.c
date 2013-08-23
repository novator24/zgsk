#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <glib.h>
#include "../hash/gskhash.h"
#include "../gskutils.h"

int main(int argc, char **argv)
{
  const char *filename = "/usr/share/dict/words";
  FILE *fp = fopen (filename, "r");
  char buf[512];
  if (fp == NULL)
    g_error ("error opening %s: %s", filename, g_strerror (errno));
  while (fgets (buf, sizeof (buf), fp))
    {
      char *hex1, hex[33];
      GskHash *md5;
      g_strstrip (buf);
      if (buf[0] == 0)
        hex1 = g_strdup(".");
      else
        hex1 = gsk_escape_memory_hex (buf, strlen (buf));
      md5 = gsk_hash_new_md5 ();
      gsk_hash_feed_str (md5, buf);
      gsk_hash_done (md5);
      gsk_hash_get_hex (md5, hex);
      gsk_hash_destroy (md5);
      g_print ("%s %s\n", hex1, hex);
      g_free (hex1);
    }
  fclose (fp);
  return 0;
}
