#include "test-codegen-generated.h"
#include "../gskutils.h"

gboolean verbose = FALSE;
GskbContext *parsed_context;
GskbNamespace *parsed_namespace;                /* namespace 'test' */

static void
test_int (void)
{
  /* 'int' tests */
  struct {
    gint32 value;
    guint encoded_len;
    guint8 bytes[5];
  } tests[] = {
    { 0,          1, { 0x00,0x00,0x00,0x00,0x00 } },
    { 0x3f,       1, { 0x3f,0x00,0x00,0x00,0x00 } },
    { 0x40,       2, { 0xc0,0x00,0x00,0x00,0x00 } },
    { 0x1fff,     2, { 0xff,0x3f,0x00,0x00,0x00 } },
    { 0x2000,     3, { 0x80,0xc0,0x00,0x00,0x00 } },
    { 0xfffff,    3, { 0xff,0xff,0x3f,0x00,0x00 } },
    { 0x100000,   4, { 0x80,0x80,0xc0,0x00,0x00 } },
    { 0x7ffffff,  4, { 0xff,0xff,0xff,0x3f,0x00 } },
    { 0x8000000,  5, { 0x80,0x80,0x80,0xc0,0x00 } },
    { 0x7fffffff, 5, { 0xff,0xff,0xff,0xff,0x07 } },
    { -1,         1, { 0x7f,0x00,0x00,0x00,0x00 } },
    { -64,        1, { 0x40,0x00,0x00,0x00,0x00 } },  /* ...1111 1000000 */
    { -65,        2, { 0xbf,0x7f,0x00,0x00,0x00 } },  /* ...1111 0111111 */
    { -(1<<13),   2, { 0x80,0x40,0x00,0x00,0x00 } },
    { -(1<<13)-1, 3, { 0xff,0xbf,0x7f,0x00,0x00 } },
    { -(1<<20),   3, { 0x80,0x80,0x40,0x00,0x00 } },
    { -(1<<20)-1, 4, { 0xff,0xff,0xbf,0x7f,0x00 } },
    { -(1<<27),   4, { 0x80,0x80,0x80,0x40,0x00 } },
    { -(1<<27)-1, 5, { 0xff,0xff,0xff,0xbf,0x7f } },
    { G_MININT32, 5, { 0x80,0x80,0x80,0x80,0x78 } }
  };
  guint i;
  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      guint8 slab[5];
      gint32 unpacked;
      if (verbose)
        g_printerr ("[%d]", tests[i].value);
      g_assert (gskb_int_get_packed_size (tests[i].value) == tests[i].encoded_len);
      g_assert (gskb_int_pack_slab (tests[i].value, slab) == tests[i].encoded_len);
      g_assert (memcmp (slab, tests[i].bytes, tests[i].encoded_len) == 0);
      g_assert (gskb_int_validate_partial (6, slab, NULL) == tests[i].encoded_len);
      g_assert (gskb_int_unpack (slab, &unpacked) == tests[i].encoded_len);
      g_assert (unpacked == tests[i].value);
    }
}

static void
test_uint (void)
{
  /* 'uint' tests */
  struct {
    guint32 value;
    guint encoded_len;
    guint8 bytes[5];
  } tests[] = {
    { 0,          1, { 0x00,0x00,0x00,0x00,0x00 } },
    { 0x3f,       1, { 0x3f,0x00,0x00,0x00,0x00 } },
    { 0x40,       1, { 0x40,0x00,0x00,0x00,0x00 } },
    { 0x7f,       1, { 0x7f,0x00,0x00,0x00,0x00 } },
    { 0x80,       2, { 0x80,0x01,0x00,0x00,0x00 } },
    { 0x3fff,     2, { 0xff,0x7f,0x00,0x00,0x00 } },
    { 0x4000,     3, { 0x80,0x80,0x01,0x00,0x00 } },
    { 0x1fffff,   3, { 0xff,0xff,0x7f,0x00,0x00 } },
    { 0x200000,   4, { 0x80,0x80,0x80,0x01,0x00 } },
    { 0xfffffff,  4, { 0xff,0xff,0xff,0x7f,0x00 } },
    { 0x10000000, 5, { 0x80,0x80,0x80,0x80,0x01 } },
    { G_MAXUINT32,5, { 0xff,0xff,0xff,0xff,0x0f } }
  };
  guint i;
  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      guint8 slab[5];
      guint32 unpacked;
      if (verbose)
        g_printerr ("[%u]", tests[i].value);
      g_assert (gskb_uint_get_packed_size (tests[i].value) == tests[i].encoded_len);
      g_assert (gskb_uint_pack_slab (tests[i].value, slab) == tests[i].encoded_len);
      g_assert (memcmp (slab, tests[i].bytes, tests[i].encoded_len) == 0);
      g_assert (gskb_uint_validate_partial (6, slab, NULL) == tests[i].encoded_len);
      g_assert (gskb_uint_unpack (slab, &unpacked) == tests[i].encoded_len);
      g_assert (unpacked == tests[i].value);
    }
}

static void
test_long (void)
{
  /* 'long' tests */
  struct {
    gint64 value;
    guint encoded_len;
    guint8 bytes[10];
  } tests[] = {
    { 0,              1, { 0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x3f,           1, { 0x3f,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x40,           2, { 0xc0,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x1fff,         2, { 0xff,0x3f,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x2000,         3, { 0x80,0xc0,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0xfffff,        3, { 0xff,0xff,0x3f,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x100000,       4, { 0x80,0x80,0xc0,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x7ffffff,      4, { 0xff,0xff,0xff,0x3f,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x8000000,      5, { 0x80,0x80,0x80,0xc0,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x7fffffff,     5, { 0xff,0xff,0xff,0xff,0x07, 0x00,0x00,0x00,0x00,0x00 } },
    { -1,             1, { 0x7f,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { -64,            1, { 0x40,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },  /* ...1111 1000000 */
    { -65,            2, { 0xbf,0x7f,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },  /* ...1111 0111111 */
    { -(1<<13),       2, { 0x80,0x40,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { -(1<<13)-1,     3, { 0xff,0xbf,0x7f,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { -(1<<20),       3, { 0x80,0x80,0x40,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { -(1<<20)-1,     4, { 0xff,0xff,0xbf,0x7f,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { -(1<<27),       4, { 0x80,0x80,0x80,0x40,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { -(1<<27)-1,     5, { 0xff,0xff,0xff,0xbf,0x7f, 0x00,0x00,0x00,0x00,0x00 } },
    { G_MININT32,     5, { 0x80,0x80,0x80,0x80,0x78, 0x00,0x00,0x00,0x00,0x00 } },
    { -(1LL<<34),     5, { 0x80,0x80,0x80,0x80,0x40, 0x00,0x00,0x00,0x00,0x00 } },
    { -(1LL<<34)-1,   6, { 0xff,0xff,0xff,0xff,0xbf, 0x7f,0x00,0x00,0x00,0x00 } },
    { -(1LL<<41),     6, { 0x80,0x80,0x80,0x80,0x80, 0x40,0x00,0x00,0x00,0x00 } },
    { -(1LL<<41)-1,   7, { 0xff,0xff,0xff,0xff,0xff, 0xbf,0x7f,0x00,0x00,0x00 } },
    { -(1LL<<48),     7, { 0x80,0x80,0x80,0x80,0x80, 0x80,0x40,0x00,0x00,0x00 } },
    { -(1LL<<48)-1,   8, { 0xff,0xff,0xff,0xff,0xff, 0xff,0xbf,0x7f,0x00,0x00 } },
    { -(1LL<<55),     8, { 0x80,0x80,0x80,0x80,0x80, 0x80,0x80,0x40,0x00,0x00 } },
    { -(1LL<<55)-1,   9, { 0xff,0xff,0xff,0xff,0xff, 0xff,0xff,0xbf,0x7f,0x00 } },
  };
  guint i;
  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      guint8 slab[10];
      gint64 unpacked;
      if (verbose)
        g_printerr ("[%"G_GINT64_FORMAT"]", tests[i].value);
      g_assert (gskb_long_get_packed_size (tests[i].value) == tests[i].encoded_len);
      g_assert (gskb_long_pack_slab (tests[i].value, slab) == tests[i].encoded_len);
      g_assert (memcmp (slab, tests[i].bytes, tests[i].encoded_len) == 0);
      g_assert (gskb_long_validate_partial (11, slab, NULL) == tests[i].encoded_len);
      g_assert (gskb_long_unpack (slab, &unpacked) == tests[i].encoded_len);
      g_assert (unpacked == tests[i].value);
    }
}

static void
test_ulong (void)
{
  /* 'ulong' tests */
  struct {
    guint64 value;
    guint encoded_len;
    guint8 bytes[10];
  } tests[] = {
    { 0,                   1, { 0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x3f,                1, { 0x3f,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x40,                1, { 0x40,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x7f,                1, { 0x7f,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x80,                2, { 0x80,0x01,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x3fff,              2, { 0xff,0x7f,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x4000,              3, { 0x80,0x80,0x01,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x1fffff,            3, { 0xff,0xff,0x7f,0x00,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x200000,            4, { 0x80,0x80,0x80,0x01,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0xfffffff,           4, { 0xff,0xff,0xff,0x7f,0x00, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x10000000,          5, { 0x80,0x80,0x80,0x80,0x01, 0x00,0x00,0x00,0x00,0x00 } },
    { G_MAXUINT32,         5, { 0xff,0xff,0xff,0xff,0x0f, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x7ffffffffULL,      5, { 0xff,0xff,0xff,0xff,0x7f, 0x00,0x00,0x00,0x00,0x00 } },
    { 0x800000000ULL,      6, { 0x80,0x80,0x80,0x80,0x80, 0x01,0x00,0x00,0x00,0x00 } },
    { 0x3ffffffffffULL,    6, { 0xff,0xff,0xff,0xff,0xff, 0x7f,0x00,0x00,0x00,0x00 } },
    { 0x40000000000ULL,    7, { 0x80,0x80,0x80,0x80,0x80, 0x80,0x01,0x00,0x00,0x00 } },
    { 0x1ffffffffffffULL,  7, { 0xff,0xff,0xff,0xff,0xff, 0xff,0x7f,0x00,0x00,0x00 } },
    { 0x2000000000000ULL,  8, { 0x80,0x80,0x80,0x80,0x80, 0x80,0x80,0x01,0x00,0x00 } },
    { 0xffffffffffffffULL, 8, { 0xff,0xff,0xff,0xff,0xff, 0xff,0xff,0x7f,0x00,0x00 } },
    { 0x100000000000000ULL,9, { 0x80,0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x01,0x00 } },
  };
  guint i;
  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      guint8 slab[10];
      guint64 unpacked;
      if (verbose)
        g_printerr ("[%"G_GUINT64_FORMAT"]", tests[i].value);
      g_assert (gskb_ulong_get_packed_size (tests[i].value) == tests[i].encoded_len);
      g_assert (gskb_ulong_pack_slab (tests[i].value, slab) == tests[i].encoded_len);
      g_assert (memcmp (slab, tests[i].bytes, tests[i].encoded_len) == 0);
      g_assert (gskb_ulong_validate_partial (11, slab, NULL) == tests[i].encoded_len);
      g_assert (gskb_ulong_unpack (slab, &unpacked) == tests[i].encoded_len);
      g_assert (unpacked == tests[i].value);
    }
}

static void
test_string (void)
{
  guint8 slab[64];
  char *str;
  g_assert (gskb_string_pack_slab ("hi mom", slab) == 7);
  g_assert (strcmp ((char*)slab, "hi mom") == 0);
  g_assert (gskb_string_unpack_mempool (slab, &str, NULL) == 7);
  g_assert (str == (char*)slab);
  g_assert (strcmp (str, "hi mom") == 0);
  g_assert (gskb_string_unpack (slab, &str) == 7);
  g_assert (strcmp (str, "hi mom") == 0);
  gskb_string_destruct (&str);
}

static void test_fixed_length_struct (void)
{
        /*struct Foo
        {
          int8 a;
          int16 b;
          int32 c;
          int64 d;
          uint8 e;
          uint16 f;
          uint32 g;
          uint64 h;
          bit i;
        };*/
  static struct {
    Test_Foo foo;
    guint8 packed[31];
  } tests[] = {
    { {1,42,17,100,3,9,1,3,1},
      {1, 42,0, 17,0,0,0, 100,0,0,0,0,0,0,0,
       3, 9,0, 1,0,0,0, 3,0,0,0,0,0,0,0, 1} }
  };
  guint i;
  g_assert (test_foo_format->any.fixed_length == 31);
  g_assert (test_foo_get_packed_size () == 31);
  g_assert (!test_foo_format->any.requires_destruct);

  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      guint8 slab[31];
      Test_Foo eg;
      g_assert (test_foo_pack_slab (&tests[i].foo, slab) == 31);
      g_assert (memcmp (tests[i].packed, slab, 31) == 0);
      g_assert (test_foo_unpack (slab, &eg) == 31);
      g_assert (eg.a == tests[i].foo.a);
      g_assert (eg.b == tests[i].foo.b);
      g_assert (eg.c == tests[i].foo.c);
      g_assert (eg.d == tests[i].foo.d);
      g_assert (eg.e == tests[i].foo.e);
      g_assert (eg.f == tests[i].foo.f);
      g_assert (eg.g == tests[i].foo.g);
      g_assert (eg.h == tests[i].foo.h);
      g_assert (eg.i == tests[i].foo.i);
    }
}

static void
byte_array_append (guint len,
                   const guint8 *data,
                   gpointer append_data)
{
  g_byte_array_append (append_data, data, len);
}

static void
test_extensible_struct (void)
{
  Test_Ioo ioo, ioo_copy;
  Test_IooExt1 ioo1;
  //Test_IooExt2 ioo2;
  GByteArray *ba = g_byte_array_new ();
  GByteArray *ba2 = g_byte_array_new ();
  GskbFormat *ioo_fmt = gskb_namespace_lookup_format (parsed_namespace, "Ioo");
  GskbFormat *ioo1_fmt = gskb_namespace_lookup_format (parsed_namespace, "IooExt1");
  GskbFormat *ioo2_fmt = gskb_namespace_lookup_format (parsed_namespace, "IooExt2");

  /* TODO: assertions about ioo*_fmt locals and test_ioo_*format globals. */

  g_assert (ioo_fmt != NULL);
  g_assert (ioo1_fmt != NULL);
  g_assert (ioo2_fmt != NULL);

  guint packed_size;
  guint8 *packed_slab;


  memset (&ioo, 0, sizeof (ioo));
  ioo.has.a = 1;       /*2+strlen(thirty)+1 + 2+1 + 1  = 9 + 3 + 1 = 13 */
  ioo.a = "thirty";
  ioo.has.b = 1;
  ioo.b = 24;
  packed_size = test_ioo_get_packed_size (&ioo);
  g_assert (packed_size == 13);
  packed_slab = g_malloc (packed_size);
  test_ioo_pack_slab (&ioo, packed_slab);
  test_ioo_pack (&ioo, byte_array_append, ba);
  g_assert (ba->len == packed_size);
  g_assert (memcmp (ba->data, packed_slab, ba->len) == 0);
  test_ioo_unpack (packed_slab, &ioo_copy);
  g_free (packed_slab);
  packed_slab = NULL;
  g_assert (ioo_copy.has.a);
  g_assert (strcmp (ioo_copy.a, "thirty") == 0);
  g_assert (ioo_copy.has.b);
  g_assert (ioo_copy.b == 24);
  test_ioo_destruct (&ioo_copy);
  memset (&ioo_copy, 0, sizeof (ioo_copy));
  test_ioo_ext1_unpack (ba->data, &ioo1);
  g_assert (ioo1.has.a);
  g_assert (strcmp (ioo1.a, "thirty") == 0);
  g_assert (ioo1.has.b);
  g_assert (ioo1.b == 24);
  ioo1.has.q = 1;
  ioo1.q = -11111;
  g_byte_array_set_size (ba, 0);
  test_ioo_ext1_pack (&ioo1, byte_array_append, ba);
  test_ioo_unpack (ba->data, &ioo_copy);
  test_ioo_pack (&ioo_copy, byte_array_append, ba2);
  g_assert (ba->len == ba2->len);
  g_assert (memcmp (ba->data, ba2->data, ba->len) == 0);
  test_ioo_ext1_destruct (&ioo1);
  memset (&ioo1, 0xff, sizeof (ioo1));     /* not necessary */
  test_ioo_ext1_unpack (ba->data, &ioo1);
  g_assert (ioo1.has.a);
  g_assert (strcmp (ioo1.a, "thirty") == 0);
  g_assert (ioo1.has.b);
  g_assert (ioo1.b == 24);
  g_assert (!ioo1.has.c);
  g_assert (ioo1.has.q);
  g_assert (ioo1.q == -11111);

  /* TODO: more shuffling around, say through Ext2 as well */
  /* TODO: cut-n-paste tests, using generic api and _fmt variables */

  g_free (packed_slab);
  g_byte_array_free (ba, TRUE);
  g_byte_array_free (ba2, TRUE);
}


static struct {
  const char *test_name;
  GVoidFunc test;
} tests[] = {
  { "int pack/unpack", test_int },
  { "uint pack/unpack", test_uint },
  { "long pack/unpack", test_long },
  { "ulong pack/unpack", test_ulong },
  { "string pack/unpack", test_string },
  { "fixed-length integer struct", test_fixed_length_struct },
  { "extensible structs", test_extensible_struct },
};


static GOptionEntry op_entries[] = {
  { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
    "print extra stuff", NULL },
  { NULL, 0, 0, 0, NULL, NULL, NULL }
};

int main(int argc, char **argv)
{
  guint test;
  GOptionContext *op_context;
  GError *error = NULL;

  op_context = g_option_context_new (NULL);
  g_option_context_set_summary (op_context, "gskb unit test");
  g_option_context_add_main_entries (op_context, op_entries, NULL);
  if (!g_option_context_parse (op_context, &argc, &argv, &error))
    gsk_fatal_user_error ("error parsing command-line options: %s",
                          error->message);
  g_option_context_free (op_context);

  parsed_context = gskb_context_new ();
  if (!gskb_context_parse_file (parsed_context,
                                "test-codegen.formats",
                                &error))
    g_error ("error parsing context file: %s", error->message);
  parsed_namespace = gskb_context_find_namespace (parsed_context, "test");
  g_assert (parsed_namespace);

  g_assert (gskb_uint8_get_packed_size (42) == 1);
  g_assert (gskb_int8_get_packed_size (42) == 1);
  g_assert (gskb_uint16_get_packed_size (42) == 2);
  g_assert (gskb_int16_get_packed_size (42) == 2);
  g_assert (gskb_uint32_get_packed_size (42) == 4);
  g_assert (gskb_int32_get_packed_size (42) == 4);
  g_assert (gskb_uint64_get_packed_size (42) == 8);
  g_assert (gskb_int64_get_packed_size (42) == 8);

  for (test = 0; test < G_N_ELEMENTS (tests); test++)
    {
      g_printerr ("test %s: ", tests[test].test_name);
      (*tests[test].test) ();
      g_printerr (" ok.\n");
    }

  return 0;
}
