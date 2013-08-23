
#define GSK_TABLE_MMAP_MAX_PAGE_SIZE    4096
#define GSK_TABLE_MMAP_SIZE             (64*1024)

/* --- reader: for read-only, sequential access --- */
struct _GskTableMmapReader
{
  int fd;
  guint64 file_size;
  const guint8 *mmapped;
  guint mmap_size;                  /* number of bytes mmapped */
  guint64 mmapped_offset;           /* offset of 'mmapped' in file */
  guint pos;                        /* offset within 'mmapped' */
};

void               gsk_table_mmap_reader_init (GskTableMmapReader *reader,
                                               int                 fd,
                                               guint64             file_size);
G_INLINE_FUNC gboolean gsk_table_mmap_reader_peek (GskTableMmapReader *reader,
                                                   guint               size,
                                                   const guint8      **out,
                                                   GByteArray         *pad,
                                                   GError            **error);
/* advance reader to the first page that contains the next byte to read. */
gboolean               gsk_table_mmap_reader_advance(GskTableMmapReader *reader,
                                                     GError            **error);

/* used when peek cannot be fulfilled directly. */ 
gboolean               gsk_table_mmap_reader_big_peek(GskTableMmapReader *reader,
                                                      guint               size,
                                                      const guint8      **out,
                                                      GByteArray         *pad,
                                                      GError            **error);

void                   gsk_table_mmap_reader_close   (GskTableMmapReader *reader);


/* --- writer: for write-only, sequential access --- */
struct _GskTableMmapWriter
{
  int fd;
  guint8 *mmapped;
  guint64 file_size;                /* currently allocated bytes of file */
  guint mmap_size;                  /* number of bytes mmapped */
  guint64 mmapped_offset;           /* offset of 'mmapped' in file */
  guint pos;                        /* offset within 'mmapped' */
};

gboolean           gsk_table_mmap_writer_init  (GskTableMmapWriter *writer,
                                                int                 fd,
                                                GError            **error);
gboolean           gsk_table_mmap_writer_init_at(GskTableMmapWriter *writer,
                                                int                 fd,
                                                guint64             offset,
                                                GError            **error);
G_INLINE_FUNC gboolean gsk_table_mmap_writer_write(GskTableMmapWriter *writer,
                                                   guint               len,
                                                   const guint8       *data,
                                                   GError            **error);
G_INLINE_FUNC gboolean gsk_table_mmap_writer_pwrite(GskTableMmapWriter *writer,
                                                    guint64            offset,
                                                    guint               len,
                                                    const guint8       *data,
                                                    GError            **error);

/* advance reader to the first page that contains the next byte to read. */
gboolean               gsk_table_mmap_writer_advance(GskTableMmapWriter *writer,
                                                     GError            **error);

#define gsk_table_mmap_writer_offset(writer) \
  ((writer)->mmapped_offset + (writer)->pos)
void                   gsk_table_mmap_writer_close   (GskTableMmapWriter *writer);


/* --- buffer/buffer: reads and writes in a sequential fashion --- */
struct _GskTableMmapBuffer
{
  int fd;

  guint8 *read_area;
  guint read_pos;
  guint64 read_area_offset;

  guint8 *write_area;
  guint write_pos;
  guint64 write_area_offset;

  GByteArray *read_buffer;

  /* NOTE: if write_area_offset==read_area_offset then read_area==write_area.
     Otherwise, the write and read areas are disjoint. */

  guint mmap_size;                  /* number of bytes mmapped per area */
};

void               gsk_table_mmap_buffer_init  (GskTableMmapBuffer *buffer,
                                                int                 fd);

/* *data_out will be filled with a reference to the data internal
   to the buffer.  it must NOT be freed by caller.
   It is only good until gsk_table_mmap_buffer_read is called again.

   NOTE: we depend on the fact that it IS valid
   even if you call gsk_table_mmap_buffer_write()! */
gboolean           gsk_table_mmap_buffer_read  (GskTableMmapBuffer *buffer,
                                                guint               size,
                                                const guint8      **data_out);
void               gsk_table_mmap_buffer_write (GskTableMmapBuffer *buffer,
                                                guint               len,
                                                const guint8       *data);
void               gsk_table_mmap_buffer_close (GskTableMmapBuffer *buffer);



/* inlines */
#if defined (G_CAN_INLINE) || defined (GSK_INTERNAL_IMPLEMENT_INLINES)
G_INLINE_FUNC gboolean gsk_table_mmap_reader_peek (GskTableMmapReader *reader,
                                                   guint               size,
                                                   const guint8      **out,
                                                   GByteArray         *pad,
                                                   GError            **error)
{
  if (G_LIKELY (size + reader->pos <= reader->mmap_size))
    {
      *out = reader->mmapped + reader->pos;
      reader->pos += size;
      return TRUE;
    }
  else if (size + GSK_TABLE_MMAP_MAX_PAGE_SIZE < reader->mmap_size)
    {
      if (!gsk_table_mmap_reader_advance (reader, error))
        return FALSE;
      g_assert (reader->mmapped_offset + reader->pos + size > reader->file_size);
      g_assert (size + reader->pos <= reader->mmap_size);
      *out = reader->mmapped + reader->pos;
      reader->pos += size;
      return TRUE;
    }
  else
    return gsk_table_mmap_reader_big_peek (reader, size, out, pad);
}
G_INLINE_FUNC gboolean gsk_table_mmap_writer_write(GskTableMmapWriter *writer,
                                                   guint               len,
                                                   const guint8       *data,
                                                   GError            **error)
{
  while (G_UNLIKELY (len > writer->mmap_size - writer->pos))
    {
      guint rem = writer->mmap_size - writer->pos;
      memcpy (writer->mmapped + writer->pos, data, rem);
      data += rem;
      len -= rem;
      writer->pos += rem;
      if (!gsk_table_mmap_writer_advance (writer, error))
        return FALSE;
    }
  memcpy (writer->mmapped + writer->pos, data, len);
  writer->pos += len;
  return TRUE;
}
#endif
