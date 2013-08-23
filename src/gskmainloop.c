/*
    GSK - a library to write servers
    Copyright (C) 2001-2004 Dave Benson

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

#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "gskmainloop.h"
#include "gskmacros.h"
#include "gskrbtreemacros.h"
#include "config.h"
#include "gskghelpers.h"
#include "gskerrno.h"
#include "gskerror.h"
#include "gskinit.h"
#include "gskdebug.h"
#include "cycle.h"
#include "debug.h"

/* --- prototypes --- */
static GObjectClass *parent_class = NULL;

/* lifetime of a source;
      - created
      - run (maybe recursively) (maybe repeatedly)
      - removed from list of sources / io changed
      - user destroy function run
      - memory for source free'd
 */

/* --- a main-loop source --- */
typedef enum
{
  GSK_SOURCE_TYPE_IDLE,
  GSK_SOURCE_TYPE_TIMER,
  GSK_SOURCE_TYPE_IO,
  GSK_SOURCE_TYPE_SIGNAL,
  GSK_SOURCE_TYPE_PROCESS
} GskSourceType;

struct _GskSource
{
  GskSourceType          type;
  /* are we calling the users' callback? */
  guint                  run_count : 16;

  /* is the user's destroy function called? */
  guint                  is_destroyed : 1;

  /* was this remove'd while running? */
  guint                  must_remove : 1;

  /* are we allowed to recursively invoke this source? */
  guint                  is_reentrant : 1;

  /* only for timers */
  guint                  timer_adjusted_while_running : 1;
  guint                  timer_is_red : 1;
  guint                  timer_in_tree : 1;

  GskMainLoop           *main_loop;

  gpointer               user_data;
  GDestroyNotify         destroy;

  union
  {
    struct 
    {
      int                    fd;
      GIOCondition           events;
      GskMainLoopIOFunc      func;
    } io;
    struct
    {
      GTimeVal               expire_time;
      gint64                 milli_period;
      GskMainLoopTimeoutFunc func;
      GskSource             *left, *right, *parent;
    } timer;
    struct
    {
      int                    process_id;
      GskMainLoopWaitPidFunc func;
      GskSource             *prev;
      GskSource             *next;
    } process;
    struct
    {
      int                    number;
      GskMainLoopSignalFunc  func;
      GskSource             *prev;
      GskSource             *next;
    } signal;
    struct
    {
      GskMainLoopIdleFunc    func;
      GskSource             *prev;
      GskSource             *next;
    } idle;
  } data;
};

struct _GskMainLoopContextList
{
  GMainContext *context;
  guint num_fds_alloced;
  GPollFD *poll_fds;
  GskSource **sources;		/* one for each poll_fd */
  guint num_fds_received;
  gint priority;
  GskMainLoopContextList *next;
};

/* --- helper macros --- */
#define MAIN_LOOP_CLASS(main_loop) 	GSK_MAIN_LOOP_GET_CLASS(main_loop)
#define DEBUG_POLL(args)		\
		_GSK_DEBUG_PRINTF (GSK_DEBUG_MAIN_LOOP,args)

/* gskrbtree of timers */
#define TIMER_GET_IS_RED(source)    ((source)->timer_is_red)
#define TIMER_SET_IS_RED(source, v) (source)->timer_is_red=v
#define TIMEVALS_COMPARE(a,b)                           \
              (a).tv_sec < (b).tv_sec ? -1              \
            : (a).tv_sec > (b).tv_sec ? +1              \
            : (a).tv_usec < (b).tv_usec ? -1            \
            : (a).tv_usec > (b).tv_usec ? +1            \
            : 0
#define TIMER_COMPARE(a,b, rv) \
  rv = TIMEVALS_COMPARE(a->data.timer.expire_time, b->data.timer.expire_time); \
  if (rv == 0) { if (a < b) rv = -1; else if (a > b) rv = 1; }
#define GET_MAIN_LOOP_TIMER_TREE(main_loop) \
  (main_loop)->timers, GskSource *, TIMER_GET_IS_RED, TIMER_SET_IS_RED,  \
  data.timer.parent, data.timer.left, data.timer.right, TIMER_COMPARE


static inline GIOCondition
get_io_events (GskMainLoop *main_loop,
	       guint        fd)
{
  return (main_loop->read_sources->pdata[fd] ? (G_IO_IN|G_IO_HUP)  : 0)
       | (main_loop->write_sources->pdata[fd] ? (G_IO_OUT|G_IO_HUP) : 0);
}

static inline void
gsk_main_loop_change_signal (GskMainLoop  *main_loop,
			     guint         signal,
			     gboolean      add)
{
  GskMainLoopChange change;
  change.type = GSK_MAIN_LOOP_EVENT_SIGNAL;
  change.data.signal.number = signal;
  change.data.signal.add = add;
  (*MAIN_LOOP_CLASS (main_loop)->change) (main_loop, &change);
  DEBUG_POLL (("gsk_main_loop_change_signal: %s watch on signal %d",
	      (add ? "adding" : "removing"), signal));
}

static inline void
gsk_main_loop_change_io (GskMainLoop  *main_loop,
			 GIOCondition  old_events,
			 guint         fd)
{
  GskMainLoopChange change;
  change.type = GSK_MAIN_LOOP_EVENT_IO;
  change.data.io.fd = fd;
  change.data.io.old_events = old_events;
  change.data.io.events = get_io_events (main_loop, fd);

  (*MAIN_LOOP_CLASS (main_loop)->change) (main_loop, &change);

  DEBUG_POLL (("gsk_main_loop_change_io: fd=%d: old events:%s%s; "
	       "new events=%s%s",
	       fd,
	       (old_events & G_IO_IN) ? " in" : "",
	       (old_events & G_IO_OUT) ? " out" : "",
	       (change.data.io.events & G_IO_IN) ? " in" : "",
	       (change.data.io.events & G_IO_OUT) ? " out" : ""));
}

static inline void
gsk_main_loop_change_process (GskMainLoop  *main_loop,
			      guint         pid,
                              gboolean      did_exit,
			      gboolean      add)
{
  GskMainLoopChange change;
  if (did_exit)
    g_assert (!add);
  change.type = GSK_MAIN_LOOP_EVENT_PROCESS;
  change.data.process.pid = pid;
  change.data.process.add = add;
  change.data.process.did_exit = did_exit;
  (*MAIN_LOOP_CLASS (main_loop)->change) (main_loop, &change);
  DEBUG_POLL (("gsk_main_loop_change_process: %s watch on %d",
	       (add ? "adding" : "removing"), pid));
}

static guint
gsk_main_loop_run_io_sources (GskMainLoop     *main_loop,
			      guint            fd,
			      GIOCondition     condition)
{
  GskSource *read_source = NULL;
  GskSource *write_source = NULL;
  g_return_val_if_fail (main_loop->read_sources->len > fd, 0);
  if (condition & G_IO_IN) 
    read_source = main_loop->read_sources->pdata[fd];
  if (condition & G_IO_OUT) 
    write_source = main_loop->write_sources->pdata[fd];
  if (read_source == NULL && write_source == NULL)
    {
      g_message ("WARNING: got event %u for unknown file-descriptor %d",
                 (guint) condition, fd);
      return 0;
    }
  if (read_source == write_source)
    {
      read_source->run_count++;
      if (!(*read_source->data.io.func) (fd, G_IO_IN | G_IO_OUT,
			                 read_source->user_data))
	read_source->must_remove = 1;
      read_source->run_count--;
      if (read_source->run_count == 0 && read_source->must_remove)
	gsk_source_remove (read_source);
      return 1;
    }
  else 
    {
      if (read_source != NULL)
	{
	  read_source->run_count++;
	  if (!(*read_source->data.io.func) (fd, G_IO_IN,
				             read_source->user_data))
	    read_source->must_remove = 1;
	  read_source->run_count--;
	  if (read_source->run_count == 0 && read_source->must_remove)
	    gsk_source_remove (read_source);
	}
      if (write_source != NULL)
	{
	  write_source->run_count++;
	  if (!(*write_source->data.io.func) (fd, G_IO_OUT,
				              write_source->user_data))
	    write_source->must_remove = 1;
	  write_source->run_count--;
	  if (write_source->run_count == 0 && write_source->must_remove)
	    gsk_source_remove (write_source);
	}
      return (read_source && write_source) ? 2 : 1;
    }
}

static guint
gsk_main_loop_run_signal_sources (GskMainLoop     *main_loop,
			          guint            signal)
{
  GskSource *at;
  guint rv = 0;
  if (main_loop->signal_source_lists->len <= signal)
    return rv;
  at = main_loop->signal_source_lists->pdata[signal];
  if (at != NULL)
    at->run_count++;
  while (at != NULL)
    {
      GskSource *next;
      rv++;
      if (!(*at->data.signal.func) (signal, at->user_data))
	at->must_remove = 1;
      next = at->data.signal.next;
      if (next != NULL)
	next->run_count++;
      at->run_count--;
      if (at->run_count == 0 && at->must_remove)
	gsk_source_remove (at);
      at = next;
    }
  return rv;
}

static guint
gsk_main_loop_run_process_sources (GskMainLoop     *main_loop,
			           GskMainLoopWaitInfo *wait_info)
{
  GskSource *at;
  guint rv = 0;

  g_hash_table_remove (main_loop->alive_pids, GUINT_TO_POINTER (wait_info->pid));
  at = g_hash_table_lookup (main_loop->process_source_lists,
			  GUINT_TO_POINTER (wait_info->pid));
  if (at != NULL)
    at->run_count++;
  while (at != NULL)
    {
      GskSource *next;
      rv++;
      (*at->data.process.func) (wait_info, at->user_data);
      at->must_remove = 1;
      next = at->data.process.next;
      if (next != NULL)
	next->run_count++;
      at->run_count--;
      if (at->run_count == 0 && at->must_remove)
	gsk_source_remove (at);
      at = next;
    }
  return rv;
}

#define INITIAL_MAX_EVENTS		2048

static inline void
g_time_val_add_millis (GTimeVal  *in_out,
		       guint64    millis)
{
  /* the two cases in this if() statement are
     equivalent, if millis is really a 32-bit number.
     but on x86 which lacks good 64-bit divides,
     the first branch is much faster.

     1<<32 milliseconds is about 50 days,
     so MOST timeouts are in the first case. */

  /* TODO: we should really decide at compile
     time if we have fast 64-bit int support,
     and if so, only use the second branch,
     to save code space.  Of course, i have
     no idea how to detect it, but it could be based 
     on "cpu".
     
     TODO: another nice thing would be support for
     microsecond precision timers. */
#ifndef HAVE_FAST_64BIT_INT_SUPPORT     /* TODO: define this on non-x86 */
  if (G_LIKELY ((millis>>32) == 0))
    {
      guint ms = (guint) millis;
      in_out->tv_sec += ms / 1000;
      in_out->tv_usec += (guint)(ms % 1000) * 1000;
    }
  else
#endif
    {
      in_out->tv_sec += millis / 1000;
      in_out->tv_usec += (guint)(millis % 1000) * 1000;
    }

  /* check for overflow of the microseconds column. */
  if (in_out->tv_usec > 1000 * 1000)
    {
      in_out->tv_usec -= 1000 * 1000;
      in_out->tv_sec++;
    }
}

#ifdef HAVE_TICK_COUNTER
static guint64 last_tick;
static GTimeVal last_tick_time;
typedef enum
{
  TICK_STATE_INIT,
  TICK_STATE_HAS_LAST_TICK,
  TICK_STATE_HAS_TICK_RATE,
  TICK_STATE_READY,
  TICK_STATE_FALLBACK
} TickState;

#if defined(TICKS_PER_USEC)
static guint usecs_per_tick = USECS_PER_TICK;
static guint usecs_per_tick_shift = USECS_PER_TICK_SHIFT;
static TickState tick_state = TICK_STATE_HAS_TICK_RATE;
static guint64 max_tick_delta = ((1000000ULL * TICKS_PER_USEC_b20) >> 20);
#else
static guint usecs_per_tick;
static guint usecs_per_tick_shift;
static TickState tick_state = TICK_STATE_INIT;
static guint64 max_tick_delta;
#endif

#if GTIMEVAL_EQUALS_SYS_TIMEVAL
#define _gsk_g_get_current_time(tv) gettimeofday((struct timeval*)(tv), NULL)
#else
#define _gsk_g_get_current_time(tv) g_get_current_time(tv)
#endif

void
gsk_get_current_time (GTimeVal *tv)
{
  switch (tick_state)
    {
    case TICK_STATE_INIT:
      _gsk_g_get_current_time (&last_tick_time);
      last_tick = getticks ();
      *tv = last_tick_time;
      tick_state = TICK_STATE_HAS_LAST_TICK;
      break;
    case TICK_STATE_HAS_LAST_TICK:
      _gsk_g_get_current_time (tv);
      if (tv->tv_sec > last_tick_time.tv_sec + 3)
        {
          /* compute tick rate */
          gdouble dusec = ((double)tv->tv_usec - last_tick_time.tv_usec)
                        + 1e6 * ((double)tv->tv_sec - last_tick_time.tv_sec);
          guint64 this_ticks = getticks ();
          gint64 dticks = this_ticks - last_tick;
          gdouble ticks_per_usec_d = (gdouble)dticks / dusec;
          if (ticks_per_usec_d <= 1.0)
            {
              g_message ("ticking too slow... not using cpu timer");
              tick_state = TICK_STATE_FALLBACK;
              break;
            }
          usecs_per_tick_shift = 30;
          usecs_per_tick = (double)(1 << usecs_per_tick_shift) / ticks_per_usec_d;
          max_tick_delta = (guint64)(1000000ULL * ticks_per_usec_d);
          tick_state = TICK_STATE_READY;
          if (usecs_per_tick == 0)
            {
              g_message ("ticking miscalc");
              tick_state = TICK_STATE_FALLBACK;
              break;
            }
          last_tick = this_ticks;
          last_tick_time = *tv;
        }
      break;
    case TICK_STATE_HAS_TICK_RATE:
      _gsk_g_get_current_time (&last_tick_time);
      last_tick = getticks ();
      *tv = last_tick_time;
      tick_state = TICK_STATE_READY;
      break;
    case TICK_STATE_READY:
      {
        guint64 this_ticks = getticks ();
        guint64 delta = this_ticks - last_tick;
        if (delta > max_tick_delta)
          {
            last_tick = this_ticks;
            _gsk_g_get_current_time (&last_tick_time);
            *tv = last_tick_time;
            //g_message ("resync: %u.%06u [ticks=%llu]",(guint)last_tick_time.tv_sec,(guint)last_tick_time.tv_usec,last_tick);
          }
        else
          {
            guint usecs = (guint)(((guint64)delta * usecs_per_tick) >> usecs_per_tick_shift);
            //g_message ("delta=%llu; cached time: usecs=%u", delta,usecs);
            *tv = last_tick_time;
            tv->tv_usec += usecs;
            while (tv->tv_usec >= 1000000)
              {
                tv->tv_usec -= 1000000;
                tv->tv_sec += 1;
              }
          }
        break;
      }
    case TICK_STATE_FALLBACK:
      _gsk_g_get_current_time (tv);
      break;

    default:
      g_assert_not_reached ();
    }
}
#else
void
gsk_get_current_time (GTimeVal *tv)
{
  _gsk_g_get_current_time (tv);
}
#endif


/**
 * gsk_main_loop_update_current_time:
 * @main_loop: main loop whose notion of time should be updated.
 *
 * Update the main loop's cached time value by querying the
 * system clock.
 */
void
gsk_main_loop_update_current_time (GskMainLoop *main_loop)
{
  gsk_get_current_time (&main_loop->current_time);
}

/**
 * gsk_main_loop_do_waitpid:
 * @pid: the process id to wait for.
 * @wait_info: place to collect termination status of the process.
 *
 * Do a waitpid system call on the process and munge the
 * data into @wait_info for the caller to use.
 *
 * returns: whether the waitpid succeeded.
 */
gboolean
gsk_main_loop_do_waitpid (int                  pid,
                          GskMainLoopWaitInfo *wait_info)
{
  int status;

retry:
  pid = waitpid (pid, &status, WNOHANG);
  if (pid < 0)
    {
      if (gsk_errno_is_ignorable (errno))
        goto retry;

      /*gsk_log_errno ("error running waitpid");*/
      return FALSE;
    }
  if (pid == 0)
    return FALSE;
  wait_info->pid = pid;
  wait_info->exited = WIFEXITED (status);
  if (wait_info->exited)
    {
      wait_info->d.exit_status = WEXITSTATUS (status);
      wait_info->dumped_core = FALSE;
    }
  else
    {
      wait_info->d.signal = WTERMSIG (status);
#ifdef WCOREDUMP
      wait_info->dumped_core = WCOREDUMP (status);
#else
      wait_info->dumped_core = FALSE;
#endif
    }
  return TRUE;
}


static gboolean
handle_poll_fd_set (int          fd,
		    GIOCondition condition,
		    gpointer     data)
{
  GPollFD *poll_fd = data;
  g_assert (poll_fd->fd == fd);
  poll_fd->revents = condition;
  return TRUE;
}

/**
 * gsk_main_loop_run:
 * @main_loop: the main loop to run.
 * @timeout: the maximum number of milliseconds to run, or -1 for no maximum.
 * @t_waited_out: the number of milliseconds out used, if non-NULL.
 *
 * Run the main loop once, for a specified number of milliseconds.
 *
 * returns: the number of sources processed.
 */
guint
gsk_main_loop_run          (GskMainLoop       *main_loop,
			    gint               timeout,
			    guint             *t_waited_out)
{
  GskMainLoopEvent *events;
  GskMainLoopClass *class = MAIN_LOOP_CLASS (main_loop);
  guint num_events;
  guint rv = 0;
  GTimeVal old_time;
  GTimeVal *current_time;
  guint i;
  GskSource *at;
  GskMainLoopContextList *list;
  GskMainLoopContextList **plist;

  g_return_val_if_fail (!main_loop->is_running, 0);
  main_loop->is_running = 1;

  gsk_main_loop_update_current_time (main_loop);
  current_time = &main_loop->current_time;
  old_time = *current_time;
  if (main_loop->first_idle != NULL)
    timeout = 0;
  GSK_RBTREE_FIRST (GET_MAIN_LOOP_TIMER_TREE (main_loop), at);
  if (at != NULL)
    {
      /* This rounds upward to the nearest millisecond */
      GTimeVal *exp_time = &at->data.timer.expire_time;
      if (exp_time->tv_sec < current_time->tv_sec
       || (exp_time->tv_sec == current_time->tv_sec
           && exp_time->tv_usec <= current_time->tv_usec))
        timeout = 0;
      else
        {
          guint ts = exp_time->tv_sec - current_time->tv_sec;
          gint tus = exp_time->tv_usec - current_time->tv_usec;
          gint t;
          if (tus < 0)
            {
              tus += 1000000;
              ts--;
            }
          t = ts * 1000 + (tus + 999) / 1000;
          if (timeout < 0 || t < timeout)
            timeout = t;
        }
    }

  /* TODO: this doesn't work for reentrant main-loops. */
  events = main_loop->event_array_cache;

  for (plist = &main_loop->first_context; *plist != NULL; )
    {
      GskMainLoopContextList *list = *plist;
      GMainContext *context = list->context;
      gint c_timeout;

      /* XXX: how can we tell if g_main_loop_quit was called? */
#if 0
      if (context->has_quit)	
	{
	  *plist = list->next;
	  if (*plist == NULL && main_loop->first_context == NULL)
	    main_loop->last_context = NULL;

	  g_free (list->poll_fds);
	  g_free (list->sources);
	  g_free (list);
	  continue;
	}
#endif

      if (!g_main_context_prepare (context, &list->priority))
	{
	  /* i think that TRUE means we can ditch calling 'query';
	   * but we always call query anyways.
	   */
	}

      /* hmm, is this allowed? */
retry_query:
      list->num_fds_received = g_main_context_query (context, list->priority,
						     &c_timeout,
						     list->poll_fds,
						     list->num_fds_alloced);
      /* if we filled the entire array, double its size */
      if (list->num_fds_received == list->num_fds_alloced)
	{
	  guint new_count = list->num_fds_alloced * 2;
	  list->poll_fds = g_renew (GPollFD, list->poll_fds, new_count);
	  list->sources = g_renew (GskSource *, list->sources, new_count);
	  list->num_fds_alloced = new_count;
	  goto retry_query;
	}

      if (timeout < 0 || (c_timeout >= 0 && c_timeout < timeout))
	timeout = c_timeout;

      /* add indication of interest in fd_events/n_events */
      for (i = 0; i < list->num_fds_received; i++)
	{
	  GPollFD *poll_fd = &list->poll_fds[i];
	  poll_fd->revents = 0;
	  list->sources[i] = gsk_main_loop_add_io (main_loop,
						   poll_fd->fd,
						   poll_fd->events,
						   handle_poll_fd_set,
						   poll_fd,
						   NULL);
	}
      plist = &((*plist)->next);
    }
  num_events = (*class->poll) (main_loop, main_loop->max_events, events, timeout);
  gsk_main_loop_update_current_time (main_loop);
  /* run i/o, signal and process handlers */
  for (i = 0; i < num_events; i++)
    {
      switch (events[i].type)
	{
        case GSK_MAIN_LOOP_EVENT_IO:
	  rv += gsk_main_loop_run_io_sources (main_loop,
					      events[i].data.io.fd,
					      events[i].data.io.events);
	  break;
        case GSK_MAIN_LOOP_EVENT_SIGNAL:
	  rv += gsk_main_loop_run_signal_sources (main_loop,
					          events[i].data.signal);
	  break;
        case GSK_MAIN_LOOP_EVENT_PROCESS:
	  rv += gsk_main_loop_run_process_sources (main_loop,
					           &events[i].data.process_wait_info);
	  break;
	}
    }

  /* deal with the glib-style main contexts */
  for (list = main_loop->first_context; list != NULL; list = list->next)
    {
      GMainContext *context = list->context;
      for (i = 0; i < list->num_fds_received; i++)
	gsk_source_remove (list->sources[i]);
      g_main_context_check (context, list->priority, list->poll_fds, list->num_fds_received);
      g_main_context_dispatch (context);
    }

  /* run idle functions */
  at = main_loop->first_idle;
  if (at != NULL)
    at->run_count++;
  while (at != NULL)
    {
      GskSource *next;
      if (!(*at->data.idle.func) (at->user_data))
	at->must_remove = 1;
      rv++;
      next = at->data.idle.next;
      if (next)
	next->run_count++;
      at->run_count--;
      if (at->run_count == 0 && at->must_remove)
	gsk_source_remove (at);
      at = next;
    }

  /* expire timers */
  for (;;)
    {
      GSK_RBTREE_FIRST (GET_MAIN_LOOP_TIMER_TREE (main_loop), at);
      if (at == NULL)
        break;
      if (at->data.timer.expire_time.tv_sec > current_time->tv_sec
       || (at->data.timer.expire_time.tv_sec == current_time->tv_sec
        && at->data.timer.expire_time.tv_usec >= current_time->tv_usec))
	break;

      at->run_count++;
      g_assert (at->timer_in_tree);
      GSK_RBTREE_REMOVE (GET_MAIN_LOOP_TIMER_TREE (main_loop), at);
      at->timer_in_tree = 0;
      if (!(*at->data.timer.func) (at->user_data))
	at->must_remove = 1;
      rv++;
      at->run_count--;
      if (at->run_count == 0 && at->must_remove)
	gsk_source_remove (at);
      else
	{
          if (at->timer_adjusted_while_running)
	    at->timer_adjusted_while_running = 0;
	  else
	    g_time_val_add_millis (&at->data.timer.expire_time, 
				   at->data.timer.milli_period);
          g_assert (!at->timer_in_tree);
          {
            GskSource *unused;
            GSK_RBTREE_INSERT (GET_MAIN_LOOP_TIMER_TREE (main_loop),
                               at, unused);
            at->timer_in_tree = 1;
          }
	}
    }

  g_return_val_if_fail (main_loop->is_running, rv);
  main_loop->is_running = 0;

  if (t_waited_out != NULL)
    {
      *t_waited_out = (main_loop->current_time.tv_sec - old_time.tv_sec) * 1000
	            + (main_loop->current_time.tv_usec - old_time.tv_usec) / 1000;
    }

  if (main_loop->max_events == num_events)
    {
      /* If we are pushing the number-of-events threshold,
	 then double the size of our buffer. */
      main_loop->max_events *= 2;
      main_loop->event_array_cache
	= g_renew (GskMainLoopEvent,
		   main_loop->event_array_cache, main_loop->max_events);
    }
  return rv;
}

static GMemChunk *gsk_source_chunk = NULL;
G_LOCK_DEFINE_STATIC (gsk_source_chunk);

static inline GskSource  *
gsk_source_new   (GskSourceType     type,
		  GskMainLoop      *main_loop,
		  gpointer          user_data,
		  GDestroyNotify    destroy)
{
  GskSource *rv;
  G_LOCK (gsk_source_chunk);
  if (gsk_source_chunk == NULL)
    gsk_source_chunk = g_mem_chunk_create (GskSource, 16, G_ALLOC_AND_FREE);
  rv = g_mem_chunk_alloc (gsk_source_chunk);
  G_UNLOCK (gsk_source_chunk);
  rv->type = type;
  rv->user_data = user_data;
  rv->destroy = destroy;
  rv->main_loop = main_loop;
  rv->run_count = 0;
  rv->must_remove = 0;
  rv->is_destroyed = 0;
  rv->is_reentrant = 0;
  return rv;
}
static inline void
gsk_source_free (GskSource *gsk_source)
{
  G_LOCK (gsk_source_chunk);
  g_mem_chunk_free (gsk_source_chunk, gsk_source);
  G_UNLOCK (gsk_source_chunk);
}

/**
 * gsk_main_loop_add_idle:
 * @main_loop: the loop to add the idle function to.
 * @source_func: the function to call.
 * @user_data: parameter to be passed to @source_func
 * @destroy: to be called when the source is destroyed.
 *
 * This adds an idle function to the main loop.
 * An idle function is a function which gets called every
 * time the main loop is run.  Furthermore, while there are
 * idle functions, the main loop will never block.
 *
 * One popular use of idle functions is to defer
 * an operation, usually because either something is not
 * in a good state to call immediately,
 * or because there may be many requests that should
 * be handled at one time.
 *
 * returns: a #GskSource which can be removed (or ignored).
 */
GskSource *
gsk_main_loop_add_idle     (GskMainLoop       *main_loop,
			    GskMainLoopIdleFunc source_func,
			    gpointer           user_data,
			    GDestroyNotify     destroy)
{
  GskSource *source = gsk_source_new (GSK_SOURCE_TYPE_IDLE, main_loop, user_data, destroy);
  source->data.idle.func = source_func;
  source->data.idle.prev = main_loop->last_idle;
  source->data.idle.next = NULL;
  if (main_loop->last_idle == NULL)
    main_loop->first_idle = source;
  else
    main_loop->last_idle->data.idle.next = source;
  main_loop->last_idle = source;
  return source;
}

/**
 * gsk_main_loop_add_signal:
 * @main_loop: the loop to add the unix signal handler function to.
 * @signal_number: the number of the signal handler, like SIGINT.
 * @signal_func: the function to run synchronously when a unix signal is raised.
 * @user_data: data to be passed to @signal_func.
 * @destroy: to be called when the source is destroyed.
 *
 * Add a signal handler to the main loop.
 *
 * Please note that unlike a normal unix signal handler (as provided by
 * signal(2) or sigaction(2)), this handler will be run synchronously,
 * so you can call non-reentrant methods.
 *
 * Also, because unix signal delivery is unreliable, if the signal is raised a
 * few times in rapid succession, you may miss some callbacks.
 * 
 * It is ok to connect multiple times to a single signal simulataneously.
 *
 * returns: a #GskSource which can be removed (or ignored).
 */
GskSource *
gsk_main_loop_add_signal   (GskMainLoop       *main_loop,
			    int                signal_number,
			    GskMainLoopSignalFunc signal_func,
			    gpointer           user_data,
			    GDestroyNotify     destroy)
{
  GskSource *source;
  GskSource *first;
  GskSource *last;
  //g_return_val_if_fail (signal_number != SIGCHLD, NULL);
  source = gsk_source_new (GSK_SOURCE_TYPE_SIGNAL, main_loop, user_data, destroy);
  if (main_loop->signal_source_lists->len <= (guint) signal_number)
    g_ptr_array_set_size (main_loop->signal_source_lists, signal_number + 1);
  first = main_loop->signal_source_lists->pdata[signal_number];
  last = first;

  /* XXX: really need to save last elements!!! */
  if (last != NULL)
    while (last->data.signal.next != NULL)
      last = last->data.signal.next;

  source->data.signal.number = signal_number;
  source->data.signal.func = signal_func;
  source->data.signal.prev = last;
  source->data.signal.next = NULL;
  if (last == NULL)
    {
      gsk_main_loop_change_signal (main_loop, signal_number, TRUE);
      main_loop->signal_source_lists->pdata[signal_number] = source;
    }
  else
    last->data.signal.next = source;
  main_loop->num_sources++;
  return source;
}

/**
 * gsk_main_loop_add_waitpid:
 * @main_loop: the loop to add the child-process termination handler function to.
 * @process_id: the child's process-id to wait for.
 * @waitpid_func: function to call when the process terminates in some way or another.
 * @user_data: data to be passed to @waitpid_func
 * @destroy: to be called when the source is destroyed.
 *
 * Add a handler to trap process termination.
 *
 * Only one handler is allowed per child process.
 *
 * The handler will be invoked synchronously.
 *
 * returns: #GskSource which can be removed (or ignored).
 */
GskSource *
gsk_main_loop_add_waitpid  (GskMainLoop       *main_loop,
			    int                process_id,
			    GskMainLoopWaitPidFunc waitpid_func,
			    gpointer           user_data,
			    GDestroyNotify     destroy)
{
  GskSource *source = gsk_source_new (GSK_SOURCE_TYPE_PROCESS, main_loop, user_data, destroy);
  GskSource *first;
  GskSource *last;
  first = g_hash_table_lookup (main_loop->process_source_lists,
                               GUINT_TO_POINTER (process_id));
  /* XXX: really need to save last elements!!! */
  last = first;
  if (last != NULL)
    while (last->data.process.next != NULL)
      last = last->data.process.next;
  source->data.process.process_id = process_id;
  source->data.process.prev = last;
  source->data.process.next = NULL;
  source->data.process.func = waitpid_func;
  if (last == NULL)
    {
      gsk_main_loop_change_process (main_loop, process_id, FALSE, TRUE);
      g_hash_table_insert (main_loop->process_source_lists,
			   GUINT_TO_POINTER (process_id),
			   source);
    }
  else
    last->data.process.next = source;
  main_loop->num_sources++;
  return source;
}

/**
 * gsk_main_loop_add_io:
 * @main_loop: the loop to add the i/o watch to.
 * @fd: the file-descriptor to watch for events.
 * @events: initial I/O events to watch for.
 * @io_func: a function to call when the currently requested
 * events occur.
 * @user_data: data to be passed to @io_func
 * @destroy: to be called when the source is destroyed.
 *
 * Add a handler to input or output on a file-descriptor (a socket or a pipe, usually).
 *
 * Only one handler trap is allowed per file-descriptor.
 *
 * The handler will be re-invoked until the event subsides.
 * For example, if you read only part of the data when a input event is raised,
 * the @io_func will be invoked again at every iteration of the main-loop
 * until there is no data available.
 *
 * returns: a #GskSource which can be removed or altered.
 */
GskSource *
gsk_main_loop_add_io       (GskMainLoop       *main_loop,
			    int                fd,
			    guint              events,
			    GskMainLoopIOFunc  io_func,
			    gpointer           user_data,
			    GDestroyNotify     destroy)
{
  GskSource *source;
  GIOCondition old_events;
  g_return_val_if_fail (fd >= 0, NULL);
  if ((guint) fd >= main_loop->read_sources->len)
    {
      g_ptr_array_set_size (main_loop->read_sources, fd + 1);
      g_ptr_array_set_size (main_loop->write_sources, fd + 1);
    }
  old_events = get_io_events (main_loop, fd);
  g_return_val_if_fail ((old_events & events & (G_IO_IN|G_IO_OUT)) == 0, NULL);
  source = gsk_source_new (GSK_SOURCE_TYPE_IO, main_loop, user_data, destroy);
  source->data.io.fd = fd;
  source->data.io.events = events;
  source->data.io.func = io_func;
  if ((events & G_IO_IN) == G_IO_IN)
    {
      g_return_val_if_fail (main_loop->read_sources->pdata[fd] == NULL, NULL);
      main_loop->read_sources->pdata[fd] = source;
    }
  if ((events & G_IO_OUT) == G_IO_OUT)
    {
      g_return_val_if_fail (main_loop->write_sources->pdata[fd] == NULL, NULL);
      main_loop->write_sources->pdata[fd] = source;
    }
  gsk_main_loop_change_io (main_loop, old_events, fd);
  main_loop->num_sources++;
  return source;
}

/**
 * gsk_source_adjust_io:
 * @source: the I/O source which now wants to watch different events.
 * @events: the new events to watch for the I/O source.
 *
 * This changes the types of events being watched by the main-loop.
 *
 * Note:  each new file-descriptor needs a new GskSource.
 * You must reuse this GskSource for a new file-descriptor
 * even if it happens to have the same numeric value
 * as a file-descriptor you closed.
 * (The reason why:  GSK automatically coagulates
 * multiple adjust_io calls.  This is fine with all main-loops.
 * However, kqueue(2) on BSD, and possibly others, automatically
 * unregister all interest in an event if the file-descriptor closes.
 * Hence, if the file-descriptor is re-opened and re-used with the
 * same GskSource, GSK will not be able to determine
 * that anything has changed, and will not issue a new #GskMainLoopChange.
 * This will break main-loops that are sensitive to exact which file-descriptor
 * (not just the number) was registered.)
 */
void
gsk_source_adjust_io    (GskSource         *source,
			 guint              events)
{
  guint fd;
  GIOCondition old_events;
  GskMainLoop *main_loop = source->main_loop;

  g_return_if_fail (source != NULL);
  g_return_if_fail (source->type == GSK_SOURCE_TYPE_IO);
  g_return_if_fail (main_loop->read_sources->len > (guint) source->data.io.fd);

  fd = source->data.io.fd;

  if ((events & (G_IO_IN|G_IO_OUT)) == (source->data.io.events & (G_IO_IN|G_IO_OUT)))
    return;
  old_events = get_io_events (main_loop, fd);
  if ((events & G_IO_IN) == G_IO_IN)
    {
      GskSource *old = main_loop->read_sources->pdata[fd];
      g_return_if_fail (old == source || old == NULL);
      main_loop->read_sources->pdata[fd] = source;
    }
  else if (main_loop->read_sources->pdata[fd] == source)
    main_loop->read_sources->pdata[fd] = NULL;

  if ((events & G_IO_OUT) == G_IO_OUT)
    {
      GskSource *old = main_loop->write_sources->pdata[fd];
      g_return_if_fail (old == source || old == NULL);
      main_loop->write_sources->pdata[fd] = source;
    }
  else if (main_loop->write_sources->pdata[fd] == source)
    main_loop->write_sources->pdata[fd] = NULL;
  source->data.io.events = events;
  gsk_main_loop_change_io (main_loop, old_events, fd);
}

/**
 * gsk_source_add_io_events:
 * @source: the input/output source whose events-of-interest set
 * should be expanded.
 * @events: new events which should cause @source to wake-up.
 *
 * Cause this source to be notified if any of the events in
 * the @events parameter are set, in addition to the events which
 * already caused this source to be woken up.
 */
void
gsk_source_add_io_events (GskSource *source,
			  guint      events)
{
  g_return_if_fail (source != NULL);
  g_return_if_fail (source->type == GSK_SOURCE_TYPE_IO);
  gsk_source_adjust_io (source, source->data.io.events | events);
}

/**
 * gsk_source_remove_io_events:
 * @source: the input/output source whose events-of-interest set
 * should be reduced.
 * @events: new events which should stop causing @source to wake-up.
 *
 * Cause this source to stop being notified if any of the events in
 * the @events parameter are set.
 */
void
gsk_source_remove_io_events (GskSource *source,
			     guint      events)
{
  g_return_if_fail (source != NULL);
  g_return_if_fail (source->type == GSK_SOURCE_TYPE_IO);
  gsk_source_adjust_io (source, source->data.io.events & (~events));
}

/**
 * gsk_main_loop_add_timer:
 * @main_loop: the main-loop which should keep track and run the timeout.
 * @timer_func: function to call when the requested amount of time elapses.
 * @timer_data: data to pass to @timer_func.
 * @timer_destroy: optional function to call to destroy the @timer_data.
 * @millis_expire: number of milliseconds to wait before running @timer_func.
 * @milli_period: period between subsequent invocation of the timeout.
 * This may be -1 to indicate that the timeout is a one-shot.
 *
 * Add a timeout function to the main-loop.
 * This is a function that will be called after
 * a fixed amount of time passes, and then may be called
 * at a regular interval thereafter.
 *
 * returns: #GskSource which can be removed or altered.
 */
#undef gsk_main_loop_add_timer
GskSource *
gsk_main_loop_add_timer64  (GskMainLoop       *main_loop,
			    GskMainLoopTimeoutFunc timer_func,
			    gpointer           timer_data,
			    GDestroyNotify     timer_destroy,
			    gint64             millis_expire,
			    gint64             milli_period)
{
  GskSource *source = gsk_source_new (GSK_SOURCE_TYPE_TIMER, main_loop, timer_data, timer_destroy);
  GskSource *unused;
  source->data.timer.expire_time = main_loop->current_time;
  g_time_val_add_millis (&source->data.timer.expire_time, millis_expire);
  source->data.timer.milli_period = milli_period;
  source->data.timer.func = timer_func;
  source->timer_adjusted_while_running = FALSE;
  GSK_RBTREE_INSERT (GET_MAIN_LOOP_TIMER_TREE (main_loop), source, unused);
  source->timer_in_tree = 1;
  main_loop->num_sources++;
  return source;
}
GskSource *
gsk_main_loop_add_timer    (GskMainLoop       *main_loop,
			    GskMainLoopTimeoutFunc timer_func,
			    gpointer           timer_data,
			    GDestroyNotify     timer_destroy,
			    gint               millis_expire,
			    gint               milli_period)
{
  return gsk_main_loop_add_timer64 (main_loop,
                                    timer_func, timer_data, timer_destroy,
                                    millis_expire, milli_period);
}

/**
 * gsk_main_loop_add_timer_absolute:
 * @main_loop: the main-loop which should keep track and run the timeout.
 * @timer_func: function to call when the requested amount of time elapses.
 * @timer_data: data to pass to @timer_func.
 * @timer_destroy: optional function to call to destroy the @timer_data.
 * @unixtime: number of seconds since Jan 1, 1970 GMT that will have passed when the 
 * timer should expire.
 * @unixtime_micro: fractional part of @unixtime, in microseconds.
 *
 * Add a timeout function to the main-loop.
 * The @timer_func will be called as soon as we detect that the specified time has passed.
 *
 * The time to wait until is (@unixtime + @unixtime_micro * 10^{-6}) seconds after
 * New Years, Jan 1, 1970 GMT.
 *
 * returns: a #GskSource which can be removed or altered.
 */
GskSource *
gsk_main_loop_add_timer_absolute (GskMainLoop       *main_loop,
				  GskMainLoopTimeoutFunc timer_func,
				  gpointer           timer_data,
				  GDestroyNotify     timer_destroy,
				  int                unixtime,
				  int                unixtime_micro)
{
  GskSource *source = gsk_source_new (GSK_SOURCE_TYPE_TIMER, main_loop, timer_data, timer_destroy);
  GskSource *unused;
  source->data.timer.expire_time.tv_sec = unixtime;
  source->data.timer.expire_time.tv_usec = unixtime_micro;
  source->data.timer.milli_period = -1;
  source->data.timer.func = timer_func;
  source->timer_adjusted_while_running = 0;
  GSK_RBTREE_INSERT (GET_MAIN_LOOP_TIMER_TREE (main_loop), source, unused);
  source->timer_in_tree = 1;
  main_loop->num_sources++;
  return source;
}

/**
 * gsk_source_adjust_timer:
 * @timer_source: the timeout source returned by gsk_main_loop_add_timer() or gsk_main_loop_add_timer_absolute().
 * @millis_expire: the number of milliseconds from now that the timer should run.
 * @milli_period: the period between subsequent runs of the timer, or -1 to indicate that 
 * the timer is a one-shot.
 *
 * Adjust the timeout and period for an already existing timer source.
 * (You may only call this on timer sources.)
 */
#undef gsk_source_adjust_timer                              /* compatibility hack */
#define gsk_source_adjust_timer gsk_source_adjust_timer64   /* compatibility hack */
void
gsk_source_adjust_timer   (GskSource         *timer_source,
			   gint64             millis_expire,
			   gint64             milli_period)
{
  GskMainLoop *main_loop = timer_source->main_loop;
  g_return_if_fail (timer_source->type == GSK_SOURCE_TYPE_TIMER);
  if (timer_source->timer_in_tree)
    {
      GSK_RBTREE_REMOVE (GET_MAIN_LOOP_TIMER_TREE (main_loop), timer_source);
      timer_source->timer_in_tree = 0;
    }
  timer_source->data.timer.expire_time = main_loop->current_time;
  g_time_val_add_millis (&timer_source->data.timer.expire_time, millis_expire);
  timer_source->data.timer.milli_period = milli_period;
  if (timer_source->run_count == 0)
    {
      if (!timer_source->timer_in_tree)
        {
          GskSource *unused;
          GSK_RBTREE_INSERT (GET_MAIN_LOOP_TIMER_TREE (main_loop),
                             timer_source,
                             unused);
          timer_source->timer_in_tree = 1;
        }
    }
  else
    timer_source->timer_adjusted_while_running = 1;
}
#undef gsk_source_adjust_timer                  /* compatibility hack */
void                                            /* compatibility hack */
gsk_source_adjust_timer   (GskSource         *timer_source,
			   gint               millis_expire,
			   gint               milli_period)
{
  gsk_source_adjust_timer64 (timer_source, millis_expire, milli_period);
}

static inline void
gsk_main_loop_block_io (GskMainLoop *main_loop,
			GskSource   *source)
{
  switch (source->type)
    {
      case GSK_SOURCE_TYPE_IDLE:
	if (source->data.idle.prev != NULL)
	  source->data.idle.prev->data.idle.next = source->data.idle.next;
	else
	  main_loop->first_idle = source->data.idle.next;
	if (source->data.idle.next != NULL)
	  source->data.idle.next->data.idle.prev = source->data.idle.prev;
	else
	  main_loop->last_idle = source->data.idle.prev;
	break;

      case GSK_SOURCE_TYPE_TIMER:
        if (source->timer_in_tree)
          {
            GSK_RBTREE_REMOVE (GET_MAIN_LOOP_TIMER_TREE (main_loop), source);
            source->timer_in_tree = 0;
          }
	break;

      case GSK_SOURCE_TYPE_IO:
	{
	  int fd = source->data.io.fd;
	  GIOCondition old_events = get_io_events (main_loop, fd);
	  if ((source->data.io.events & G_IO_IN) == G_IO_IN)
	    {
	      g_return_if_fail (main_loop->read_sources->pdata[fd] == source);
	      main_loop->read_sources->pdata[fd] = NULL;
	    }
	  if ((source->data.io.events & G_IO_OUT) == G_IO_OUT)
	    {
	      g_return_if_fail (main_loop->write_sources->pdata[fd] == source);
	      main_loop->write_sources->pdata[fd] = NULL;
	    }
	  gsk_main_loop_change_io (main_loop, old_events, fd);
	}
	break;

      case GSK_SOURCE_TYPE_SIGNAL:
	{
	  int signo = source->data.signal.number;
	  if (source->data.signal.prev != NULL)
	    source->data.signal.prev->data.signal.next = source->data.signal.next;
	  else
	    {
	      main_loop->signal_source_lists->pdata[signo]
		= source->data.signal.next;
	      if (main_loop->signal_source_lists->pdata[signo] == NULL)
		gsk_main_loop_change_signal (main_loop, signo, FALSE);
	    }
	  if (source->data.signal.next != NULL)
	    source->data.signal.next->data.signal.prev = source->data.signal.prev;
	  break;
	}

      case GSK_SOURCE_TYPE_PROCESS:
	{
	  guint pid = source->data.process.process_id;
	  if (source->data.process.prev != NULL)
	    source->data.process.prev->data.process.next
	      = source->data.process.next;
	  else
	    {
	      if (source->data.process.next == NULL)
		{
		  g_hash_table_remove (main_loop->process_source_lists,
				       GUINT_TO_POINTER (pid));
                  if (g_hash_table_lookup (main_loop->alive_pids,
                                           GUINT_TO_POINTER (pid)))
                    {
		      gsk_main_loop_change_process (main_loop, pid, FALSE, FALSE);
                      g_hash_table_remove (main_loop->alive_pids,
                                           GUINT_TO_POINTER (pid));
                    }
                  else
		    gsk_main_loop_change_process (main_loop, pid, TRUE, FALSE);
		}
	      else
		g_hash_table_insert (main_loop->process_source_lists,
				     GUINT_TO_POINTER (pid),
				     source->data.process.next);
	    }
	  if (source->data.process.next != NULL)
	    source->data.process.next->data.process.prev
	      = source->data.process.prev;
	  break;
	}
    }
}

/**
 * gsk_source_peek_main_loop:
 * @source: the source to query.
 *
 * Get the main-loop where the source was created.
 *
 * returns: the main-loop associated with the source.
 */
GskMainLoop *gsk_source_peek_main_loop (GskSource *source)
{
  return source->main_loop;
}

/**
 * gsk_source_remove:
 * @source: the source to remove and destroy.
 *
 * Destroy a main loop's source.
 *
 * If the source is currently running,
 * it's destroy method will not be called until the
 * source's callback returns.  (This way, important data won't be 
 * deleted unexpectedly in the middle of the user's callback.)
 */
void
gsk_source_remove (GskSource         *source)
{
  GskMainLoop *main_loop = source->main_loop;
  if (source->run_count > 0)
    {
      /* Remove fd interest immediately, even if reenterring.

         This is important because the
         caller may be planning to close the file descriptor. */
      if (source->type == GSK_SOURCE_TYPE_IO)
        {
          int fd = source->data.io.fd;
	  GIOCondition old_events = get_io_events (main_loop, fd);
          if (old_events)
            {
              if (source->data.io.events & G_IO_IN)
                main_loop->read_sources->pdata[fd] = NULL;
              if (source->data.io.events & G_IO_OUT)
                main_loop->write_sources->pdata[fd] = NULL;
              source->data.io.events = 0;
              gsk_main_loop_change_io (main_loop, old_events, fd);
            }
        }
      source->must_remove = 1;
      return;
    }

  if (!source->is_destroyed)
    {
      source->is_destroyed = 1;
      if (source->destroy != NULL)
	(*source->destroy) (source->user_data);
    }

  /* remove from lists and change i/o handling */
  gsk_main_loop_block_io (main_loop, source);
  main_loop->num_sources--;

  gsk_source_free (source);
}

/**
 * gsk_main_loop_quit:
 * @main_loop: the main-loop which is being asked to quit.
 *
 * Set the main-loop flag that indicates that it should really stop running.
 *
 * If you are executing a GskMainLoop using gsk_main_loop_run(),
 * then you should probably check gsk_main_loop_should_continue() at every iteration
 * to ensure that you should not have quit by now.
 */
void
gsk_main_loop_quit (GskMainLoop       *main_loop)
{
  main_loop->quit = 1;
}

/**
 * gsk_main_loop_should_continue:
 * @main_loop: the main-loop to query.
 *
 * Query whether the main-loop should keep running or not.
 *
 * returns: whether to keep running this main-loop.
 */
gboolean
gsk_main_loop_should_continue (GskMainLoop       *main_loop)
{
#if 0
  if (main_loop->quit)
    return FALSE;
  return main_loop->num_sources > 0 || main_loop->first_context != NULL;
#else
  return !main_loop->quit;
#endif
}

/* XXX: this kind of trick should be done constantly (whenever a source
        of the appropriate type is removed) */
static void
check_if_all_handlers_clear (GskMainLoop *main_loop)
{
  guint max_len;
  guint i;

  /* if all signals are untrapped, reset the array */
  max_len = 0;
  for (i = 0; i < main_loop->signal_source_lists->len; i++)
    if (main_loop->signal_source_lists->pdata[i] != NULL)
      max_len = i + 1;
  g_ptr_array_set_size (main_loop->signal_source_lists, max_len);

  /* if all fds are untrapped, reset the array */
  max_len = 0;
  for (i = 0; i < main_loop->read_sources->len; i++)
    if (main_loop->read_sources->pdata[i] != NULL
     || main_loop->write_sources->pdata[i] != NULL)
      max_len = i + 1;
  g_ptr_array_set_size (main_loop->read_sources, max_len);
  g_ptr_array_set_size (main_loop->write_sources, max_len);
}

void
gsk_main_loop_destroy_all_sources (GskMainLoop *main_loop)
{
  /* temporary variables */
  GskSource *at;
  GSList *list, *at_list;
  guint i;

  /* Destroy idle functions */
  at = main_loop->first_idle;
  while (at != NULL)
    {
      GskSource *next = at->data.idle.next;
      if (next != NULL)
	next->run_count++;
      gsk_source_remove (at);
      if (next != NULL)
	next->run_count--;

      at = next;     /* possibly must_remove is set, but it's ok */
    }

  /* Destroy timers */
  while (main_loop->timers)
    gsk_source_remove (main_loop->timers);

  /* Destroy i/o handlers */
  for (i = 0; i < main_loop->read_sources->len; i++)
    {
      at = main_loop->read_sources->pdata[i];
      if (at != NULL)
	gsk_source_remove (at);
      at = main_loop->write_sources->pdata[i];
      if (at != NULL)
	gsk_source_remove (at);
    }

  /* Destroy signal handlers */
  for (i = 0; i < main_loop->signal_source_lists->len; i++)
    {
      at = main_loop->signal_source_lists->pdata[i];
      while (at != NULL)
	{
	  GskSource *next = at->data.signal.next;
	  if (next != NULL)
	    next->run_count++;
	  gsk_source_remove (at);
	  if (next != NULL)
	    next->run_count--;

	  at = next;     /* possibly must_remove is set, but it's ok */
	}
    }


  /* Destroy waitpid handlers */
  list = gsk_g_hash_table_key_slist (main_loop->process_source_lists);
  for (at_list = list; at_list != NULL; at_list = at_list->next)
    {
      at = g_hash_table_lookup (main_loop->process_source_lists, at_list->data);
      while (at != NULL)
	{
	  GskSource *next = at->data.idle.next;
	  if (next != NULL)
	    next->run_count++;
	  gsk_source_remove (at);
	  if (next != NULL)
	    next->run_count--;

	  at = next;     /* possibly must_remove is set, but it's ok */
	}
    }
  g_slist_free (list);

  check_if_all_handlers_clear (main_loop);
}

#define CHECK_INVARIANTS(main_loop)				              \
  G_STMT_START{							              \
    g_assert (main_loop->write_sources->len == main_loop->read_sources->len); \
  }G_STMT_END

static void
gsk_main_loop_finalize (GObject *object)
{
  GskMainLoop *main_loop = GSK_MAIN_LOOP (object);
  gsk_main_loop_destroy_all_sources (main_loop);

  g_assert (main_loop->first_idle == NULL);
  g_assert (main_loop->last_idle == NULL);
  g_assert (main_loop->timers == NULL);
  g_assert (g_hash_table_size (main_loop->process_source_lists) == 0);
  g_assert (main_loop->running_source == NULL);
  CHECK_INVARIANTS (main_loop);

  g_hash_table_destroy (main_loop->process_source_lists);
  g_ptr_array_free (main_loop->read_sources, TRUE);
  g_ptr_array_free (main_loop->write_sources, TRUE);
  g_ptr_array_free (main_loop->signal_source_lists, TRUE);
  g_free (main_loop->event_array_cache);

  g_hash_table_destroy (main_loop->alive_pids);

  (*parent_class->finalize) (object);
}

/* --- functions --- */
static void
gsk_main_loop_init (GskMainLoop *main_loop)
{
  main_loop->timers = NULL;
  main_loop->read_sources = g_ptr_array_new ();
  main_loop->write_sources = g_ptr_array_new ();
  main_loop->signal_source_lists = g_ptr_array_new ();
  main_loop->process_source_lists = g_hash_table_new (NULL, NULL);
  main_loop->alive_pids = g_hash_table_new (NULL, NULL);
  main_loop->max_events = INITIAL_MAX_EVENTS;
  main_loop->event_array_cache = g_new (GskMainLoopEvent, main_loop->max_events);
  gsk_main_loop_update_current_time (main_loop);
}

static void
gsk_main_loop_class_init (GskMainLoopClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  parent_class = g_type_class_peek_parent (class);
  object_class->finalize = gsk_main_loop_finalize;
}

GType gsk_main_loop_get_type()
{
  static GType main_loop_type = 0;
  if (!main_loop_type)
    {
      static const GTypeInfo main_loop_info =
      {
	sizeof(GskMainLoopClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_main_loop_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskMainLoop),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_main_loop_init,
	NULL		/* value_table */
      };
      GType parent = G_TYPE_OBJECT;
      main_loop_type = g_type_register_static (parent,
                                                  "GskMainLoop",
						  &main_loop_info, 0);
    }
  return main_loop_type;
}

/* --- GskMainLoopWaitInfo type registration --- */
static GskMainLoopWaitInfo *
gsk_main_loop_wait_info_copy (const GskMainLoopWaitInfo *wait_info)
{
  return g_memdup (wait_info, sizeof (GskMainLoopWaitInfo));
}

GType gsk_main_loop_wait_info_get_type(void)
{
  static GType rv = 0;
  if (rv == 0)
    rv = g_boxed_type_register_static ("GskMainLoopWaitInfo",
                                       (GBoxedCopyFunc) gsk_main_loop_wait_info_copy,
                                       (GBoxedFreeFunc) g_free);
  return rv;
}


/* --- Default Construction --- */
#include "main-loops/gskmainloopkqueue.h"
#include "main-loops/gskmainloopdevpoll.h"
#include "main-loops/gskmainloopepoll.h"
#include "main-loops/gskmainlooppoll.h"
#include "main-loops/gskmainloopselect.h"

static struct
{
  GType (*get_type_func) () G_GNUC_CONST;
  const char *env_value;
  gboolean supports_threads;
} main_loop_types[] = {
#if HAVE_EPOLL_SUPPORT
  { gsk_main_loop_epoll_get_type,    "epoll",   TRUE  },
#endif
#if HAVE_SYS_DEV_POLL
  { gsk_main_loop_dev_poll_get_type, "devpoll", TRUE  },
#endif
#if HAVE_KQUEUE
  { gsk_main_loop_kqueue_get_type,   "kqueue",  FALSE },
#endif
#if HAVE_POLL
  { gsk_main_loop_poll_get_type,     "poll",    TRUE  },
#endif
#if HAVE_SELECT
  { gsk_main_loop_select_get_type,   "select",  TRUE  },
#endif
  { NULL, NULL, FALSE }
};


GskMainLoop *
gsk_main_loop_new (GskMainLoopCreateFlags create_flags)
{
  gboolean threads = (create_flags & GSK_MAIN_LOOP_NEEDS_THREADS) != 0;
  GType type = 0;
  /* Try the GSK_MAIN_LOOP_TYPE environment variable */
  guint i = 0;
  const char *env_type = g_getenv ("GSK_MAIN_LOOP_TYPE");

  if (env_type != NULL)
    {
      GSK_SKIP_WHITESPACE (env_type);
      if (*env_type == '\0')
	env_type = NULL;
    }

  if (env_type != NULL)
    {
      GskMainLoop *main_loop;
      while (main_loop_types[i].get_type_func != NULL)
	{
	  if (strcmp (env_type, main_loop_types[i].env_value) == 0
	   && (!threads || main_loop_types[i].supports_threads))
	    {
	      type = (*main_loop_types[i].get_type_func) ();
	      break;
	    }
	  i++;
	}
      if (type == 0)
	{
	  if (strcmp (env_type, "kqueue") == 0)
	    g_warning ("kqueue doesn't support threads; "
		       "falling back to poll");
	  else
	    g_warning ("GSK_MAIN_LOOP_TYPE set to %s: unsupported", env_type);
	}
      else
	{
	  GskMainLoopClass *class;
	  main_loop = g_object_new (type, NULL);
	  class = GSK_MAIN_LOOP_GET_CLASS (main_loop);
	  if (class->setup == NULL || (*class->setup) (main_loop))
	    return main_loop;
	  else
	    {
	      g_warning ("could not setup main-loop of type %s", env_type);
	    }
	  g_object_unref (main_loop);
	}
    }

  /* Try autoconf-detected defaults, in our preferred order. */
  for (i = 0; main_loop_types[i].get_type_func != NULL; i++)
    {
      if (!threads || main_loop_types[i].supports_threads)
	{
	  GskMainLoop *main_loop;
	  GskMainLoopClass *class;
	  type = (*main_loop_types[i].get_type_func) ();
	  main_loop = g_object_new (type, NULL);
	  class = GSK_MAIN_LOOP_GET_CLASS (main_loop);
	  main_loop->is_setup = 1;
	  if (class->setup != NULL && !((*class->setup) (main_loop)))
	    {
	      g_object_unref (main_loop);
	      continue;
	    }
	  return main_loop;
	}
      i++;
    }

  g_warning ("No type of GskMainLoop can be constructed");
  return NULL;
}

/* --- GMainLoop support --- */
/**
 * gsk_main_loop_add_context:
 * @main_loop: main-loop which will take responsibility for @context.
 * @context: a GMainContext that should be handled by gsk_main_loop_run().
 * 
 * Indicate that a particular #GskMainLoop will take care of
 * invoking the necessary methods of @context.
 */
void
gsk_main_loop_add_context (GskMainLoop  *main_loop,
			   GMainContext *context)
{
  GskMainLoopContextList *node = g_new (GskMainLoopContextList, 1);
  node->context = context;
  node->num_fds_alloced = 16;
  node->poll_fds = g_new (GPollFD, node->num_fds_alloced);
  node->sources = g_new (GskSource *, node->num_fds_alloced);
  node->num_fds_received = 0;
  node->priority = G_PRIORITY_DEFAULT;

  node->next = NULL;
  if (main_loop->last_context != NULL)
    main_loop->last_context->next = node;
  else
    main_loop->first_context = node;
  main_loop->last_context = node;
}

/* --- per-thread default main-loop --- */

/* globals for non-threaded main-loops */
static GskMainLoop *non_thread_main_loop = NULL;

/* globals for per-thread main-loops */
static GPrivate *private_main_loop_key;

/**
 * gsk_main_loop_default:
 *
 * Get the main-loop which is associated with the current thread.
 *
 * returns: a pointer to the main-loop.  This function does not
 * increase the ref-count on the main-loop, so you do not need 
 * to call g_object_unref() on the return value.
 */
GskMainLoop *
gsk_main_loop_default ()
{
  if (gsk_init_get_support_threads ())
    {
      GskMainLoop *main_loop = g_private_get (private_main_loop_key);
      if (main_loop != NULL)
	return main_loop;
      main_loop = gsk_main_loop_new (GSK_MAIN_LOOP_NEEDS_THREADS);
      g_assert (main_loop != NULL);
      g_private_set (private_main_loop_key, main_loop);
      return main_loop;
    }
  else
    {
      if (non_thread_main_loop != NULL)
	return non_thread_main_loop;
      non_thread_main_loop = gsk_main_loop_new (0);
      g_assert (non_thread_main_loop != NULL);
      return non_thread_main_loop;
    }
}

/* --- fork wrapper --- */
void
_gsk_main_loop_fork_notify ()
{
  //g_message ("_gsk_main_loop_fork_notify: pid=%u, support-threads=%u", getpid(), gsk_init_get_support_threads ());
  if (gsk_init_get_support_threads ())
    {
      GskMainLoop *main_loop = g_private_get (private_main_loop_key);
      if (main_loop == NULL)
	return;
      g_private_set (private_main_loop_key, NULL);
    }
  else
    {
      /* NOTE: we don't free the memory for the main-loop,
	 but it should not amount to much.

	 However, critical resources that may not be shared,
	 like file-descriptors should probably be registered
	 with the gsk_fork_* functions. */

      non_thread_main_loop = NULL;
    }
}

/* --- initialization --- */
void
_gsk_main_loop_init (void)
{
  if (gsk_init_get_support_threads ())
    private_main_loop_key = g_private_new (g_object_unref);
}
