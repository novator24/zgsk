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

#ifndef __GSK_MAIN_LOOP_KQUEUE_H_
#define __GSK_MAIN_LOOP_KQUEUE_H_

#include "../gskmainloop.h"

G_BEGIN_DECLS

typedef struct _GskMainLoopKqueue GskMainLoopKqueue;
typedef struct _GskMainLoopKqueueClass GskMainLoopKqueueClass;

/* --- type macros --- */
GType gsk_main_loop_kqueue_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MAIN_LOOP_KQUEUE			(gsk_main_loop_kqueue_get_type ())
#define GSK_MAIN_LOOP_KQUEUE(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_MAIN_LOOP_KQUEUE, GskMainLoopKqueue))
#define GSK_MAIN_LOOP_KQUEUE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_MAIN_LOOP_KQUEUE, GskMainLoopKqueueClass))
#define GSK_MAIN_LOOP_KQUEUE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_MAIN_LOOP_KQUEUE, GskMainLoopKqueueClass))
#define GSK_IS_MAIN_LOOP_KQUEUE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_MAIN_LOOP_KQUEUE))
#define GSK_IS_MAIN_LOOP_KQUEUE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_MAIN_LOOP_KQUEUE))


struct _GskMainLoopKqueueClass
{
  GskMainLoopClass	main_loop_class;
};

struct _GskMainLoopKqueue
{
  GskMainLoop		main_loop;

  guint                 num_updates;
  guint                 max_updates;
  gpointer              kevent_array;

  /* this may be used by init_polling / do_polling as the class desires,
   * except you must reset it to -1 before finalizing, or else
   * we will close(2) it.
   */
  int			kernel_queue_id;
};

G_END_DECLS

#endif
