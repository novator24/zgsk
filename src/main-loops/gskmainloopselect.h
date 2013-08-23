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
/* GskMainLoopSelect: A main loop based around the select(2) system call. */

/* Notes:
 * 
            !!! UNTESTED !!!

 * even if it were tested, use would be discouraged in favor of
 * the poll(2) implementation.
 */

#ifndef __GSK_MAIN_LOOP_SELECT_H_
#define __GSK_MAIN_LOOP_SELECT_H_

/* for fd_set */
#include <sys/time.h>
#include <sys/types.h>

#include "gskmainlooppollbase.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskMainLoopSelect GskMainLoopSelect;
typedef struct _GskMainLoopSelectClass GskMainLoopSelectClass;


/* --- type macros --- */
GType gsk_main_loop_select_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MAIN_LOOP_SELECT			(gsk_main_loop_select_get_type ())
#define GSK_MAIN_LOOP_SELECT(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_MAIN_LOOP_SELECT, GskMainLoopSelect))
#define GSK_MAIN_LOOP_SELECT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_MAIN_LOOP_SELECT, GskMainLoopSelectClass))
#define GSK_MAIN_LOOP_SELECT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_MAIN_LOOP_SELECT, GskMainLoopSelectClass))
#define GSK_IS_MAIN_LOOP_SELECT(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_MAIN_LOOP_SELECT))
#define GSK_IS_MAIN_LOOP_SELECT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_MAIN_LOOP_SELECT))


/* --- structures --- */
struct _GskMainLoopSelectClass 
{
  GskMainLoopPollBaseClass	main_loop_poll_base_class;
};
struct _GskMainLoopSelect 
{
  GskMainLoopPollBase		main_loop_poll_base;
  GTree                        *fd_tree;
  fd_set			read_set;
  fd_set			write_set;
  fd_set                        except_set;
};

G_END_DECLS

#endif
