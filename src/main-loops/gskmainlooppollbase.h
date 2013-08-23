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

/* GskMainLoopPollBase: A main loop based around a system call like poll(2)
 *    or select(2) that calls a `poll' function with a collection of file
 *    descriptors.  This class implements signal handling and process-end
 *    notification using the normal unix EINTR mechanism.
 */

/* NOTE: everything here is private, except GskMainLoopPollBaseClass,
 *       which is protected. (this exposition is to permit derivation)
 */
 

#ifndef __GSK_MAIN_LOOP_POLL_BASE_H_
#define __GSK_MAIN_LOOP_POLL_BASE_H_


/* This is a derived class of GskMainLoop that uses
 * poll(2) or select(2) or similar internally.
 */

/* Note: when deriving from this class,
 *       note that its finalize method will call `config_fd(0)'
 *       perhaps, so should chain the finalize method *first*
 */

#include "../gskmainloop.h"
#include "../gskbuffer.h"

G_BEGIN_DECLS

/* --- type macros --- */
GType gsk_main_loop_poll_base_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MAIN_LOOP_POLL_BASE			(gsk_main_loop_poll_base_get_type ())
#define GSK_MAIN_LOOP_POLL_BASE(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_MAIN_LOOP_POLL_BASE, GskMainLoopPollBase))
#define GSK_MAIN_LOOP_POLL_BASE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_MAIN_LOOP_POLL_BASE, GskMainLoopPollBaseClass))
#define GSK_MAIN_LOOP_POLL_BASE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_MAIN_LOOP_POLL_BASE, GskMainLoopPollBaseClass))
#define GSK_IS_MAIN_LOOP_POLL_BASE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_MAIN_LOOP_POLL_BASE))
#define GSK_IS_MAIN_LOOP_POLL_BASE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_MAIN_LOOP_POLL_BASE))

/* --- Typedefs & structures --- */
typedef struct _GskMainLoopPollBase GskMainLoopPollBase;
typedef struct _GskMainLoopPollBaseClass GskMainLoopPollBaseClass;


/* --- GskMainLoopPollBase structures --- */
struct _GskMainLoopPollBaseClass 
{
  GskMainLoopClass    main_loop_class;

  void              (*config_fd)       (GskMainLoopPollBase   *main_loop,
                                        int                    fd,
					GIOCondition           old_io_conditions,
				        GIOCondition           io_conditions);

  /* returns FALSE if the poll function has an error.
   */
  gboolean          (*do_polling)      (GskMainLoopPollBase   *main_loop,
				        int                    max_timeout,
				        guint                  max_events,
				        guint                 *num_events_out,
                                        GskMainLoopEvent      *events);
};

struct _GskMainLoopPollBase
{
  GskMainLoop  	   main_loop;

  /*< private >*/

  /* signals that have been raised, as int's */
  GskBuffer        signal_ids;

  /* process-termination notifications in the queue */
  GskBuffer        process_term_notifications;

  /* a pipe which can be written to wake up the main-loop synchronously */
  GskSource       *wakeup_read_pipe;
  gint             wakeup_read_fd;
  gint             wakeup_write_fd;
  
  /* whether we need to manually try waitpid() calls
     at the start of the next iteration. */
  guint try_waitpid : 1;
};

/* this function is multi-thread and signal safe! */
void gsk_main_loop_poll_base_wakeup (GskMainLoopPollBase *poll_base);

G_END_DECLS

#endif
