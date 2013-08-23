#ifndef __GSK_TIMEGM_H_
#define __GSK_TIMEGM_H_

#include <glib.h>

/* Normally we really avoid including system headers,
   but it's somewhat unavoidable in this case. */
#include <time.h>

G_BEGIN_DECLS

time_t gsk_timegm(const struct tm *t);

G_END_DECLS

#endif
