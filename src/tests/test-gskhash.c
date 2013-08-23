#include <../hash/gskhash.h>
#include <../gskghelpers.h>
#include <string.h>

typedef struct _HashSumTest HashSumTest;
struct _HashSumTest
{
  const char *str;
  char md5[16];
  char sha1[20];
  char sha256[32];
};

static HashSumTest tests[] =
{
  {
    "a",
    { 0x0c, 0xc1, 0x75, 0xb9, 0xc0, 0xf1, 0xb6, 0xa8,
      0x31, 0xc3, 0x99, 0xe2, 0x69, 0x77, 0x26, 0x61 },
    { 0x86, 0xF7, 0xE4, 0x37, 0xFA, 0xA5, 0xA7, 0xFC,
      0xE1, 0x5D, 0x1D, 0xDC, 0xB9, 0xEA, 0xEA, 0xEA,
      0x37, 0x76, 0x67, 0xB8 },
    { 0xca, 0x97, 0x81, 0x12, 0xca, 0x1b, 0xbd, 0xca,
      0xfa, 0xc2, 0x31, 0xb3, 0x9a, 0x23, 0xdc, 0x4d,
      0xa7, 0x86, 0xef, 0xf8, 0x14, 0x7c, 0x4e, 0x72,
      0xb9, 0x80, 0x77, 0x85, 0xaf, 0xee, 0x48, 0xbb }
  }
};
#define TEST_COUNT		G_N_ELEMENTS(tests)

int main ()
{
  GskHash *h;
  guint i;
  for (i = 0; i < TEST_COUNT; i++)
    {
      guint8 buf[32];

      h = gsk_hash_new_md5 ();
      gsk_hash_feed_str (h, tests[i].str);
      gsk_hash_done (h);
      gsk_hash_get (h, buf);
      g_assert (memcmp (buf, tests[i].md5, 16) == 0);
      gsk_hash_destroy (h);

      h = gsk_hash_new_sha1 ();
      gsk_hash_feed_str (h, tests[i].str);
      gsk_hash_done (h);
      gsk_hash_get (h, buf);
      g_assert (memcmp (buf, tests[i].sha1, 20) == 0);
      gsk_hash_destroy (h);

      h = gsk_hash_new_sha256 ();
      gsk_hash_feed_str (h, tests[i].str);
      gsk_hash_done (h);
      gsk_hash_get (h, buf);
      g_assert (memcmp (buf, tests[i].sha256, 32) == 0);
      gsk_hash_destroy (h);
    }
  return 0;
}

