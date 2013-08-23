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

#include <string.h>
#include "gskb-str-table.h"
#include "gskb-str-table-internals.h"
#include "gskb-config.h"
#include "../gskqsortmacro.h"

typedef GskbStrTableHashEntry HashEntry;
typedef GskbStrTableBSearchEntry BSearchEntry;

#define ALIGNOF_BSEARCH_ENTRY GSKB_STR_TABLE_BSEARCH_ENTRY__ALIGNOF
#define ALIGNOF_HASH_ENTRY    GSKB_STR_TABLE_HASH_ENTRY__ALIGNOF

/* test if p is prime.  assume p odd. */
static gboolean
is_odd_number_prime (guint p)
{
  guint i = 3;
  guint i_squared = 9;
  while (i_squared < p)
    {
      if (p % i == 0)
        return FALSE;
      i_squared += 4 * i + 4;
      i += 2;
    }
  if (i_squared == p)
    return FALSE;
  return TRUE;
}

static guint
find_perfect_hash_prime (guint          min_size,
                         guint          max_size,
                         guint          n_values,
                         const guint32 *values)
{
  /* see if there is any hope for this hash-function */
  {
    guint32 *hashes = g_memdup (values, sizeof (guint32) * n_values);
    guint i;
#define COMPARE_UINT(a,b, rv)  rv = ((a<b) ? -1 : (a>b) ? 1 : 0)
    GSK_QSORT (hashes, guint32, n_values, COMPARE_UINT);
#undef COMPARE_UINT
    for (i = 0; i + 1 < n_values; i++)
      if (hashes[i] == hashes[i+1])
        {
          g_free (hashes);
          return 0;
        }
    g_free (hashes);
  }

  /* iterate over odd numbers between min_size and max_size */
  {
    guint p, i;
    guint pad_size = (max_size + 7) / 8;
    guint8 *pad;

    pad = g_malloc (pad_size);
    for (p = min_size - min_size%2 + 1; p < max_size; p += 2)
      if (is_odd_number_prime (p))
        {
          memset (pad, 0, pad_size);
          for (i = 0; i < n_values; i++)
            {
              guint bin = values[i] % p;
              guint8 bit = (1<<(bin%8));
              if (pad[bin/8] & bit)
                break;
              else
                pad[bin/8] |= bit;
            }
          if (i == n_values)
            {
              /* 'p' is a perfect prime for this hash-fct */
              g_free (pad);
              return p;
            }
        }
    g_free (pad);
  }

  return 0;
}

static void
align_data (gsize base_size,
            gsize base_align,
            gsize data_size,
            gsize data_align,
            gsize *entry_size_out,
            gsize *entry_offset_out)
{
  guint s = base_size;
  s = GSKB_ALIGN (s, data_align);
  *entry_offset_out = s;
  s += data_size;
  s = GSKB_ALIGN (s, base_align);
  *entry_size_out = s;
}

static GskbStrTable *
make_bsearch_table (gsize     sizeof_entry_data,
                    gsize     alignof_entry_data,
                    guint     n_entries,
                    const GskbStrTableEntry *entries)
{
  GskbStrTableEntry *entries_sorted
    = g_memdup (entries, sizeof(GskbStrTableEntry) * n_entries);
  guint8 *bs_entries;
  guint i;
  guint str_slab_size = 0;
  char *str_slab_at;
  GskbStrTable *table;
#define COMPARE_STR_TABLE_ENTRY(a,b,rv) rv = strcmp (a.str, b.str)
  GSK_QSORT (entries_sorted, GskbStrTableEntry, n_entries,
             COMPARE_STR_TABLE_ENTRY);
#undef COMPARE_STR_TABLE_ENTRY
  for (i = 0; i + 1 < n_entries; i++)
    if (strcmp (entries_sorted[i].str, entries_sorted[i+1].str) == 0)
      {
        g_free (entries_sorted);
        return NULL;
      }
  for (i = 0; i + 1 < n_entries; i++)
    str_slab_size += strlen (entries_sorted[i].str) + 1;

  table = g_slice_new (GskbStrTable);
  bs_entries = g_malloc ((sizeof(BSearchEntry) + sizeof_entry_data) * n_entries);
  table->type = GSKB_STR_TABLE_BSEARCH;
  table->table_size = n_entries;
  table->table_data = bs_entries;
  table->sizeof_entry_data = sizeof_entry_data;
  table->is_global = FALSE;
  table->is_ptr = FALSE;
  table->str_slab = g_malloc (str_slab_size);
  align_data (sizeof (BSearchEntry), ALIGNOF_BSEARCH_ENTRY,
              sizeof_entry_data, alignof_entry_data,
              &table->sizeof_entry, &table->entry_data_offset);
  str_slab_at = table->str_slab;
  for (i = 0; i < n_entries; i++)
    {
      BSearchEntry *entry = (BSearchEntry *) bs_entries;
      gpointer user_data = bs_entries + table->entry_data_offset;
      bs_entries += table->sizeof_entry;

      entry->str_slab_offset = str_slab_at - table->str_slab;
      str_slab_at = g_stpcpy (str_slab_at, entries_sorted[i].str) + 1;
      memcpy (user_data, entries_sorted[i].entry_data, sizeof_entry_data);
    }
  g_assert (str_slab_at == table->str_slab + str_slab_size);
  table->str_slab_size = str_slab_size;
  return table;
}

static GskbStrTable *
make_collision_free_hash_table (gsize     sizeof_entry_data,
                                gsize     alignof_entry_data,
                                guint     n_entries,
                                const GskbStrTableEntry *entries,
                                GskbStrTableType table_type,
                                guint size,
                                const guint32 *hashes)
{
  GskbStrTable *table;
  table = g_slice_new (GskbStrTable);
  guint ent_size;
  guint8 *ht_entries;
  guint i;
  guint str_slab_size, str_slab_offset;
  align_data (sizeof (HashEntry), ALIGNOF_HASH_ENTRY,
              sizeof_entry_data, alignof_entry_data,
              &table->sizeof_entry, &table->entry_data_offset);
  ent_size = table->sizeof_entry;
  ht_entries = g_malloc (ent_size * size);
  table->table_size = size;
  table->type = table_type;
  table->table_data = ht_entries;
  table->sizeof_entry_data = sizeof_entry_data;
  table->is_global = FALSE;
  table->is_ptr = FALSE;

  /* mark all entries unfilled. */
  for (i = 0; i < size; i++)
    {
      HashEntry *e;
      e = (HashEntry*)(ht_entries + ent_size * i);
      e->hash_code = 0;
      e->str_slab_offset = G_MAXUINT32;
      memset (e + 1, 0, sizeof_entry_data);
    }
  str_slab_size = 0;
  str_slab_offset = 0;
  if (table_type == GSKB_STR_TABLE_ABLZ)
    {
      for (i = 0; i < n_entries; i++)
        {
          guint len = strlen (entries[i].str);
          if (len < 3)
            str_slab_size += 1;
          else if (len < 65536)
            str_slab_size += 1 + (len - 3);
          else
            g_return_val_if_reached (NULL);
        }
      table->str_slab = g_malloc (str_slab_size);
      for (i = 0; i < n_entries; i++)
        {
          guint len = strlen (entries[i].str);
          guint index = hashes[i] % size;
          HashEntry *e;
          e = (HashEntry *) (ht_entries + ent_size * index);
          g_assert (e->str_slab_offset == G_MAXUINT32);
          e->str_slab_offset = str_slab_offset;
          e->hash_code = hashes[i];
          memcpy ((char*)e + table->entry_data_offset,
                  entries[i].entry_data, sizeof_entry_data);
          table->str_slab[str_slab_offset++] = len>>8;
          if (len > 3)
            {
              memcpy (table->str_slab + str_slab_offset,
                      entries[i].str + 2,
                      len - 3);
              str_slab_offset += len - 3;
            }
        }
    }
  else
    {
      for (i = 0; i < n_entries; i++)
        {
          guint len = strlen (entries[i].str);
          str_slab_size += len + 1;
        }
      table->str_slab = g_malloc (str_slab_size);
      for (i = 0; i < n_entries; i++)
        {
          guint len = strlen (entries[i].str);
          guint index = hashes[i] % size;
          HashEntry *e;
          e = (HashEntry *) (ht_entries + ent_size * index);
          g_assert (e->str_slab_offset == G_MAXUINT32);
          e->str_slab_offset = str_slab_offset;
          e->hash_code = hashes[i];
          memcpy ((char*)e + table->entry_data_offset,
                  entries[i].entry_data, sizeof_entry_data);
          memcpy (table->str_slab + str_slab_offset, entries[i].str,
                  len + 1);
          str_slab_offset += len + 1;
        }
    }
  g_assert (str_slab_offset == str_slab_size);
  table->str_slab_size = str_slab_size;
  return table;
}

static inline guint32
compute_hash_ablz (const char *str, guint *len_out)
{
  if (str[0] == 0)
    {
      *len_out = 0;
      return 0;
    }
  else
    {
      guint len = strlen (str);
      *len_out = len;
      return (guint)((guint8)str[0])
           + ((guint)((guint8)str[1])<<8)
           + (((guint)(len&0xff))<<16)
           + ((guint)((guint8)str[len-1])<<24);
    }
}
static inline guint32
compute_hash_5003_33 (const char *str)
{
  guint32 ph = 5003;
  const char *at;
  for (at = str; *at; at++)
    {
      ph += * (guint8 *) at;
      ph *= 33;
    }
  return ph;
}
GskbStrTable *gskb_str_table_new    (gsize     sizeof_entry_data,
                                     gsize     alignof_entry_data,
                                     guint     n_entries,
				     const GskbStrTableEntry *entries)
{
  guint32 *hashes_ablz;
  guint32 *hashes_5003_33;
  GskbStrTable *table = NULL;
  gboolean can_use_ablz = TRUE;
  guint i;
  guint min_size, max_size;
  guint size;

  if (n_entries > 1024)
    {
      /* should be rare, and there is unlikely to be
         a collision-free hash for such a large number of entries. */
      return make_bsearch_table (sizeof_entry_data, alignof_entry_data,
                                 n_entries, entries);
    }

  hashes_ablz = g_new (guint32, n_entries);
  hashes_5003_33 = g_new (guint32, n_entries);
  for (i = 0; i < n_entries; i++)
    {
      const char *s = entries[i].str;
      guint len;
      hashes_ablz[i] = compute_hash_ablz (s, &len);
      if (len >= (1<<16))
        can_use_ablz = FALSE;

      /* some random hash function */
      hashes_5003_33[i] = compute_hash_5003_33 (s);
    }

  min_size  = n_entries + 3;
  max_size = min_size + 450;

  if (can_use_ablz
   && (size=find_perfect_hash_prime (min_size, max_size, n_entries, hashes_ablz)) != 0)
    {
      table = make_collision_free_hash_table (sizeof_entry_data,
                                              alignof_entry_data,
                                              n_entries, entries,
                                              GSKB_STR_TABLE_ABLZ,
                                              size, hashes_ablz);
    }
  else if ((size=find_perfect_hash_prime (min_size, max_size, n_entries, hashes_5003_33)) != 0)
    {
      table = make_collision_free_hash_table (sizeof_entry_data,
                                              alignof_entry_data,
                                              n_entries, entries,
                                              GSKB_STR_TABLE_5003_33,
                                              size, hashes_5003_33);
    }
  else
    {
      table = make_bsearch_table (sizeof_entry_data, alignof_entry_data,
                                  n_entries, entries);
    }

  g_free (hashes_ablz);
  g_free (hashes_5003_33);

  return table;
}
GskbStrTable *gskb_str_table_new_ptr(guint     n_entries,
				     const GskbStrTableEntry *entries)
{
  GskbStrTableEntry *ptr_entries = g_new (GskbStrTableEntry, n_entries);;
  guint i;
  GskbStrTable *rv;
  for (i = 0; i < n_entries; i++)
    {
      if (entries[i].entry_data == NULL)
        g_warning ("NULL table in ptr table (at '%s')", entries[i].str);

      ptr_entries[i].str = entries[i].str;
      ptr_entries[i].entry_data = (gpointer) &entries[i].entry_data;
    }
  rv = gskb_str_table_new (sizeof (gpointer), GSKB_ALIGNOF_POINTER, n_entries, ptr_entries);
  g_free (ptr_entries);
  rv->is_ptr = TRUE;
  return rv;
}

void
gskb_str_table_print_compilable_deps(GskbStrTable *table,
				     const char   *table_name,
                                     const char   *entry_type_name,
                                     GskbStrTableEntryOutputFunc render_func,
				     GskBuffer    *output)
{
  /* emit string slab */
  guint bytes_per_line = 10;
  guint byte_at;
  g_return_if_fail (table != NULL);
  g_return_if_fail (table_name != NULL);
  g_return_if_fail (entry_type_name != NULL);
  g_return_if_fail (render_func != NULL);
  gsk_buffer_printf (output,
                     "static char %s__str_slab[%u] = \n",
                     table_name, table->str_slab_size);
  for (byte_at = 0; byte_at < table->str_slab_size; byte_at += bytes_per_line)
    {
      guint rem = table->str_slab_size - byte_at;
      guint b;
      if (rem > bytes_per_line)
        rem = bytes_per_line;
      gsk_buffer_printf (output, "  \"");
      for (b = 0; b < rem; b++)
        {
          guint8 byte = ((guint8*)table->str_slab)[byte_at+b];
          if (byte == '\\')
            gsk_buffer_append (output, "\\\\", 2);
          else if (byte == '"')
            gsk_buffer_append (output, "\\\"", 2);
          else if (32 <= byte && byte <= 126)
            gsk_buffer_append_char (output, byte);
          else
            gsk_buffer_printf (output, "\\%03o", byte);
        }
      gsk_buffer_printf (output, "\"   /* bytes %u..%u */\n",
                         byte_at, byte_at+rem-1);
    }
  gsk_buffer_append (output, "  ;\n", 4);

  if (table->type == GSKB_STR_TABLE_BSEARCH)
    {
      guint ent_size = sizeof(BSearchEntry) + table->sizeof_entry_data;
      BSearchEntry *at = (BSearchEntry*)(table->table_data);
      guint i;
      /* emit bsearch table data */
      gsk_buffer_printf (output,
                         "static struct {\n"
                         "  GskbStrTableBSearchEntry bsearch_entry;\n"
                         "  %s entry_value;\n"
                         "} %s__table_data[%u] = {\n",
                         entry_type_name, table_name, table->table_size);
      for (i = 0; i < table->table_size; i++)
        {
          gsk_buffer_printf (output,
                             "  { { %u }, ", at->str_slab_offset);
          render_func (at+1, output);
          gsk_buffer_printf (output, " },\n");
          at = (gpointer)((char*)at + ent_size);
        }
      gsk_buffer_printf (output, "};\n");
    }
  else
    {
      /* emit hash-table */
      guint ent_size = sizeof(HashEntry) + table->sizeof_entry_data;
      HashEntry *at = (HashEntry*)(table->table_data);
      guint i;
      gsk_buffer_printf (output,
                         "static struct {\n"
                         "  GskbStrTableHashEntry hash_entry;\n"
                         "  %s entry_value;\n"
                         "} %s__table_data[%u] = {\n",
                         entry_type_name, table_name, table->table_size);
      for (i = 0; i < table->table_size; i++)
        {
          gpointer value_ptr;
          gsk_buffer_printf (output, "  { { %uU, ", at->hash_code);
          if (at->str_slab_offset == G_MAXUINT32)
            gsk_buffer_append_string (output, "G_MAXUINT32");
          else
            gsk_buffer_printf (output, "%u", at->str_slab_offset);
          gsk_buffer_append_string (output, " }, ");
          value_ptr = ((char*)at + table->entry_data_offset);
          if (table->is_ptr)
            {
              value_ptr = * (gpointer *) value_ptr;
              if (value_ptr == NULL)
                gsk_buffer_append_string (output, "NULL");
              else
                render_func (value_ptr, output);
            }
          else
            render_func (value_ptr, output);
          gsk_buffer_append_string (output, " },\n");
          at = (gpointer)((char*)at + ent_size);
        }
      gsk_buffer_printf (output, "};\n");
    }
}

void
gskb_str_table_print_compilable_object
                                    (GskbStrTable *table,
				     const char   *table_name,
                                     const char   *sizeof_entry_data_str,
                                     const char   *alignof_entry_data_str,
				     GskBuffer    *output)
{
  const char *type_name;
  const char *entry_base_type;
  const char *entry_align = "GSKB_UINT32_ALIGN";
  switch (table->type)
    {
    case GSKB_STR_TABLE_BSEARCH:
      type_name = "GSKB_STR_TABLE_BSEARCH";
      entry_base_type = "GskbStrTableBSearchEntry";
      entry_align = "GSKB_STR_TABLE_BSEARCH_ENTRY__ALIGNOF";
      break;
    case GSKB_STR_TABLE_ABLZ:
      type_name = "GSKB_STR_TABLE_ABLZ";
      entry_base_type = "GskbStrTableHashEntry";
      entry_align = "GSKB_STR_TABLE_HASH_ENTRY__ALIGNOF";
      break;
    case GSKB_STR_TABLE_5003_33:
      type_name = "GSKB_STR_TABLE_5003_33";
      entry_base_type = "GskbStrTableHashEntry";
      entry_align = "GSKB_STR_TABLE_HASH_ENTRY__ALIGNOF";
      break;
    default:
      g_return_if_reached ();
    }
  gsk_buffer_printf (output,
                     "{\n"
                     "  %s,\n"
                     "  %u,\n"
                     "  %s__table_data,\n"
                     "  %s,\n"
                     "  GSKB_ALIGN(GSKB_ALIGN(sizeof(%s),%s)+%s, %s),\n"
                     "  GSKB_ALIGN(sizeof(%s),%s),\n"
                     "  TRUE,        /* is_global */\n"
                     "  %s,          /* is_ptr */\n"
                     "  %s__str_slab,\n"
                     "  %u\n"
                     "}",
                     type_name,
                     table->table_size,
                     table_name,
                     sizeof_entry_data_str,
                     entry_base_type, alignof_entry_data_str, sizeof_entry_data_str, entry_align,
                     entry_base_type, alignof_entry_data_str,
                     table->is_ptr ? "TRUE" : "FALSE",
                     table_name,
                     table->str_slab_size);
}

const void   *
gskb_str_table_lookup (GskbStrTable *table,
                       const char   *str)
{
  switch (table->type)
    {
    case GSKB_STR_TABLE_BSEARCH:
      {
        guint ent_size = table->sizeof_entry;
        guint start = 0, n = table->table_size;
        guint8 *td = table->table_data;
        BSearchEntry *e;
        while (n > 1)
          {
            guint mid = start + n / 2;
            int rv;
            e = (BSearchEntry*)(td + ent_size * mid);
            rv = strcmp (table->str_slab + e->str_slab_offset, str);
            if (rv < 0)
              n = n / 2;
            else if (rv > 0)
              {
                n = (start + n) - (mid + 1);
                start = mid + 1;
              }
            else
              {
                return (char*)e + table->entry_data_offset;
              }
          }
        if (n == 0)
          return NULL;
        e = (BSearchEntry*)(td + ent_size * start);
        if (strcmp (table->str_slab + e->str_slab_offset, str) == 0)
          {
            gpointer rv_ptr = (char*)e + table->entry_data_offset;
            if (table->is_ptr)
              rv_ptr = * (gpointer *) rv_ptr;
            return rv_ptr;
          }
        else
          return NULL;
      }
    case GSKB_STR_TABLE_ABLZ:
      {
        guint len;
        guint32 code = compute_hash_ablz (str, &len);
        guint32 index;
        guint ent_size = table->sizeof_entry;
        HashEntry *e;
        const guint8 *ss;
        if (len >= (1<<16))
          return NULL;
        index = code % table->table_size;
        e = (HashEntry*)(table->table_data + ent_size * index);
        if (e->hash_code != code)
          return NULL;
        if (e->str_slab_offset == G_MAXUINT32)
          return NULL;
        ss = (const guint8 *) table->str_slab + e->str_slab_offset;
        if ((len>>8) != *ss)
          return NULL;
        if (len > 3 && memcmp (ss + 1, str + 2, len - 3) != 0)
          return NULL;
        {
          gpointer rv_ptr = (char*)e + table->entry_data_offset;
          if (table->is_ptr)
            rv_ptr = * (gpointer *) rv_ptr;
          return rv_ptr;
        }
      }
    case GSKB_STR_TABLE_5003_33:
      {
        guint32 code = compute_hash_5003_33 (str);
        guint32 index;
        guint ent_size = table->sizeof_entry;
        HashEntry *e;
        index = code % table->table_size;
        e = (HashEntry *)(table->table_data + ent_size * index);
        if (e->str_slab_offset == G_MAXUINT32)
          return NULL;
        if (strcmp (table->str_slab + e->str_slab_offset, str) != 0)
          return NULL;
        {
          gpointer rv_ptr = (char*)e + table->entry_data_offset;
          if (table->is_ptr)
            rv_ptr = * (gpointer *) rv_ptr;
          return rv_ptr;
        }
      }
    default:
      g_return_val_if_reached (NULL);
    }
}
void          gskb_str_table_free   (GskbStrTable *table)
{
  g_assert (!table->is_global);
  g_free (table->str_slab);
  g_free (table->table_data);
  g_slice_free (GskbStrTable, table);
}

void
gskb_str_table_print(GskbStrTable *table,
                     gboolean      is_global,
                     const char   *table_name,
                     const char   *entry_type_name,
                     GskbStrTableEntryOutputFunc render_func,
                     const char   *sizeof_entry_str,
                     const char   *alignof_entry_str,
                     GskBuffer    *output)
{
  gskb_str_table_print_compilable_deps (table,
                                        table_name,
                                        entry_type_name,
                                        render_func,
                                        output);
  gsk_buffer_printf (output,
                     "%sGskbStrTable %s = ",
                     is_global ? "" : "static ", table_name);
  gskb_str_table_print_compilable_object (table,
                                          table_name,
                                          sizeof_entry_str, alignof_entry_str,
                                          output);
  gsk_buffer_printf (output, ";\n");
}

