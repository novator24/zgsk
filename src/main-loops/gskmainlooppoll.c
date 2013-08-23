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

#include "gskmainlooppoll.h"
#include "../gskerrno.h"
#include "../config.h"

#if HAVE_POLL
#if HAVE_SYS_POLL_H
#include <sys/poll.h>
#endif
#include <errno.h>

static GObjectClass *parent_class = NULL;

/* --- GskMainLoopPollBase methods --- */
static void     
gsk_main_loop_poll_config_fd(GskMainLoopPollBase   *main_loop,
                             int                    fd,
			     GIOCondition           old_io_conditions,
			     GIOCondition           io_conditions)
{
  GskMainLoopPoll *poll_loop = (GskMainLoopPoll *) main_loop;
  struct pollfd *pfd;
  int index = -1;
  if (poll_loop->fd_to_poll_fd_alloced > fd)
    index = poll_loop->fd_to_poll_fd_index[fd];
  else
    {
      int old_alloced = poll_loop->fd_to_poll_fd_alloced;
      int alloced = old_alloced;
      if (alloced == 0)
        alloced = 16;
      while (alloced <= fd)
        alloced += alloced;
      poll_loop->fd_to_poll_fd_index
        = g_renew (int, poll_loop->fd_to_poll_fd_index, alloced);
      while (old_alloced < alloced)
        poll_loop->fd_to_poll_fd_index[old_alloced++] = -1;
      poll_loop->fd_to_poll_fd_alloced = alloced;
    }
  
  if (index < 0)
    {
      g_return_if_fail (old_io_conditions == 0);
      if (io_conditions == 0)
        return;
      /* Try to remove from free-list */
      if (poll_loop->first_free_index >= 0)
        {
	  /* Remove from the oddly linked list that roams
	   * through the pollfd array. */
	  index = poll_loop->first_free_index;
	  pfd = &g_array_index (poll_loop->poll_fds, struct pollfd, index);
	  poll_loop->first_free_index = - 2 - pfd->fd;
	  pfd->fd = fd;
	}
      else
        {
	  struct pollfd tmp;
	  /* free-list is empty: append to the list of events to wait for. */
	  index = poll_loop->poll_fds->len;
	  tmp.fd = fd;
	  g_array_append_val (poll_loop->poll_fds, tmp);
	  pfd = &g_array_index (poll_loop->poll_fds, struct pollfd, index);
	}
      poll_loop->fd_to_poll_fd_index[fd] = index;
    }
  else
    {
      pfd = &g_array_index (poll_loop->poll_fds, struct pollfd, index);

      /* If we aren't polling on this fd, then add it back to the free list */
      if (io_conditions == 0)
        {
	  pfd->fd = - 2 - poll_loop->first_free_index;
	  poll_loop->first_free_index = index;
	  poll_loop->fd_to_poll_fd_index[fd] = -1;
	  return;
	}
    }
  pfd->events = io_conditions;
}

static inline void pollfd_to_event  (GskMainLoopEvent    *event,
                                     const struct pollfd *poll_fd)
{
  event->type = GSK_MAIN_LOOP_EVENT_IO;
  event->data.io.fd = poll_fd->fd;
#if G_POLL_FLAGS_MATCH_SYSTEM_POLL_FLAGS
  event->data.io.events = poll_fd->revents;
#else
  event->data.io.events = 0;
  if (poll_fd->revents & (POLLIN|POLLHUP|POLLERR))
    event->data.io.events |= G_IO_IN;
  if (poll_fd->revents & (POLLOUT|POLLERR))
    event->data.io.events |= G_IO_OUT;
  if (poll_fd->revents & POLLERR)
    event->data.io.events |= G_IO_ERR;
#endif
}

/* Remember: this function may be cancelled by a signal at any time! */
static gboolean 
gsk_main_loop_poll_do_polling(GskMainLoopPollBase   *main_loop,
			      int                    max_timeout,
			      guint                  max_events,
			      guint                 *num_events_out,
                              GskMainLoopEvent      *events)
{
  GskMainLoopPoll *poll_loop = (GskMainLoopPoll *) main_loop;
  GArray *poll_array = poll_loop->poll_fds;
  int rv;
  struct pollfd *poll_fd_array = (struct pollfd *) poll_array->data;
  guint num_out;
  guint i;

  /* Compact the pollfd array. */
  if (poll_loop->first_free_index >= 0)
    {
      const struct pollfd *src = poll_fd_array;
      struct pollfd *dst = poll_fd_array;
      int num_src = poll_array->len;
      int num_dst = 0;

      while (num_src-- > 0)
        {
	  if (src->fd >= 0)
	    {
	      poll_loop->fd_to_poll_fd_index[src->fd] = num_dst;
	      *dst++ = *src;
	      num_dst++;
	    }
	  src++;
	}
      g_array_set_size (poll_array, num_dst);
      poll_fd_array = (struct pollfd *) poll_array->data;
      poll_loop->first_free_index = -1;
    }

  poll_fd_array = (struct pollfd *) poll_array->data;

  /* Do the polling. */
  rv = poll (poll_fd_array, poll_array->len, max_timeout);

  /* Handle exceptional circumstances. */
  if (rv < 0)
    {
      if (gsk_errno_is_ignorable (errno))
        {
	  *num_events_out = 0;
	  return TRUE;
	}
      g_warning ("the system call poll() failed: %s", g_strerror (errno));
      return FALSE;
    }

  /* Handle timeout. */
  if (rv == 0)
    {
      *num_events_out = 0;
      return TRUE;
    }

  /* Handle a nuisance case: when the user has decided they want no fds
   * returned.
   */
  if (max_events == 0)
    {
      *num_events_out = 0;
      return TRUE;
    }

  /* Return as many relevant events as possible. */
  num_out = 0;
  for (i = 0; i < poll_array->len; i++)
    {
      if (poll_fd_array[i].revents != 0)
	{
	  pollfd_to_event (events + num_out, poll_fd_array + i);
	  num_out++;
	}
      if (num_out == poll_array->len)
	break;
    }
  *num_events_out = num_out;
  return TRUE;
}

/* --- GObject methods --- */
static void gsk_main_loop_poll_finalize(GObject *object)
{
  GskMainLoopPoll *poll_loop;

  gsk_main_loop_destroy_all_sources (GSK_MAIN_LOOP (object));

  poll_loop = (GskMainLoopPoll *) object;
  g_array_free (poll_loop->poll_fds, TRUE);
  poll_loop->poll_fds = NULL;

  g_free (poll_loop->fd_to_poll_fd_index);
  poll_loop->fd_to_poll_fd_index = NULL;
  poll_loop->fd_to_poll_fd_alloced = 0;

  (*parent_class->finalize) (object);
}

/* --- Class methods --- */
static void gsk_main_loop_poll_init (GskMainLoopPoll* main_loop_poll)
{
  main_loop_poll->poll_fds = g_array_new (FALSE, FALSE, sizeof (struct pollfd));
  main_loop_poll->fd_to_poll_fd_alloced = 0;
  main_loop_poll->fd_to_poll_fd_index = NULL;
  main_loop_poll->first_free_index = -1;
}

static void gsk_main_loop_poll_class_init (GskMainLoopPollBaseClass* class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  parent_class = g_type_class_peek_parent (class);
  class->config_fd = gsk_main_loop_poll_config_fd;
  class->do_polling = gsk_main_loop_poll_do_polling;
  object_class->finalize = gsk_main_loop_poll_finalize;
}

GType gsk_main_loop_poll_get_type()
{
  static GType main_loop_poll_type = 0;
  if (!main_loop_poll_type)
    {
      static const GTypeInfo main_loop_poll_info =
      {
	sizeof(GskMainLoopPollClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_main_loop_poll_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskMainLoopPoll),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_main_loop_poll_init,
	NULL		/* value_table */
      };
      GType parent = GSK_TYPE_MAIN_LOOP_POLL_BASE;
      main_loop_poll_type = g_type_register_static (parent,
                                                  "GskMainLoopPoll",
						  &main_loop_poll_info, 0);
    }
  return main_loop_poll_type;
}
#endif /* HAVE_POLL */
