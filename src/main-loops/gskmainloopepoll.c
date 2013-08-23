#include "gskmainloopepoll.h"
#include "../config.h"
#include "../gskerrno.h"
#include "../gskutils.h"

static GObjectClass *parent_class = NULL;

#if HAVE_EPOLL_SUPPORT

#include <sys/epoll.h>
#include <errno.h>

/* epoll_create() etc based main-loop.

   Note that we use Level-Triggered behavior,
   not edge-triggered, since that is how GskMainLoops are.
 */

#define EPOLL_INITIAL_SIZE	2048
#define MAX_EPOLL_EVENTS	512

static inline const char *
op_to_string (int op)
{
  return (op == EPOLL_CTL_DEL) ? "del"
       : (op == EPOLL_CTL_MOD) ? "mod"
       : (op == EPOLL_CTL_ADD) ? "add"
       : "op-unknown";
}

/* --- GskMainLoopPollBase methods --- */
static gboolean
gsk_main_loop_epoll_setup  (GskMainLoop       *main_loop)
{
  GskMainLoopClass *pclass = GSK_MAIN_LOOP_CLASS (parent_class);
  int fd;
  if (pclass->setup != NULL)
    if (!(*pclass->setup) (main_loop))
      return FALSE;
  fd = epoll_create (EPOLL_INITIAL_SIZE);
  if (fd < 0)
    return FALSE;
  gsk_fd_set_close_on_exec (fd, TRUE);
  GSK_MAIN_LOOP_EPOLL (main_loop)->fd = fd;
  return TRUE;
}

static void
gsk_main_loop_epoll_config_fd (GskMainLoopPollBase   *main_loop,
                               int                    fd,
			       GIOCondition           old_io_conditions,
                               GIOCondition           io_conditions)
{
  GskMainLoopEpoll *epoll = GSK_MAIN_LOOP_EPOLL (main_loop);
  int op = (io_conditions == 0) ? EPOLL_CTL_DEL
         : (old_io_conditions == 0) ? EPOLL_CTL_ADD
	 : EPOLL_CTL_MOD;
  struct epoll_event event;
  if (old_io_conditions == 0 && io_conditions == 0)
    return;
  event.events = ((io_conditions & G_IO_IN) ? (EPOLLIN) : 0)
               | ((io_conditions & G_IO_OUT) ? (EPOLLOUT) : 0)
               | ((io_conditions & G_IO_HUP) ? (EPOLLHUP) : 0)
	       ;
  event.data.fd = fd;
  if (epoll_ctl (epoll->fd, op, fd, &event) < 0)
    {
      g_warning ("epoll_ctl: op=%s, fd=%d, new_events=%x failed: %s",
		 op_to_string (op), fd, event.events, g_strerror (errno));
    }
}

static gboolean
gsk_main_loop_epoll_do_polling (GskMainLoopPollBase   *main_loop,
                                int                    max_timeout,
                                guint                  max_events,
                                guint                 *num_events_out,
                                GskMainLoopEvent      *events)
{
  GskMainLoopEpoll *main_loop_epoll = GSK_MAIN_LOOP_EPOLL (main_loop);
  struct epoll_event *e_events = main_loop_epoll->epoll_events;
  int n_events;
  int i;
  guint n_out = 0;
  errno = EINTR;		/* HACK: ignore errors which don't set errno !?! */
  n_events = epoll_wait (main_loop_epoll->fd, e_events,
			 MIN (max_events, MAX_EPOLL_EVENTS),
			 max_timeout);
#if 0
  g_message ("epoll_wait: max_timeout=%d, max_events=%u, n_events out=%d",
	     max_timeout, max_events, n_events);
#endif
  if (n_events < 0)
    {
      int e = errno;
      *num_events_out = 0;
      if (gsk_errno_is_ignorable (e))
	return TRUE;
      g_warning ("error running epoll_wait: %s", g_strerror (e));
      return TRUE;
    }

  for (i = 0; i < n_events; i++)
    {
      int fd = e_events[i].data.fd;
      unsigned e = e_events[i].events;
      GIOCondition condition = 0;
      if (e & EPOLLIN)
	condition |= G_IO_IN;
      if (e & EPOLLHUP)
	condition |= (G_IO_HUP|G_IO_IN);
      if (e & EPOLLERR)
	condition |= (G_IO_ERR|G_IO_IN|G_IO_OUT);
      if (e & EPOLLOUT)
	condition |= G_IO_OUT;
      events[n_out].type = GSK_MAIN_LOOP_EVENT_IO;
      events[n_out].data.io.events = condition;
      events[n_out].data.io.fd = fd;
      n_out++;
    }
  *num_events_out = n_out;

  return TRUE;
}

static void
gsk_main_loop_epoll_finalize (GObject *object)
{
  GskMainLoopEpoll *main_loop_epoll = GSK_MAIN_LOOP_EPOLL (object);
  g_free (main_loop_epoll->epoll_events);
  (*parent_class->finalize) (object);
}
#endif  /* HAVE_EPOLL_SUPPORT */

/* --- functions --- */
static void
gsk_main_loop_epoll_init (GskMainLoopEpoll *main_loop_epoll)
{
#if HAVE_EPOLL_SUPPORT
  main_loop_epoll->fd = -1;
  main_loop_epoll->epoll_events = g_new (struct epoll_event, MAX_EPOLL_EVENTS);
#endif  /* HAVE_EPOLL_SUPPORT */
}

static void
gsk_main_loop_epoll_class_init (GskMainLoopEpollClass *class)
{
#if HAVE_EPOLL_SUPPORT
  GskMainLoopPollBaseClass *main_loop_poll_base_class = GSK_MAIN_LOOP_POLL_BASE_CLASS (class);
  GskMainLoopClass *main_loop_class = GSK_MAIN_LOOP_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  main_loop_class->setup = gsk_main_loop_epoll_setup;
  main_loop_poll_base_class->config_fd = gsk_main_loop_epoll_config_fd;
  main_loop_poll_base_class->do_polling = gsk_main_loop_epoll_do_polling;
  object_class->finalize = gsk_main_loop_epoll_finalize;
#endif  /* HAVE_EPOLL_SUPPORT */
  parent_class = g_type_class_peek_parent (class);
}

GType gsk_main_loop_epoll_get_type()
{
  static GType main_loop_epoll_type = 0;
  if (!main_loop_epoll_type)
    {
      static const GTypeInfo main_loop_epoll_info =
      {
	sizeof(GskMainLoopEpollClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_main_loop_epoll_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskMainLoopEpoll),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_main_loop_epoll_init,
	NULL		/* value_table */
      };
      main_loop_epoll_type = g_type_register_static (GSK_TYPE_MAIN_LOOP_POLL_BASE,
                                                  "GskMainLoopEpoll",
						  &main_loop_epoll_info, 0);
    }
  return main_loop_epoll_type;
}
