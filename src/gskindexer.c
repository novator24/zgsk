#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "gskqsortmacro.h"
#include "gskindexer.h"

#define MAX_INDEXER_FILES    64
#define MAX_IN_MEMORY        2048
#define MAX_DIR_RETRIES      100
#define MAX_FILENAME         1024

typedef struct _InMemoryData InMemoryData;
struct _InMemoryData
{
  unsigned orig_index;
  GByteArray *array;
};
struct _GskIndexer
{
  char *dir;
  guint64 files[MAX_INDEXER_FILES];

  unsigned n_in_memory;
  InMemoryData in_memory_data[MAX_IN_MEMORY];

  guint64 next_file_id;

  GskIndexerCompareFunc compare;
  GskIndexerMergeFunc merge;
  gpointer user_data;

  GByteArray *tmp_pad;
};

static void
mk_filename (char *buf,
             GskIndexer *indexer,
             guint64 file_no)
{
  g_snprintf (buf, MAX_FILENAME, "%s/%llx", indexer->dir, file_no);
}

static void
unlink_file (GskIndexer *indexer,
             guint64 file_no)
{
  char buf[MAX_FILENAME];
  mk_filename (buf, indexer, file_no);
  unlink (buf);
}

GskIndexer       *gsk_indexer_new         (GskIndexerCompareFunc compare,
                                           GskIndexerMergeFunc   merge,
			                   void                 *user_data)
{
  /* make tmp dir */
  char buf[MAX_FILENAME];
  unsigned ct = 1;
  unsigned i;
  GskIndexer *indexer;
  while (ct < MAX_DIR_RETRIES)
    {
      g_snprintf (buf, sizeof (buf), "/tmp/gskidx-%u-%05u",
                  (unsigned)time(NULL), ct++);
      if (mkdir (buf, 0755) == 0)
        break;
    }
  if (ct == MAX_DIR_RETRIES)
    return NULL;

  indexer = g_new (GskIndexer, 1);
  indexer->dir = g_strdup (buf);
  indexer->compare = compare;
  indexer->merge = merge;
  indexer->user_data = user_data;
  for (i = 0; i < MAX_INDEXER_FILES; i++)
    indexer->files[i] = 0;
  indexer->n_in_memory = 0;
  for (i = 0; i < MAX_IN_MEMORY; i++)
    indexer->in_memory_data[i].array = g_byte_array_new ();
  indexer->next_file_id = 1;
  indexer->tmp_pad = g_byte_array_new ();
  return indexer;
}

static void
read_data (FILE *fp, GByteArray **p_arr)
{
  guint32 len;
  if (fread (&len, 4, 1, fp) != 1)
    {
      if (feof (fp))
        {
          g_byte_array_free (*p_arr, TRUE);
          *p_arr = NULL;
          return;
        }
      else
        {
          g_error ("error reading data from file");
        }
    }
  g_byte_array_set_size (*p_arr, len);
  if (len == 0)
    return;
  if (fread ((*p_arr)->data, len, 1, fp) != 1)
    g_error ("incomplete record");
}

static void
write_data (FILE *fp, GByteArray *data)
{
  guint32 length = data->len;
  if (fwrite (&length, 4, 1, fp) != 1)
    g_error ("error writing length prefix");
  if (length != 0 && fwrite (data->data, data->len, 1, fp) != 1)
    g_error ("error writing data body");
}

static guint64
merge_files (GskIndexer *indexer,
             guint64     old,
             guint64     new)
{
  /* open readers */
  char buf[MAX_FILENAME];
  FILE *reader_a, *reader_b;
  FILE *writer;
  GByteArray *a_data, *b_data;
  guint64 id;
  GByteArray *tmp_pad = indexer->merge ? g_byte_array_new () : NULL;
  mk_filename (buf, indexer, old);
  reader_a = fopen (buf, "rb");
  if (reader_a == NULL)
    g_error ("error opening %s: %s", buf, g_strerror (errno));
  a_data = g_byte_array_new ();
  read_data (reader_a, &a_data);
  mk_filename (buf, indexer, new);
  reader_b = fopen (buf, "rb");
  if (reader_b == NULL)
    g_error ("error opening %s: %s", buf, g_strerror (errno));
  b_data = g_byte_array_new ();
  read_data (reader_b, &b_data);

  /* open writer */
  id = indexer->next_file_id++;
  mk_filename (buf, indexer, id);
  writer = fopen (buf, "wb");
  if (writer == NULL)
    g_error ("error creating writer");

  /* merge */
  while (a_data && b_data)
    {
      int cmp = indexer->compare (a_data->len, a_data->data,
                                  b_data->len, b_data->data,
                                  indexer->user_data);
      if (cmp < 0)
        {
          write_data (writer, a_data);
          read_data (reader_a, &a_data);
        }
      else if (cmp > 0)
        {
          write_data (writer, b_data);
          read_data (reader_b, &b_data);
        }
      else
        {
          if (indexer->merge == NULL)
            {
              write_data (writer, a_data);
              write_data (writer, b_data);
            }
          else
            {
              switch (indexer->merge (a_data->len, a_data->data,
                                      b_data->len, b_data->data,
                                      tmp_pad,
                                      indexer->user_data))
                {
                case GSK_INDEXER_MERGE_RETURN_A:
                  write_data (writer, a_data);
                  break;
                case GSK_INDEXER_MERGE_RETURN_B:
                  write_data (writer, b_data);
                  break;
                case GSK_INDEXER_MERGE_IN_PAD:
                  write_data (writer, tmp_pad);
                  break;
                case GSK_INDEXER_MERGE_DISCARD:
                  break;
                default:
                  g_assert_not_reached ();
                }
            }
          read_data (reader_a, &a_data);
          read_data (reader_b, &b_data);
        }
    }
  while (a_data)
    {
      write_data (writer, a_data);
      read_data (reader_a, &a_data);
    }
  while (b_data)
    {
      write_data (writer, b_data);
      read_data (reader_b, &b_data);
    }

  fclose (reader_a);
  fclose (reader_b);
  if (tmp_pad)
    g_byte_array_free (tmp_pad, TRUE);
  fclose (writer);
  return id;
}

static void
flush_in_memory_data (GskIndexer *indexer)
{
  guint64 fno;
  FILE *fp;
  char buf[MAX_FILENAME];
  InMemoryData *imd = indexer->in_memory_data;
  unsigned n_imd = indexer->n_in_memory;
  if (indexer->n_in_memory == 0)
    return;
  
  /* qsort */
#define COMPARE_IN_MEMORY_DATA(a,b, rv)                 \
  rv = indexer->compare (a.array->len, a.array->data, \
                         b.array->len, b.array->data, \
                         indexer->user_data);           \
  if (rv == 0)                                          \
    {                                                   \
      if (a.orig_index < b.orig_index) rv = -1;       \
      else if (a.orig_index > b.orig_index) rv = 1;   \
    }
  GSK_QSORT (imd, InMemoryData, n_imd, COMPARE_IN_MEMORY_DATA);
#undef COMPARE_IN_MEMORY_DATA

  if (indexer->merge)
    {
      /* merge */
      unsigned n_output = 1;
      unsigned i;
      for (i = 1; i < n_imd; i++)
        {
          if (n_output > 0
           && indexer->compare (imd[n_output-1].array->len,
                                imd[n_output-1].array->data,
                                imd[i].array->len,
                                imd[i].array->data,
                                indexer->user_data) == 0)
            {
              switch (indexer->merge (imd[n_output-1].array->len,
                                      imd[n_output-1].array->data,
                                      imd[i].array->len,
                                      imd[i].array->data,
                                      indexer->tmp_pad,
                                      indexer->user_data))
                {
                case GSK_INDEXER_MERGE_RETURN_A:
                  break;
                case GSK_INDEXER_MERGE_RETURN_B:
                  {
                    GByteArray *tmp = imd[n_output-1].array;
                    imd[n_output-1].array = imd[i].array;
                    imd[i].array = tmp;
                  }
                  break;
                case GSK_INDEXER_MERGE_IN_PAD:
                  {
                    GByteArray *tmp = imd[n_output-1].array;
                    imd[n_output-1].array = indexer->tmp_pad;
                    indexer->tmp_pad = tmp;
                    break;
                  }
                case GSK_INDEXER_MERGE_DISCARD:
                  n_output--;
                  break;
                }
            }
          else
            {
              InMemoryData tmp = imd[n_output];
              imd[n_output] = imd[i];
              imd[i] = tmp;
              n_output++;
            }
        }

      /* no point to write out 0 length file */
      if (n_output == 0)
        {
          indexer->n_in_memory = 0;
          return;
        }
      n_imd = n_output;
    }
  fno = indexer->next_file_id++;
  mk_filename (buf, indexer, fno);
  fp = fopen (buf, "wb");
  if (fp == NULL)
    g_error ("error creating %s: %s", buf, g_strerror (errno));
  unsigned i;
  for (i = 0; i < n_imd; i++)
    {
      write_data (fp, indexer->in_memory_data[i].array);
    }
  fclose (fp);

  i = 0;
  for (i = 0; i < MAX_INDEXER_FILES && indexer->files[i] != 0; i++)
    {
      /* merge indexer->files[i] (older) and fno */
      guint64 old_id = indexer->files[i];
      guint64 id = merge_files (indexer, old_id, fno);
      indexer->files[i] = 0;
      unlink_file (indexer, old_id);
      unlink_file (indexer, fno);
      fno = id;
    }
  g_assert (i < MAX_INDEXER_FILES);
  indexer->files[i] = fno;
  indexer->n_in_memory = 0;
}

void              gsk_indexer_add         (GskIndexer           *indexer,
                                           unsigned              length,
					   const guint8         *data)
{
  unsigned old_n = indexer->n_in_memory;
  GByteArray *a = indexer->in_memory_data[old_n].array;
  g_byte_array_set_size (a, length);
  memcpy (a->data, data, length);
  indexer->in_memory_data[old_n].orig_index = old_n;
  if (++(indexer->n_in_memory) == MAX_IN_MEMORY)
    {
      /* flush file */
      flush_in_memory_data (indexer);
    }
} 

struct _GskIndexerReader
{
  FILE *fp;
  GByteArray *pad;
};

GskIndexerReader *gsk_indexer_make_reader (GskIndexer           *indexer)
{
  guint i;
  int last_file_index = -1;
  GByteArray *pad;
  flush_in_memory_data (indexer);

  for (i = 0; i < MAX_INDEXER_FILES; i++)
    {
      if (indexer->files[i])
        {
          if (last_file_index >= 0)
            {
              /* merge */
              guint64 id = merge_files (indexer,
                                        indexer->files[i],
                                        indexer->files[last_file_index]);
              unlink_file (indexer, indexer->files[i]);
              unlink_file (indexer, indexer->files[last_file_index]);
              indexer->files[last_file_index] = 0;
              indexer->files[i] = id;
              last_file_index = i;
            }
          else
            last_file_index = i;
        }
    }
  FILE *fp;
  pad = g_byte_array_new ();
  if (last_file_index == -1)
    {
      fp = fopen ("/dev/null", "rb");
    }
  else
    {
      char buf[MAX_FILENAME];
      guint32 len;
      mk_filename (buf, indexer, indexer->files[last_file_index]);
                  
      fp = fopen (buf, "rb");
      if (fp == NULL)
        g_error ("error creating indexer reader: %s", g_strerror (errno));

      /* peek record */
      if (fread (&len, 4, 1, fp) == 1)
        {
          g_byte_array_set_size (pad, len);
          if (len != 0 && fread (pad->data, pad->len, 1, fp) != 1)
            {
              g_error ("incomplete record");
            }
        }
      else if (feof (fp))
        {
          fclose (fp);
          fp = NULL;
        }
      else if (ferror (fp))
        {
          g_error ("error reading file");
        }
      else
        g_assert_not_reached ();
    }
  GskIndexerReader *reader;
  reader = g_new (GskIndexerReader, 1);
  reader->fp = fp;
  reader->pad = pad;
  return reader;
}
  
  /* merge all existing pieces together */
void              gsk_indexer_destroy     (GskIndexer           *indexer)
{
  guint i;
  for (i = 0; i < MAX_INDEXER_FILES; i++)
    if (indexer->files[i])
      unlink_file (indexer, indexer->files[i]);
  g_byte_array_free (indexer->tmp_pad, TRUE);
  rmdir (indexer->dir);
  g_free (indexer->dir);
  g_free (indexer);
}

gboolean      gsk_indexer_reader_has_data (GskIndexerReader *reader)
{
  return reader->fp != NULL;
}
gboolean      gsk_indexer_reader_peek_data(GskIndexerReader *reader,
                                           unsigned         *len_out,
                                           const guint8    **data_out)
{
  if (reader->fp == NULL)
    return FALSE;
  *len_out = reader->pad->len;
  *data_out = reader->pad->data;
  return TRUE;
}
gboolean      gsk_indexer_reader_advance  (GskIndexerReader *reader)
{
  guint32 len;
  if (reader->fp == NULL)
    return FALSE;
  if (fread (&len, 4, 1, reader->fp) == 1)
    {
      g_byte_array_set_size (reader->pad, len);
      if (len > 0
        && fread (reader->pad->data, reader->pad->len, 1, reader->fp) != 1)
        {
          g_error ("partial record");
        }
      return TRUE;
    }
  else if (feof (reader->fp))
    {
      fclose (reader->fp);
      reader->fp = NULL;
      return FALSE;
    }
  else if (ferror (reader->fp))
    g_error ("error reading file");
  else
    g_assert_not_reached ();
}


void          gsk_indexer_reader_destroy  (GskIndexerReader *reader)
{
  if (reader->fp)
    fclose (reader->fp);
  g_byte_array_free (reader->pad, TRUE);
  g_free (reader);
}


