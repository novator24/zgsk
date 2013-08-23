/* Implementation of gsk_table_file_factory_new_btree() */

/* === NOTES === */

/* Nomenclature:
     - we _emit_ _messages_ to the _buffer_ for later processing;
       these messages allow all the tree-structuring logic to be
       included in the _leaf-level_ code.
       (The leaf-level is the level (height-1))
     - all other data is _written_ to either the _.btree_ or _.value_ file
 */

/* === DECLARATIONS === */
/* --- required headers --- */
#include "gsktable.h"
#include "gsktable-mmap.h"
#include "gsktable-file.h"

/* --- private structures and declarations --- */
typedef struct _BtreeFileData BtreeFileData;
typedef struct _BtreeFile BtreeFile;

struct _BtreeFactory
{
  GskTableFileFactory base_instance;
  guint branches_per_level;
  guint values_per_leaf;
};

struct _BtreeFileData
{
  guint key_len, value_len;
  guint8 *data;
  guint alloced;
};

struct _BtreeFile
{
  GskTableFile base_instance;
  const char *dir;
  guint64 id;

  guint height;
  guint cur_level;

  /* start of btree and value data for current level 
     within the whole file. */
  guint64 cur_level_start_offset;
  guint64 cur_level_start_value_offset;

  guint *level_indices;         /* only used during leaf level processing */

  /* size of the current level in bytes, for the .btree and .value files */
  guint64 level_btree_size;
  guint64 level_value_size;

  GskTableBuffer child_node_buffer;
  GskTableBuffer last_key;
  guint n_data_in_child_node_buffer;

  gboolean is_building;
  gboolean has_building_materials;
  union
  {
    struct {
      /* Output the finished tree.
         We will have to go back and setup some metadata. */
      GskTableMmapWriter writer;

      /* Commands are written here, and eventually read out again. */
      GskTableMmapBuffer buffer;

      /* we must maintain a little buffer while writing the leaf
         nodes, since we need to have some warning before the end-of-file */
      guint first_buffered_data;
      guint n_buffered_data;                /* max buffered is 'height' */
      BtreeFileData *buffered_data;
    } building;
    struct {
      GskTableMmapReader queryable;
    } readable;
  } info;
};

enum
{
  MSG__CHILD_NODE=42,   /* params: length as uint32le */
  MSG__BRANCH_VALUE,    /* params: level as byte, value as remainder of data */
  MSG__BRANCH_ENDED,    /* params: level as byte */
  MSG__LEVEL_ENDED,     /* params: none */
  MSG__DUMMY_BRANCH     /* params: level (always 0) */

} MsgType;


#define TEST_CHAIN_ERROR_FULL(call, CLEANUP, error_rv)          \
  G_STMT_START{                                                 \
    if (G_UNLIKELY (!call))                                     \
      {                                                         \
        { CLEANUP; }                                            \
        gsk_g_error_add_prefix (error, "function %s (%s:%u)",   \
                                G_STRFUNC, __FILE__, __LINE__); \
        return error_rv;                                        \
      }                                                         \
  }G_STMT_END
#define TEST_CHAIN_ERROR(call) \
        TEST_CHAIN_ERROR_RV(call, /*no cleanup*/, FALSE)
#define TEST_CHAIN_ERROR_NULL(call) \
        TEST_CHAIN_ERROR_RV(call, /*no cleanup*/, NULL)

/* === PRIVATE HELPER FUNCTIONS === */
static gboolean
emit_branch_value (BtreeFile *f,
                   guint      level,
                   BtreeFileData *d)
{
  /* re-emit this value */
  guint8 buf[9] = { level,
                    d->key_len,
                    d->key_len>>8,
                    d->key_len>>16,
                    d->key_len>>24,
                    d->value_len,
                    d->value_len>>8,
                    d->value_len>>16,
                    d->value_len>>24 };
  BtreeFileData bfd;
  TEST_CHAIN_ERROR (gsk_table_mmap_buffer_write (&f->buffer, 9, buf, error));
  TEST_CHAIN_ERROR (gsk_table_mmap_buffer_write (&f->buffer,
                                               d->key_len + d->value_len,
                                               d->data, error));
  return TRUE;
}

static gboolean
emit_branch_ended (BtreeFile *f,
                   guint level)
{
  guint8 data[2] = { MSG__BRANCH_ENDED, level };
  TEST_CHAIN_ERROR (gsk_table_mmap_buffer_write (&f->buffer, 2, data));
  return TRUE;
}

static void
init_child_node_buffer (BtreeFile *f)
{
  guint8 data[2 + GSK_TABLE_VLI64_MAX_LEN * 2];
  guint tmp, n_used = 2;
  data[0] = data[1] = 0;
  gsk_table_vli64_encode (data,
                          gsk_table_mmap_writer_offset (&f->writer)
                          - f->cur_level_start_offset,
                          &tmp);
  n_used += tmp;
  gsk_table_vli64_encode (data,
                          gsk_table_mmap_writer_offset (&f->value_writer)
                          - f->cur_level_start_value_offset,
                          &tmp);
  n_used += tmp;
  memcpy (gsk_table_buffer_set_len (&rv->child_node_buffer, n_used),
          data, n_used);
}

static gboolean
emit_child_node (BtreeFile *f,
                 GError   **error)
{
  /* emit message to buffer */
  guint32 len = f->child_node_buffer.len;
  guint8 data[5] = { MSG__CHILD_NODE,
                     len,
                     len>>8,
                     len>>16,
                     len>>24 };
  TEST_CHAIN_ERROR (gsk_table_mmap_buffer_write (&f->buffer, 5, data, error));
  f->child_node_buffer->data[0] = f->n_data_in_child_node_buffer;
  f->child_node_buffer->data[1] = f->n_data_in_child_node_buffer >> 8;
  g_assert (f->n_data_in_child_node_buffer < (1<<16));
  gsk_table_mmap_writer_write (&f->writer, len, f->child_node_buffer.data);

  /* add to total size of that level */
  f->level_btree_size += len;

  /* clear the child-node buffer */
  gsk_table_buffer_set_len (&f->child_node_buffer, 0);
  return TRUE;
}

static guint
find_nchars_equal (const guint8 *a,
                   const guint8 *b,
                   guint         len)
{
  guint rv = 0;
  for (rv = 0; rv < len; rv++)
    if (a[rv] != b[rv])
      break;
  return rv;
}

static gboolean
add_data_to_child_node_buffer (BtreeFile *f,
                               BtreeFileData *d,
                               GError   **error)
{
  guint8 key_lengths_buf[GSK_TABLE_VLI_MAX_LEN * 2];
  guint8 value_len_buf[GSK_TABLE_VLI_MAX_LEN];
  guint key_lengths_len;
  guint value_len_len;
  guint tmp;
  guint prefix_len = find_nchars_equal (f->last_key.data,
                                        d->key_data,
                                        MIN (f->last_key.len, d->key_len));
  gsk_table_vli_encode (key_lengths_buf, prefix_len, &tmp);
  key_lengths_len = tmp;
  if (f->key_is_fixed_length)
    {
      g_assert (d->key_len == f->fixed_key_length);
    }
  else
    {
      gsk_table_vli_encode (key_lengths_buf + key_lengths_len,
                            d->key_len - prefix_len, &tmp);
      key_lengths_len += tmp;
    }
  if (f->value_is_fixed_length)
    {
      g_assert (d->value_len == f->fixed_value_length);
      value_len_len = 0;
    }
  else
    {
      gsk_table_vli_encode (value_len_buf, d->value_len, &value_len_len);
    }

  /* append key length info, key data, value length info */
  slab = gsk_table_buffer_append (&f->child_node_buffer,
                                  key_lengths_len + (d->key_len - prefix_len)
                                  + value_len_len);
  memcpy (slab, key_lengths_buf, key_lengths_len);
  slab += key_lengths_len;
  memcpy (slab, d->key_data + prefix_len, d->key_len - prefix_len);
  slab += d->key_len - prefix_len;
  memcpy (slab, value_len_buf, value_len_len);

  /* set last_key */
  memcpy (gsk_table_buffer_set_len (&f->last_key, d->key_len) + prefix_len,
          d->key_data + prefix_len,
          d->key_len - prefix_len);

  /* append to value heap */
  TEST_CHAIN_ERROR (gsk_table_mmap_writer_write (&f->value_writer,
                                                 d->value_len,
                                                 d->value_data,
                                                 error));
  f->level_value_size += d->value_len;
  return TRUE;
}

static void
add_subchild_ref_to_child_node_buffer (BtreeFile *f,
                                       guint      len)
{
  guint8 vli_buf[GSK_TABLE_VLI_MAX_LEN];
  guint vli_len;
  gsk_table_vli_encode (vli_buf, len, &vli_len);
  memcpy (gsk_table_buffer_append (&f->child_node_buffer, vli_len),
          vli_buf, vli_len);
}

static gboolean
update_level_header (BtreeFile *f,
                     GError   **error)
{
  guint64 off = f->cur_level_start_offset;
  guint64 len = gsk_table_mmap_writer_offset (&f->writer) - off;
  guint64 loc_le[2];
  loc_le[0] = GUINT64_TO_LE (off);
  loc_le[1] = GUINT64_TO_LE (len);
  g_assert (BTREE_HEADER_LEVEL_SIZE == sizeof (loc_le));
  TEST_CHAIN_ERROR (gsk_table_mmap_writer_pwrite (f->writer,
                                BTREE_HEADER_SIZE
                                + BTREE_HEADER_LEVEL_SIZE * f->cur_level,
                                BTREE_HEADER_LEVEL_SIZE,
                                (const guint8 *) loc_le,
                                error));
  return TRUE;
}

static gboolean
write_short_tree_from_buffer (BtreeFile *f,
                              GError   **error)
{
  guint header_level_start = 
                4               /* magic */
              + 1               /* height */
              + 1               /* flags */
              + 2               /* reserved */
              + (f->key_is_fixed_length ? 4 : 0)
              + (f->value_is_fixed_length ? 4 : 0)
              ;

  /* adjust height to be '1' */
  TEST_CHAIN_ERROR (gsk_table_mmap_writer_pwrite (&f->writer,
                                                  4, &one, 1, error));

  /* build leaf node */
  GByteArray *node = g_byte_array_new ();
  GByteArray *value_node = g_byte_array_new ();
  GByteArray *key_prefix = g_byte_array_new ();
  ...

  for (i = 0; i < f->n_buffered_data; f++)
    {


}

/* free and clear the buffered_data */
static void
free_buffered_data (BtreeFile *f)
{
  guint i;
  for (i = 0; i < f->height; i++)
    g_free (f->buffered_data[i].data);
  g_free (f->buffered_data);
  f->buffered_data = NULL;
  f->n_buffered_data = 0;
}

/* === GskTableFile methods === */
static GskTableFile *
btree__create_file      (GskTableFileFactory      *factory,
                         const char               *dir,
                         guint64                   id,
                         const GskTableFileHints  *hints,
                         GError                  **error)
{
  BtreeFile *rv = g_new (BtreeFile, 1);
  guint64 cur_file_size;
  char fname_buf[GSK_TABLE_MAX_PATH];
  guint height = compute_height (hints, (BtreeFactory *) factory);

  g_assert (height < 256);

  rv->dir = GSK_TABLE_FILE_DIR_MAYBE_STRDUP (dir);
  rv->id = id;
  gsk_table_mk_fname (fname_buf, dir, id, "btree");
  fd = open (fname_buf, O_RDWR|O_CREAT|O_TRUNC, 0644);
  if (hints->allocate_disk_space_based_on_max_sizes)
    {
      cur_file_size = (hints->max_entries << 3)
                    + hints->max_key_bytes
                    + hints->max_value_bytes;
      if (ftruncate (fd, n_bytes) < 0)
        {
          ..
        }
    }
  else
    cur_file_size = 0;
  gsk_table_mmap_writer_init (&rv->writer, fd, cur_file_size);

  gsk_table_mk_fname (fname_buf, dir, id, "buffer");
  fd = open (fname_buf, O_RDWR|O_CREAT|O_TRUNC, 0644);
  gsk_table_mmap_buffer_init (&rv->buffer, fd, 0, 0);

  /* write header (at least those parts that we can) */
  guint header_len = BTREE_HEADER_SIZE + BTREE_HEADER_PER_HEIGHT_SIZE * height;
  guint8 *header_buffer = g_malloc0 (header_len);
  memcpy (header_buffer, BTREE_MAGIC_HEADER, 4);
  header_buffer[4] = height;
  if (hints->min_key_size == hints->max_key_size)
    {
      guint32 size_le = GUINT32_TO_LE (hints->min_key_size);
      header_buffer[5] |= 1;
      memcpy (header + 8, &size_le, 4);
      rv->key_is_fixed_length = 1;
      rv->fixed_key_length = hints->min_key_size;
    }
  if (hints->min_value_size == hints->max_value_size)
    {
      guint32 size_le = GUINT32_TO_LE (hints->min_value_size);
      header_buffer[5] |= 2;
      memcpy (header + 12, &size_le, 4);
      rv->value_is_fixed_length = 1;
      rv->fixed_value_length = hints->min_value_size;
    }

  /* TODO: cleanup memory */
  TEST_CHAIN_ERROR_NULL (gsk_table_mmap_writer_write (&rv->writer,
                                                      header_buffer,
                                                      header_len,
                                                      error));
  g_free (header_buffer);

  rv->cur_level = height - 1;
  rv->cur_level_start_offset = header_len;
  rv->cur_level_start_value_offset = 0;
  rv->height = height;
  rv->level_indices = g_new0 (guint, height);
  rv->level_btree_size = 0;
  rv->level_value_size = 0;
  rv->is_building = rv->has_building_materials = TRUE;

  rv->n_data_in_child_node_buffer = 0;
  gsk_table_buffer_init (&rv->child_node_buffer);
  init_child_node_buffer (&rv->child_node_buffer);

  gsk_table_buffer_init (&rv->last_key);
  rv->n_data_in_child_node_buffer = 0;
  rv->n_buffered_data = 0;
  rv->first_buffered_data = 0;
  rv->buffered_data = g_new0 (BtreeFileData, height);

  rv->factory = factory;
  return &rv->base_instance;
}

static GskTableFile *
btree__open_building_file (GskTableFileFactory     *factory,
                           const char               *dir,
                           guint64                   id,
                           guint                     state_len,
                           const guint8             *state_data,
                           GError                  **error)
{
  ...
}

static GskTableFile *
btree__open_file          (GskTableFileFactory      *factory,
                           const char               *dir,
                           guint64                   id,
                           GError                  **error)
{
  ...
}

/* methods for a file which is being built */
static gboolean
btree__feed_entry       (GskTableFile             *file,
                         guint                     real_key_len,
                         const guint8             *real_key_data,
                         guint                     real_value_len,
                         const guint8             *real_value_data,
                         GError                  **error)
{
  BtreeFile *f = (BtreeFile *) file;

  /* NOTE: we don't generally want to actually process the "real"
     data, instead we put it in our circular buffer,
     after processing data that is exactly "height" elements old.
     The extra elements are used to finish up the tree. */

  if (f->n_buffered_data == f->height)
    {
      guint level;
      BtreeFileData *d = &f->buffered_data[f->first_buffered_data];
      for (level = 0; level + 1 < file->height; level++)
        if (f->level_indices[level] % 2 == 0)
          break;
      if (level + 1 < file->height)
        {
          /* defer this data for the next pass */
          emit_branch_value (f, level, d);
        }
      else
        {
          /* add this data to current leaf node */
          add_data_to_child_node (f, d);
        }

      if (level + 1 == file->height)
        {
          if (f->level_indices[level] == f->values_per_leaf)
            {
              /* emit child node */
              TEST_CHAIN_ERROR (emit_child_node (f, error));

              f->level_indices[level] = 0;
              level--;
            }
          else
            {
              f->level_indices[level] += 1;
              goto done_incrementing_array;
            }
        }
      for (;;)
        {
          f->level_indices[level] += 1;
          if (level == 0)
            break;
          if (f->level_indices[level] == f->branches_per_level * 2 + 1)
            {
              /* emit branch-end message for level */
              emit_branch_ended (f, level);

              f->level_indices[level] = 0;
              level--;
            }
        }

      /* replace oldest element with real data */
      if (G_UNLIKELY (d->alloced < real_key_len + real_value_len))
        {
          guint new_alloced = d->alloced * 2;
          while (new_alloced < real_key_len + real_value_len)
            new_alloced *= 2;
          g_free (d->data);
          d->data = g_malloc (new_alloced);
        }
      d->key_len = real_key_len;
      d->value_len = real_value_len;
      memcpy (d->data, real_key_data, real_key_len);
      memcpy (d->data + real_key_len, real_value_data, real_value_len);

      f->first_buffered_data += 1;
      if (f->first_buffered_data == f->height)
        f->first_buffered_data = 0;
    }
  else
    {
      /* add new data into buffer */
      BtreeFileData *d = &f->buffered_data[f->n_buffered_data];
      guint alloced = 16;
      while (alloced < real_key_len + real_value_len)
        alloced *= 2;

      d->key_len = real_key_len;
      d->value_len = real_value_len;
      d->alloced = alloced;
      d->data = g_malloc (alloced);
      memcpy (d->data, real_key_data, real_key_len);
      memcpy (d->data + real_key_len, real_value_data, real_value_len);

      f->n_buffered_data++;
    }
}

static gboolean
btree__done_feeding     (GskTableFile             *file,
                         gboolean                 *ready_out,
                         GError                  **error)
{
  BtreeFile *f = (BtreeFile *) file;
  guint i;
  if (f->level_indices[0] == 0)
    {
      /* --- a short tree --- */
      TEST_CHAIN_ERROR (write_short_tree_from_buffer (f, error));
      *ready_out = TRUE;

      /* free buffer */
      for (i = 0; i < f->n_buffered_data; i++)
        g_free (f->buffered_data[i].data);
      g_free (f->buffered_data);
      f->buffered_data = NULL;

      return TRUE;
    }
  else
    {
      guint level = 0;
      while (level < f->height - 1)
        if (f->level_indices[level] % 2 == 0)
          break;
      if (G_LIKELY (level == f->height - 1))
        {
          /* add one element to leaf node */
          BtreeFileData *bfd = f->buffered_data + f->first_buffered_data;
          TEST_CHAIN_ERROR (add_data_to_child_node (f, bfd, error));

          /* emit child node */
          TEST_CHAIN_ERROR (emit_child_node (f, error));

          /* update header to reflect level's offset and length */
          TEST_CHAIN_ERROR (update_level_header (f, error));

          f->first_buffered_data++;
          if (f->first_buffered_data == f->height)
            f = 0;
          f->n_buffered_data--;
          level--;
        }
      while (level > 0)
        {
          BtreeFileData *d = &f->buffered_data[f->first_buffered_data];
          TEST_CHAIN_ERROR (emit_branch_value (f, level, d, error));
          TEST_CHAIN_ERROR (emit_branch_ended (f, level, error));

          f->first_buffered_data++;
          if (f->first_buffered_data == f->height)
            f = 0;
          g_assert (f->n_buffered_data > 0);
          f->n_buffered_data--;
          level--;
        }

      /* toss extra elements into level 0 */
      g_assert (f->n_buffered_data > 0);
      for (;;)
        {
          BtreeFileData *d = &f->buffered_data[f->first_buffered_data];
          TEST_CHAIN_ERROR (emit_branch_value (f, level, d, error));
          f->first_buffered_data++;
          if (f->first_buffered_data == f->height)
            f = 0;
          f->n_buffered_data--;
          if (f->n_buffered_data > 0)
            TEST_CHAIN_ERROR (emit_dummy_branch (f, level, error));
          else
            break;
        }

      if (f->cur_level == 0)
        {
          *ready_out = TRUE;

          /* finishing touches? */
          ...
        }
      else
        {
          TEST_CHAIN_ERROR (emit_level_ended (f, error));
          f->cur_level--;
        }
    }

  free_buffered_data (f);

  return TRUE;
}

static gboolean
btree__get_build_state  (GskTableFile             *file,
                         guint                    *state_len_out,
                         guint8                  **state_data_out,
                         GError                  **error)
{
  g_assert (file->has_building_materials);
  g_assert (file->is_building);
  ...
}

static gboolean
btree__build_file       (GskTableFile             *file,
                         gboolean                 *ready_out,
                         GError                  **error)
{
  guint i;
  for (i = 0; i < N_MESSAGES_PER_BUILD_FILE; i++)
    {
      guint32 msg_le;
      const guint8 *msg_data;
      TEST_CHAIN_ERROR (gsk_table_mmap_buffer_read (&f->buffer,
                                                  1, &msg_data,
                                                  error));
      switch (msg_data[0])
        {
        case MSG__CHILD_NODE:
          /* write subtree reference into branch */
          {
            guint32 len_le, len;
            TEST_CHAIN_ERROR (gsk_table_mmap_buffer_read (&f->buffer,
                                                        4, &len_data,
                                                        error));
            memcpy (&len_le, len_data, 4);
            len = GUINT32_FROM_LE (len_le);
            add_subchild_ref_to_child_node_buffer (f, len);
          }
          break;

        case MSG__BRANCH_VALUE:
          {
            const guint8 *level_kv_len_le;
            const guint8 *kv_data;
            guint level;
            guint32 key_len_le, key_len;
            guint32 value_len_le, value_len;
            BtreeFileData bfd;

            TEST_CHAIN_ERROR (gsk_table_mmap_buffer_read (&f->buffer,
                                                        9, &level_kv_len_le,
                                                        error));
            level = level_kv_len_le[0];
            memcpy (&key_len_le, level_kv_len_le + 1, 4);
            key_len = GUINT32_FROM_LE (key_len_le);
            memcpy (&value_len_le, level_kv_len_le + 5, 4);
            value_len = GUINT32_FROM_LE (value_len_le);
            TEST_CHAIN_ERROR (gsk_table_mmap_buffer_read (&f->buffer,
                                                        key_len + value_len,
                                                        &kv_data,
                                                        error));
            bfd.key_len = key_len;
            bfd.value_len = value_len;
            bfd.data = (guint8 *) kv_data;
            if (level == f->cur_level)
              TEST_CHAIN_ERROR (add_data_to_child_node (f, &bfd, error));
            else
              /* NOTE: assumes that buffer_write doesn't invalidate kv_data! */
              TEST_CHAIN_ERROR (emit_branch_value (f, level, &bfd, error));
            break;
          }

        case MSG__BRANCH_ENDED:
          {
            const guint8 *b;
            guint level;
            TEST_CHAIN_ERROR (gsk_table_mmap_buffer_read (&f->buffer, 1, &b,
                                                        error));
            level = b[0];
            if (level == f->cur_level)
              TEST_CHAIN_ERROR (emit_child_node (f, error));
            else
              TEST_CHAIN_ERROR (emit_branch_ended (f, level, error));
            break;
          }

        case MSG__DUMMY_BRANCH:
          {
            const guint8 *b;
            guint level;
            TEST_CHAIN_ERROR (gsk_table_mmap_buffer_read (&f->buffer, 1, &b,
                                                        error));
            level = b[0];
            g_assert (level == 0);
            if (level == f->cur_level)
              add_subchild_ref_to_child_node_buffer (f, 0);
            else
              TEST_CHAIN_ERROR (emit_dummy_branch (f, level, error));
          }

        case MSG__LEVEL_ENDED:

          /* child_node should be empty
             (there should be a BRANCH_ENDED message if needed) */
          g_assert (f->n_data_in_child_node_buffer == 0);

          if (f->cur_level == 0)
            goto finished_level0;

          f->cur_level--;
          TEST_CHAIN_ERROR (emit_level_ended (f, error));
          ... reset other vars ...
          break;

        default:
          g_return_val_if_reached (FALSE);
        }
    }
  *ready_out = FALSE;
  return TRUE;

finished_level0:

  /* convert writer to reader; close buffer */

  ...

  *ready_out = TRUE;
  return TRUE;
}

static void
btree__release_build_data(GskTableFile            *file)
{
  BtreeFile *f = (BtreeFile *) file;
  char fname_buf[GSK_TABLE_MAX_PATH];

  /* unlink buffer */
  gsk_table_mk_fname (fname_buf, f->dir, f->id, "buffer");
  unlink (fname_buf);
}

/* methods for a file which has been constructed */
static gboolean
btree__query_file       (GskTableFile             *file,
                         GskTableFileQuery        *query_inout,
                         GError                  **error)
{
  ...
}

static GskTableReader *
btree__create_reader    (GskTableFile             *file,
                         GError                  **error)
{
  ...
}

static gboolean
btree__get_reader_state (GskTableFile             *file,
                         GskTableReader           *reader,
                         guint                    *state_len_out,
                         guint8                  **state_data_out,
                         GError                  **error)
{
  ...
}

static GskTableReader *
btree__recreate_reader  (GskTableFile             *file,
                         guint                     state_len,
                         const guint8             *state_data,
                         GError                  **error)
{
  ...
}

/* destroying files and factories */
static gboolean
btree__destroy_file     (GskTableFile             *file,
                         gboolean                  erase,
                         GError                  **error)
{
  BtreeFile *f = (BtreeFile *) file;
  if (erase)
    {
      /* maybe unlink pipe */
      ...

      /* unlink .btree file */
      ...

      /* unlink .value file */
      ...
    }
  if (f->buffered_data)
    free_buffered_data (f);
  GSK_TABLE_FILE_DIR_MAYBE_FREE (f->dir);
  return TRUE;
}

static void
btree__destroy_factory  (GskTableFileFactory      *factory)
{
  /* no-op for static factory */
}


/* for now, return a static factory object */
GskTableFileFactory *gsk_table_file_factory_new_btree (void)
{
  static BtreeFactory the_factory =
    {
      {
        btree__create_file,
        btree__open_building_file,
        btree__open_file,
        btree__feed_entry,
        btree__done_feeding,
        btree__can_get_build_state,
        btree__get_build_state,
        btree__build_file,
        btree__release_build_data,
        btree__query_file,
        btree__create_reader,
        btree__get_reader_state,
        btree__recreate_reader,
        btree__destroy_file,
        btree__destroy_factory
      },
      16,               /* branches_per_level */
      32                /* values_per_leaf */
    };

  return &the_factory.base_instance;
}
