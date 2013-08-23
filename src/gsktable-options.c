#include "gsktable.h"


GskTableOptions     *gsk_table_options_new    (void)
{
  GskTableOptions *rv = g_new0 (GskTableOptions, 1);
  rv->max_in_memory_bytes = 1024*1024;
  rv->max_in_memory_entries = 2048;
  rv->journal_mode = GSK_TABLE_JOURNAL_DEFAULT;
  return rv;
}

void                 gsk_table_options_destroy(GskTableOptions *options)
{
  g_free (options);
}

static GskTableMergeResult
merge_return_b (guint         key_len,
                const guint8 *key_data,
                guint         a_len,
                const guint8 *a_data,
                guint         b_len,
                const guint8 *b_data,
                GskTableBuffer *output,
                gpointer      user_data)
{
  return GSK_TABLE_MERGE_RETURN_B;
}

static GskTableMergeResult
merge_no_len_return_b (const guint8 *key_data,
                       const guint8 *a_data,
                       const guint8 *b_data,
                       GskTableBuffer *output,
                       gpointer      user_data)
{
  return GSK_TABLE_MERGE_RETURN_B;
}

static gboolean
always_stable (guint         key_len,
               const guint8 *key_data,
               guint         value_len,
               const guint8 *value_data,
               gpointer      user_data)
{
  return TRUE;
}

void gsk_table_options_set_replacement_semantics (GskTableOptions *options)
{
  if (options->compare_no_len == NULL)
    options->merge = merge_return_b;
  else
    options->merge_no_len = merge_no_len_return_b;
  options->is_stable = always_stable;
}

