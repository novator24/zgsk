/*
    GSK - a library to write servers

    Copyright (C) 2006 Dave Benson

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

#ifndef __GSK_STREAM_WATCHDOG_H_
#define __GSK_STREAM_WATCHDOG_H_

typedef struct _GskStreamWatchdogClass GskStreamWatchdogClass;
typedef struct _GskStreamWatchdog GskStreamWatchdog;

#include "gskstream.h"
#include "gskmainloop.h"

GType gsk_stream_watchdog_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_STREAM_WATCHDOG              (gsk_stream_watchdog_get_type ())
#define GSK_STREAM_WATCHDOG(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_STREAM_WATCHDOG, GskStreamWatchdog))
#define GSK_STREAM_WATCHDOG_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_STREAM_WATCHDOG, GskStreamWatchdogClass))
#define GSK_STREAM_WATCHDOG_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_STREAM_WATCHDOG, GskStreamWatchdogClass))
#define GSK_IS_STREAM_WATCHDOG(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_STREAM_WATCHDOG))
#define GSK_IS_STREAM_WATCHDOG_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_STREAM_WATCHDOG))

struct _GskStreamWatchdogClass
{
  GskStreamClass base_class;
};
struct _GskStreamWatchdog
{
  GskStream base_instance;
  GskStream *underlying;
  GskSource *timeout;
  guint max_inactivity_millis;
};

GskStream *gsk_stream_watchdog_new (GskStream       *underlying_stream,
                                    guint            max_inactivity_millis);

#endif
