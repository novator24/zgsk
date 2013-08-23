#ifndef __GSK_TABLE_H_
#define __GSK_TABLE_H_

/* GskTable.
 *
 * A efficient on-disk table, meaning a mapping from an
 * uninterpreted piece of binary-data to a value.
 * When multiple values are added with the same
 * key, they are merged, using user-definable semantics.
 *
 * application notes:
 * - you may store data in the directory, but it the filename
 *   must begin with a capital letter.
 */
typedef struct _GskTableReader GskTableReader;

#include "gskmempool.h"

typedef struct
{
  guint len;
  guint8 *data;
  guint alloced;
} GskTableBuffer;

/* --- public table buffer api -- needed for defining merge functions --- */
/* calls realloc so beginning of buffer is preserved */
G_INLINE_FUNC guint8 *gsk_table_buffer_set_len (GskTableBuffer *buffer,
                                                guint           len);

/* returns pointer to new area in buffer */
G_INLINE_FUNC guint8 *gsk_table_buffer_append  (GskTableBuffer *buffer,
                                                guint           len);


typedef  int (*GskTableCompareFunc)      (guint         a_key_len,
                                          const guint8 *a_key_data,
                                          guint         b_key_len,
                                          const guint8 *b_key_data,
                                          gpointer      user_data);
typedef  int (*GskTableCompareFuncNoLen) (const guint8 *a_key_data,
                                          const guint8 *b_key_data,
                                          gpointer      user_data);
typedef enum
{
  GSK_TABLE_MERGE_RETURN_A,
  GSK_TABLE_MERGE_RETURN_B,
  GSK_TABLE_MERGE_SUCCESS,
  GSK_TABLE_MERGE_DROP,
} GskTableMergeResult;
typedef GskTableMergeResult (*GskTableMergeFunc) (guint         key_len,
                                                  const guint8 *key_data,
                                                  guint         a_len,
                                                  const guint8 *a_data,
                                                  guint         b_len,
                                                  const guint8 *b_data,
                                                  GskTableBuffer *output,
                                                  gpointer      user_data);
typedef GskTableMergeResult (*GskTableMergeFuncNoLen) (const guint8 *key_data,
                                                       const guint8 *a_data,
                                                       const guint8 *b_data,
                                                       GskTableBuffer *output,
                                                       gpointer      user_data);

/* used for merges that go back to the beginning of the indexer */
typedef enum
{
  GSK_TABLE_SIMPLIFY_IDENTITY,
  GSK_TABLE_SIMPLIFY_SUCCESS,
  GSK_TABLE_SIMPLIFY_DELETE
} GskTableSimplifyResult;
typedef GskTableSimplifyResult (*GskTableSimplifyFunc)(guint         key_len,
                                                       const guint8 *key_data,
                                                       guint         value_len,
                                                       const guint8 *value_data,
                                                       GskTableBuffer*val_out,
                                                       gpointer      user_data);
typedef GskTableSimplifyResult (*GskTableSimplifyFuncNoLen)
                                                      (const guint8 *key_data,
                                                       const guint8 *value_data,
                                                       GskTableBuffer*val_out,
                                                       gpointer      user_data);

/* only used for querying */
typedef gboolean          (*GskTableValueIsStableFunc)(guint         key_len,
                                                       const guint8 *key_data,
                                                       guint         value_len,
                                                       const guint8 *value_data,
                                                       gpointer      user_data);

typedef enum
{
  GSK_TABLE_JOURNAL_NONE,
  GSK_TABLE_JOURNAL_OCCASIONALLY,
  GSK_TABLE_JOURNAL_DEFAULT
} GskTableJournalMode;

typedef struct _GskTableOptions GskTableOptions;
struct _GskTableOptions
{
  /* compare configuration */
  GskTableCompareFunc compare;
  GskTableCompareFuncNoLen compare_no_len;

  /* merging configuration */
  GskTableMergeFunc merge;
  GskTableMergeFuncNoLen merge_no_len;

  /* final merging */
  GskTableSimplifyFunc simplify;
  GskTableSimplifyFuncNoLen simplify_no_len;

  /* query stability */
  GskTableValueIsStableFunc is_stable;

  /* user data */
  gpointer user_data;
  GDestroyNotify destroy_user_data;

  /* journalling mode */
  GskTableJournalMode journal_mode;

  /* tunables */
  gsize max_in_memory_entries;
  gsize max_in_memory_bytes;
};

GskTableOptions     *gsk_table_options_new    (void);
void                 gsk_table_options_destroy(GskTableOptions *options);

void gsk_table_options_set_replacement_semantics (GskTableOptions *options);

typedef enum
{
  GSK_TABLE_MAY_EXIST = (1<<0),
  GSK_TABLE_MAY_CREATE = (1<<1)
} GskTableNewFlags;

typedef struct _GskTable GskTable;

/* NOTE: hinting options will be ignored if the table already exists. */
GskTable *  gsk_table_new         (const char            *dir,
                                   const GskTableOptions *options,
                                   GskTableNewFlags       flags,
	          	           GError               **error);
gboolean    gsk_table_add         (GskTable              *table,
                                   guint                  key_len,
	          	           const guint8          *key_data,
                                   guint                  value_len,
	          	           const guint8          *value_data,
                                   GError               **error);
gboolean    gsk_table_query       (GskTable              *table,
                                   guint                  key_len,
			           const guint8          *key_data,
                                   gboolean              *found_value_out,
			           guint                 *value_len_out,
			           guint8               **value_data_out,
                                   GError               **error);
const char *gsk_table_peek_dir    (GskTable              *table);
void        gsk_table_destroy     (GskTable              *table);


struct _GskTableReader
{
  gboolean eof;
  GError *error;
  guint key_len;
  const guint8 *key_data;
  guint value_len;
  const guint8 *value_data;

  void     (*advance) (GskTableReader *reader);
  void     (*destroy) (GskTableReader *reader);
};
typedef enum
{
  GSK_TABLE_BOUND_NONE,
  GSK_TABLE_BOUND_STRICT,
  GSK_TABLE_BOUND_INCLUSIVE
} GskTableBoundType;
GskTableReader *gsk_table_make_reader_with_bounds (GskTable *table,
                                       GskTableBoundType start_bound_type,
                                       guint start_bound_len,
                                       const guint8 *start_bound_data,
                                       GskTableBoundType end_bound_type,
                                       guint end_bound_len,
                                       const guint8 *end_bound_data,
                                       GError  **error);
#define gsk_table_make_reader(table, error) \
  gsk_table_make_reader_with_bounds (table, \
                                     GSK_TABLE_BOUND_NONE, 0, NULL, \
                                     GSK_TABLE_BOUND_NONE, 0, NULL, \
                                     error)

G_INLINE_FUNC void     gsk_table_reader_advance (GskTableReader *reader);
G_INLINE_FUNC void     gsk_table_reader_destroy (GskTableReader *reader);





/* --- protected parts of the table-buffer api --- */
/* this stuff is used throughout the implementation:
   we also cavalierly access 'len', 'data' and 'alloced'
   and assume that they have the expected properties.
   we do not modify 'data' or 'alloced' and we always
   ensure that len < alloced. */

#define GSK_TABLE_BUFFER_INIT   { 0, NULL, 0 }
G_INLINE_FUNC void    gsk_table_buffer_init         (GskTableBuffer *buffer);

G_INLINE_FUNC void    gsk_table_buffer_clear        (GskTableBuffer *buffer);

G_INLINE_FUNC void    gsk_table_buffer_ensure_size  (GskTableBuffer *buffer,
                                                     guint           min_size);
G_INLINE_FUNC void    gsk_table_buffer_ensure_extra (GskTableBuffer *buffer,
                                                     guint           addl_size);


#if defined (G_CAN_INLINE) || defined (__GSK_DEFINE_INLINES__)
G_INLINE_FUNC void    gsk_table_buffer_init    (GskTableBuffer *buffer)
{
  buffer->len = 0;
  buffer->data = NULL;
  buffer->alloced = 0;
}

/* calls realloc so beginning of buffer is preserved */
G_INLINE_FUNC void
gsk_table_buffer_ensure_size (GskTableBuffer *buffer,
                              guint           min_size)
{
  if (G_UNLIKELY (buffer->alloced < min_size))
    {
      guint new_size = buffer->alloced ? buffer->alloced * 2 : 32;
      while (new_size < min_size)
        new_size += new_size;
      buffer->data = g_realloc (buffer->data, new_size);
      buffer->alloced = new_size;
    }
}

G_INLINE_FUNC guint8 *
gsk_table_buffer_set_len (GskTableBuffer *buffer,
                          guint           len)
{
  gsk_table_buffer_ensure_size (buffer, len);
  buffer->len = len;
  return buffer->data;
}

/* returns pointer to new area in buffer */
G_INLINE_FUNC guint8 *
gsk_table_buffer_append  (GskTableBuffer *buffer,
                          guint           len)
{
  guint old_len = buffer->len;
  return gsk_table_buffer_set_len (buffer, old_len + len) + old_len;
}

G_INLINE_FUNC void
gsk_table_buffer_clear   (GskTableBuffer *buffer)
{
  g_free (buffer->data);
}

G_INLINE_FUNC void
gsk_table_buffer_ensure_extra (GskTableBuffer *buffer,
                               guint           extra_bytes)
{
  gsk_table_buffer_ensure_size (buffer, buffer->len + extra_bytes);
}
G_INLINE_FUNC void
gsk_table_reader_advance (GskTableReader *reader)
{
  reader->advance (reader);
}
G_INLINE_FUNC void
gsk_table_reader_destroy (GskTableReader *reader)
{
  reader->destroy (reader);
}
#endif

#endif
