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

#ifndef __GSK_DATE_H_
#define __GSK_DATE_H_

#include <time.h>
#include <glib.h>

G_BEGIN_DECLS

/* --- date formats --- */

/* Parse dates according to various RFC's;
 *
 * In fact the idea and examples are principally borrowed
 * from the HTTP RFC, rfc2616.
 */

typedef enum
{
  /* rfc 822, obsoleted by rfc 1123:
   *     Sun, 06 Nov 1994 08:49:37 GMT
   */
  GSK_DATE_FORMAT_1123 = (1 << 0),

  /* rfc 850, obsoleted by rfc 1036:
   *     Sunday, 06-Nov-94 08:49:37 GMT
   */
  GSK_DATE_FORMAT_1036 = (1 << 1), /* rfc 850, obsoleted by rfc 1036 */

  /* ansi c's asctime () format:
   *     Sun Nov  6 08:49:37 1994
   */
  GSK_DATE_FORMAT_ANSI_C = (1 << 2),

  /* ISO 8601 defines a variety of timestamps:
       2003-04-04  YYYY-MM-DD
       2003-04
       2003
       2003-035    YYYY-DOY  [DOY=day-of-year]

       NOTE: hyphens may be omitted.

       plus optional time-of-day:

       23:59:59.34  HH:MM:SS.SS
       23:59:59     HH:MM:SS
       23:59        HH:MM   
       23           HH

       NOTE: colons may be omitted.

       Either a space or 'T' separate date/time.

       Timezone:
          Z suffix means UTC
	  +hh:mm or +hhmm or +hh   [or - versions]
     */

  GSK_DATE_FORMAT_ISO8601 = (1 << 3),


  GSK_DATE_FORMAT_HTTP = (GSK_DATE_FORMAT_1123 
                        | GSK_DATE_FORMAT_1036
			| GSK_DATE_FORMAT_ANSI_C)
} GskDateFormatMask;


/* --- prototypes --- */

/* Parsing */
gboolean gsk_date_parse            (const char        *date_str,
                                    struct tm         *tm_out,
				    int               *tzoffset_out,
				    GskDateFormatMask  formats_allowed);
gboolean gsk_date_parse_timet      (const char        *date_str,
                                    time_t            *out,
				    GskDateFormatMask  formats_allowed);

/* Printing */
#define GSK_DATE_MAX_LENGTH 256
void     gsk_date_print            (const struct tm   *tm,
                                    char              *date_str_out,
				    int                date_str_max_len,
				    GskDateFormatMask  format);
void     gsk_date_print_timet      (time_t             t,
                                    char              *date_str_out,
				    int                date_str_max_len,
				    GskDateFormatMask  format);

G_END_DECLS

#endif
