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
/* GskMainLoopPoll: A main loop based around the poll(2) system call. */


#ifndef __GSK_MAIN_LOOP_POLL_H_
#define __GSK_MAIN_LOOP_POLL_H_

#include "gskmainlooppollbase.h"

G_BEGIN_DECLS

/* --- type macros --- */
GType gsk_main_loop_poll_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MAIN_LOOP_POLL			(gsk_main_loop_poll_get_type ())
#define GSK_MAIN_LOOP_POLL(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_MAIN_LOOP_POLL, GskMainLoopPoll))
#define GSK_MAIN_LOOP_POLL_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_MAIN_LOOP_POLL, GskMainLoopPollClass))
#define GSK_MAIN_LOOP_POLL_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_MAIN_LOOP_POLL, GskMainLoopPollClass))
#define GSK_IS_MAIN_LOOP_POLL(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_MAIN_LOOP_POLL))
#define GSK_IS_MAIN_LOOP_POLL_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_MAIN_LOOP_POLL))


/* --- structures & typedefs --- */
typedef struct _GskMainLoopPoll GskMainLoopPoll;
typedef struct _GskMainLoopPollClass GskMainLoopPollClass;

struct _GskMainLoopPollClass
{
  GskMainLoopPollBaseClass  main_loop_poll_base_class;
};
struct _GskMainLoopPoll
{
  GskMainLoopPollBase 	    main_loop_poll_base;
  GArray                   *poll_fds;
  int                       fd_to_poll_fd_alloced;
  int                      *fd_to_poll_fd_index;
  int                       first_free_index;
};

G_END_DECLS

#endif
