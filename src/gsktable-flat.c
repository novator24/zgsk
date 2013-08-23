/* for pread() */

#define _XOPEN_SOURCE 500
#define _GNU_SOURCE             /* for unlocked_stdio */
#include "config.h"

/* TODO: handle EINTR everywhere. */

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <zlib.h>
#include <errno.h>

#include "gskutils.h"
#include "gskerror.h"
#include "gsklistmacros.h"
#include "gsktable.h"
#include "gsktable-file.h"
#include "gsktable-helpers.h"

#define FTELLO          ftello          /* TODO: track the offsets ourselves */
#define FSEEKO          fseeko
#define FREAD           fread_unlocked

/* debug invocation of cache-entry force */
#define DEBUG_CACHE_ENTRY_FORCE                 0

/* debug compressed data contents */
#define DEBUG_DUMP_COMPRESSED_DATA              0

/* debug chunk sizes and offsets while reading */
#define DEBUG_READ_CHUNK                        0

/* debug serialization and deserialization of cache entries */
#define DEBUG_ENTRY_SERIALIZATION               0

/* debug flushing the current chunk of data */
#define DEBUG_FLUSH                             0

/* debug serialization of fixed-length index-entry objects */
#define DEBUG_INDEX_ENTRIES                     0

typedef struct _FlatFactory FlatFactory;
typedef struct _MmapReader MmapReader;
typedef struct _MmapWriter MmapWriter;
typedef struct _FlatFileBuilder FlatFileBuilder;
typedef struct _FlatFile FlatFile;
typedef struct _FlatFileReader FlatFileReader;

typedef enum
{
  FILE_INDEX,
  FILE_FIRSTKEYS,
  FILE_DATA
} WhichFile;
#define N_FILES 3

/* MUST be a power-of-two */
#define MMAP_WRITER_SIZE                (512*1024)

#define MAX_MMAP                (1024*1024)

static const char *file_extensions[N_FILES] = { "index", "firstkeys", "data" };

struct _FlatFactory
{
  GskTableFileFactory base_factory;
  guint bytes_per_chunk;
  guint compression_level;
  guint n_recycled_builders;
  guint max_recycled_builders;
  FlatFileBuilder *recycled_builders;
  guint max_cache_entries;
};


struct _MmapReader
{
  gint fd;
  guint64 file_size;
  guint8 *mmapped;
  GskTableBuffer tmp_buf;               /* only if !mmapped */
};

struct _MmapWriter
{
  gint fd;
  guint64 file_size;
  guint64 mmap_offset;
  guint8 *mmapped;
  guint cur_offset;             /* in mmapped */
  GskTableBuffer tmp_buf;
};


struct _FlatFileBuilder
{
  GskTableBuffer input;

  gboolean has_last_key;
  GskTableBuffer first_key;
  GskTableBuffer last_key;

  GskTableBuffer uncompressed;
  GskTableBuffer compressed;

  guint n_compressed_entries;
  guint uncompressed_data_len;

  MmapWriter writers[N_FILES];
 
  z_stream compressor;
  GskMemPool compressor_allocator;
  guint8 *compressor_allocator_scratchpad;
  gsize compressor_allocator_scratchpad_len;

  FlatFileBuilder *next_recycled_builder;
};

typedef struct _CacheEntry CacheEntry;
typedef struct _CacheEntryRecord CacheEntryRecord;
struct _CacheEntryRecord
{
  guint key_len;
  const guint8 *key_data;
  guint value_len;
  const guint8 *value_data;
};
struct _CacheEntry
{
  guint n_entries;
  guint64 index;
  CacheEntry *prev_lru, *next_lru;
  CacheEntry *bin_next;
  CacheEntryRecord records[1];          /* must be last! */
};

struct _FlatFile
{
  GskTableFile base_file;
  gint         fds[N_FILES];
  FlatFileBuilder *builder;

  gboolean has_readers;         /* builder and has_readers are exclusive: they
                                   cannot be set at the same time */
  MmapReader readers[N_FILES];

  guint cache_entries_len;
  CacheEntry **cache_entries;
  guint cache_entries_count;
  guint max_cache_entries;
  CacheEntry *most_recently_used, *least_recently_used;
};

struct _FlatFileReader
{
  GskTableReader base_reader;
  FILE *fps[N_FILES];
  guint64 chunk_file_offsets[N_FILES];
  CacheEntry *cache_entry;
  guint record_index;
  guint64 index_entry_index;
};

#define GET_LRU_LIST(file) \
  CacheEntry *, (file)->most_recently_used, (file)->least_recently_used, \
  prev_lru, next_lru

typedef struct _IndexEntry IndexEntry;
struct _IndexEntry
{
  guint64 firstkeys_offset;
  guint32 firstkeys_len;
  guint64 compressed_data_offset;
  guint32 compressed_data_len;
};

static gboolean
do_pread (FlatFile *ffile,
          WhichFile f,
          guint64   offset,
          guint     length,
          guint8    *ptr_out,
          GError   **error);

static guint
uint32_vli_encode (guint32 to_encode,
                   guint8 *buf)         /* min length 5 */
{
  if (to_encode < 0x80)
    {
      buf[0] = to_encode;
      return 1;
    }
  else if (to_encode < (1<<14))
    {
      buf[0] = 0x80 | (to_encode >> 7);
      buf[1] = to_encode & 0x7f;
      return 2;
    }
  else if (to_encode < (1<<21))
    {
      buf[0] = 0x80 | (to_encode >> 14);
      buf[1] = 0x80 | (to_encode >> 7);
      buf[2] = to_encode & 0x7f;
      return 3;
    }
  else if (to_encode < (1<<28))
    {
      buf[0] = 0x80 | (to_encode >> 21);
      buf[1] = 0x80 | (to_encode >> 14);
      buf[2] = 0x80 | (to_encode >> 7);
      buf[3] = to_encode & 0x7f;
      return 4;
    }
  else
    {
      buf[0] = 0x80 | (to_encode >> 28);
      buf[1] = 0x80 | (to_encode >> 21);
      buf[2] = 0x80 | (to_encode >> 14);
      buf[3] = 0x80 | (to_encode >> 7);
      buf[4] = to_encode & 0x7f;
      return 5;
    }
}
static guint
uint32_vli_decode (const guint8 *input,
                   guint32      *decoded)
{
  guint32 val = input[0] & 0x7f;
  if (input[0] & 0x80)
    {
      guint used = 1;
      do
        {
          val <<= 7;
          val |= (input[used] & 0x7f);
        }
      while ((input[used++] & 0x80) != 0);
      *decoded = val;
      return used;
    }
  else
    {
      *decoded = val;
      return 1;
    }
}

typedef struct _CacheEntryTmpRecord CacheEntryTmpRecord;
struct _CacheEntryTmpRecord
{
  guint prefix_len;
  guint key_len;
  guint value_len;
  const guint8 *keydata;          /* key without prefix */
  const guint8 *value;
};
static CacheEntry *
cache_entry_deserialize (guint64       index,
                         guint         firstkey_len,
                         const guint8 *firstkey_data,
                         guint         compressed_data_len,
                         const guint8 *compressed_data,
                         GError      **error)
{
  guint used, tmp;
  guint n_compressed_entries, uncompressed_data_len;
  CacheEntryTmpRecord *records, *to_free = NULL;
  guint8 *uncompressed_data, *to_free2 = NULL;
  guint i;
  int zrv;
  guint8 *uc_at;
  guint data_size;
  CacheEntry *rv;
  const guint8 *last_key;
  guint8 *heap_at;
  used = uint32_vli_decode (compressed_data, &n_compressed_entries);
  used += uint32_vli_decode (compressed_data + used, &uncompressed_data_len);

#if DEBUG_ENTRY_SERIALIZATION
  g_message ("deserialize %llu: n_compressed_entry=%u, uncompressed_data_len=%u, compressed_header_len=%u, actual zlib compressed_len=%u", index, n_compressed_entries, uncompressed_data_len, used, compressed_data_len - used);
#if DEBUG_DUMP_COMPRESSED_DATA
  {
    char *hex = gsk_escape_memory_hex (compressed_data + used, compressed_data_len - used);
    g_message ("  compressed_data=%s", hex);
    g_free (hex);
  }
#endif
#endif

  /* uncompress */
  if (uncompressed_data_len < 32*1024)
    uncompressed_data = g_alloca (uncompressed_data_len);
  else
    uncompressed_data = to_free2 = g_malloc (uncompressed_data_len);

  z_stream uncompress_buf;
  memset (&uncompress_buf, 0, sizeof (uncompress_buf));
  inflateInit (&uncompress_buf);
  uncompress_buf.avail_in = compressed_data_len - used;
  uncompress_buf.next_in = (guint8 *) compressed_data + used;
  uncompress_buf.avail_out = uncompressed_data_len;
  uncompress_buf.next_out = uncompressed_data;
  zrv = inflate (&uncompress_buf, Z_SYNC_FLUSH);
  if (zrv != Z_OK)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_CORRUPT,
                   "error uncompressing zlib compressed data (zrv=%d)",
                   zrv);
      g_free (to_free2);
      return NULL;
    }

  /* parse data */
  if (n_compressed_entries < 512)
    records = g_newa (CacheEntryTmpRecord, n_compressed_entries);
  else
    records = to_free = g_new (CacheEntryTmpRecord, n_compressed_entries);

  uc_at = uncompressed_data;
  data_size = 0;
#if DEBUG_ENTRY_SERIALIZATION
  g_message ("uncompressed_data_len=%u, n_compressed_entries=%u",uncompressed_data_len,n_compressed_entries);
#endif
  for (i = 0; i < n_compressed_entries; i++)
    {
      if (i > 0)
        {
          uc_at += uint32_vli_decode (uc_at, &tmp);
          records[i].prefix_len = tmp;
          uc_at += uint32_vli_decode (uc_at, &tmp);
          records[i].key_len = records[i].prefix_len + tmp;
          records[i].keydata = uc_at;
          uc_at += tmp;              /* skip keydata for now */
        }
      else
        {
          records[i].key_len = firstkey_len;
          records[i].keydata = firstkey_data;
          records[i].prefix_len = 0;
        }
      uc_at += uint32_vli_decode (uc_at, &tmp);
      records[i].value_len = tmp;
      records[i].value = uc_at;
      uc_at += tmp;

      data_size += records[i].key_len + records[i].value_len;
    }
  if (uc_at - uncompressed_data != (gssize) uncompressed_data_len)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_CORRUPT,
                   "data corrupt uncompressing block (distance %d)",
                   (int)(uc_at-uncompressed_data+uncompressed_data_len));
      g_free (to_free);
      g_free (to_free2);
      return NULL;
    }

  rv = g_malloc (sizeof (CacheEntry)
                 + (n_compressed_entries-1) * sizeof (CacheEntryRecord)
                 + data_size);
  last_key = NULL;
  heap_at = (guint8 *) (rv->records + n_compressed_entries);
  
  rv->n_entries = n_compressed_entries;
  rv->index = index;

  for (i = 0; i < n_compressed_entries; i++)
    {
      guint key_len = records[i].key_len, pref_len = records[i].prefix_len;
      guint val_len = records[i].value_len;
      rv->records[i].key_len = key_len;
      rv->records[i].value_len = val_len;
      rv->records[i].key_data = heap_at;
      memcpy (heap_at, last_key, pref_len);
      memcpy (heap_at + pref_len, records[i].keydata, key_len - pref_len);
      heap_at += key_len;
      memcpy (heap_at, records[i].value, val_len);
      rv->records[i].value_data = heap_at;
      heap_at += val_len;
      last_key = rv->records[i].key_data;
    }
  g_free (to_free);
  g_free (to_free2);
  return rv;
}

static CacheEntry *
cache_entry_force (FlatFile  *ffile,
                   guint64    index,
                   IndexEntry *index_entry,
                   guint8     *firstkey_data,
                   GError    **error)
{
  guint bin;
  CacheEntry *entry;
#if DEBUG_CACHE_ENTRY_FORCE
  g_message ("cache_entry_force: index=%llu [key offset/length=%llu/%u; data offset/length=%llu/%u]", index,index_entry->firstkeys_offset,index_entry->firstkeys_len, index_entry->compressed_data_offset, index_entry->compressed_data_len);
#endif
  if (ffile->cache_entries_len == 0)
    {
      ffile->cache_entries_len = g_spaced_primes_closest (ffile->max_cache_entries);
      ffile->cache_entries = g_new0 (CacheEntry *, ffile->cache_entries_len);
    }
  bin = (guint) index % ffile->cache_entries_len;
  for (entry = ffile->cache_entries[bin]; entry != NULL; entry = entry->bin_next)
    if (entry->index == index)
      {
        if (entry->prev_lru != NULL)
          {
            GSK_LIST_REMOVE (GET_LRU_LIST (ffile), entry);
            GSK_LIST_PREPEND (GET_LRU_LIST (ffile), entry);
          }
        return entry;
      }

  /* possibly evict old cache entry */
  if (ffile->cache_entries_count == ffile->max_cache_entries)
    {
      CacheEntry *evicted = ffile->least_recently_used;
      guint bin = (guint) evicted->index % ffile->cache_entries_len;
      CacheEntry **pprev;
      GSK_LIST_REMOVE_LAST (GET_LRU_LIST (ffile));

      /* remove from hash-table */
      for (pprev = ffile->cache_entries + bin;
           *pprev != evicted;
           pprev = &((*pprev)->bin_next))
        ;
      *pprev = evicted->bin_next;

      ffile->cache_entries_count--;
      g_free (evicted);
    }

  /* create new entry */
  guint8 *compressed_data;
  compressed_data = g_malloc (index_entry->compressed_data_len);
  if (!do_pread (ffile, FILE_DATA,
                 index_entry->compressed_data_offset,
                 index_entry->compressed_data_len,
                 compressed_data, error))
    {
      g_free (compressed_data);
      return NULL;
    }

  /* deserialize the cache entry */
  entry = cache_entry_deserialize (index,
                                   index_entry->firstkeys_len, firstkey_data,
                                   index_entry->compressed_data_len,
                                   compressed_data,
                                   error);
  if (entry == NULL)
    {
      g_free (compressed_data);
      return NULL;
    }
  entry->bin_next = ffile->cache_entries[bin];
  ffile->cache_entries[bin] = entry;
  ffile->cache_entries_count++;
  g_free (compressed_data);
  GSK_LIST_PREPEND (GET_LRU_LIST (ffile), entry);

  return entry;
}

/* --- mmap reading implementation --- */
static gboolean
mmap_reader_init (MmapReader     *reader,
                  gint            fd,
                  GError        **error)
{
  struct stat stat_buf;
  reader->fd = fd;
  if (fstat (fd, &stat_buf) < 0)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_FILE_STAT,
                   "error stating fd %d: %s",
                   fd, g_strerror (errno));
      return FALSE;
    }
  reader->file_size = stat_buf.st_size;

  if (reader->file_size < MAX_MMAP)
    {
      reader->mmapped = mmap (NULL, reader->file_size, PROT_READ, MAP_SHARED, fd, 0);
      if (reader->mmapped == NULL || reader->mmapped == MAP_FAILED)
        {
          reader->mmapped = NULL;
          g_set_error (error, GSK_G_ERROR_DOMAIN,
                       GSK_ERROR_FILE_MMAP,
                       "error mmapping fd %d: %s",
                       fd, g_strerror (errno));
          return FALSE;
        }
    }
  else
    {
      reader->mmapped = NULL;
      gsk_table_buffer_init (&reader->tmp_buf);
    }
  return TRUE;
}

static gboolean
mmap_reader_pread (MmapReader     *reader,
                   guint64         offset,
                   guint           length,
                   guint8         *data_out,
                   GError        **error)
{
  g_assert (offset + length <= reader->file_size);
  if (reader->mmapped)
    {
      memcpy (data_out, reader->mmapped + (gsize)offset, length);
      return TRUE;
    }
  else
    {
      gssize rv = pread (reader->fd, data_out, length, offset);
      if (rv < 0)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_FILE_PREAD,
                       "error calling pread(): %s",
                       g_strerror (errno));
          return FALSE;
        }
      else if (rv < (gssize) length)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_PREMATURE_EOF,
                       "premature end-of-file calling pread() (mmap reader pread; offset=%"G_GUINT64_FORMAT"; length=%u, got=%u)",
                       offset, length, (guint) rv);
          return FALSE;
        }
      return TRUE;
    }
}

static void
mmap_reader_clear (MmapReader *reader)
{
  if (reader->mmapped)
    munmap (reader->mmapped, reader->file_size);
  else
    gsk_table_buffer_clear (&reader->tmp_buf);
}

static gboolean
mmap_writer_init_at (MmapWriter *writer,
                     gint        fd,
                     guint64     offset,
                     GError    **error)
{
  guint64 mmap_offset = offset & (~(guint64)(MMAP_WRITER_SIZE-1));
  struct stat stat_buf;
  guint64 file_size;
  writer->fd = fd;
  if (fstat (fd, &stat_buf) < 0)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_FILE_STAT,
                   "error getting size of file-descriptor %d: %s",
                   fd, g_strerror (errno));
      return FALSE;
    }
  file_size = stat_buf.st_size;
  if (mmap_offset + MMAP_WRITER_SIZE > file_size)
    {
      if (ftruncate (fd, mmap_offset + MMAP_WRITER_SIZE) < 0)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN,
                       GSK_ERROR_FILE_STAT,
                       "error expanding mmap writer file size: %s",
                       g_strerror (errno));
          return FALSE;
        }
      file_size = mmap_offset + MMAP_WRITER_SIZE;
    }
  writer->file_size = file_size;
  writer->mmap_offset = mmap_offset;
  writer->mmapped = mmap (NULL, MMAP_WRITER_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mmap_offset);
  if (writer->mmapped == MAP_FAILED)
    {
      writer->mmapped = NULL;
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_FILE_MMAP,
                   "error mmapping for writing: %s",
                   g_strerror (errno));
      return FALSE;
    }
  writer->cur_offset = offset - mmap_offset;
  gsk_table_buffer_init (&writer->tmp_buf);
  return TRUE;
}

static inline guint64
mmap_writer_offset (MmapWriter *writer)
{
  return writer->mmap_offset + writer->cur_offset;
}

static gboolean
writer_advance_to_next_page (MmapWriter *writer,
                             GError    **error)
{
  munmap (writer->mmapped, MMAP_WRITER_SIZE);

  writer->mmap_offset += MMAP_WRITER_SIZE;

  if (writer->mmap_offset + MMAP_WRITER_SIZE > writer->file_size)
    {
      if (ftruncate (writer->fd, writer->mmap_offset + MMAP_WRITER_SIZE) < 0)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN,
                       GSK_ERROR_FILE_STAT,
                       "error expanding mmap writer file size: %s",
                       g_strerror (errno));
          return FALSE;
        }
      writer->file_size = writer->mmap_offset + MMAP_WRITER_SIZE;
    }
  writer->cur_offset = 0;
  writer->mmapped = mmap (NULL, MMAP_WRITER_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, writer->fd, writer->mmap_offset);
  if (writer->mmapped == MAP_FAILED)
    {
      writer->mmapped = NULL;
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_FILE_MMAP,
                   "mmap failed on writer: %s",
                   g_strerror (errno));
      return FALSE;
    }

  return TRUE;
}

static gboolean
mmap_writer_write (MmapWriter   *writer,
                   guint         len,
                   const guint8 *data,
                   GError      **error)
{
  if (G_LIKELY (writer->cur_offset + len < MMAP_WRITER_SIZE))
    {
      memcpy (writer->mmapped + writer->cur_offset, data, len);
      writer->cur_offset += len;
    }
  else
    {
      guint n_written = MMAP_WRITER_SIZE - writer->cur_offset;
      memcpy (writer->mmapped + writer->cur_offset, data, n_written);

      /* advance to next page */
      if (!writer_advance_to_next_page (writer, error))
        return FALSE;

      while (G_UNLIKELY (n_written + MMAP_WRITER_SIZE <= len))
        {
          /* write a full page */
          memcpy (writer->mmapped, data + n_written, MMAP_WRITER_SIZE);
          n_written += MMAP_WRITER_SIZE;

          /* advance to next page */
          if (!writer_advance_to_next_page (writer, error))
            return FALSE;
        }
      if (G_LIKELY (n_written < len))
        {
          memcpy (writer->mmapped, data + n_written, len - n_written);
          writer->cur_offset = len - n_written;
        }
    }
  return TRUE;
}

static gboolean
mmap_writer_pread (MmapWriter   *writer,
                   guint64       offset,
                   guint         length,
                   guint8       *data_out,
                   GError      **error)
{
  g_assert (offset + length <= writer->mmap_offset + writer->cur_offset);
  if (offset + length <= writer->mmap_offset)
    {
      /* pure pread() */
      gssize rv = pread (writer->fd, data_out, length, offset);
      if (rv < 0)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_FILE_PREAD,
                       "error calling pread(): %s",
                       g_strerror (errno));
          return FALSE;
        }
      else if (rv < (gssize) length)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_PREMATURE_EOF,
                       "premature end-of-file calling pread() (mmap writer pread; offset=%"G_GUINT64_FORMAT"; length=%u, got=%u; case 0)",
                       offset, length, (guint) rv);
          return FALSE;
        }
      return TRUE;
    }
  else if (offset < writer->mmap_offset)
    {
      /* pread() + memcpy() */
      guint pread_len = writer->mmap_offset - offset;
      gssize rv = pread (writer->fd, data_out, pread_len, offset);
      if (rv < 0)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_FILE_PREAD,
                       "error calling pread(): %s",
                       g_strerror (errno));
          return FALSE;
        }
      else if (rv < (gssize) pread_len)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_PREMATURE_EOF,
                       "premature end-of-file calling pread() (mmap writer pread; offset=%"G_GUINT64_FORMAT"; length=%u, got=%u; case 1)",
                       offset, length, (guint) rv);
          return FALSE;
        }
      memcpy (data_out + pread_len, writer->mmapped, length - pread_len);
      return TRUE;
    }
  else
    {
      /* pure memcpy() */
      guint buf_offset = offset - writer->mmap_offset;
      memcpy (data_out, writer->mmapped + buf_offset, length);
      return TRUE;
    }
}

static void
mmap_writer_clear (MmapWriter *writer)
{
  munmap (writer->mmapped, MMAP_WRITER_SIZE);
  gsk_table_buffer_clear (&writer->tmp_buf);
}


/* --- index entry serialization --- */
#define SIZEOF_INDEX_ENTRY 24
#define INDEX_HEADER_SIZE   8    /* the number of records, as a LE int64 */
/* each entry in index file is:
     8 bytes -- initial key offset
     4 bytes -- initial key length
     8 bytes -- data offset
     4 bytes -- data length
 */

static void
index_entry_serialize (const IndexEntry *index_entry,
                       guint8           *data_out)
{
  guint32 tmp32, tmp32_le;
  guint64 tmp64, tmp64_le;

  tmp64 = index_entry->firstkeys_offset;
  tmp64_le = GUINT64_TO_LE (tmp64);
  memcpy (data_out + 0, &tmp64_le, 8);
  tmp32 = index_entry->firstkeys_len;
  tmp32_le = GUINT32_TO_LE (tmp32);
  memcpy (data_out + 8, &tmp32_le, 4);

  tmp64 = index_entry->compressed_data_offset;
  tmp64_le = GUINT64_TO_LE (tmp64);
  memcpy (data_out + 12, &tmp64_le, 8);
  tmp32 = index_entry->compressed_data_len;
  tmp32_le = GUINT32_TO_LE (tmp32);
  memcpy (data_out + 20, &tmp32_le, 4);
}

static void
index_entry_deserialize (const guint8     *data_in,
                         IndexEntry       *index_entry_out)
{
  guint32 tmp32_le;
  guint64 tmp64_le;

  memcpy (&tmp64_le, data_in + 0, 8);
  index_entry_out->firstkeys_offset = GUINT64_FROM_LE (tmp64_le);
  memcpy (&tmp32_le, data_in + 8, 4);
  index_entry_out->firstkeys_len = GUINT32_FROM_LE (tmp32_le);
  memcpy (&tmp64_le, data_in + 12, 8);
  index_entry_out->compressed_data_offset = GUINT64_FROM_LE (tmp64_le);
  memcpy (&tmp32_le, data_in + 20, 4);
  index_entry_out->compressed_data_len = GUINT32_FROM_LE (tmp32_le);
}


static voidpf my_mem_pool_alloc (voidpf opaque, uInt items, uInt size)
{
  FlatFileBuilder *builder = opaque;
  /* TODO: hack: use alloc0 to avoid uninitialized warnings in valgrind */
  return gsk_mem_pool_alloc0 (&builder->compressor_allocator, items*size);
}
static void my_mem_pool_free (voidpf opaque, voidpf address)
{
}

static inline void
reinit_compressor (FlatFileBuilder *builder,
                   guint            compression_level,
                   gboolean         preowned_mempool)
{
  if (preowned_mempool)
    {
      if (builder->compressor_allocator.all_chunk_list != NULL)
        {
          gsk_mem_pool_destruct (&builder->compressor_allocator);
          builder->compressor_allocator_scratchpad_len *= 2;
          builder->compressor_allocator_scratchpad = g_realloc (builder->compressor_allocator_scratchpad,
                                                                builder->compressor_allocator_scratchpad_len);
        }
    }
  gsk_mem_pool_construct_with_scratch_buf (&builder->compressor_allocator,
                                           builder->compressor_allocator_scratchpad,
                                           builder->compressor_allocator_scratchpad_len);
  memset (&builder->compressor, 0, sizeof (z_stream));
  builder->compressor.zalloc = my_mem_pool_alloc;
  builder->compressor.zfree = my_mem_pool_free;
  builder->compressor.opaque = builder;
  deflateInit (&builder->compressor, compression_level);
  builder->n_compressed_entries = 0;
  builder->uncompressed_data_len = 0;
  builder->has_last_key = FALSE;
  gsk_table_buffer_set_len (&builder->compressed, 0);
}

static FlatFileBuilder *
flat_file_builder_new (FlatFactory *factory)
{
  if (factory->recycled_builders)
    {
      FlatFileBuilder *builder = factory->recycled_builders;
      factory->recycled_builders = builder->next_recycled_builder;
      factory->n_recycled_builders--;
      g_assert (builder->n_compressed_entries == 0
             && builder->uncompressed_data_len == 0);
      return builder;
    }
  else
    {
      FlatFileBuilder *builder = g_slice_new (FlatFileBuilder);
      gsk_table_buffer_init (&builder->input);
      gsk_table_buffer_init (&builder->first_key);
      gsk_table_buffer_init (&builder->last_key);
      gsk_table_buffer_init (&builder->compressed);
      gsk_table_buffer_init (&builder->uncompressed);
      builder->compressor_allocator_scratchpad_len = 1024;
      builder->compressor_allocator_scratchpad = g_malloc (builder->compressor_allocator_scratchpad_len);
      reinit_compressor (builder, factory->compression_level, FALSE);
      return builder;
    }
}

static void
builder_recycle (FlatFactory *ffactory,
                 FlatFileBuilder *builder)
{
  if (ffactory->n_recycled_builders == ffactory->max_recycled_builders)
    {
      gsk_table_buffer_clear (&builder->input);
      gsk_table_buffer_clear (&builder->first_key);
      gsk_table_buffer_clear (&builder->last_key);
      gsk_table_buffer_clear (&builder->compressed);
      gsk_table_buffer_clear (&builder->uncompressed);
      gsk_mem_pool_destruct (&builder->compressor_allocator);
      g_free (builder->compressor_allocator_scratchpad);
      g_slice_free (FlatFileBuilder, builder);
    }
  else
    {
      reinit_compressor (builder, ffactory->compression_level, TRUE);
      builder->next_recycled_builder = ffactory->recycled_builders;
      ffactory->recycled_builders = builder;
      ffactory->n_recycled_builders++;
    }
}

typedef enum
{
  OPEN_MODE_CREATE,
  OPEN_MODE_CONTINUE_CREATE,
  OPEN_MODE_READONLY
} OpenMode;

static gboolean
open_3_files (FlatFile                 *file,
              const char               *dir,
              guint64                   id,
              OpenMode                  open_mode,
              GError                  **error)
{
  char fname_buf[GSK_TABLE_MAX_PATH];
  guint open_flags;
  const char *participle;
  guint f;
  switch (open_mode)
    {
    case OPEN_MODE_CREATE:
      open_flags = O_RDWR | O_CREAT | O_TRUNC;
      participle = "creating";
      break;
    case OPEN_MODE_CONTINUE_CREATE:
      open_flags = O_RDWR;
      participle = "opening for writing";
      break;
    case OPEN_MODE_READONLY:
      open_flags = O_RDONLY;
      participle = "opening for reading";
      break;
    default:
      g_assert_not_reached ();
    }

  for (f = 0; f < N_FILES; f++)
    {
      gsk_table_mk_fname (fname_buf, dir, id, file_extensions[f]);
      file->fds[f] = open (fname_buf, open_flags, 0644);
      if (file->fds[f] < 0)
        {
          guint tmp_f;
          g_set_error (error, GSK_G_ERROR_DOMAIN,
                       GSK_ERROR_FILE_CREATE,
                       "error %s %s: %s",
                       participle, fname_buf, g_strerror (errno));
          for (tmp_f = 0; tmp_f < f; tmp_f++)
            close (file->fds[tmp_f]);
          return FALSE;
        }
    }
  return TRUE;
}

static GskTableFile *
flat__create_file      (GskTableFileFactory      *factory,
                        const char               *dir,
                        guint64                   id,
                        const GskTableFileHints  *hints,
                        GError                  **error)
{
  FlatFactory *ffactory = (FlatFactory *) factory;
  FlatFile *rv = g_slice_new (FlatFile);
  guint f;
  rv->base_file.factory = factory;
  rv->base_file.id = id;
  rv->base_file.n_entries = 0;

  if (!open_3_files (rv, dir, id, OPEN_MODE_CREATE, error))
    {
      g_slice_free (FlatFile, rv);
      return NULL;
    }
  rv->builder = flat_file_builder_new (ffactory);
  for (f = 0; f < N_FILES; f++)
    {
      if (!mmap_writer_init_at (&rv->builder->writers[f], rv->fds[f], 0, error))
        {
          guint tmp_f;
          for (tmp_f = 0; tmp_f < f; tmp_f++)
            mmap_writer_clear (&rv->builder->writers[tmp_f]);
          for (f = 0; f < N_FILES; f++)
            close (rv->fds[f]);
          builder_recycle (ffactory, rv->builder);
          g_slice_free (FlatFile, rv);
          return NULL;
        }
    }

  /* write the index file's header */
  {
    guint64 zero_le = 0;
    if (!mmap_writer_write (&rv->builder->writers[FILE_INDEX], 8,
                            (guint8 *) &zero_le, error))
      {
        for (f = 0; f < N_FILES; f++)
          {
            mmap_writer_clear (&rv->builder->writers[f]);
            close (rv->fds[f]);
          }
        builder_recycle (ffactory, rv->builder);
        g_slice_free (FlatFile, rv);
        return NULL;
      }
  }


  rv->has_readers = FALSE;
  rv->cache_entries_len = 0;
  rv->cache_entries = NULL;
  rv->cache_entries_count = 0;
  rv->max_cache_entries = ffactory->max_cache_entries;
  return &rv->base_file;
}

static GskTableFile *
flat__open_building_file(GskTableFileFactory     *factory,
                         const char               *dir,
                         guint64                   id,
                         guint                     state_len,
                         const guint8             *state_data,
                         GError                  **error)
{
  FlatFactory *ffactory = (FlatFactory *) factory;
  FlatFile *rv = g_slice_new (FlatFile);
  rv->base_file.factory = factory;
  rv->base_file.id = id;
  if (!open_3_files (rv, dir, id, OPEN_MODE_CONTINUE_CREATE, error))
    {
      g_slice_free (FlatFile, rv);
      return NULL;
    }

  rv->builder = flat_file_builder_new (ffactory);

  /* seek according to 'state_data' */
  g_assert (state_len == 33);
  g_assert (state_data[0] == 0);
  {
    guint f;
    for (f = 0; f < N_FILES; f++)
      {
        guint64 offset_le;
        guint64 offset;
        memcpy (&offset_le, state_data + 8 * f + 1, 8);
        offset = GUINT64_FROM_LE (offset_le);
        if (!mmap_writer_init_at (&rv->builder->writers[f], rv->fds[f], offset, error))
          {
            guint tmp_f;
            for (tmp_f = 0; tmp_f < f; tmp_f++)
              mmap_writer_clear (&rv->builder->writers[tmp_f]);
            for (tmp_f = 0; tmp_f < N_FILES; tmp_f++)
              close (rv->fds[tmp_f]);
            builder_recycle (ffactory, rv->builder);
            g_slice_free (FlatFile, rv);
            return NULL;
          }
      }
    {
      guint64 n_entries_le, n_entries;
      memcpy (&n_entries_le, state_data + 1 + 3*8, 8);
      n_entries = GUINT64_FROM_LE (n_entries_le);
      rv->base_file.n_entries = n_entries;
    }
  }
  rv->has_readers = FALSE;

  rv->cache_entries_len = 0;
  rv->cache_entries = NULL;
  rv->cache_entries_count = 0;
  rv->max_cache_entries = ffactory->max_cache_entries;

  return &rv->base_file;
}

GskTableFile *
flat__open_file        (GskTableFileFactory      *factory,
                        const char               *dir,
                        guint64                   id,
                        GError                  **error)
{
  FlatFactory *ffactory = (FlatFactory *) factory;
  FlatFile *rv = g_slice_new (FlatFile);
  guint f;
  rv->base_file.factory = factory;
  rv->base_file.id = id;
  if (!open_3_files (rv, dir, id, OPEN_MODE_READONLY, error))
    {
      g_slice_free (FlatFile, rv);
      return NULL;
    }
  rv->builder = NULL;

  /* pread() to get the number of records */
  {
    guint64 n_entries_le;
    int prv = pread (rv->fds[FILE_INDEX], &n_entries_le, 8, 0);
    if (prv < 0)
      {
        g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_FILE_PREAD,
                     "error reading nrecords from index file: %s",
                     g_strerror (errno));
        for (f = 0; f < N_FILES; f++)
          close (rv->fds[f]);
        g_slice_free (FlatFile, rv);
        return NULL;
      }
    if (prv < 8)
      {
        g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_FILE_PREAD,
                     "premature eof reading nrecords from index file: %s",
                     g_strerror (errno));
        for (f = 0; f < N_FILES; f++)
          close (rv->fds[f]);
        g_slice_free (FlatFile, rv);
        return NULL;
      }
    rv->base_file.n_entries = GUINT64_FROM_LE (n_entries_le);
  }

  /* mmap small files for reading */
  for (f = 0; f < N_FILES; f++)
    {
      if (!mmap_reader_init (&rv->readers[f], rv->fds[f], error))
        {
          guint tmp_f;
          for (tmp_f = 0; tmp_f < f; tmp_f++)
            mmap_reader_clear (&rv->readers[tmp_f]);
          for (f = 0; f < N_FILES; f++)
            close (rv->fds[f]);
          g_slice_free (FlatFile, rv);
          return NULL;
        }
    }
  rv->has_readers = TRUE;

  rv->cache_entries_len = 0;
  rv->cache_entries = NULL;
  rv->cache_entries_count = 0;
  rv->max_cache_entries = ffactory->max_cache_entries;

  return &rv->base_file;
}

static inline void
do_compress (FlatFileBuilder *builder,
             guint            len,
             const guint8    *data)
{
  //g_message ("do_compress: len=%u",len);
  builder->uncompressed_data_len += len;

  /* ensure there is enough data at the end of 'compressed' */
  gsk_table_buffer_ensure_extra (&builder->compressed, len / 2 + 16);

  /* initialize input and output buffers */
  builder->compressor.next_in = (Bytef *) data;
  builder->compressor.avail_in = len;
  builder->compressor.next_out = builder->compressed.data
                               + builder->compressed.len;
  builder->compressor.avail_out = builder->compressed.alloced
                                - builder->compressed.len;

  /* deflate until all input consumed */
  while (builder->compressor.avail_in > 0)
    {

      int zrv;
retry_deflate:
      zrv = deflate (&builder->compressor, 0);
      g_assert (zrv == Z_OK);
      builder->compressed.len = (guint8 *) builder->compressor.next_out
                              - (guint8 *) builder->compressed.data;

      if (builder->compressor.avail_out == 0)
        {
          gsk_table_buffer_ensure_extra (&builder->compressed,
                                         builder->compressor.avail_in / 2 + 16);
          builder->compressor.next_out = builder->compressed.data
                                       + builder->compressed.len;
          builder->compressor.avail_out = builder->compressed.alloced
                                        - builder->compressed.len;
          goto retry_deflate;
        }
    }
}

static void
do_compress_flush (FlatFileBuilder *builder)
{
  /* 6 bytes is sufficient according to the zlib header file docs;
     add 10 for good measure. */
  gsk_table_buffer_ensure_extra (&builder->compressed, 6 + 10);
  builder->compressor.next_in = NULL;
  builder->compressor.avail_in = 0;
  builder->compressor.next_out = builder->compressed.data
                               + builder->compressed.len;
  builder->compressor.avail_out = builder->compressed.alloced
                                - builder->compressed.len;
  for (;;)
    {
      if (deflate (&builder->compressor, Z_SYNC_FLUSH) != Z_OK)
        g_assert_not_reached ();
      builder->compressed.len = builder->compressor.next_out
                              - builder->compressed.data;
      if (builder->compressor.avail_out > 0)
        break;

      gsk_table_buffer_ensure_extra (&builder->compressed, 64);

      builder->compressor.next_out = builder->compressed.data
                                   + builder->compressed.len;
      builder->compressor.avail_out = builder->compressed.alloced
                                    - builder->compressed.len;
    }
}

static gboolean
flush_to_files (FlatFileBuilder *builder,
                GError **error)
{
  /* emit index, keyfile and data file stuff */
  guint8 header[SIZEOF_INDEX_ENTRY];
  guint8 compressed_header[5 + 5];
  guint compressed_header_len = 0;
  IndexEntry index_entry;
  guint tmp;

  /* flush compressor */
  do_compress_flush (builder);

  /* write uncompressed_data_len and n_compressed_entries
     to the compressed_header */
  compressed_header_len = uint32_vli_encode (builder->n_compressed_entries,
                                             compressed_header);
  tmp = uint32_vli_encode (builder->uncompressed_data_len,
                           compressed_header + compressed_header_len);
  compressed_header_len += tmp;

#if DEBUG_FLUSH
  g_message ("flush_to_files: n_compressed_entry=%u, uncompressed_data_len=%u, compressed_header_len=%u, compressed_len=%u", builder->n_compressed_entries, builder->uncompressed_data_len, compressed_header_len, builder->compressed.len);
#if DEBUG_DUMP_COMPRESSED_DATA
  {
    char *hex = gsk_escape_memory_hex (builder->compressed.data, builder->compressed.len);
    g_message ("  compressed_data=%s", hex);
    g_free (hex);
  }
#endif
#endif

  /* encode index entry */
  index_entry.firstkeys_offset = mmap_writer_offset (&builder->writers[FILE_FIRSTKEYS]);
  index_entry.firstkeys_len = builder->first_key.len;
  index_entry.compressed_data_offset = mmap_writer_offset (&builder->writers[FILE_DATA]);
  index_entry.compressed_data_len = compressed_header_len + builder->compressed.len;
#if DEBUG_INDEX_ENTRIES
  g_message ("writing index entry firstkey offset/len=%llu/%u; compressed %llu/%u", index_entry.firstkeys_offset, index_entry.firstkeys_len, index_entry.compressed_data_offset, index_entry.compressed_data_len);
#endif
  index_entry_serialize (&index_entry, header);

  /* write data to files */
  if (!mmap_writer_write (builder->writers + FILE_INDEX, SIZEOF_INDEX_ENTRY, header, error)
   || !mmap_writer_write (builder->writers + FILE_FIRSTKEYS, builder->first_key.len, builder->first_key.data, error)
   || !mmap_writer_write (builder->writers + FILE_DATA, compressed_header_len, compressed_header, error)
   || !mmap_writer_write (builder->writers + FILE_DATA, builder->compressed.len, builder->compressed.data, error))
    return FALSE;
  return TRUE;
}

/* methods for a file which is being built */
static GskTableFeedEntryResult
flat__feed_entry      (GskTableFile             *file,
                       guint                     key_len,
                       const guint8             *key_data,
                       guint                     value_len,
                       const guint8             *value_data,
                       GError                  **error)
{
  FlatFile *ffile = (FlatFile *) file;
  FlatFactory *ffactory = (FlatFactory *) file->factory;
  FlatFileBuilder *builder = ffile->builder;
  guint8 enc_buf[5+5];
  guint encoded_len, tmp;

  g_assert (builder != NULL);

  file->n_entries++;

  if (builder->has_last_key)
    {
      /* compute prefix length */
      guint prefix_len = 0;
      guint max_prefix_len = MIN (key_len, builder->last_key.len);
      while (prefix_len < max_prefix_len
          && key_data[prefix_len] == builder->last_key.data[prefix_len])
        prefix_len++;

      /* encode prefix_length, and (key_len-prefix_length) */
      encoded_len = uint32_vli_encode (prefix_len, enc_buf);
      tmp = uint32_vli_encode (key_len - prefix_len, enc_buf + encoded_len);
      encoded_len += tmp;
      memcpy (gsk_table_buffer_set_len (&builder->uncompressed, encoded_len),
              enc_buf, encoded_len);

      /* copy non-prefix portion of key */
      memcpy (gsk_table_buffer_append (&builder->uncompressed, key_len - prefix_len),
              key_data + prefix_len, key_len - prefix_len);
    }
  else
    {
      /* the key's length will be in the index file
         (no prefix-compression possible on the first key);
         the key's data will be in the firstkeys file */
      builder->has_last_key = TRUE;
      memcpy (gsk_table_buffer_set_len (&builder->first_key,
                                        key_len), key_data, key_len);
      gsk_table_buffer_set_len (&builder->uncompressed, 0);
    }

  builder->n_compressed_entries++;

  /* encode value length */
  encoded_len = uint32_vli_encode (value_len, enc_buf);
  memcpy (gsk_table_buffer_append (&builder->uncompressed, encoded_len),
          enc_buf, encoded_len);

  /* compress the non-value portion */
  do_compress (builder, builder->uncompressed.len, builder->uncompressed.data);

  /* compress the value portion */
  do_compress (builder, value_len, value_data);

  if (builder->compressed.len >= ffactory->bytes_per_chunk)
    {
      if (!flush_to_files (builder, error))
        return GSK_TABLE_FEED_ENTRY_ERROR;

      reinit_compressor (builder, ffactory->compression_level, TRUE);
      builder->has_last_key = FALSE;
    }
  else
    {
      builder->has_last_key = TRUE;
      memcpy (gsk_table_buffer_set_len (&builder->last_key,
                                        key_len), key_data, key_len);
    }
  return builder->has_last_key ? GSK_TABLE_FEED_ENTRY_WANT_MORE
                               : GSK_TABLE_FEED_ENTRY_SUCCESS;
}

static gboolean 
flat__done_feeding     (GskTableFile             *file,
                        gboolean                 *ready_out,
                        GError                  **error)
{
  FlatFile *ffile = (FlatFile *) file;
  FlatFactory *ffactory = (FlatFactory *) file->factory;
  FlatFileBuilder *builder = ffile->builder;
  guint f;
  if (builder->has_last_key)
    {
      if (!flush_to_files (builder, error))
        return FALSE;
    }

  /* unmap and ftruncate all files */
  for (f = 0; f < N_FILES; f++)
    {
      guint64 offset = mmap_writer_offset (&builder->writers[f]);
      mmap_writer_clear (&builder->writers[f]);
      if (ftruncate (ffile->fds[f], offset) < 0)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN,
                       GSK_ERROR_FILE_TRUNCATE,
                       "error truncating %s file: %s",
                       file_extensions[f], g_strerror (errno));
          return FALSE;
        }
    }

  /* write the number of records to the front */
  {
    guint64 n_entries = file->n_entries;
    guint64 n_entries_le = GUINT64_TO_LE (n_entries);
    int pwrite_rv = pwrite (ffile->fds[FILE_INDEX], &n_entries_le, 8, 0);
    if (pwrite_rv < 0)
      {
        g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_FILE_PWRITE,
                     "pwrite failed writing n_entries: %s",
                     g_strerror (errno));
        return FALSE;
      }
    if (pwrite_rv < 8)
      {
        g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_FILE_PWRITE,
                     "pwrite partial data write???");
        return FALSE;
      }
  }

  /* mmap for reading small files */
  for (f = 0; f < N_FILES; f++)
    if (!mmap_reader_init (&ffile->readers[f], ffile->fds[f], error))
      {
        guint tmp_f;
        for (tmp_f = 0; tmp_f < f; tmp_f++)
          mmap_reader_clear (&ffile->readers[tmp_f]);
        return FALSE;
      }
  ffile->has_readers = TRUE;

  /* recycle/free the builder object */
  ffile->builder = NULL;
  builder_recycle (ffactory, builder);
  *ready_out = TRUE;

  return TRUE;
}

static gboolean 
flat__get_build_state  (GskTableFile             *file,
                        guint                    *state_len_out,
                        guint8                  **state_data_out,
                        GError                  **error)
{
  guint f;
  FlatFile *ffile = (FlatFile *) file;
  FlatFileBuilder *builder = ffile->builder;
  guint8 *data;
  g_assert (builder != NULL);
  *state_len_out = 1 + 3 * 8 + 8;
  data = *state_data_out = g_malloc (*state_len_out);
  data[0] = 0;               /* phase 0; reserved to allow multiphase processing in future */
  for (f = 0; f < N_FILES; f++)
    {
      guint64 offset = mmap_writer_offset (builder->writers + f);
      guint64 offset_le = GUINT64_TO_LE (offset);
      memcpy (data + 8 * f + 1, &offset_le, 8);
    }
  {
    guint64 n_entries = file->n_entries;
    guint64 n_entries_le = GUINT64_TO_LE (n_entries);
    memcpy (data + 1 + 3*8, &n_entries_le, 8);
  }
  return TRUE;
}

static gboolean 
flat__build_file       (GskTableFile             *file,
                        gboolean                 *ready_out,
                        GError                  **error)
{
  *ready_out = TRUE;
  return TRUE;
}

static void     
flat__release_build_data(GskTableFile            *file)
{
  /* nothing to do, since we finish building immediately */
}

/* --- query api --- */
static gboolean
do_pread (FlatFile *ffile,
          WhichFile f,
          guint64   offset,
          guint     length,
          guint8    *ptr_out,
          GError   **error)
{
  if (ffile->builder)
    {
      return mmap_writer_pread (ffile->builder->writers + f,
                                offset, length, ptr_out, error);
    }
  else
    {
      g_assert (ffile->has_readers);
      return mmap_reader_pread (ffile->readers + f,
                                offset, length, ptr_out, error);
    }
}

static gboolean 
flat__query_file       (GskTableFile             *file,
                        GskTableFileQuery        *query_inout,
                        GError                  **error)
{
  FlatFile *ffile = (FlatFile *) file;
  guint64 n_index_records, first, n;
  CacheEntry *cache_entry;
  IndexEntry index_entry;
  gboolean index_entry_up_to_date = FALSE;
  guint8 index_entry_data[SIZEOF_INDEX_ENTRY];
  if (ffile->builder != NULL)
    n_index_records = (mmap_writer_offset (&ffile->builder->writers[FILE_INDEX]) - INDEX_HEADER_SIZE)
                    / SIZEOF_INDEX_ENTRY;
  else if (ffile->has_readers)
    n_index_records = (ffile->readers[FILE_INDEX].file_size - INDEX_HEADER_SIZE)
                    / SIZEOF_INDEX_ENTRY;
  else
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_INVALID_STATE,
                   "flat file in error state");
      return FALSE;
    }

  if (n_index_records == 0)
    {
      query_inout->found = FALSE;
      return TRUE;
    }
  first = 0;
  n = n_index_records;
  GskTableBuffer firstkey;
  gsk_table_buffer_init (&firstkey);
  while (n > 1)
    {
      guint64 mid = first + n / 2;
      gint compare_rv;

      /* read index entry */
      if (!do_pread (ffile, FILE_INDEX, mid * SIZEOF_INDEX_ENTRY + INDEX_HEADER_SIZE, SIZEOF_INDEX_ENTRY, index_entry_data, error))
        {
          gsk_table_buffer_clear (&firstkey);
          return FALSE;
        }
      index_entry_deserialize (index_entry_data, &index_entry);

      /* read key */
      gsk_table_buffer_set_len (&firstkey, index_entry.firstkeys_len);
      if (!do_pread (ffile, FILE_FIRSTKEYS, index_entry.firstkeys_offset, index_entry.firstkeys_len,
                     firstkey.data, error))
        {
          gsk_table_buffer_clear (&firstkey);
          return FALSE;
        }

      /* invoke comparator */
      compare_rv = query_inout->compare (index_entry.firstkeys_len,
                                         firstkey.data,
                                         query_inout->compare_data);

      if (compare_rv < 0)
        {
          n = mid - first;
          index_entry_up_to_date = FALSE;
        }
      else if (compare_rv > 0)
        {
          n = first + n - mid;
          first = mid;
          index_entry_up_to_date = TRUE;
        }
      else
        {
          CacheEntryRecord *record;
          cache_entry = cache_entry_force (ffile, mid,
                                           &index_entry, firstkey.data,
                                           error);
          if (cache_entry == NULL)
            {
              gsk_table_buffer_clear (&firstkey);
              return FALSE;
            }
          record = cache_entry->records + 0;
          memcpy (gsk_table_buffer_set_len (&query_inout->value, record->value_len),
                  record->value_data, record->value_len);
          query_inout->found = TRUE;
          gsk_table_buffer_clear (&firstkey);
          return TRUE;
        }
    }

  if (!index_entry_up_to_date)
    {
      /* read index entry */
      if (!do_pread (ffile, FILE_INDEX, first * SIZEOF_INDEX_ENTRY + INDEX_HEADER_SIZE, SIZEOF_INDEX_ENTRY, index_entry_data, error))
        return FALSE;
      index_entry_deserialize (index_entry_data, &index_entry);

      /* read firstkey */
      gsk_table_buffer_set_len (&firstkey, index_entry.firstkeys_len);
      if (!do_pread (ffile, FILE_FIRSTKEYS, index_entry.firstkeys_offset, index_entry.firstkeys_len,
                     firstkey.data, error))
        {
          gsk_table_buffer_clear (&firstkey);
          return FALSE;
        }
    }

  /* uncompress block, cache */
  cache_entry = cache_entry_force (ffile, first,
                                   &index_entry, firstkey.data,
                                   error);
  if (cache_entry == NULL)
    {
      gsk_table_buffer_clear (&firstkey);
      return FALSE;
    }

  /* bsearch the uncompressed block */
  {
    guint first = 0;
    guint n = cache_entry->n_entries;
    while (n > 1)
      {
        guint mid = first + n / 2;
        CacheEntryRecord *record = cache_entry->records + mid;
        int compare_rv = query_inout->compare (record->key_len, record->key_data,
                                               query_inout->compare_data);
        if (compare_rv < 0)
          {
            n = mid - first;
          }
        else if (compare_rv > 0)
          {
            n = first + n - mid;
            first = mid;
          }
        else
          {
            memcpy (gsk_table_buffer_set_len (&query_inout->value, record->value_len),
                    record->value_data, record->value_len);
            query_inout->found = TRUE;
            return TRUE;
          }
      }
    if (n == 1 && first < cache_entry->n_entries)
      {
        CacheEntryRecord *record = cache_entry->records + first;
        int compare_rv = query_inout->compare (record->key_len, record->key_data,
                                               query_inout->compare_data);
        if (compare_rv == 0)
          {
            memcpy (gsk_table_buffer_set_len (&query_inout->value, record->value_len),
                    record->value_data, record->value_len);
            query_inout->found = TRUE;
            return TRUE;
          }
      }
  }
  query_inout->found = FALSE;
  return TRUE;
}

/* --- reader api --- */
static void
read_and_uncompress_chunk (FlatFileReader *freader)
{
  /* read index fp record, or set eof flag or error */
  guint8 index_data[SIZEOF_INDEX_ENTRY];
  IndexEntry index_entry;
  guint8 *firstkey;
  guint f;

  /* set up state before reading */
  for (f = 0; f < 3; f++)
    freader->chunk_file_offsets[f] = FTELLO (freader->fps[f]);

  if (FREAD (index_data, SIZEOF_INDEX_ENTRY, 1, freader->fps[FILE_INDEX]) != 1)
    {
      freader->base_reader.eof = 1;
      return;
    }
  index_entry_deserialize (index_data, &index_entry);

#if DEBUG_READ_CHUNK
  g_message ("chunk offsets=%llu,%llu,%llu; ie.compressed_len=%u",
             freader->chunk_file_offsets[0],
             freader->chunk_file_offsets[1],
             freader->chunk_file_offsets[2],
             index_entry.compressed_data_len);
#endif

  /* allocate buffers in one piece */
  firstkey = g_malloc (index_entry.firstkeys_len + index_entry.compressed_data_len);
  guint8 *compressed_data;
  compressed_data = firstkey + index_entry.firstkeys_len;

  /* read firstkey */
  if (index_entry.firstkeys_len != 0
    && FREAD (firstkey, index_entry.firstkeys_len, 1, freader->fps[FILE_FIRSTKEYS]) != 1)
    {
      freader->base_reader.error = g_error_new (GSK_G_ERROR_DOMAIN,
                                    GSK_ERROR_PREMATURE_EOF,
                                    "premature eof in firstkey file [firstkey len=%u]", index_entry.firstkeys_len);
      g_free (firstkey);
      return;
    }

  /* read data */
  if (FREAD (compressed_data, index_entry.compressed_data_len, 1, freader->fps[FILE_DATA]) != 1)
    {
      freader->base_reader.error = g_error_new (GSK_G_ERROR_DOMAIN,
                                    GSK_ERROR_PREMATURE_EOF,
                                    "premature eof in compressed-data file [compressed_data_len=%u]",
                                    index_entry.compressed_data_len);
      g_free (firstkey);
      return;
    }

  /* do the actual un-gzipping and scanning */
  freader->cache_entry = cache_entry_deserialize (freader->index_entry_index++,
                                                  index_entry.firstkeys_len, firstkey,
                                                  index_entry.compressed_data_len, compressed_data,
                                                  &freader->base_reader.error);
  g_free (firstkey);
}

static inline void
init_base_reader_record (FlatFileReader *freader)
{
  CacheEntryRecord *record = freader->cache_entry->records + freader->record_index;
  freader->base_reader.key_len = record->key_len;
  freader->base_reader.key_data = record->key_data;
  freader->base_reader.value_len = record->value_len;
  freader->base_reader.value_data = record->value_data;
}

static void
reader_advance (GskTableReader *reader)
{
  FlatFileReader *freader = (FlatFileReader *) reader;
  if (freader->base_reader.eof || freader->base_reader.error)
    return;
  if (++freader->record_index == freader->cache_entry->n_entries)
    {
      g_free (freader->cache_entry);
      freader->cache_entry = NULL;
      read_and_uncompress_chunk (freader);
      if (reader->eof || reader->error != NULL)
        return;
      freader->record_index = 0;
    }
  init_base_reader_record (freader);
}
static void
reader_destroy (GskTableReader *reader)
{
  guint f;
  FlatFileReader *freader = (FlatFileReader *) reader;
  if (freader->cache_entry)
    g_free (freader->cache_entry);
  for (f = 0; f < N_FILES; f++)
    if (freader->fps[f] != NULL)
      fclose (freader->fps[f]);
  g_slice_free (FlatFileReader, freader);
}

static FlatFileReader *
reader_open_fps (GskTableFile *file,
                 const char   *dir,
                 GError      **error)
{
  FlatFileReader *freader = g_slice_new (FlatFileReader);
  guint f;
  freader->base_reader.eof = FALSE;
  freader->base_reader.error = NULL;
  for (f = 0; f < N_FILES; f++)
    {
      char fname_buf[GSK_TABLE_MAX_PATH];
      gsk_table_mk_fname (fname_buf, dir, file->id, file_extensions[f]);
      freader->fps[f] = fopen (fname_buf, "rb");
      if (freader->fps[f] == NULL)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN,
                       GSK_ERROR_FILE_OPEN,
                       "error opening %s for reading: %s",
                       fname_buf, g_strerror (errno));
          g_slice_free (FlatFileReader, freader);
          return NULL;
        }
    }
  freader->base_reader.advance = reader_advance;
  freader->base_reader.destroy = reader_destroy;
  return freader;
}
static FlatFileReader *
reader_open_eof (void)
{
  FlatFileReader *freader = g_slice_new (FlatFileReader);
  guint f;
  freader->base_reader.eof = TRUE;
  freader->base_reader.error = NULL;
  for (f = 0; f < N_FILES; f++)
    freader->fps[f] = NULL;
  freader->base_reader.advance = reader_advance;
  freader->base_reader.destroy = reader_destroy;
  return freader;
}

static GskTableReader *
flat__create_reader    (GskTableFile             *file,
                        const char               *dir,
                        GError                  **error)
{
  FlatFileReader *freader = reader_open_fps (file, dir, error);
  guint64 ief_header;
  if (freader == NULL)
    return NULL;

  if (FREAD (&ief_header, 8, 1, freader->fps[FILE_INDEX]) != 1)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_FILE_READ,
                   "premature eof reading index file header");
      return NULL;
    }

  read_and_uncompress_chunk (freader);
  if (!freader->base_reader.eof && freader->base_reader.error == NULL)
    {
      freader->record_index = 0;
      init_base_reader_record (freader);
    }

  return &freader->base_reader;
}

/* you must always be able to get reader state */
static gboolean 
flat__get_reader_state (GskTableFile             *file,
                        GskTableReader           *reader,
                        guint                    *state_len_out,
                        guint8                  **state_data_out,
                        GError                  **error)
{
  FlatFileReader *freader = (FlatFileReader *) reader;
  guint8 *data;
  guint f;
  /* state is:
       1 byte state -- 0=in progress;  1=eof
     if state==0:
       8 bytes index file offset
       8 bytes firstkeys file offset
       8 bytes data offset
       4 bytes index into the compressed byte to return next
     if state==1:
       no other data
   */
  g_assert (reader->error == NULL);
  if (reader->eof)
    {
      *state_len_out = 1;
      *state_data_out = g_malloc (1);
      (*state_data_out)[0] = 1;
      return TRUE;
    }
  *state_len_out = 1 + 8 + 8 + 8 + 4;
  data = *state_data_out = g_malloc (*state_len_out);
  data[0] = 0;
  for (f = 0; f < N_FILES; f++)
    {
      guint64 tmp64 = freader->chunk_file_offsets[f];
      guint64 tmp_le = GUINT64_TO_LE (tmp64);
      memcpy (data + 1 + 8 * f, &tmp_le, 8);
    }
  {
    guint32 tmp32 = freader->record_index;
    guint32 tmp32_le = GUINT32_TO_LE (tmp32);
    memcpy (data + 1 + 8*3, &tmp32_le, 4);
  }
  return TRUE;
}

static GskTableReader *
flat__recreate_reader  (GskTableFile             *file,
                        const char               *dir,
                        guint                     state_len,
                        const guint8             *state_data,
                        GError                  **error)
{
  FlatFileReader *freader;
  guint f;
  if (freader == NULL)
    return NULL;
  switch (state_data[0])
    {
    case 0:             /* in progress */
      freader = reader_open_fps (file, dir, error);
      if (freader == NULL)
        return NULL;

      /* seek */
      for (f = 0; f < 3; f++)
        {
          guint64 tmp_le, tmp;
          memcpy (&tmp_le, state_data + 1 + 8*f, 8);
          tmp = GUINT64_FROM_LE (tmp_le);
          if (FSEEKO (freader->fps[f], tmp, SEEK_SET) < 0)
            {
              guint tmp_f;
              g_set_error (error, GSK_G_ERROR_DOMAIN,
                           GSK_ERROR_FILE_SEEK,
                           "error seeking %s file: %s",
                           file_extensions[f], g_strerror (errno));
              for (tmp_f = 0; tmp_f < N_FILES; tmp_f++)
                fclose (freader->fps[tmp_f]);
              g_slice_free (FlatFileReader, freader);
              return NULL;
            }
        }

      read_and_uncompress_chunk (freader);

      if (freader->base_reader.eof
       || freader->base_reader.error != NULL)
        {
          if (freader->base_reader.error)
            g_propagate_error (error, freader->base_reader.error);
          else
            g_set_error (error, GSK_G_ERROR_DOMAIN,
                         GSK_ERROR_PREMATURE_EOF,
                         "unexpected eof restoring file reader");
          for (f = 0; f < N_FILES; f++)
            fclose (freader->fps[f]);
          g_slice_free (FlatFileReader, freader);
          return NULL;
        }
      {
        guint32 tmp_le;
        memcpy (&tmp_le, state_data + 1 + 3*8, 4);
        freader->record_index = GUINT32_FROM_LE (tmp_le);
        if (freader->record_index >= freader->cache_entry->n_entries)
          {
            g_set_error (error, GSK_G_ERROR_DOMAIN,
                         GSK_ERROR_PREMATURE_EOF,
                         "record index out-of-bounds in state-data");
            for (f = 0; f < N_FILES; f++)
              fclose (freader->fps[f]);
            g_slice_free (FlatFileReader, freader);
            return NULL;
          }
      }
      init_base_reader_record (freader);

      break;
    case 1:             /* eof */
      g_assert (state_len == 1);
      freader = reader_open_eof ();
      break;
    default:
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_PARSE,
                   "unknown state for reader");
      return NULL;
    }
  return &freader->base_reader;
}


/* destroying files and factories */
static gboolean
flat__destroy_file     (GskTableFile             *file,
                        const char               *dir,
                        gboolean                  erase,
                        GError                  **error)
{
  FlatFactory *ffactory = (FlatFactory *) file->factory;
  FlatFile *ffile = (FlatFile *) file;
  FlatFileBuilder *builder = ffile->builder;
  guint f;
  if (builder != NULL)
    {
      for (f = 0; f < N_FILES; f++)
        mmap_writer_clear (builder->writers + f);
      builder_recycle (ffactory, builder);
    }
  else if (ffile->has_readers)
    {
      for (f = 0; f < N_FILES; f++)
        mmap_reader_clear (ffile->readers + f);
    }
  for (f = 0; f < N_FILES; f++)
    close (ffile->fds[f]);
  if (erase)
    {
      for (f = 0; f < N_FILES; f++)
        {
          char fname_buf[GSK_TABLE_MAX_PATH];
          gsk_table_mk_fname (fname_buf, dir, file->id, file_extensions[f]);
          unlink (fname_buf);
        }
    }
  g_slice_free (FlatFile, ffile);
  return TRUE;
}

static void
flat__destroy_factory  (GskTableFileFactory      *factory)
{
  /* static factory */
}


/* for now, return a static factory object */
GskTableFileFactory *gsk_table_file_factory_new_flat (void)
{
  static FlatFactory the_factory =
    {
      {
        flat__create_file,
        flat__open_building_file,
        flat__open_file,
        flat__feed_entry,
        flat__done_feeding,
        flat__get_build_state,
        flat__build_file,
        flat__release_build_data,
        flat__query_file,
        flat__create_reader,
        flat__get_reader_state,
        flat__recreate_reader,
        flat__destroy_file,
        flat__destroy_factory
      },
      16384,
      3,                        /* zlib compression level */
      0,                        /* n recycled builders */
      8,                        /* max recycled builders */
      NULL,                     /* recycled builder list */
      24                        /* max cache entries */
    };

  return &the_factory.base_factory;
}
