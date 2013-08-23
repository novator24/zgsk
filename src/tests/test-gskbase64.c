#include "../gskinit.h"
#include "../common/gskbase64.h"
#include <string.h>
#include <stdlib.h>

static void test_encode_decode (const char *data, guint len)
{
  char *encoded;
  guint encoded_len = GSK_BASE64_GET_ENCODED_LEN (len);
  char *out;
  GByteArray *array;
  encoded = g_new (char, encoded_len);
  out = g_new (char, len + 1);

  out[len] = (char) 129;

  gsk_base64_encode (encoded, data, len);
  {
    char *out2 = gsk_base64_encode_alloc (data, len);
    g_assert (strlen (out2) == encoded_len);
    g_assert (memcmp (out2, encoded, encoded_len) == 0);
    g_free (out2);
  }
  g_assert (gsk_base64_decode (out, len, encoded, -1) == len);
  g_assert (memcmp (data, out, len) == 0);
  g_assert (out[len] == (char) 129);
  array = gsk_base64_decode_alloc (encoded);
  g_assert (array->len == len);
  g_assert (memcmp (array->data, data, len) == 0);
  g_byte_array_free (array, TRUE);
  g_free (encoded);
  g_free (out);
}

int main(int argc, char** argv)
{
  char buf[256];
  guint i;
  gsk_init (&argc, &argv, NULL);
  test_encode_decode ("hello", 5);
  for (i = 0; i < sizeof(buf); i++)
    buf[i] = rand();
  test_encode_decode (buf, sizeof(buf));
  test_encode_decode (buf, sizeof(buf) / 2);
  test_encode_decode (buf, sizeof(buf) / 4);
  test_encode_decode (buf, 0);
  return 0;
}
