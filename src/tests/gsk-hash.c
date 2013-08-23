#include "../hash/gskhash.h"
#include <stdio.h>
#include <string.h>

int main (int argc, char **argv)
{
  GskHash *hash = NULL;
  char buf[8192];
  int nread;
  if (argc != 2)
    g_error ("usage: %s [sha1|md5|crc32-le|crc32-be|sha256]", argv[0]);
  if (strcmp (argv[1], "sha1") == 0)
    hash = gsk_hash_new_sha1 ();
  else if (strcmp (argv[1], "md5") == 0)
    hash = gsk_hash_new_md5 ();
  else if (strcmp (argv[1], "sha256") == 0)
    hash = gsk_hash_new_sha256 ();
  else if (strcmp (argv[1], "crc32-le") == 0)
    hash = gsk_hash_new_crc32 (FALSE);
  else if (strcmp (argv[1], "crc32-be") == 0)
    hash = gsk_hash_new_crc32 (TRUE);
  else
    g_error ("unknown hash %s", argv[1]);

  while ((nread=fread(buf, 1, sizeof(buf), stdin)) > 0)
    gsk_hash_feed (hash, buf, nread);
  gsk_hash_done (hash);

  g_assert (hash->size * 2 + 1 < sizeof (buf));
  gsk_hash_get_hex (hash, buf);
  g_print ("%s\n", buf);
  gsk_hash_destroy (hash);
  return 0;
}
