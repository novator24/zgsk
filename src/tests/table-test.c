#include "../gsktable.h"
#include "../gsktable-file.h"

static void
test_file_basics (GskTableFileFactory *factory,
                  const char          *dir)
{
  GskTableFile *file;
  GskTableFileHints hints = GSK_TABLE_FILE_HINTS_DEFAULTS;

  file = gsk_table_file_factory_create_file (factory, dir, 123, &hints, &error);
  ...
  if (!gsk_table_file_factory_destroy_file (file, TRUE, &error))
    g_error ("error destroying file: %s", error->message);
}

int
main (int argc, char **argv)
{
  GskTableFileFactory *factory;

  factory = gsk_table_file_factory_new_btree ();
  test_file_basics (factory);
  gsk_table_file_factory_destroy (factory);

  return 0;
}
