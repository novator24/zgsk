#ifndef __GSK_SYSLOG_H_
#define __GSK_SYSLOG_H_

#include <gsk/gskbasic.h>

G_BEGIN_DECLS

typedef enum
{
  /* For user processes. */
  GSK_SYSLOG_DEFAULT = 0,

  /* Authorization log messages (should be kept private) */
  GSK_SYSLOG_AUTHORIZATION,

  /* For `clock daemons', like cron and at. */
  GSK_SYSLOG_CLOCK_DAEMONS,

  /* For all other daemons. */
  GSK_SYSLOG_DAEMONS,

  /* For the kernel (don't use this) */
  GSK_SYSLOG_KERNEL,

  /* For printer subsystems. */
  GSK_SYSLOG_PRINTING,

  /* For mail subsystems. */
  GSK_SYSLOG_MAIL,

  /* For news subsystems. */
  GSK_SYSLOG_NEWS,
} GskSyslogFacility;

typedef enum
{
  GSK_SYSLOG_LEVEL_NORMAL = 0,

  GSK_SYSLOG_LEVEL_WARNING,
  GSK_SYSLOG_LEVEL_ERROR,
  GSK_SYSLOG_LEVEL_CRITICAL,
  GSK_SYSLOG_LEVEL_DEBUG
} GskSyslogLevel;

/* If you don't call this, we will use g_basename(g_get_prgname()) */
void         gsk_syslog_init         (const char        *ident);

/* Run syslog(3).
 *
 * Just pass in 0 for the first two parameters
 * for the standard use.
 */
void         gsk_syslog              (GskSyslogFacility  facility,
		                      GskSyslogLevel     level,
		                      const char        *message);

/* enums <=> strings */
const char *gsk_syslog_level_name    (GskSyslogLevel     level);
const char *gsk_syslog_facility_name (GskSyslogFacility  facility);
gboolean    gsk_syslog_level_parse   (const char        *level_name,
				      GskSyslogLevel    *level_out);
gboolean    gsk_syslog_facility_parse(const char        *facility_name,
				      GskSyslogLevel    *facility_out);

G_END_DECLS

#endif
