/*
    GSK - a library to write servers
    Copyright (C) 2001 Dave Benson

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



#include "gskmainloopdevpoll.h"

#include "../config.h"

/* --- prototypes --- */
static GObjectClass *parent_class = NULL;

#if HAVE_SYS_DEV_POLL
#include <signal.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>


/* Enable a lot of debugging output. */
#define DEBUG_DEV_POLL	0

/* Maximum number of poll info to read back from the system. */
#define MAX_DEV_POLL_COUNT	128

/* XXX: Hmm, the maximum signal we can encounter? */
#define MAX_SIGNALS		256


#include <sys/devpoll.h>

/* TODO: implement these! */
static void
gsk_main_loop_dev_poll_config_fd       (GskMainLoopPollBase   *main_loop,
                                        int                    fd,
				        GIOCondition           io_conditions)
{
  ...
}

static void
gsk_main_loop_dev_poll_setup_do_polling(GskMainLoopPollBase   *main_loop)
{
  ...
}

  /* returns FALSE if the poll function has an error.
   *
   * CAUTION: this function may * be interrupted by a signal at any time
   * so don't do memory allocation or ever fail to maintain the invariants
   * on your structure, or otherwise forget cancellation --
   * do that stuff in `setup_do_polling', which cannot be cancelled.
   */
static gboolean
gsk_main_loop_dev_poll_do_polling      (GskMainLoopPollBase   *main_loop,
				        int                    max_timeout,
				        guint                  max_events,
				        guint                 *num_events_out,
                                        GskMainLoopEvent      *events)
{
  ...
}

#endif

/* --- functions --- */
static void
gsk_main_loop_dev_poll_init (GskMainLoopDevPoll *main_loop_dev_poll)
{
  main_loop_dev_poll->dev_poll_fd = -1;
}

static gboolean
gsk_main_loop_dev_poll_setup (GskMainLoop *main_loop)
{
#if HAVE_SYS_DEV_POLL
  GskMainLoopDevPoll *main_loop_dev_poll = GSK_MAIN_LOOP_DEV_POLL (main_loop);
  main_loop_kqueue->dev_poll_fd = open ("/dev/poll", O_RDWR);
  return (main_loop_dev_poll->dev_poll_fd >= 0);
#else
  (void) main_loop;
  return FALSE;
#endif
}

static void
gsk_main_loop_dev_poll_class_init (GskMainLoopDevPollClass *dev_poll_class)
{
  GskMainLoopClass *class = GSK_MAIN_LOOP_CLASS (dev_poll_class);
  parent_class = g_type_class_peek_parent (class);
#if HAVE_SYS_DEV_POLL
  class->config_fd = gsk_main_loop_dev_poll_config_fd;
  class->setup_do_polling = gsk_main_loop_dev_poll_setup_do_polling;
  class->do_polling = gsk_main_loop_dev_poll_do_polling;
#endif
  class->setup = gsk_main_loop_dev_poll_setup;
}

GType gsk_main_loop_dev_poll_get_type()
{
  static GType main_loop_dev_poll_type = 0;
  if (!main_loop_dev_poll_type)
    {
      static const GTypeInfo main_loop_dev_poll_info =
      {
	sizeof(GskMainLoopDevPollClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_main_loop_dev_poll_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskMainLoopDevPoll),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_main_loop_dev_poll_init,
	NULL		/* value_table */
      };
      GType parent = GSK_TYPE_MAIN_LOOP_POLL_BASE;
      main_loop_dev_poll_type = g_type_register_static (parent,
                                                  "GskMainLoopDevPoll",
						  &main_loop_dev_poll_info, 0);
    }
  return main_loop_dev_poll_type;
}
