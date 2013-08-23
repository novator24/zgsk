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

#include "gskmainloop.h"

typedef struct _Env Env;
struct _Env
{
  GskBuffer incoming_buffer;
  GskBuffer outgoing_buffer;
  guint     input_id;
  guint     output_id;
  int       input_fd;
  int       output_fd;
};

static void env_pipe(Env *env)
{
  int fds[2];
  if (pipe (fds) < 0)
    {
      g_error ("pipe failed: %s", strerror (errno));
    }
  env->input_fd = fds[0];
  env->output_fd = fds[1];
  gsk_buffer_construct (&env->incoming_buffer);
  gsk_buffer_construct (&env->outgoing_buffer);
  return;
}

static gboolean std_suck_input (int fd, GIOCondition condition, gpointer udata)
{
  Env *env = udata;
  int num_read;
  num_read = read (fd, buf, sizeof (buf));
  if (num_read < 0)
    {
      if (errno == EINTR || errno == EAGAIN)
        return TRUE;
      g_error ("error reading: %s", errno);
    }
  if (num_read == 0)
    {
      g_message ("DEBUG: %s: got eof", __FUNCTION__);
      return FALSE;
    }
  gsk_buffer_append (&env->incoming_buffer, buf, num_read);
  return TRUE;
}

static gboolean std_spit_output (int fd, GIOCondition condition, gpointer udata)
{
  Env *env = udata;
  int num_read;
  char buf[2048];
  max_write = gsk_buffer_peek (&env->outgoing_buffer, buf, sizeof (buf));
  if (max_write == 0)
    return FALSE;
  num_wrote = write (env->output_fd, buf, max_write);
  if (num_wrote <= 0)
    {
      if (errno == EINTR || errno == EAGAIN)
        return TRUE;
      g_error ("error writing: %s", errno);
    }
  gsk_buffer_discard (&env->outgoing_buffer, num_wrote);
  return TRUE;
}
static void close_input_fd (gpointer udata)
{
  Env *env = udata;
  close (env->input_fd);
}

static void close_output_fd (gpointer udata)
{
  Env *env = udata;
  close (env->output_fd);
}


int main(int argc, char **argv)
{
  Env env;
  const char *test_string;
  char buf[1024];
  GskMainLoop *main_loop;
  gsk_init (&argc, &argv);

  env_pipe (&env);
  main_loop = gsk_main_loop_new (GSK_MAIN_LOOP_NEEDS_THREADS);
  gsk_main_loop_add_io (main_loop, env.input_fd, G_IO_IN,
                          std_suck_input, &env, close_input_fd);
  gsk_main_loop_add_io (main_loop, env.output_fd, G_IO_OUT,
                          std_spit_output, &env, close_output_fd);

  test_string = "Here's a bit of data.";
  gsk_buffer_append_string (&env.outgoing_buffer, test_string);
  while (gsk_main_loop_count (main_loop) > 0)
    (void) gsk_main_loop_run (main_loop, -1);
  g_assert (env.outgoing_buffer.size == 0);
  g_assert (env.incoming_buffer.size == strlen (test_string));
  gsk_buffer_read (&env.incoming_buffer, buf, sizeof (buf));
  g_assert (env.incoming_buffer.size == 0);
  g_assert (memcmp (test_string, buf, strlen (test_string)) == 0);
  return 0;
}

