/*
    GSKB - a batch processing framework

    make-parser-symbol-table:  generate the symbol table for the parser.

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
#include <errno.h>
#include "../gskerrno.h"


static const char *symbols[] = {
"int8",
"int16",
"int32",
"int64",
"uint8",
"uint16",
"uint32",
"uint64",
"int",
"long",
"uint",
"ulong",
"bit",
"float32",
"float64",
"string",
"extensible",
"struct",
"union",
"bitfields",
"enum",
"alias",
"namespace",
"void"
};

static void
render_index_as_parser_token (gconstpointer entry_data,
                              GskBuffer *output)
{
  guint32 index = * (const guint32 *) entry_data;
  char *uc = g_ascii_strup (symbols[index], -1);
  gsk_buffer_printf (output, "GSKB_TOKEN_TYPE_%s", uc);
  g_free (uc);
}

int main()
{
  guint32 *indices = g_new (guint32, G_N_ELEMENTS (symbols));
  GskbStrTableEntry *entries = g_new (GskbStrTableEntry, G_N_ELEMENTS (symbols));
  guint i;
  GskBuffer buffer = GSK_BUFFER_STATIC_INIT;
  GskbStrTable *table;
  for (i = 0; i < G_N_ELEMENTS (symbols); i++)
    {
      indices[i] = i;
      entries[i].str = symbols[i];
      entries[i].entry_data = indices + i;
    }
  table = gskb_str_table_new (4, 4, G_N_ELEMENTS (symbols), entries);
  g_assert (table != NULL);
  gskb_str_table_print (table, FALSE, "gskb_parser_symbol_table", "guint32",
                        render_index_as_parser_token,
                        "sizeof(guint32)", "GSKB_ALIGNOF_UINT32", &buffer);
  while (buffer.size > 0)
    {
      if (gsk_buffer_writev (&buffer, 1) < 0)
        {
          if (!gsk_errno_is_ignorable (errno))
            g_error ("gsk_buffer_writev: %s", g_strerror (errno));
        }
    }
  gskb_str_table_free (table);
  g_free (entries);
  g_free (indices);
  return 0;
}
