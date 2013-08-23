/*
    GSKB - a batch processing framework

    gskb-str-table:  efficient, statically declarable string -> any mapping.

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

#ifndef __GSKB_STR_TABLE_H_
#define __GSKB_STR_TABLE_H_

typedef struct _GskbStrTable GskbStrTable;

#include "../gskbuffer.h"

struct _GskbStrTable
{
  /*< private >*/
  guint type;
  guint table_size;
  gpointer table_data;
  gsize sizeof_entry_data;
  gsize sizeof_entry;
  gsize entry_data_offset;
  gboolean is_global;
  gboolean is_ptr;
  char *str_slab;
  guint str_slab_size;
};

typedef struct _GskbStrTableEntry GskbStrTableEntry;
struct _GskbStrTableEntry
{
  const char *str;
  void *entry_data;
};

typedef void (*GskbStrTableEntryOutputFunc) (gconstpointer entry_data,
                                             GskBuffer    *dest);

/* if this returns NULL, it is because of duplicate entries */
GskbStrTable *gskb_str_table_new    (gsize         sizeof_entry_data,
                                     gsize         alignof_entry_data,
                                     guint         n_entries,
				     const GskbStrTableEntry *entry);
GskbStrTable *gskb_str_table_new_ptr(guint         n_entries,
				     const GskbStrTableEntry *entry);
void          gskb_str_table_print_compilable_deps
                                    (GskbStrTable *table,
				     const char   *table_name,
                                     const char   *entry_type_name,
                                     GskbStrTableEntryOutputFunc output_func,
				     GskBuffer    *output);
void          gskb_str_table_print_compilable_object
                                    (GskbStrTable *table,
				     const char   *table_name,
                                     const char   *sizeof_entry_data_str,
                                     const char   *alignof_entry_data_str,
				     GskBuffer    *output);
/* helper function which calls the above to functions to declare the table staticly. */
void          gskb_str_table_print  (GskbStrTable *table,
                                     gboolean      is_global,
				     const char   *table_name,
                                     const char   *entry_type_name,
                                     GskbStrTableEntryOutputFunc output_func,
                                     const char   *sizeof_entry_data_str,
                                     const char   *alignof_entry_data_str,
				     GskBuffer    *output);
const void   *gskb_str_table_lookup (GskbStrTable *table,
                                     const char   *str);
void          gskb_str_table_free   (GskbStrTable *table);


#endif
