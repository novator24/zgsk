/*
    GSKB - a batch processing framework

    gskb-uint-table-internals:  implementation of our uint -> any mapping.

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

#include <string.h>
#include "../gskqsortmacro.h"
#include "gskb-uint-table.h"
#include "gskb-uint-table-internals.h"

#define COMPARE_UINT(a,b, rv)  rv = ((a<b) ? -1 : (a>b) ? 1 : 0)

static gpointer
make_entry_data (guint sizeof_entry_data,
                 guint n_entries,
                 const GskbUIntTableEntry *entries)
{
  char *rv = g_malloc (sizeof_entry_data * n_entries);
  guint i;
  for (i = 0; i < n_entries; i++)
    memcpy (rv + sizeof_entry_data * i,
            entries[i].entry_data, sizeof_entry_data);
  return rv;
}

GskbUIntTable *
gskb_uint_table_new   (gsize     sizeof_entry_data,
                       gsize     alignof_entry_data,
                       guint     n_entries,
                       const GskbUIntTableEntry *entry)
{
  GskbUIntTableEntry *entries_sorted;
  GskbUIntTable *table = NULL;
  guint n_ranges;
  guint i;
  if (n_entries == 0)
    {
      /* special 0 length table */
      table = g_slice_new0 (GskbUIntTable);
      table->type = GSKB_UINT_TABLE_EMPTY;
      return table;
    }
  entries_sorted = g_memdup (entry, n_entries * sizeof (GskbUIntTableEntry));
#define COMPARE_UINT_TABLE_ENTRY(a,b,rv) COMPARE_UINT(a.value,b.value, rv)
  GSK_QSORT (entries_sorted, GskbUIntTableEntry, n_entries,
             COMPARE_UINT_TABLE_ENTRY);
#undef COMPARE_UINT_TABLE_ENTRY
  n_ranges = 1;
  for (i = 0; i + 1 < n_entries; i++)
    {
      if (entries_sorted[i].value == entries_sorted[i+1].value)
        {
          g_free (entries_sorted);
          return NULL;
        }
      if (entries_sorted[i].value + 1 != entries_sorted[i+1].value)
        n_ranges++;
    }
  if (n_ranges == 1 && entries_sorted[i].value == 0)
    {
      /* direct map */
      table = g_slice_new (GskbUIntTable);
      table->type = GSKB_UINT_TABLE_DIRECT;
      table->table_size = n_entries;
      table->table_data = NULL;
      table->n_entry_data = n_entries;
      table->entry_data = make_entry_data (sizeof_entry_data,
                                           n_entries, entries_sorted);
      table->sizeof_entry_data = sizeof_entry_data;
      table->max_value = entries_sorted[n_entries-1].value;
      table->is_global = FALSE;
    }
  else if (n_ranges <= n_entries / 4 + 1)
    {
      /* range-wise optimization */
      GskbUIntTableRange *ranges;
      guint range_index;
      table = g_slice_new (GskbUIntTable);
      table->type = GSKB_UINT_TABLE_RANGES;
      table->n_entry_data = n_entries;
      table->entry_data = make_entry_data (sizeof_entry_data,
                                           n_entries, entries_sorted);
      ranges = g_new (GskbUIntTableRange, n_ranges);
      ranges[0].start = entries_sorted[0].value;
      ranges[0].count = 0;
      ranges[0].entry_data_offset = 0;
      range_index = 0;
      for (i = 0; i < n_entries; i++)
        {
          if (ranges[range_index].start + ranges[range_index].count ==
              entries_sorted[i].value)
            {
              ranges[range_index].count += 1;
            }
          else
            {
              range_index++;
              ranges[range_index].start = entries_sorted[i].value;
              ranges[range_index].count = 1;
              ranges[range_index].entry_data_offset
                    = ranges[range_index-1].entry_data_offset
                    + ranges[range_index-1].count;
            }
        }
      table->sizeof_entry_data = sizeof_entry_data;
      table->table_data = ranges;
      table->table_size = n_ranges;
      table->max_value = entries_sorted[n_entries-1].value;
    }
#if 0                   /* might be useful for some apps somewhere? */
  else if (n_entries > 5 && entries_sorted[n_entries-1].value < G_MAXUINT32)
    {
      /* try a few direct hashes */
      ...
    }
#endif
  if (table == NULL)
    {
      /* fall back on bsearch-style */
      guint32 *values = g_new (guint32, n_entries);
      table = g_slice_new (GskbUIntTable);
      table->type = GSKB_UINT_TABLE_BSEARCH;
      table->table_size = n_entries;
      table->entry_data = make_entry_data (sizeof_entry_data,
                                           n_entries, entries_sorted);
      table->n_entry_data = n_entries;
      for (i = 0; i < n_entries; i++)
        values[i] = entries_sorted[i].value;
      table->table_data = (guint8 *) values;
      table->sizeof_entry_data = sizeof_entry_data;
      table->max_value = entries_sorted[n_entries-1].value;
      table->is_global = FALSE;
    }
  g_free (entries_sorted);

  return table;
}

void
gskb_uint_table_print_compilable_deps (GskbUIntTable *table,
	         		       const char    *table_name,
                                       const char    *type_name,
                                       GskbUIntTableEntryOutputFunc output_func,
	         		       GskBuffer     *output)
{
  guint i;
  if (table->entry_data != NULL)
    {
      gsk_buffer_printf (output,
                         "static %s %s__entry_data[%u] = {\n",
                         type_name, table_name, table->n_entry_data);
      for (i = 0; i < table->n_entry_data; i++)
        {
          gsk_buffer_append (output, "  ", 2);
          output_func ((char*)table->entry_data + table->sizeof_entry_data * i,
                       output);
          gsk_buffer_append (output, ",\n", 2);
        }
      gsk_buffer_append (output, "};\n", 3);
    }

  switch (table->type)
    {
    case GSKB_UINT_TABLE_EMPTY:
      gsk_buffer_printf (output, "#define %s__table_data NULL\n", table_name);
      break;
    case GSKB_UINT_TABLE_DIRECT:
      gsk_buffer_printf (output, "#define %s__table_data NULL\n", table_name);
      break;
    case GSKB_UINT_TABLE_BSEARCH:
      gsk_buffer_printf (output,
                         "static guint32 %s__table_data[] = {\n", table_name);
      for (i = 0; i < table->table_size; i++)
        {
          gsk_buffer_printf (output,
                             "  %uU,\n",
                             ((guint32*)table->table_data)[i]);
        }
      gsk_buffer_printf (output, "};\n");
      break;
    case GSKB_UINT_TABLE_RANGES:
      gsk_buffer_printf (output,
                         "static GskbUIntTableRange %s__table_data[] = {\n", table_name);
      for (i = 0; i < table->table_size; i++)
        {
          GskbUIntTableRange r = ((GskbUIntTableRange*)table->table_data)[i];
          gsk_buffer_printf (output, "  { %uU, %uU, %uU },\n",
                             r.start, r.count, r.entry_data_offset);
        }
      gsk_buffer_printf (output, "};\n");
    }
}

void           gskb_uint_table_print_compilable_object
                                     (GskbUIntTable *table,
	         		      const char   *table_name,
                                      const char   *sizeof_entry_data_str,
                                      const char   *alignof_entry_data_str,
	         		      GskBuffer    *output)
{
  const char *type_name = NULL;
  switch (table->type)
    {
    case GSKB_UINT_TABLE_EMPTY:
      type_name = "GSKB_UINT_TABLE_EMPTY";
      break;
    case GSKB_UINT_TABLE_DIRECT:
      type_name = "GSKB_UINT_TABLE_DIRECT";
      break;
    case GSKB_UINT_TABLE_BSEARCH:
      type_name = "GSKB_UINT_TABLE_BSEARCH";
      break;
    case GSKB_UINT_TABLE_RANGES:
      type_name = "GSKB_UINT_TABLE_RANGES";
      break;
    }

  gsk_buffer_printf (output,
                     "{\n"
                     "  %s,\n"
                     "  %u,\n"
                     "  %s__table_data,\n"
                     "  %u,\n"
                     "  %s__entry_data,\n"
                     "  %s,\n"
                     "  %u,\n"
                     "  TRUE   /* is_global */\n"
                     "}",
                     type_name,
                     table->table_size,
                     table_name,
                     table->n_entry_data,
                     table_name,
                     sizeof_entry_data_str,
                     table->max_value);
}

void           gskb_uint_table_print (GskbUIntTable *table,
                                      gboolean       is_global,
	         		      const char    *table_name,
                                      const char    *type_name,
                                      GskbUIntTableEntryOutputFunc output_func,
                                      const char    *sizeof_entry_data_str,
                                      const char    *alignof_entry_data_str,
	         		      GskBuffer     *output)
{
  gskb_uint_table_print_compilable_deps (table, table_name, type_name,
                                         output_func, output);
  gsk_buffer_printf (output, "static GskbUIntTable %s = ", table_name);
  gskb_uint_table_print_compilable_object (table, table_name,
                                           sizeof_entry_data_str,
                                           alignof_entry_data_str,
                                           output);
  gsk_buffer_printf (output, ";\n");
}

const void   * gskb_uint_table_lookup(GskbUIntTable *table,
                                      guint32       value)
{
  switch (table->type)
    {
    case GSKB_UINT_TABLE_EMPTY:
      return NULL;
    case GSKB_UINT_TABLE_DIRECT:
      if (value > table->max_value)
        return NULL;
      return (char*)table->entry_data + table->sizeof_entry_data * value;
    case GSKB_UINT_TABLE_BSEARCH:
      {
        guint start = 0, n = table->table_size;
        const guint32 *values = table->table_data;
        while (n > 1)
          {
            guint mid = start + n / 2;
            guint32 mid_value = values[mid];
            if (mid_value < value)
              {
                guint new_start = mid + 1;
                guint end = start + n;      /* new_end==old_end */
                n = end - new_start;
                start = new_start;
              }
            else if (mid_value > value)
              {
                guint new_end = mid;
                n = new_end - start;
              }
            else
              {
                return (char*)table->entry_data
                       + mid * table->sizeof_entry_data;
              }
          }
        if (n == 0)
          return NULL;
        if (values[start] != value)
          return NULL;
        return (char*)table->entry_data
               + start * table->sizeof_entry_data;
      }

    case GSKB_UINT_TABLE_RANGES:
      {
        guint start_range = 0, n_range = table->table_size;
        const GskbUIntTableRange *ranges = table->table_data;
        while (n_range > 1)
          {
            guint mid_range = start_range + n_range / 2;
            guint32 mid_value = ranges[mid_range].start;
            guint32 end_mid_value = mid_range + ranges[mid_range].count;
            if (end_mid_value < value)
              {
                guint new_start = mid_range + 1;
                guint end = start_range + n_range;  /* new_end==old_end */
                n_range = end - new_start;
                start_range = new_start;
              }
            else if (mid_value > value)
              {
                guint new_end = mid_range;
                n_range = new_end - start_range;
              }
            else
              {
                return (char*)table->entry_data
                       + table->sizeof_entry_data
                       * (value
                          - ranges[mid_range].start
                          + ranges[mid_range].entry_data_offset);
              }
          }
        if (n_range == 0)
          return NULL;
        if (value < ranges[start_range].start
         || value >= ranges[start_range].start + ranges[start_range].count)
          return NULL;
        return (char*)table->entry_data
               + table->sizeof_entry_data
               * (value
                  - ranges[start_range].start
                  + ranges[start_range].entry_data_offset);
      }
    }
  g_return_val_if_reached (NULL);
}

void
gskb_uint_table_free  (GskbUIntTable *table)
{
  g_return_if_fail (!table->is_global);
  g_free (table->entry_data);
  g_free (table->table_data);
  g_slice_free (GskbUIntTable, table);
}
