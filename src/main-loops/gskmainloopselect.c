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

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "gskmainloopselect.h"
#include "../gskghelpers.h"
#include "../config.h"

#if HAVE_SELECT

/* If set, produce lots of debugging output. */
#define DEBUG_SELECT	0

static gint
compare_raw_ints (gconstpointer a, gconstpointer b)
{
  guint a_int = GPOINTER_TO_UINT (a);
  guint b_int = GPOINTER_TO_UINT (b);
  if (a_int < b_int)
    return -1;
  if (a_int > b_int)
    return +1;
  return 0;
}

/* --- prototypes --- */
static GObjectClass *parent_class = NULL;

/* --- GskMainLoopPollBase methods --- */
#define IFF(a,b)	(((a) ? 1 : 0) == ((b) ? 1 : 0))
static void     
gsk_main_loop_select_config_fd(GskMainLoopPollBase   *main_loop,
                               int                    fd,
			       GIOCondition           old_io_conditions,
			       GIOCondition           io_conditions)
{
  GskMainLoopSelect *select_loop = (GskMainLoopSelect *) main_loop;
#if DEBUG_SELECT
  g_message ("select: config-fd: fd=%d: events:%s%s",
	     fd,
	     (io_conditions & G_IO_IN) ? " IN" : "",
	     (io_conditions & G_IO_OUT) ? " OUT" : "");
#endif
  g_return_if_fail (IFF ((old_io_conditions & G_IO_IN) == G_IO_IN,
		         FD_ISSET (fd, &select_loop->read_set)));
  g_return_if_fail (IFF ((old_io_conditions & G_IO_OUT) == G_IO_OUT,
		         FD_ISSET (fd, &select_loop->write_set)));

  if (io_conditions == 0)
    g_tree_remove (select_loop->fd_tree, GUINT_TO_POINTER (fd));
  else
    g_tree_insert (select_loop->fd_tree,
                   GUINT_TO_POINTER (fd),
                   GUINT_TO_POINTER (fd));
  if ((io_conditions & G_IO_IN) == G_IO_IN)
    FD_SET (fd, &select_loop->read_set);
  else
    FD_CLR (fd, &select_loop->read_set);
  if ((io_conditions & G_IO_OUT) == G_IO_OUT)
    FD_SET (fd, &select_loop->write_set);
  else
    FD_CLR (fd, &select_loop->write_set);
  if ((io_conditions & G_IO_ERR) == G_IO_ERR)
    FD_SET (fd, &select_loop->except_set);
  else
    FD_CLR (fd, &select_loop->except_set);
}

typedef struct _TreeIterData TreeIterData;
struct _TreeIterData
{
  guint             max_events;
  guint             num_events_out;
  GskMainLoopEvent *events;
  fd_set            read_set;
  fd_set            write_set;
  fd_set            except_set;
};

static int
foreach_tree_node_add_event   (gpointer key,
                               gpointer value,
			       gpointer user_data)
{
  int fd = GPOINTER_TO_UINT (key);
  gboolean readable;
  gboolean writable;
  TreeIterData *iter_data = user_data;
  g_assert (key == value);
  readable = FD_ISSET (fd, &iter_data->read_set);
  writable = FD_ISSET (fd, &iter_data->write_set);

  if (readable || writable)
    {
      guint cur_num = iter_data->num_events_out;
      GIOCondition condition = 0;

      if (readable)
        condition |= G_IO_IN;
      if (writable)
        condition |= G_IO_OUT;

      iter_data->events[cur_num].type = GSK_MAIN_LOOP_EVENT_IO;
      iter_data->events[cur_num].data.io.fd = fd;
      iter_data->events[cur_num].data.io.events = condition;

      iter_data->num_events_out = cur_num + 1;
      if (iter_data->num_events_out == iter_data->max_events)
        return 1;		/* stop iterating... */
    }
  return 0;
}
  
static gboolean 
gsk_main_loop_select_do_polling(GskMainLoopPollBase   *main_loop,
			        int                    max_timeout,
			        guint                  max_events,
			        guint                 *num_events_out,
                                GskMainLoopEvent      *events)
{
  GskMainLoopSelect *select_loop = (GskMainLoopSelect *) main_loop;
  int max_fd = GPOINTER_TO_UINT (gsk_g_tree_max (select_loop->fd_tree));
  TreeIterData iter_data;
  struct timeval tv;
  struct timeval *ptv = NULL;
  iter_data.read_set = select_loop->read_set;
  iter_data.write_set = select_loop->write_set;
  iter_data.except_set = select_loop->except_set;
  if (max_timeout >= 0)
    {
      tv.tv_sec = max_timeout / 1000;
      tv.tv_usec = max_timeout % 1000 * 1000;
      ptv = &tv;
    }
  if (select (max_fd + 1,
	      &iter_data.read_set,
	      &iter_data.write_set,
	      &iter_data.except_set,
	      ptv) < 0)
    {
      if (errno == EINTR)
        {
	  *num_events_out = 0;
	  return TRUE;
	}
      g_warning ("Select failed: %s", g_strerror (errno));
      return FALSE;
    }

  if (max_events == 0)
    {
      *num_events_out = 0;
      return TRUE;
    }
  
  /* scan through and accumulate events... */
  iter_data.max_events = max_events;
  iter_data.events = events;
  iter_data.num_events_out = 0;
  g_tree_traverse (select_loop->fd_tree, 
                   foreach_tree_node_add_event,
		   G_IN_ORDER,		/* order is actually irrelevant */
		   &iter_data);
  *num_events_out = iter_data.num_events_out;
  return TRUE;
}

static void
gsk_main_loop_select_finalize (GObject *object)
{
  gsk_main_loop_destroy_all_sources (GSK_MAIN_LOOP (object));
  (*parent_class->finalize) (object);
}

/* --- functions --- */
static void
gsk_main_loop_select_init (GskMainLoopSelect *main_loop_select)
{
  main_loop_select->fd_tree = g_tree_new (compare_raw_ints);

  /* XXX: waste of time unless FD_ZERO is not a bitwise zero-ing */
  FD_ZERO (&main_loop_select->read_set);
  FD_ZERO (&main_loop_select->write_set);
  FD_ZERO (&main_loop_select->except_set);
}

static void
gsk_main_loop_select_class_init (GskMainLoopPollBaseClass *class)
{
  parent_class = g_type_class_peek_parent (class);
  G_OBJECT_CLASS (class)->finalize = gsk_main_loop_select_finalize;
  class->config_fd = gsk_main_loop_select_config_fd;
  class->do_polling = gsk_main_loop_select_do_polling;
}


GType gsk_main_loop_select_get_type()
{
  static GType main_loop_select_type = 0;
  if (!main_loop_select_type)
    {
      static const GTypeInfo main_loop_select_info =
      {
	sizeof(GskMainLoopSelectClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_main_loop_select_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskMainLoopSelect),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_main_loop_select_init,
	NULL		/* value_table */
      };
      GType parent = GSK_TYPE_MAIN_LOOP_POLL_BASE;
      main_loop_select_type = g_type_register_static (parent,
                                                  "GskMainLoopSelect",
						  &main_loop_select_info, 0);
    }
  return main_loop_select_type;
}

#endif /* HAVE_SELECT */
