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

#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <errno.h>
#include "gskmainlooppollbase.h"
#include "../gskerrno.h"
#include "../gskghelpers.h"
#include <unistd.h>		/* for pipe(2), write(2) */

/* What makes a main-loop type a PollBase main-loop.
   Answer: the main-loop mechanism has no explicit signal
   or process-termination notification.

   Hence they can share a default hack-ish implementation,
   which we now document.
 */

/* Signal and Process termination strategy.

   Basically we keep an array of file-descriptors, usually one per main-loop,
   which are used to write when the signal-handler is run.

   This means only one main-loop will get notification directly 
   from the signal.  So that main-loop must be set to 
   propagate the notification around.

   In order to avoid a thundering-herd problem with waitpid notification,
   we have dispatch all waitpid's from a single-thread!  ('dispatch' means: add to their
   waitpid_buffer and wake them up).
 */

typedef guint8 AtomicSignalType;
typedef guint SignalType;

static volatile GArray *signal_fds;
static GHashTable *signal_no_to_slist_of_mainloops = NULL;
G_LOCK_DEFINE_STATIC (signal_fds);


static GskSource *waitpid_dispatcher = NULL;
static GHashTable *pid_to_slist_of_mainloops = NULL;
G_LOCK_DEFINE_STATIC (waitpid_dispatcher);

#define SIGNAL_FDS_INDEX_FROM_SIGNAL_NO(no)  ((((SignalType)(no)) >> (sizeof(AtomicSignalType)*8-1)) >> 1)
#define SIGNAL_FDS_INDEX_TO_HIGH_BITS(no)    ((((SignalType)(no)) << (sizeof(AtomicSignalType)*8-1)) << 1)

static GObjectClass *parent_class = NULL;

/* --- Block/unblock a single signal. --- */

static void
reentrant_handle_signal (int signal_id)
{
  AtomicSignalType sig_short = (AtomicSignalType) signal_id;
  volatile GArray *sfds = signal_fds;
  if (sfds->len > SIGNAL_FDS_INDEX_FROM_SIGNAL_NO (signal_id))
    {
      int fd = g_array_index (sfds, int, SIGNAL_FDS_INDEX_FROM_SIGNAL_NO (signal_id));
      int rv;
      if (fd >= 0)
	{
	  rv = write (fd, &sig_short, sizeof (AtomicSignalType));
	  if (rv > 0 && rv < (int) sizeof (AtomicSignalType))
	    {
	      g_warning ("could not write AtomicSignalType atomicallly");
	    }
	}
    }
}

static gboolean
gsk_main_loop_handle_sigchld (int       sig_no,
			      gpointer  user_data)
{
  GskMainLoopWaitInfo wait_info;
  g_assert (sig_no == SIGCHLD && user_data == NULL);
  while (gsk_main_loop_do_waitpid (-1, &wait_info))
    {
      GSList *at;
      G_LOCK (waitpid_dispatcher);
      for (at = g_hash_table_lookup (pid_to_slist_of_mainloops,
				     GUINT_TO_POINTER (wait_info.pid));
	   at != NULL;
	   at = at->next)
	{
	  GskMainLoopPollBase *pb = GSK_MAIN_LOOP_POLL_BASE (at->data);
	  gsk_buffer_append (&pb->process_term_notifications, &wait_info, sizeof (wait_info));
	  gsk_main_loop_poll_base_wakeup (pb);
	}
      G_UNLOCK (waitpid_dispatcher);
    }
  return TRUE;
}

static gboolean
handle_signal_pipe_input  (int                   fd,
			   GIOCondition          condition,
			   gpointer              user_data)
{
  guint high_bits;
  AtomicSignalType atomic_sigs[1024];
  int n_bytes_read;
  guint i;

  if ((condition & G_IO_IN) != G_IO_IN)
    return TRUE;

  G_LOCK (signal_fds);
  high_bits = GPOINTER_TO_UINT (user_data);
  n_bytes_read = read (fd, atomic_sigs, sizeof (atomic_sigs));
  if (n_bytes_read < 0)
    {
      if (gsk_errno_is_ignorable (errno))
	{
	  G_UNLOCK (signal_fds);
	  return TRUE;
	}
      g_warning ("error reading from signal pipe");
      G_UNLOCK (signal_fds);
      return FALSE;
    }
  if (n_bytes_read == 0)
    {
      g_warning ("unexpected end-of-file from signal pipe");
      G_UNLOCK (signal_fds);
      return FALSE;
    }
  if ((n_bytes_read % sizeof (AtomicSignalType)) != 0)
    {
      g_warning ("did not get an integer number of signal-ids from pipe");
    }

  for (i = 0; i < n_bytes_read / sizeof(AtomicSignalType); i++)
    {
      int sig = high_bits | (SignalType) (AtomicSignalType) atomic_sigs[i];
      GSList *loops;
      for (loops = g_hash_table_lookup (signal_no_to_slist_of_mainloops, GUINT_TO_POINTER (sig));
	   loops != NULL;
	   loops = loops->next)
	{
	  GskMainLoopPollBase *pb = GSK_MAIN_LOOP_POLL_BASE (loops->data);
	  gsk_buffer_append (&pb->signal_ids, &sig, sizeof (int));
	  gsk_main_loop_poll_base_wakeup (pb);
	}
    }
  G_UNLOCK (signal_fds);
  return TRUE;
}

static gboolean
handle_wakeup (int fd, GIOCondition condition, gpointer user_data)
{
  char buf[4096];
  g_return_val_if_fail (GSK_IS_MAIN_LOOP_POLL_BASE (user_data), FALSE);
  if ((condition & G_IO_IN) != G_IO_IN)
    return TRUE;
  while (read (fd, buf, sizeof (buf)) == sizeof (buf))
    ;
  return TRUE;
}

static void
gsk_main_loop_poll_base_init_wakeup (GskMainLoopPollBase *poll_base)
{
  int pipe_fds[2];
  g_return_if_fail (poll_base->wakeup_write_fd == -1);
  g_return_if_fail (poll_base->wakeup_read_fd == -1);
  g_return_if_fail (poll_base->wakeup_read_pipe == NULL);

  if (pipe (pipe_fds) < 0)
    {
      g_warning ("error creating wakeup pipe");
      return;
    }
  gsk_fd_set_nonblocking (pipe_fds[0]);
  gsk_fd_set_nonblocking (pipe_fds[1]);
  poll_base->wakeup_read_fd = pipe_fds[0];
  poll_base->wakeup_write_fd = pipe_fds[1];
  poll_base->wakeup_read_pipe = gsk_main_loop_add_io (GSK_MAIN_LOOP (poll_base),
						      poll_base->wakeup_read_fd,
						      G_IO_IN,
						      handle_wakeup,
						      poll_base,
						      NULL);
}



static void
gsk_main_loop_poll_base_change     (GskMainLoop           *main_loop,
		                    GskMainLoopChange     *change)
{
  GskMainLoopPollBase *poll_base = GSK_MAIN_LOOP_POLL_BASE (main_loop);
  switch (change->type)
    {
    case GSK_MAIN_LOOP_EVENT_SIGNAL:
      {
	guint signal_number = change->data.signal.number;
	gboolean add = change->data.signal.add;
	struct sigaction sigaction_info;

	if (poll_base->wakeup_write_fd == -1)
	  gsk_main_loop_poll_base_init_wakeup (poll_base);

	if (add)
	  {
	    GSList *old_list;
	    guint signal_fd_index = SIGNAL_FDS_INDEX_FROM_SIGNAL_NO (signal_number);
	    G_LOCK (signal_fds);
	    old_list = g_hash_table_lookup (signal_no_to_slist_of_mainloops,
					    GUINT_TO_POINTER (signal_number));
	    if (old_list == NULL)
	      {
		struct sigaction action;

		g_hash_table_insert (signal_no_to_slist_of_mainloops,
				     GUINT_TO_POINTER (signal_number),
				     g_slist_prepend (NULL, main_loop));

		/* set-up signal's fd */
		if (signal_fds->len <= signal_fd_index
		 || g_array_index (signal_fds, int, signal_fd_index) == -1)
		  {
		    int pipe_fds[2];
		    GskSource *signal_source;
		    if (pipe (pipe_fds) < 0)
		      {
			g_warning ("error creating signal-pipe: %s", g_strerror (errno));
			return;		/* yikes */
		      }
		    gsk_fd_set_nonblocking (pipe_fds[0]);
		    gsk_fd_set_nonblocking (pipe_fds[1]);
		    signal_source = gsk_main_loop_add_io (main_loop, pipe_fds[0], G_IO_IN,
							  handle_signal_pipe_input, 
							  GUINT_TO_POINTER (SIGNAL_FDS_INDEX_TO_HIGH_BITS (signal_fd_index)), NULL);
		    if (signal_fds->len <= signal_fd_index)
		      {
			GArray *sfds = g_array_new (FALSE, FALSE, sizeof (int));
			volatile GArray *old_sfds;
			int minus_1 = -1;
			g_array_set_size (sfds, signal_fds->len);
			memcpy (sfds->data, signal_fds->data, sizeof (int) * signal_fds->len);
			while (sfds->len < signal_fd_index)
			  g_array_append_val (sfds, minus_1);
			g_array_append_val (sfds, pipe_fds[1]);

			old_sfds = signal_fds;
			signal_fds = sfds;

			// XXX: leak memory here for the moment,
			// since it's not really clear when it will
			// be unused by any signal handler.
			//g_array_free (old_sfds);
		      }
		    else
		      {
			g_array_index (signal_fds, int, signal_fd_index) = pipe_fds[1];
		      }
		  }

		/* trap signal using sigaction */
		action.sa_handler = reentrant_handle_signal;
		action.sa_flags = SA_RESTART;
		sigemptyset (&action.sa_mask);
		sigaction (signal_number, &action, NULL);
	      }
	    else
	      {
		g_slist_append (old_list, main_loop);
	      }
	    G_UNLOCK (signal_fds);
	  }
      else
	{
	  GSList *loops;

	  /* remove from list of signals and stop trapping it */
	  G_LOCK (signal_fds);
	  loops = g_hash_table_lookup (signal_no_to_slist_of_mainloops,
				       GUINT_TO_POINTER (signal_number));
	  loops = g_slist_remove (loops, main_loop);
	  if (loops == NULL)
	    {
	      g_hash_table_remove (signal_no_to_slist_of_mainloops,
				   GUINT_TO_POINTER (signal_number));

	      bzero (&sigaction_info, sizeof (sigaction_info));
	      sigaction_info.sa_handler = SIG_IGN;
	      sigaction (signal_number, &sigaction_info, NULL);
	    }
	  else
	    {
	      g_hash_table_insert (signal_no_to_slist_of_mainloops,
				   GUINT_TO_POINTER (signal_number),
				   loops);
	    }

	  G_UNLOCK (signal_fds);
	}
      break;
    }
    case GSK_MAIN_LOOP_EVENT_IO:
      {
	GskMainLoopPollBase *main_loop_poll_base = (GskMainLoopPollBase *) main_loop;
	GskMainLoopPollBaseClass *class = GSK_MAIN_LOOP_POLL_BASE_GET_CLASS (main_loop);
	(*class->config_fd) (main_loop_poll_base,
			     change->data.io.fd,
			     change->data.io.old_events,
			     change->data.io.events);
	break;
      }
    case GSK_MAIN_LOOP_EVENT_PROCESS:
      {
	if (change->data.process.add)
	  {
	    GSList *old_list;
	    G_LOCK (waitpid_dispatcher);
	    if (waitpid_dispatcher == NULL)
	      {
		waitpid_dispatcher = gsk_main_loop_add_signal (main_loop,
							       SIGCHLD,
							       gsk_main_loop_handle_sigchld,
							       NULL,
							       NULL);
		poll_base->try_waitpid = 1;
	      }
	    old_list = g_hash_table_lookup (pid_to_slist_of_mainloops,
					    GUINT_TO_POINTER (change->data.process.pid));
	    if (old_list == NULL)
	      g_hash_table_insert (pid_to_slist_of_mainloops,
				   GUINT_TO_POINTER (change->data.process.pid),
				   g_slist_prepend (NULL, main_loop));
	    else
	      g_slist_append (old_list, main_loop);
	    G_UNLOCK (waitpid_dispatcher);
	  }
	else
	  {
	    GSList *old_list;
	    G_LOCK (waitpid_dispatcher);
	    old_list = g_hash_table_lookup (pid_to_slist_of_mainloops,
					    GUINT_TO_POINTER (change->data.process.pid));
	    if (g_slist_find (old_list, main_loop) == NULL)
	      g_warning ("could not find that this main-loop was registered to the given process-id");
	    g_hash_table_insert (pid_to_slist_of_mainloops,
				 GUINT_TO_POINTER (change->data.process.pid),
				 g_slist_remove (old_list, main_loop));
	    G_UNLOCK (waitpid_dispatcher);
	  }
      break;

    default:
      g_assert_not_reached ();
    }
    }
}

static guint
gsk_main_loop_poll_base_poll(GskMainLoop        *main_loop,
                             guint               max_events_out,
                             GskMainLoopEvent   *events,
                             gint                timeout)
{
  GskMainLoopPollBase *poll_base = GSK_MAIN_LOOP_POLL_BASE (main_loop);
  GskMainLoopPollBaseClass *class = GSK_MAIN_LOOP_POLL_BASE_GET_CLASS (main_loop);
  guint tmp = 0;
  guint n_init = 0;

  if (poll_base->try_waitpid)
    {
#if 0
      GskMainLoopWaitInfo info;
      g_assert (max_events_out > 0);
      while (gsk_main_loop_do_waitpid (-1, &info))
	{
	  events[n_init].type = GSK_MAIN_LOOP_EVENT_PROCESS;
	  events[n_init].data.process_wait_info = info;
	  n_init++;
	  if (n_init == max_events_out)
	    return n_init;
	}
      events += n_init;
      max_events_out -= n_init;
      if (n_init > 0)
	timeout = 0;
#else
       gsk_main_loop_handle_sigchld (SIGCHLD, NULL);
#endif
      poll_base->try_waitpid = 0;
    }


  if (poll_base->process_term_notifications.size > 0 || poll_base->signal_ids.size > 0)
    timeout = 0;

  if (!(*class->do_polling) (poll_base, timeout, max_events_out, &tmp, events))
    {
      //g_warning ("error polling: %s", g_strerror (errno));
      return n_init;
    }
  
  /* notify of waitpid events */
  while (tmp < max_events_out)
    {
      GskMainLoopWaitInfo wait_info;
      int n_read;
      G_LOCK (waitpid_dispatcher);
      n_read = gsk_buffer_read (&poll_base->process_term_notifications, &wait_info, sizeof (wait_info));
      G_UNLOCK (waitpid_dispatcher);
      if (n_read == 0)
	break;
      g_assert (n_read == sizeof (wait_info));
      events[tmp].type = GSK_MAIN_LOOP_EVENT_PROCESS;
      events[tmp].data.process_wait_info = wait_info;
      tmp++;
    }

  /* notify of signal events */
  while (tmp < max_events_out)
    {
      int n_read;
      int sig;
      G_LOCK (waitpid_dispatcher);
      n_read = gsk_buffer_read (&poll_base->signal_ids, &sig, sizeof (sig));
      G_UNLOCK (waitpid_dispatcher);
      if (n_read == 0)
	break;
      g_assert (n_read == sizeof (sig));
      events[tmp].type = GSK_MAIN_LOOP_EVENT_SIGNAL;
      events[tmp].data.signal = sig;
      tmp++;
    }

  return tmp + n_init;
}

static void    
gsk_main_loop_poll_base_finalize(GObject *object)
{
  GskMainLoopPollBase *poll_base = (GskMainLoopPollBase *) object;
  gsk_main_loop_destroy_all_sources (GSK_MAIN_LOOP (object));
  gsk_buffer_destruct (&poll_base->signal_ids);
  (*parent_class->finalize) (object);
}

/* --- Class Methods --- */
static void gsk_main_loop_poll_base_init (GskMainLoopPollBase* poll_base)
{
  G_LOCK (signal_fds);
  if (signal_fds == NULL)
    {
      GArray *sfds;
      sfds = g_array_new (FALSE, FALSE, sizeof(int));
      g_array_set_size ((GArray *) sfds, 1);
      g_array_index (sfds, int, 0) = -1;
      signal_fds = sfds;
    }
  signal_no_to_slist_of_mainloops = g_hash_table_new (NULL, NULL);
  G_UNLOCK (signal_fds);

  G_LOCK (waitpid_dispatcher);
  if (pid_to_slist_of_mainloops == NULL)
    pid_to_slist_of_mainloops = g_hash_table_new (NULL, NULL);
  G_UNLOCK (waitpid_dispatcher);

  gsk_buffer_construct (&poll_base->process_term_notifications);

  poll_base->wakeup_read_fd = poll_base->wakeup_write_fd = -1;
}

static void
gsk_main_loop_poll_base_class_init (GskMainLoopClass* loop_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (loop_class);
  parent_class = g_type_class_peek_parent (loop_class);
  loop_class->change = gsk_main_loop_poll_base_change;
  loop_class->poll = gsk_main_loop_poll_base_poll;
  object_class->finalize = gsk_main_loop_poll_base_finalize;
}


GType gsk_main_loop_poll_base_get_type()
{
  static GType main_loop_poll_base_type = 0;
  if (!main_loop_poll_base_type)
    {
      static const GTypeInfo main_loop_poll_base_info =
      {
	sizeof(GskMainLoopPollBaseClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_main_loop_poll_base_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskMainLoopPollBase),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_main_loop_poll_base_init,
	NULL		/* value_table */
      };
      GType parent = GSK_TYPE_MAIN_LOOP;
      main_loop_poll_base_type = g_type_register_static (parent,
                                                  "GskMainLoopPollBase",
						  &main_loop_poll_base_info, 
						  G_TYPE_FLAG_ABSTRACT);
    }
  return main_loop_poll_base_type;
}

void
gsk_main_loop_poll_base_wakeup (GskMainLoopPollBase *poll_base)
{
  guint8 dummy;
  g_return_if_fail (poll_base->wakeup_write_fd >= 0);
  write (poll_base->wakeup_write_fd, &dummy, 1);
}

