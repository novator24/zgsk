/*
    GSK - a library to write servers
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

#include "../gskbuffer.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void random_slice(GskBuffer* buf)
{
  GskBuffer tmpbuf;
  char *copy, *copy_at;
  guint orig_size = buf->size;
  gsk_buffer_construct (&tmpbuf);
  copy = g_new (char, buf->size);
  copy_at = copy;
  while (1)
    {
      int r;
      r = (rand () % 16384) + 1;
      r = gsk_buffer_read (buf, copy_at, r);
      gsk_buffer_append (&tmpbuf, copy_at, r);
      if (r == 0)
	break;
    }
  g_assert (copy_at == copy + orig_size);
  g_assert (buf->size == 0);
  g_assert (tmpbuf.size == orig_size);
  copy_at = g_new (char, orig_size);
  g_assert (gsk_buffer_read (&tmpbuf, copy_at, orig_size)
	    == orig_size);
  g_assert (gsk_buffer_read (&tmpbuf, copy_at, orig_size) == 0);
  g_assert (memcmp (copy, copy_at, orig_size) == 0);
  g_free (copy);
  g_free (copy_at);
}

void count(GskBuffer* buf, int start, int end)
{
  char b[1024];
  while (start <= end)
    {
      sprintf (b, "%d\n", start++);
      gsk_buffer_append (buf, b, strlen (b));
    }
}

void decount(GskBuffer* buf, int start, int end)
{
  char b[1024];
  while (start <= end)
    {
      char *rv;
      sprintf (b, "%d", start++);
      rv = gsk_buffer_read_line (buf);
      g_assert (rv != NULL);
      g_assert (strcmp (b, rv) == 0);
      g_free (rv);
    }
}

int main(int argc, char** argv)
{

  GskBuffer gskbuffer;
  char buf[1024];
  char *str;

  gsk_buffer_construct (&gskbuffer);
  g_assert (gskbuffer.size == 0);
  gsk_buffer_append (&gskbuffer, "hello", 5);
  g_assert (gskbuffer.size == 5);
  g_assert (gsk_buffer_read (&gskbuffer, buf, sizeof (buf)) == 5);
  g_assert (memcmp (buf, "hello", 5) == 0);
  g_assert (gskbuffer.size == 0);
  gsk_buffer_destruct (&gskbuffer);

  gsk_buffer_construct (&gskbuffer);
  count (&gskbuffer, 1, 100000);
  decount (&gskbuffer, 1, 100000);
  g_assert (gskbuffer.size == 0);
  gsk_buffer_destruct (&gskbuffer);

  gsk_buffer_construct (&gskbuffer);
  gsk_buffer_append_string (&gskbuffer, "hello\na\nb");
  str = gsk_buffer_read_line (&gskbuffer);
  g_assert (str);
  g_assert (strcmp (str, "hello") == 0);
  g_free (str);
  str = gsk_buffer_read_line (&gskbuffer);
  g_assert (str);
  g_assert (strcmp (str, "a") == 0);
  g_free (str);
  g_assert (gskbuffer.size == 1);
  g_assert (gsk_buffer_read_line (&gskbuffer) == NULL);
  gsk_buffer_append_char (&gskbuffer, '\n');
  str = gsk_buffer_read_line (&gskbuffer);
  g_assert (str);
  g_assert (strcmp (str, "b") == 0);
  g_free (str);
  g_assert (gskbuffer.size == 0);
  gsk_buffer_destruct (&gskbuffer);

  gsk_buffer_construct (&gskbuffer);
  gsk_buffer_append (&gskbuffer, "hello", 5);
  gsk_buffer_append_foreign (&gskbuffer, "test", 4, NULL, NULL);
  gsk_buffer_append (&gskbuffer, "hello", 5);
  g_assert (gskbuffer.size == 14);
  g_assert (gsk_buffer_read (&gskbuffer, buf, sizeof (buf)) == 14);
  g_assert (memcmp (buf, "hellotesthello", 14) == 0);
  g_assert (gskbuffer.size == 0);

  /* Test that the foreign data really is not being stored in the GskBuffer */
  {
    char test_str[5];
    strcpy (test_str, "test");
    gsk_buffer_construct (&gskbuffer);
    gsk_buffer_append (&gskbuffer, "hello", 5);
    gsk_buffer_append_foreign (&gskbuffer, test_str, 4, NULL, NULL);
    gsk_buffer_append (&gskbuffer, "hello", 5);
    g_assert (gskbuffer.size == 14);
    g_assert (gsk_buffer_peek (&gskbuffer, buf, sizeof (buf)) == 14);
    g_assert (memcmp (buf, "hellotesthello", 14) == 0);
    test_str[1] = '3';
    g_assert (gskbuffer.size == 14);
    g_assert (gsk_buffer_read (&gskbuffer, buf, sizeof (buf)) == 14);
    g_assert (memcmp (buf, "hellot3sthello", 14) == 0);
    gsk_buffer_destruct (&gskbuffer);
  }

  /* Test str_index_of */
  {
    GskBuffer buffer = GSK_BUFFER_STATIC_INIT;
    gsk_buffer_append_foreign (&buffer, "abc", 3, NULL, NULL);
    gsk_buffer_append_foreign (&buffer, "def", 3, NULL, NULL);
    gsk_buffer_append_foreign (&buffer, "gad", 3, NULL, NULL);
    g_assert (gsk_buffer_str_index_of (&buffer, "cdefg") == 2);
    g_assert (gsk_buffer_str_index_of (&buffer, "ad") == 7);
    g_assert (gsk_buffer_str_index_of (&buffer, "ab") == 0);
    g_assert (gsk_buffer_str_index_of (&buffer, "a") == 0);
    g_assert (gsk_buffer_str_index_of (&buffer, "g") == 6);
    gsk_buffer_destruct (&buffer);
  }

  return 0;
}
