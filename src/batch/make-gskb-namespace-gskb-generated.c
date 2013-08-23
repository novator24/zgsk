/*
    GSKB - a batch processing framework

    make-gskb-namespace-gskb-generated: make static code for the gskb namespace.

    Copyright (C) 2008 Dave Benson

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA

    Contact:
        daveb@ffem.org <Dave Benson>
*/

#include "gskb-str-table.h"
#include "gskb-str-table.c"             /* hack */
#include "../gskerrno.h"
#include <errno.h>

typedef struct _BuiltinFormatInfo BuiltinFormatInfo;
struct _BuiltinFormatInfo
{
  char *format_name;
  char *c_ptr_name;
};

static const char *builtin_format_info[] =
{
  "int8", "int16", "int32", "int64",
  "uint8", "uint16", "uint32", "uint64",
  "int", "uint", "long", "ulong",
  "bit",
  "string",
  "float32", "float64"
  };


static void output_format_name (gconstpointer entry_data, GskBuffer *output)
{
  gsk_buffer_printf (output, "gskb_%s_format", (const char*) entry_data);
}

int main ()
{
  guint n_entries = G_N_ELEMENTS (builtin_format_info);
  guint i;
  GskbStrTableEntry *entries = g_new (GskbStrTableEntry, n_entries);
  GskbStrTable *table;
  GskBuffer buffer = GSK_BUFFER_STATIC_INIT;
  for (i = 0; i < n_entries; i++)
    {
      entries[i].str = builtin_format_info[i];
      entries[i].entry_data = (gpointer) builtin_format_info[i];
    }
  table = gskb_str_table_new_ptr (n_entries, entries);
  g_assert (table != NULL);
  gsk_buffer_printf (&buffer, "#include \"gskb-str-table-internals.h\"\n");
  gskb_str_table_print (table,
                        FALSE,
                        "gskb_namespace_gskb__str_table",
                        "GskbFormat *",
                        output_format_name,
                        "sizeof(GskbFormat *)", "GSKB_ALIGNOF_POINTER",
                        &buffer);

  gsk_buffer_printf (&buffer, "static GskbFormat *gskb_namespace_gskb__formats[%u] =\n"
                     "{\n", n_entries);
  for (i = 0; i < n_entries; i++)
    gsk_buffer_printf (&buffer, "  gskb_%s_format,\n", builtin_format_info[i]);
  gsk_buffer_printf (&buffer, "};\n");


  while (buffer.size > 0)
    {
      if (gsk_buffer_writev (&buffer, 1) < 0)
        {
          if (!gsk_errno_is_ignorable (errno))
            g_error ("gsk_buffer_writev: %s", g_strerror (errno));
        }
    }
  return 0;
}
