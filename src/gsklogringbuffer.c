#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "gsklogringbuffer.h"

struct _GskLogRingBuffer
{
  guint buffer_alloced;
  char *buffer;

  guint read_pos, amount_buffered;
};


GskLogRingBuffer *gsk_log_ring_buffer_new (gsize             size)
{
  GskLogRingBuffer *rv = g_new (GskLogRingBuffer, 1);
  rv->buffer_alloced = size;
  rv->buffer = g_malloc (size);
  rv->read_pos = rv->amount_buffered = 0;
  return rv;
}

void              gsk_log_ring_buffer_add (GskLogRingBuffer *buffer,
                                           const char       *line)
{
  guint line_len = strlen (line);
  guint clamped_line_len = MIN (line_len, buffer->buffer_alloced / 2);
  guint app_len = clamped_line_len + 1;
  guint write_pos;
  while (buffer->amount_buffered + app_len > buffer->buffer_alloced)
    {
      /* remove oldest line */
      guint line_len;
      if (buffer->read_pos + buffer->amount_buffered > buffer->buffer_alloced)
        {
          guint p1 = buffer->buffer_alloced - buffer->read_pos;
          char *s1 = buffer->buffer + buffer->read_pos;
          guint p2 = buffer->amount_buffered - p1;
          char *s2 = buffer->buffer;
          char *found = memchr (s1, '\n', p1);
          if (found)
            line_len = found - s1;
          else
            {
              found = memchr (s2, '\n', p2);
              g_assert (found);
              line_len = (found - s2) + p1;
            }
        }
      else
        {
          char *found = memchr (buffer->buffer + buffer->read_pos,
                                '\n', buffer->amount_buffered);
          g_assert (found);
          line_len = found - (buffer->buffer + buffer->read_pos);
        }

      /* include newline in line length */
      line_len += 1;

      /* remove the line from the start of the buffer */
      buffer->read_pos += line_len;
      if (buffer->read_pos >= buffer->buffer_alloced)
        buffer->read_pos -= buffer->buffer_alloced;
      buffer->amount_buffered -= line_len;
    }
  write_pos = buffer->amount_buffered + buffer->read_pos;
  if (write_pos >= buffer->buffer_alloced)
    write_pos -= buffer->buffer_alloced;
  if (write_pos + clamped_line_len > buffer->buffer_alloced)
    {
      guint p1 = buffer->buffer_alloced - write_pos;
      guint p2 = clamped_line_len - p1;
      memcpy (buffer->buffer + write_pos, line, p1);
      memcpy (buffer->buffer, line + p1, p2);
      write_pos = p2;
    }
  else if (write_pos + clamped_line_len == buffer->buffer_alloced)
    {
      memcpy (buffer->buffer + write_pos, line,
              clamped_line_len);
      write_pos = 0;
    }
  else
    {
      memcpy (buffer->buffer + write_pos, line, clamped_line_len);
      write_pos += clamped_line_len;
    }
  g_assert (write_pos < buffer->buffer_alloced);
  buffer->buffer[write_pos] = '\n';
  buffer->amount_buffered += app_len;
}

char *
gsk_log_ring_buffer_get (const GskLogRingBuffer *buffer)
{
  char *rv = g_malloc (buffer->amount_buffered + 1);
  if (buffer->amount_buffered + buffer->read_pos > buffer->buffer_alloced)
    {
      guint p1 = buffer->buffer_alloced - buffer->read_pos;
      guint p2 = buffer->amount_buffered - p1;
      memcpy (rv, buffer->buffer + buffer->read_pos, p1);
      memcpy (rv + p1, buffer->buffer, p2);
    }
  else
    {
      memcpy (rv, buffer->buffer + buffer->read_pos, buffer->amount_buffered);
    }
  rv[buffer->amount_buffered] = 0;
  return rv;
}

char *
gsk_substitute_localtime_in_string (const char *str,
                                    const char *strftime_format)
{
  GString *rv = g_string_new ("");
  char date_buffer[256];
  char num_buffer[16];
  time_t last_t = 0;
  if (strftime_format == NULL)
    strftime_format = "%Y%m%d %k:%M:%S";
  while (*str)
    {
      const char *endline = strchr (str, '\n');
      const char *endnum = str + strspn (str, G_CSET_DIGITS);
      time_t t;
      struct tm tm;
      if (endline == NULL)
        break;
      if (endnum == str || (gint)(endnum-str) > (gint)(sizeof(num_buffer)-1))
        {
          endnum = str;
          goto pass_line;
        }
      memcpy (num_buffer, str, endnum - str);
      num_buffer[endnum - str] = 0;
      t = strtol (num_buffer, NULL, 10);
      if (t != last_t)
        {
          localtime_r (&t, &tm);
          last_t = t;
        }
      strftime (date_buffer, sizeof (date_buffer),
                strftime_format, &tm);
      g_string_append (rv, date_buffer);

pass_line:
      g_string_append_len (rv, endnum, endline - endnum + 1);
      str = endline + 1;                /* skip the newline */
    }
  return g_string_free (rv, FALSE);
}

void gsk_log_ring_buffer_free(GskLogRingBuffer *buffer)
{
  g_free (buffer->buffer);
  g_free (buffer);
}
