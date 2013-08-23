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

#ifndef __GSK_BASE64_H_
#define __GSK_BASE64_H_

#include <glib.h>

G_BEGIN_DECLS

/* The +1 is for a terminal = sign.
 * You should also probably allocate space for a NUL and set that
 * character to NUL.
 */
#define GSK_BASE64_GET_ENCODED_LEN(length)			\
		(((length) * 8 + 5) / 6 + 1)

#define GSK_BASE64_GET_MAX_DECODED_LEN(length)			\
  		(((length * 6) + 7) / 8)

/* returns the number of bytes of data written to `dst' */
guint       gsk_base64_decode       (char            *dst,
				     guint            dst_len,
                                     const char      *src,
				     gssize           src_len);
GByteArray *gsk_base64_decode_alloc (const char      *src);
void        gsk_base64_encode       (char            *dst,
                                     const char      *src,
				     guint            src_len);
char       *gsk_base64_encode_alloc (const char      *src,
				     gssize           src_len);

G_END_DECLS

#endif
