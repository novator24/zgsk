/*
    GSK - a library to write servers

    Copyright (C) 2007 Dave Benson

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


/* These functions provide an interface to the mechanism
   for passing open file-descriptors between processes.
   
   The only mechanism currently supported is the SCM_RIGHTS
   trick, which is described in unix(7), at least on my linux box.

   In this implementation the file-descriptor passing
   channel is always via a special set of file-descriptors,
   called here sender_fd and receiver_fd.

   Senders are either dedicated to another file-descriptor
   to they are anonymous.  When sending a file-descriptor
   via an anonymous sender, a unix-path must be supplied.

   The use-cases fall into two categories:
     - the sending of file-descriptors is going on between
       a parent and child process so that you can easily
       call gsk_pass_fd_make_pair() and ensure
       that the sender_fd remains open in the parent,
       and the receiver_fd remains open in the client
       (theoretically you can send fds in the other direction,
       but i can't really imagine a use-case)

     - the sending of file-descriptors is going
       on in between two processes with an agreed upon
       rendevous point.

   Auxillary information is passed along with the fd.
   The auxillary info must be at least one byte long!
 */

#ifndef __GSK_PASS_FD_H_
#define __GSK_PASS_FD_H_

#include <glib.h>

G_BEGIN_DECLS

gboolean gsk_pass_fd_make_pair    (int          *sender_fd_out,
                                   int          *receiver_fd_out,
				   GError      **error);
int      gsk_pass_fd_make_sender  (GError      **error);
int      gsk_pass_fd_bind_receiver(const char   *path,
                                   GError      **error);
gboolean gsk_pass_fd_send         (int           sender_fd,
                                   int           pass_fd,
                                   guint         aux_info_length,
                                   const guint8 *aux_info_data,
                                   GError      **error);
gboolean gsk_pass_fd_sendto       (int           sender_fd,
                                   const char   *path,
                                   int           pass_fd,
                                   guint         aux_info_length,
                                   const guint8 *aux_info_data,
                                   GError      **error);
int      gsk_pass_fd_receive      (int           receiver_fd,
                                   guint        *aux_info_length_out,
                                   guint8      **aux_info_data_out,
                                   GError      **error);

G_END_DECLS

#endif
