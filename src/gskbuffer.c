/* GSK - a library to write servers
    Copyright (C) 1999-2000 Dave Benson

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

/* Free blocks to hold around to avoid repeated mallocs... */
#define MAX_RECYCLED		16

/* Size of allocations to make. */
#define BUF_CHUNK_SIZE		32768

/* Max fragments in the iovector to writev. */
#define MAX_FRAGMENTS_TO_WRITE	16

/* This causes fragments not to be transferred from buffer to buffer,
 * and not to be allocated in pools.  The result is that stack-trace
 * based debug-allocators work much better with this on.
 *
 * On the other hand, this can mask over some abuses (eg stack-based
 * foreign buffer fragment bugs) so we disable it by default.
 */ 
#define GSK_DEBUG_BUFFER_ALLOCATIONS	0

#include "config.h"
#include "gskmacros.h"
#include <sys/types.h>
#if HAVE_WRITEV
#include <sys/uio.h>
#endif
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "gskbuffer.h"
#include "gskerrno.h"

/* --- GskBufferFragment implementation --- */
static inline int 
gsk_buffer_fragment_avail (GskBufferFragment *frag)
{
  return frag->buf_max_size - frag->buf_start - frag->buf_length;
}
static inline char *
gsk_buffer_fragment_start (GskBufferFragment *frag)
{
  return frag->buf + frag->buf_start;
}
static inline char *
gsk_buffer_fragment_end (GskBufferFragment *frag)
{
  return frag->buf + frag->buf_start + frag->buf_length;
}

/* --- GskBufferFragment recycling --- */
#if !GSK_DEBUG_BUFFER_ALLOCATIONS
static int num_recycled = 0;
static GskBufferFragment* recycling_stack = 0;
G_LOCK_DEFINE_STATIC (recycling_stack);

#endif

static GskBufferFragment *
new_native_fragment()
{
  GskBufferFragment *frag;
#if GSK_DEBUG_BUFFER_ALLOCATIONS
  frag = (GskBufferFragment *) g_malloc (BUF_CHUNK_SIZE);
  frag->buf_max_size = BUF_CHUNK_SIZE - sizeof (GskBufferFragment);
#else  /* optimized (?) */
  G_LOCK (recycling_stack);
  if (recycling_stack)
    {
      frag = recycling_stack;
      recycling_stack = recycling_stack->next;
      num_recycled--;
      G_UNLOCK (recycling_stack);
    }
  else
    {
      G_UNLOCK (recycling_stack);
      frag = (GskBufferFragment *) g_malloc (BUF_CHUNK_SIZE);
      frag->buf_max_size = BUF_CHUNK_SIZE - sizeof (GskBufferFragment);
    }
#endif	/* !GSK_DEBUG_BUFFER_ALLOCATIONS */
  frag->buf_start = frag->buf_length = 0;
  frag->next = 0;
  frag->buf = (char *) (frag + 1);
  frag->is_foreign = 0;
  return frag;
}

static GskBufferFragment *
new_foreign_fragment (gconstpointer        ptr,
		      int                  length,
		      GDestroyNotify       destroy,
		      gpointer             ddata)
{
  GskBufferFragment *fragment;
  fragment = g_slice_new (GskBufferFragment);
  fragment->is_foreign = 1;
  fragment->buf_start = 0;
  fragment->buf_length = length;
  fragment->buf_max_size = length;
  fragment->next = NULL;
  fragment->buf = (char *) ptr;
  fragment->destroy = destroy;
  fragment->destroy_data = ddata;
  return fragment;
}

#if GSK_DEBUG_BUFFER_ALLOCATIONS
#define recycle(frag) G_STMT_START{ \
    if (frag->is_foreign) { \
      { if (frag->destroy) frag->destroy (frag->destroy_data); \
        g_slice_free(GskBufferFragment, frag); } \
    else g_free (frag); \
   }G_STMT_END
#else	/* optimized (?) */
static void
recycle(GskBufferFragment* frag)
{
  if (frag->is_foreign)
    {
      if (frag->destroy)
        frag->destroy (frag->destroy_data);
      g_slice_free (GskBufferFragment, frag);
      return;
    }
  G_LOCK (recycling_stack);
#if defined(MAX_RECYCLED)
  if (num_recycled >= MAX_RECYCLED)
    {
      g_free (frag);
      G_UNLOCK (recycling_stack);
      return;
    }
#endif
  frag->next = recycling_stack;
  recycling_stack = frag;
  num_recycled++;
  G_UNLOCK (recycling_stack);
}
#endif	/* !GSK_DEBUG_BUFFER_ALLOCATIONS */

/* --- Global public methods --- */
/**
 * gsk_buffer_cleanup_recycling_bin:
 * 
 * Free unused buffer fragments.  (Normally some are
 * kept around to reduce strain on the global allocator.)
 */
void
gsk_buffer_cleanup_recycling_bin ()
{
#if !GSK_DEBUG_BUFFER_ALLOCATIONS
  G_LOCK (recycling_stack);
  while (recycling_stack != NULL)
    {
      GskBufferFragment *next;
      next = recycling_stack->next;
      g_free (recycling_stack);
      recycling_stack = next;
    }
  num_recycled = 0;
  G_UNLOCK (recycling_stack);
#endif
}
      
/* --- Public methods --- */
/**
 * gsk_buffer_construct:
 * @buffer: buffer to initialize (as empty).
 *
 * Construct an empty buffer out of raw memory.
 * (This is equivalent to filling the buffer with 0s)
 */
void
gsk_buffer_construct(GskBuffer *buffer)
{
  buffer->first_frag = buffer->last_frag = NULL;
  buffer->size = 0;
}

#if defined(GSK_DEBUG) || GSK_DEBUG_BUFFER_ALLOCATIONS
static inline gboolean
verify_buffer (const GskBuffer *buffer)
{
  const GskBufferFragment *frag;
  guint total = 0;
  for (frag = buffer->first_frag; frag != NULL; frag = frag->next)
    total += frag->buf_length;
  return total == buffer->size;
}
#define CHECK_INTEGRITY(buffer)	g_assert (verify_buffer (buffer))
#else
#define CHECK_INTEGRITY(buffer)
#endif

/**
 * gsk_buffer_append:
 * @buffer: the buffer to add data to.  Data is put at the end of the buffer.
 * @data: binary data to add to the buffer.
 * @length: length of @data to add to the buffer.
 *
 * Append data into the buffer.
 */
void
gsk_buffer_append(GskBuffer    *buffer,
                  gconstpointer data,
		  guint         length)
{
  CHECK_INTEGRITY (buffer);
  buffer->size += length;
  while (length > 0)
    {
      guint avail;
      if (!buffer->last_frag)
	{
	  buffer->last_frag = buffer->first_frag = new_native_fragment ();
	  avail = gsk_buffer_fragment_avail (buffer->last_frag);
	}
      else
	{
	  avail = gsk_buffer_fragment_avail (buffer->last_frag);
	  if (avail <= 0)
	    {
	      buffer->last_frag->next = new_native_fragment ();
	      avail = gsk_buffer_fragment_avail (buffer->last_frag);
	      buffer->last_frag = buffer->last_frag->next;
	    }
	}
      if (avail > length)
	avail = length;
      memcpy (gsk_buffer_fragment_end (buffer->last_frag), data, avail);
      data = (const char *) data + avail;
      length -= avail;
      buffer->last_frag->buf_length += avail;
    }
  CHECK_INTEGRITY (buffer);
}

void
gsk_buffer_append_repeated_char (GskBuffer    *buffer, 
                                 char          character,
                                 gsize         count)
{
  CHECK_INTEGRITY (buffer);
  buffer->size += count;
  while (count > 0)
    {
      guint avail;
      if (!buffer->last_frag)
	{
	  buffer->last_frag = buffer->first_frag = new_native_fragment ();
	  avail = gsk_buffer_fragment_avail (buffer->last_frag);
	}
      else
	{
	  avail = gsk_buffer_fragment_avail (buffer->last_frag);
	  if (avail <= 0)
	    {
	      buffer->last_frag->next = new_native_fragment ();
	      avail = gsk_buffer_fragment_avail (buffer->last_frag);
	      buffer->last_frag = buffer->last_frag->next;
	    }
	}
      if (avail > count)
	avail = count;
      memset (gsk_buffer_fragment_end (buffer->last_frag), character, avail);
      count -= avail;
      buffer->last_frag->buf_length += avail;
    }
  CHECK_INTEGRITY (buffer);
}

#if 0
void
gsk_buffer_append_repeated_data (GskBuffer    *buffer, 
                                 gconstpointer data_to_repeat,
                                 gsize         data_length,
                                 gsize         count)
{
  ...
}
#endif

/**
 * gsk_buffer_append_string:
 * @buffer: the buffer to add data to.  Data is put at the end of the buffer.
 * @string: NUL-terminated string to append to the buffer.
 *  The NUL is not appended.
 *
 * Append a string to the buffer.
 */
void
gsk_buffer_append_string(GskBuffer  *buffer,
                         const char *string)
{
  g_return_if_fail (string != NULL);
  gsk_buffer_append (buffer, string, strlen (string));
}

/**
 * gsk_buffer_append_char:
 * @buffer: the buffer to add the byte to.
 * @character: the byte to add to the buffer.
 *
 * Append a byte to a buffer.
 */
void
gsk_buffer_append_char(GskBuffer *buffer,
		       char       character)
{
  gsk_buffer_append (buffer, &character, 1);
}

/**
 * gsk_buffer_append_string0:
 * @buffer: the buffer to add data to.  Data is put at the end of the buffer.
 * @string: NUL-terminated string to append to the buffer;
 *  NUL is appended.
 *
 * Append a NUL-terminated string to the buffer.  The NUL is appended.
 */
void
gsk_buffer_append_string0      (GskBuffer    *buffer,
				const char   *string)
{
  gsk_buffer_append (buffer, string, strlen (string) + 1);
}

/**
 * gsk_buffer_read:
 * @buffer: the buffer to read data from.
 * @data: buffer to fill with up to @max_length bytes of data.
 * @max_length: maximum number of bytes to read.
 *
 * Removes up to @max_length data from the beginning of the buffer,
 * and writes it to @data.  The number of bytes actually read
 * is returned.
 *
 * returns: number of bytes transferred.
 */
guint
gsk_buffer_read(GskBuffer    *buffer,
                gpointer      data,
		guint         max_length)
{
  guint rv = 0;
  guint orig_max_length = max_length;
  CHECK_INTEGRITY (buffer);
  while (max_length > 0 && buffer->first_frag)
    {
      GskBufferFragment *first = buffer->first_frag;
      if (first->buf_length <= max_length)
	{
	  memcpy (data, gsk_buffer_fragment_start (first), first->buf_length);
	  rv += first->buf_length;
	  data = (char *) data + first->buf_length;
	  max_length -= first->buf_length;
	  buffer->first_frag = first->next;
	  if (!buffer->first_frag)
	    buffer->last_frag = NULL;
	  recycle (first);
	}
      else
	{
	  memcpy (data, gsk_buffer_fragment_start (first), max_length);
	  rv += max_length;
	  first->buf_length -= max_length;
	  first->buf_start += max_length;
	  data = (char *) data + max_length;
	  max_length = 0;
	}
    }
  buffer->size -= rv;
  g_assert (rv == orig_max_length || buffer->size == 0);
  CHECK_INTEGRITY (buffer);
  return rv;
}

/**
 * gsk_buffer_peek:
 * @buffer: the buffer to peek data from the front of.
 *    This buffer is unchanged by the operation.
 * @data: buffer to fill with up to @max_length bytes of data.
 * @max_length: maximum number of bytes to peek.
 *
 * Copies up to @max_length data from the beginning of the buffer,
 * and writes it to @data.  The number of bytes actually copied
 * is returned.
 *
 * This function is just like gsk_buffer_read() except that the 
 * data is not removed from the buffer.
 *
 * returns: number of bytes copied into data.
 */
guint
gsk_buffer_peek     (const GskBuffer *buffer,
                     gpointer         data,
		     guint            max_length)
{
  int rv = 0;
  GskBufferFragment *frag = (GskBufferFragment *) buffer->first_frag;
  CHECK_INTEGRITY (buffer);
  while (max_length > 0 && frag)
    {
      if (frag->buf_length <= max_length)
	{
	  memcpy (data, gsk_buffer_fragment_start (frag), frag->buf_length);
	  rv += frag->buf_length;
	  data = (char *) data + frag->buf_length;
	  max_length -= frag->buf_length;
	  frag = frag->next;
	}
      else
	{
	  memcpy (data, gsk_buffer_fragment_start (frag), max_length);
	  rv += max_length;
	  data = (char *) data + max_length;
	  max_length = 0;
	}
    }
  return rv;
}

/**
 * gsk_buffer_read_line:
 * @buffer: buffer to read a line from.
 *
 * Parse a newline (\n) terminated line from
 * buffer and return it as a newly allocated string.
 * The newline is changed to a NUL character.
 *
 * If the buffer does not contain a newline, then NULL is returned.
 *
 * returns: a newly allocated NUL-terminated string, or NULL.
 */
char *
gsk_buffer_read_line(GskBuffer *buffer)
{
  int len = 0;
  char *rv;
  GskBufferFragment *at;
  int newline_length;
  CHECK_INTEGRITY (buffer);
  for (at = buffer->first_frag; at; at = at->next)
    {
      char *start = gsk_buffer_fragment_start (at);
      char *got;
      got = memchr (start, '\n', at->buf_length);
      if (got)
	{
	  len += got - start;
	  break;
	}
      len += at->buf_length;
    }
  if (at == NULL)
    return NULL;
  rv = g_new (char, len + 1);
  /* If we found a newline, read it out, truncating
   * it with NUL before we return from the function... */
  if (at)
    newline_length = 1;
  else
    newline_length = 0;
  gsk_buffer_read (buffer, rv, len + newline_length);
  rv[len] = 0;
  CHECK_INTEGRITY (buffer);
  return rv;
}

/**
 * gsk_buffer_parse_string0:
 * @buffer: buffer to read a line from.
 *
 * Parse a NUL-terminated line from
 * buffer and return it as a newly allocated string.
 *
 * If the buffer does not contain a newline, then NULL is returned.
 *
 * returns: a newly allocated NUL-terminated string, or NULL.
 */
char *
gsk_buffer_parse_string0(GskBuffer *buffer)
{
  int index0 = gsk_buffer_index_of (buffer, '\0');
  char *rv;
  if (index0 < 0)
    return NULL;
  rv = g_new (char, index0 + 1);
  gsk_buffer_read (buffer, rv, index0 + 1);
  return rv;
}

/**
 * gsk_buffer_peek_char:
 * @buffer: buffer to peek a single byte from.
 *
 * Get the first byte in the buffer as a positive or 0 number.
 * If the buffer is empty, -1 is returned.
 * The buffer is unchanged.
 *
 * returns: an unsigned character or -1.
 */
int
gsk_buffer_peek_char(const GskBuffer *buffer)
{
  const GskBufferFragment *frag;

  if (buffer->size == 0)
    return -1;

  for (frag = buffer->first_frag; frag; frag = frag->next)
    if (frag->buf_length > 0)
      break;
  return * (const unsigned char *) (gsk_buffer_fragment_start ((GskBufferFragment*)frag));
}

/**
 * gsk_buffer_read_char:
 * @buffer: buffer to read a single byte from.
 *
 * Get the first byte in the buffer as a positive or 0 number,
 * and remove the character from the buffer.
 * If the buffer is empty, -1 is returned.
 *
 * returns: an unsigned character or -1.
 */
int
gsk_buffer_read_char (GskBuffer *buffer)
{
  char c;
  if (gsk_buffer_read (buffer, &c, 1) == 0)
    return -1;
  return (int) (guint8) c;
}

/**
 * gsk_buffer_discard:
 * @buffer: the buffer to discard data from.
 * @max_discard: maximum number of bytes to discard.
 *
 * Removes up to @max_discard data from the beginning of the buffer,
 * and returns the number of bytes actually discarded.
 *
 * returns: number of bytes discarded.
 */
int
gsk_buffer_discard(GskBuffer *buffer,
                   guint      max_discard)
{
  int rv = 0;
  CHECK_INTEGRITY (buffer);
  while (max_discard > 0 && buffer->first_frag)
    {
      GskBufferFragment *first = buffer->first_frag;
      if (first->buf_length <= max_discard)
	{
	  rv += first->buf_length;
	  max_discard -= first->buf_length;
	  buffer->first_frag = first->next;
	  if (!buffer->first_frag)
	    buffer->last_frag = NULL;
	  recycle (first);
	}
      else
	{
	  rv += max_discard;
	  first->buf_length -= max_discard;
	  first->buf_start += max_discard;
	  max_discard = 0;
	}
    }
  buffer->size -= rv;
  CHECK_INTEGRITY (buffer);
  return rv;
}

/**
 * gsk_buffer_writev:
 * @read_from: buffer to take data from.
 * @fd: file-descriptor to write data to.
 *
 * Writes as much data as possible to the
 * given file-descriptor using the writev(2)
 * function to deal with multiple fragments
 * efficiently, where available.
 *
 * returns: the number of bytes transferred,
 * or -1 on a write error (consult errno).
 */
int
gsk_buffer_writev (GskBuffer       *read_from,
		   int              fd)
{
  int rv;
  struct iovec *iov;
  int nfrag, i;
  GskBufferFragment *frag_at = read_from->first_frag;
  CHECK_INTEGRITY (read_from);
  for (nfrag = 0; frag_at != NULL
#ifdef MAX_FRAGMENTS_TO_WRITE
       && nfrag < MAX_FRAGMENTS_TO_WRITE
#endif
       ; nfrag++)
    frag_at = frag_at->next;
  iov = (struct iovec *) alloca (sizeof (struct iovec) * nfrag);
  frag_at = read_from->first_frag;
  for (i = 0; i < nfrag; i++)
    {
      iov[i].iov_len = frag_at->buf_length;
      iov[i].iov_base = gsk_buffer_fragment_start (frag_at);
      frag_at = frag_at->next;
    }
  rv = writev (fd, iov, nfrag);
  if (rv < 0 && gsk_errno_is_ignorable (errno))
    return 0;
  if (rv <= 0)
    return rv;
  gsk_buffer_discard (read_from, rv);
  return rv;
}

/**
 * gsk_buffer_writev_len:
 * @read_from: buffer to take data from.
 * @fd: file-descriptor to write data to.
 * @max_bytes: maximum number of bytes to write.
 *
 * Writes up to max_bytes bytes to the
 * given file-descriptor using the writev(2)
 * function to deal with multiple fragments
 * efficiently, where available.
 *
 * returns: the number of bytes transferred,
 * or -1 on a write error (consult errno).
 */
int
gsk_buffer_writev_len (GskBuffer *read_from,
		       int        fd,
		       guint      max_bytes)
{
  int rv;
  struct iovec *iov;
  int nfrag, i;
  guint bytes;
  GskBufferFragment *frag_at = read_from->first_frag;
  CHECK_INTEGRITY (read_from);
  for (nfrag = 0, bytes = 0; frag_at != NULL && bytes < max_bytes
#ifdef MAX_FRAGMENTS_TO_WRITE
       && nfrag < MAX_FRAGMENTS_TO_WRITE
#endif
       ; nfrag++)
    {
      bytes += frag_at->buf_length;
      frag_at = frag_at->next;
    }
  iov = (struct iovec *) alloca (sizeof (struct iovec) * nfrag);
  frag_at = read_from->first_frag;
  for (bytes = max_bytes, i = 0; i < nfrag && bytes > 0; i++)
    {
      guint frag_bytes = MIN (frag_at->buf_length, bytes);
      iov[i].iov_len = frag_bytes;
      iov[i].iov_base = gsk_buffer_fragment_start (frag_at);
      frag_at = frag_at->next;
      bytes -= frag_bytes;
    }
  rv = writev (fd, iov, i);
  if (rv < 0 && gsk_errno_is_ignorable (errno))
    return 0;
  if (rv <= 0)
    return rv;
  gsk_buffer_discard (read_from, rv);
  return rv;
}

/**
 * gsk_buffer_read_in_fd:
 * @write_to: buffer to append data to.
 * @read_from: file-descriptor to read data from.
 *
 * Append data into the buffer directly from the
 * given file-descriptor.
 *
 * returns: the number of bytes transferred,
 * or -1 on a read error (consult errno).
 */
/* TODO: zero-copy! */
int
gsk_buffer_read_in_fd(GskBuffer *write_to,
                      int        read_from)
{
  char buf[8192];
  int rv = read (read_from, buf, sizeof (buf));
  if (rv < 0)
    return rv;
  gsk_buffer_append (write_to, buf, rv);
  return rv;
}

/**
 * gsk_buffer_destruct:
 * @to_destroy: the buffer to empty.
 *
 * Remove all fragments from a buffer, leaving it empty.
 * The buffer is guaranteed to not to be consuming any resources,
 * but it also is allowed to start using it again.
 */
void
gsk_buffer_destruct(GskBuffer *to_destroy)
{
  GskBufferFragment *at = to_destroy->first_frag;
  CHECK_INTEGRITY (to_destroy);
  while (at)
    {
      GskBufferFragment *next = at->next;
      recycle (at);
      at = next;
    }
  to_destroy->first_frag = to_destroy->last_frag = NULL;
  to_destroy->size = 0;
}

/**
 * gsk_buffer_index_of:
 * @buffer: buffer to scan.
 * @char_to_find: a byte to look for.
 *
 * Scans for the first instance of the given character.
 * returns: its index in the buffer, or -1 if the character
 * is not in the buffer.
 */
int
gsk_buffer_index_of(GskBuffer *buffer,
                    char       char_to_find)
{
  GskBufferFragment *at = buffer->first_frag;
  int rv = 0;
  while (at)
    {
      char *start = gsk_buffer_fragment_start (at);
      char *saught = memchr (start, char_to_find, at->buf_length);
      if (saught)
	return (saught - start) + rv;
      else
	rv += at->buf_length;
      at = at->next;
    }
  return -1;
}

/**
 * gsk_buffer_str_index_of:
 * @buffer: buffer to scan.
 * @str_to_find: a string to look for.
 *
 * Scans for the first instance of the given string.
 * returns: its index in the buffer, or -1 if the string
 * is not in the buffer.
 */
int 
gsk_buffer_str_index_of (GskBuffer *buffer,
                         const char *str_to_find)
{
  GskBufferFragment *frag = buffer->first_frag;
  guint rv = 0;
  for (frag = buffer->first_frag; frag; frag = frag->next)
    {
      const char *frag_at = frag->buf + frag->buf_start;
      guint frag_rem = frag->buf_length;
      while (frag_rem > 0)
        {
          GskBufferFragment *subfrag;
          const char *subfrag_at;
          guint subfrag_rem;
          const char *str_at;
          if (G_LIKELY (*frag_at != str_to_find[0]))
            {
              frag_at++;
              frag_rem--;
              rv++;
              continue;
            }
          subfrag = frag;
          subfrag_at = frag_at + 1;
          subfrag_rem = frag_rem - 1;
          str_at = str_to_find + 1;
          if (*str_at == '\0')
            return rv;
          while (subfrag != NULL)
            {
              while (subfrag_rem == 0)
                {
                  subfrag = subfrag->next;
                  if (subfrag == NULL)
                    goto bad_guess;
                  subfrag_at = subfrag->buf + subfrag->buf_start;
                  subfrag_rem = subfrag->buf_length;
                }
              while (*str_at != '\0' && subfrag_rem != 0)
                {
                  if (*str_at++ != *subfrag_at++)
                    goto bad_guess;
                  subfrag_rem--;
                }
              if (*str_at == '\0')
                return rv;
            }
bad_guess:
          frag_at++;
          frag_rem--;
          rv++;
        }
    }
  return -1;
}

/**
 * gsk_buffer_drain:
 * @dst: buffer to add to.
 * @src: buffer to remove from.
 *
 * Transfer all data from @src to @dst,
 * leaving @src empty.
 *
 * returns: the number of bytes transferred.
 */
#if GSK_DEBUG_BUFFER_ALLOCATIONS
guint
gsk_buffer_drain (GskBuffer *dst,
		  GskBuffer *src)
{
  guint rv = src->size;
  GskBufferFragment *frag;
  CHECK_INTEGRITY (dst);
  CHECK_INTEGRITY (src);
  for (frag = src->first_frag; frag; frag = frag->next)
    gsk_buffer_append (dst,
                       gsk_buffer_fragment_start (frag),
                       frag->buf_length);
  gsk_buffer_discard (src, src->size);
  CHECK_INTEGRITY (dst);
  CHECK_INTEGRITY (src);
  return rv;
}
#else	/* optimized */
guint
gsk_buffer_drain (GskBuffer *dst,
		  GskBuffer *src)
{
  guint rv = src->size;

  CHECK_INTEGRITY (dst);
  CHECK_INTEGRITY (src);
  if (src->first_frag == NULL)
    return rv;

  dst->size += src->size;

  if (dst->last_frag != NULL)
    {
      dst->last_frag->next = src->first_frag;
      dst->last_frag = src->last_frag;
    }
  else
    {
      dst->first_frag = src->first_frag;
      dst->last_frag = src->last_frag;
    }
  src->size = 0;
  src->first_frag = src->last_frag = NULL;
  CHECK_INTEGRITY (dst);
  return rv;
}
#endif

/**
 * gsk_buffer_transfer:
 * @dst: place to copy data into.
 * @src: place to read data from.
 * @max_transfer: maximum number of bytes to transfer.
 *
 * Transfer data out of @src and into @dst.
 * Data is removed from @src.  The number of bytes
 * transferred is returned.
 *
 * returns: the number of bytes transferred.
 */
#if GSK_DEBUG_BUFFER_ALLOCATIONS
guint
gsk_buffer_transfer(GskBuffer *dst,
		    GskBuffer *src,
		    guint max_transfer)
{
  guint rv = 0;
  GskBufferFragment *frag;
  CHECK_INTEGRITY (dst);
  CHECK_INTEGRITY (src);
  for (frag = src->first_frag; frag && max_transfer > 0; frag = frag->next)
    {
      guint len = frag->buf_length;
      if (len >= max_transfer)
        {
          gsk_buffer_append (dst, gsk_buffer_fragment_start (frag), max_transfer);
          rv += max_transfer;
          break;
        }
      else
        {
          gsk_buffer_append (dst, gsk_buffer_fragment_start (frag), len);
          rv += len;
          max_transfer -= len;
        }
    }
  gsk_buffer_discard (src, rv);
  CHECK_INTEGRITY (dst);
  CHECK_INTEGRITY (src);
  return rv;
}
#else	/* optimized */
guint
gsk_buffer_transfer(GskBuffer *dst,
		    GskBuffer *src,
		    guint max_transfer)
{
  guint rv = 0;
  CHECK_INTEGRITY (dst);
  CHECK_INTEGRITY (src);
  while (src->first_frag && max_transfer >= src->first_frag->buf_length)
    {
      GskBufferFragment *frag = src->first_frag;
      src->first_frag = frag->next;
      frag->next = NULL;
      if (src->first_frag == NULL)
	src->last_frag = NULL;

      if (dst->last_frag)
	dst->last_frag->next = frag;
      else
	dst->first_frag = frag;
      dst->last_frag = frag;

      rv += frag->buf_length;
      max_transfer -= frag->buf_length;
    }
  dst->size += rv;
  if (src->first_frag && max_transfer)
    {
      GskBufferFragment *frag = src->first_frag;
      gsk_buffer_append (dst, gsk_buffer_fragment_start (frag), max_transfer);
      frag->buf_start += max_transfer;
      frag->buf_length -= max_transfer;
      rv += max_transfer;
    }
  src->size -= rv;
  CHECK_INTEGRITY (dst);
  CHECK_INTEGRITY (src);
  return rv;
}
#endif	/* !GSK_DEBUG_BUFFER_ALLOCATIONS */

/* --- foreign data --- */
/**
 * gsk_buffer_append_foreign:
 * @buffer: the buffer to append into.
 * @data: the data to append.
 * @length: length of @data.
 * @destroy: optional method to call when the data is no longer needed.
 * @destroy_data: the argument to the destroy method.
 *
 * This function allows data to be placed in a buffer without
 * copying.  It is the callers' responsibility to ensure that
 * @data will remain valid until the destroy method is called.
 * @destroy may be omitted if @data is permanent, for example,
 * if appended a static string into a buffer.
 */
void gsk_buffer_append_foreign (GskBuffer        *buffer,
                                gconstpointer     data,
				int               length,
				GDestroyNotify    destroy,
				gpointer          destroy_data)
{
  GskBufferFragment *fragment;

  CHECK_INTEGRITY (buffer);

  fragment = new_foreign_fragment (data, length, destroy, destroy_data);
  fragment->next = NULL;

  if (buffer->last_frag == NULL)
    buffer->first_frag = fragment;
  else
    buffer->last_frag->next = fragment;

  buffer->last_frag = fragment;
  buffer->size += length;

  CHECK_INTEGRITY (buffer);
}

/**
 * gsk_buffer_printf:
 * @buffer: the buffer to append to.
 * @format: printf-style format string describing what to append to buffer.
 * @Varargs: values referenced by @format string.
 *
 * Append printf-style content to a buffer.
 */
void     gsk_buffer_printf              (GskBuffer    *buffer,
					 const char   *format,
					 ...)
{
  va_list args;
  va_start (args, format);
  gsk_buffer_vprintf (buffer, format, args);
  va_end (args);
}

/**
 * gsk_buffer_vprintf:
 * @buffer: the buffer to append to.
 * @format: printf-style format string describing what to append to buffer.
 * @args: values referenced by @format string.
 *
 * Append printf-style content to a buffer, given a va_list.
 */
void     gsk_buffer_vprintf             (GskBuffer    *buffer,
					 const char   *format,
					 va_list       args)
{
  va_list args_copy;
  gsize size;
  
  G_VA_COPY (args_copy, args);
  size = g_printf_string_upper_bound (format, args_copy);
  va_end (args_copy);

  if (size < 1024)
    {
      char buf[1024];
      g_vsnprintf (buf, sizeof (buf), format, args);
      gsk_buffer_append_string (buffer, buf);
    }
  else
    {
      char *buf = g_strdup_vprintf (format, args);
      gsk_buffer_append_foreign (buffer, buf, strlen (buf), g_free, buf);
    }
}

/* --- gsk_buffer_polystr_index_of implementation --- */
/* Test to see if a sequence of buffer fragments
 * starts with a particular NUL-terminated string.
 */
static gboolean
fragment_n_str(GskBufferFragment   *frag,
               guint                frag_index,
               const char          *string)
{
  guint len = strlen (string);
  for (;;)
    {
      guint test_len = frag->buf_length - frag_index;
      if (test_len > len)
        test_len = len;

      if (memcmp (string,
                  gsk_buffer_fragment_start (frag) + frag_index,
                  test_len) != 0)
        return FALSE;

      len -= test_len;
      string += test_len;

      if (len <= 0)
        return TRUE;
      frag_index += test_len;
      if (frag_index >= frag->buf_length)
        {
          frag = frag->next;
          if (frag == NULL)
            return FALSE;
        }
    }
}

/**
 * gsk_buffer_polystr_index_of:
 * @buffer: buffer to scan.
 * @strings: NULL-terminated set of string.
 *
 * Scans for the first instance of any of the strings
 * in the buffer.
 *
 * returns: the index of that instance, or -1 if not found.
 */
int     
gsk_buffer_polystr_index_of    (GskBuffer    *buffer,
                                char        **strings)
{
  guint8 init_char_map[16];
  int num_strings;
  int num_bits = 0;
  int total_index = 0;
  GskBufferFragment *frag;
  memset (init_char_map, 0, sizeof (init_char_map));
  for (num_strings = 0; strings[num_strings] != NULL; num_strings++)
    {
      guint8 c = strings[num_strings][0];
      guint8 mask = (1 << (c % 8));
      guint8 *rack = init_char_map + (c / 8);
      if ((*rack & mask) == 0)
        {
          *rack |= mask;
          num_bits++;
        }
    }
  if (num_bits == 0)
    return 0;
  for (frag = buffer->first_frag; frag != NULL; frag = frag->next)
    {
      const char *frag_start;
      const char *at;
      int remaining = frag->buf_length;
      frag_start = gsk_buffer_fragment_start (frag);
      at = frag_start;
      while (at != NULL)
        {
          const char *start = at;
          if (num_bits == 1)
            {
              at = memchr (start, strings[0][0], remaining);
              if (at == NULL)
                remaining = 0;
              else
                remaining -= (at - start);
            }
          else
            {
              while (remaining > 0)
                {
                  guint8 i = (guint8) (*at);
                  if (init_char_map[i / 8] & (1 << (i % 8)))
                    break;
                  remaining--;
                  at++;
                }
              if (remaining == 0)
                at = NULL;
            }

          if (at == NULL)
            break;

          /* Now test each of the strings manually. */
          {
            char **test;
            for (test = strings; *test != NULL; test++)
              {
                if (fragment_n_str(frag, at - frag_start, *test))
                  return total_index + (at - frag_start);
              }
            at++;
          }
        }
      total_index += frag->buf_length;
    }
  return -1;
}

/* --- GskBufferIterator --- */

/**
 * gsk_buffer_iterator_construct:
 * @iterator: to initialize.
 * @to_iterate: the buffer to walk through.
 *
 * Initialize a new #GskBufferIterator.
 */
void 
gsk_buffer_iterator_construct (GskBufferIterator *iterator,
			       GskBuffer         *to_iterate)
{
  iterator->fragment = to_iterate->first_frag;
  if (iterator->fragment != NULL)
    {
      iterator->in_cur = 0;
      iterator->cur_data = (guint8*)gsk_buffer_fragment_start (iterator->fragment);
      iterator->cur_length = iterator->fragment->buf_length;
    }
  else
    {
      iterator->in_cur = 0;
      iterator->cur_data = NULL;
      iterator->cur_length = 0;
    }
  iterator->offset = 0;
}

/**
 * gsk_buffer_iterator_peek:
 * @iterator: to peek data from.
 * @out: to copy data into.
 * @max_length: maximum number of bytes to write to @out.
 *
 * Peek data from the current position of an iterator.
 * The iterator's position is not changed.
 *
 * returns: number of bytes peeked into @out.
 */
guint
gsk_buffer_iterator_peek      (GskBufferIterator *iterator,
			       gpointer           out,
			       guint              max_length)
{
  GskBufferFragment *fragment = iterator->fragment;

  guint frag_length = iterator->cur_length;
  const guint8 *frag_data = iterator->cur_data;
  guint in_frag = iterator->in_cur;

  guint out_remaining = max_length;
  guint8 *out_at = out;

  while (fragment != NULL)
    {
      guint frag_remaining = frag_length - in_frag;
      if (out_remaining <= frag_remaining)
	{
	  memcpy (out_at, frag_data + in_frag, out_remaining);
	  out_remaining = 0;
	  break;
	}

      memcpy (out_at, frag_data + in_frag, frag_remaining);
      out_remaining -= frag_remaining;
      out_at += frag_remaining;

      fragment = fragment->next;
      if (fragment != NULL)
	{
	  frag_data = (guint8 *) gsk_buffer_fragment_start (fragment);
	  frag_length = fragment->buf_length;
	}
      in_frag = 0;
    }
  return max_length - out_remaining;
}

/**
 * gsk_buffer_iterator_read:
 * @iterator: to read data from.
 * @out: to copy data into.
 * @max_length: maximum number of bytes to write to @out.
 *
 * Peek data from the current position of an iterator.
 * The iterator's position is updated to be at the end of
 * the data read.
 *
 * returns: number of bytes read into @out.
 */
guint
gsk_buffer_iterator_read      (GskBufferIterator *iterator,
			       gpointer           out,
			       guint              max_length)
{
  GskBufferFragment *fragment = iterator->fragment;

  guint frag_length = iterator->cur_length;
  const guint8 *frag_data = iterator->cur_data;
  guint in_frag = iterator->in_cur;

  guint out_remaining = max_length;
  guint8 *out_at = out;

  while (fragment != NULL)
    {
      guint frag_remaining = frag_length - in_frag;
      if (out_remaining <= frag_remaining)
	{
	  memcpy (out_at, frag_data + in_frag, out_remaining);
	  in_frag += out_remaining;
	  out_remaining = 0;
	  break;
	}

      memcpy (out_at, frag_data + in_frag, frag_remaining);
      out_remaining -= frag_remaining;
      out_at += frag_remaining;

      fragment = fragment->next;
      if (fragment != NULL)
	{
	  frag_data = (guint8 *) gsk_buffer_fragment_start (fragment);
	  frag_length = fragment->buf_length;
	}
      in_frag = 0;
    }
  iterator->in_cur = in_frag;
  iterator->fragment = fragment;
  iterator->cur_length = frag_length;
  iterator->cur_data = frag_data;
  iterator->offset += max_length - out_remaining;
  return max_length - out_remaining;
}

/**
 * gsk_buffer_iterator_find_char:
 * @iterator: to advance.
 * @c: the character to look for.
 *
 * If it exists,
 * skip forward to the next instance of @c and return TRUE.
 * Otherwise, do nothing and return FALSE.
 *
 * returns: whether the character was found.
 */

gboolean
gsk_buffer_iterator_find_char (GskBufferIterator *iterator,
			       char               c)
{
  GskBufferFragment *fragment = iterator->fragment;

  guint frag_length = iterator->cur_length;
  const guint8 *frag_data = iterator->cur_data;
  guint in_frag = iterator->in_cur;
  guint new_offset = iterator->offset;

  if (fragment == NULL)
    return -1;

  for (;;)
    {
      guint frag_remaining = frag_length - in_frag;
      const guint8 * ptr = memchr (frag_data + in_frag, c, frag_remaining);
      if (ptr != NULL)
	{
	  iterator->offset = (ptr - frag_data) - in_frag + new_offset;
	  iterator->fragment = fragment;
	  iterator->in_cur = ptr - frag_data;
	  iterator->cur_length = frag_length;
	  iterator->cur_data = frag_data;
	  return TRUE;
	}
      fragment = fragment->next;
      if (fragment == NULL)
	return FALSE;
      new_offset += frag_length - in_frag;
      in_frag = 0;
      frag_length = fragment->buf_length;
      frag_data = (guint8 *) fragment->buf + fragment->buf_start;
    }
}

/**
 * gsk_buffer_iterator_skip:
 * @iterator: to advance.
 * @max_length: maximum number of bytes to skip forward.
 *
 * Advance an iterator forward in the buffer,
 * returning the number of bytes skipped.
 *
 * returns: number of bytes skipped forward.
 */
guint
gsk_buffer_iterator_skip      (GskBufferIterator *iterator,
			       guint              max_length)
{
  GskBufferFragment *fragment = iterator->fragment;

  guint frag_length = iterator->cur_length;
  const guint8 *frag_data = iterator->cur_data;
  guint in_frag = iterator->in_cur;

  guint out_remaining = max_length;

  while (fragment != NULL)
    {
      guint frag_remaining = frag_length - in_frag;
      if (out_remaining <= frag_remaining)
	{
	  in_frag += out_remaining;
	  out_remaining = 0;
	  break;
	}

      out_remaining -= frag_remaining;

      fragment = fragment->next;
      if (fragment != NULL)
	{
	  frag_data = (guint8 *) gsk_buffer_fragment_start (fragment);
	  frag_length = fragment->buf_length;
	}
      else
	{
	  frag_data = NULL;
	  frag_length = 0;
	}
      in_frag = 0;
    }
  iterator->in_cur = in_frag;
  iterator->fragment = fragment;
  iterator->cur_length = frag_length;
  iterator->cur_data = frag_data;
  iterator->offset += max_length - out_remaining;
  return max_length - out_remaining;
}
