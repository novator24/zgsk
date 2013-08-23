/*
    GSKB - a batch processing framework

    gskb-uint-table-internals:  our uint -> any mapping.

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

#ifndef __GSKB_UINT_TABLE__H_
#define __GSKB_UINT_TABLE__H_

#include "../gskbuffer.h"

typedef void (*GskbUIntTableEntryOutputFunc) (gconstpointer entry_data,
                                              GskBuffer    *dest);

typedef struct _GskbUIntTable GskbUIntTable;
struct _GskbUIntTable
{
  guint type;
  guint table_size;
  void *table_data;
  guint n_entry_data;
  void *entry_data;
  gsize sizeof_entry_data;
  guint32 max_value;            /* must be less than G_MAXUINT32 */
  gboolean is_global;
};

typedef struct _GskbUIntTableEntry GskbUIntTableEntry;
struct _GskbUIntTableEntry
{
  guint32 value;
  void *entry_data;
};

GskbUIntTable *gskb_uint_table_new   (gsize          sizeof_entry_data,
                                      gsize          alignof_entry_data,
                                      guint          n_entries,
				      const GskbUIntTableEntry *entry);
void           gskb_uint_table_print_compilable_deps
                                     (GskbUIntTable *table,
	         		      const char    *table_name,
                                      const char    *type_name,
                                      GskbUIntTableEntryOutputFunc output_func,
	         		      GskBuffer     *output);
void           gskb_uint_table_print_compilable_object
                                     (GskbUIntTable *table,
	         		      const char    *table_name,
                                      const char    *sizeof_entry_data_str,
                                      const char    *alignof_entry_data_str,
	         		      GskBuffer     *output);
void           gskb_uint_table_print (GskbUIntTable *table,
                                      gboolean       is_global,
	         		      const char    *table_name,
                                      const char    *type_name,
                                      GskbUIntTableEntryOutputFunc output_func,
                                      const char    *sizeof_entry_data_str,
                                      const char    *alignof_entry_data_str,
	         		      GskBuffer     *output);
const void   * gskb_uint_table_lookup(GskbUIntTable *table,
                                      guint32        value);
void           gskb_uint_table_free  (GskbUIntTable *table);


#endif
