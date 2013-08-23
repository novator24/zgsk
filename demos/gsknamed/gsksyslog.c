
      /*  facilities --- 
     syslog ( kern | user | mail | daemon | auth | syslog | lpr |
                news | uucp | cron | authpriv | ftp |
                local0 | local1 | local2 | local3 |
                local4 | local5 | local6 | local7 )
      */
#include "gsksyslog.h"
#include <gsk/gskconfig.h>
static const char *global_ident = NULL;

void
gsk_syslog_init (const char        *ident)
{
  global_ident = ident;
}

/* enums <=> strings */
const char *
gsk_syslog_level_name    (GskSyslogLevel     level)
{
  switch (level)
    {
    case GSK_SYSLOG_LEVEL_NORMAL: return "normal";
    case GSK_SYSLOG_LEVEL_WARNING: return "warning";
    case GSK_SYSLOG_LEVEL_ERROR: return "error";
    case GSK_SYSLOG_LEVEL_CRITICAL: return "critical";
    case GSK_SYSLOG_LEVEL_DEBUG: return "debug";
    }
  return NULL;
}

const char *
gsk_syslog_facility_name (GskSyslogFacility  facility)
{
  switch (facility)
    {
    case GSK_SYSLOG_DEFAULT: return "user";
    case GSK_SYSLOG_AUTHORIZATION: return "auth";
    case GSK_SYSLOG_CLOCK_DAEMONS: return "cron";
    case GSK_SYSLOG_DAEMONS: return "daemon";
    case GSK_SYSLOG_KERNEL: return "kern";
    case GSK_SYSLOG_PRINTING: return "lpr";
    case GSK_SYSLOG_MAIL: return "mail";
    case GSK_SYSLOG_NEWS: return "news";
    }
  return NULL;
}

gboolean
gsk_syslog_level_parse   (const char        *name,
			  GskSyslogLevel    *out)
{
#define CHECK(str,val)					\
  if (strncasecmp (name, str, strlen (str)) == 0)	\
    {							\
      *out = val;					\
      return TRUE;					\
    }
  CHECK ("normal", GSK_SYSLOG_LEVEL_NORMAL);
  CHECK ("warning", GSK_SYSLOG_LEVEL_WARNING);
  CHECK ("error", GSK_SYSLOG_LEVEL_ERROR);
  CHECK ("critical", GSK_SYSLOG_LEVEL_CRITICAL);
  CHECK ("debug", GSK_SYSLOG_LEVEL_DEBUG);
  return FALSE;
}

gboolean
gsk_syslog_facility_parse(const char        *name,
			  GskSyslogLevel    *out)
{
  CHECK ("user", GSK_SYSLOG_DEFAULT);
  CHECK ("auth", GSK_SYSLOG_AUTHORIZATION);
  CHECK ("cron", GSK_SYSLOG_CLOCK_DAEMONS);
  CHECK ("daemon", GSK_SYSLOG_DAEMONS);
  CHECK ("kern", GSK_SYSLOG_KERNEL);
  CHECK ("lpr", GSK_SYSLOG_PRINTING);
  CHECK ("mail", GSK_SYSLOG_MAIL);
  CHECK ("news", GSK_SYSLOG_NEWS);
  return FALSE;
}

#if HAVE_SYSLOG
#include <syslog.h>

static int last_facility = LOG_USER;;
static gboolean inited = FALSE;

#ifndef LOG_AUTHPRIV
#define LOG_AUTHPRIV LOG_AUTH
#endif

static int
gsk_syslog_facility_to_native (GskSyslogFacility facility)
{
  switch (facility)
    {
    case GSK_SYSLOG_DEFAULT: return LOG_USER;
    case GSK_SYSLOG_AUTHORIZATION: return LOG_AUTHPRIV;
    case GSK_SYSLOG_CLOCK_DAEMONS: return LOG_CRON;
    case GSK_SYSLOG_DAEMONS: return LOG_DAEMON;
    case GSK_SYSLOG_KERNEL: return LOG_KERN;
    case GSK_SYSLOG_PRINTING: return LOG_LPR;
    case GSK_SYSLOG_MAIL: return LOG_MAIL;
    case GSK_SYSLOG_NEWS: return LOG_NEWS;
    }
  return LOG_USER;
}

static int
gsk_syslog_level_to_native (GskSyslogLevel level)
{
  switch (level)
    {
    case GSK_SYSLOG_LEVEL_NORMAL: return LOG_NOTICE;
    case GSK_SYSLOG_LEVEL_WARNING: return LOG_WARNING;
    case GSK_SYSLOG_LEVEL_ERROR: return LOG_ERR;
    case GSK_SYSLOG_LEVEL_CRITICAL: return LOG_CRIT;
    case GSK_SYSLOG_LEVEL_DEBUG: return LOG_DEBUG;
    }
  return LOG_NOTICE;
}

void
gsk_syslog              (GskSyslogFacility  facility,
			 GskSyslogLevel     level,
			 const char        *message)
{
  int fac = gsk_syslog_facility_to_native (facility);
  if (fac != last_facility || !inited)
    {
      inited = TRUE;
      if (global_ident == NULL)
	{
	  global_ident = g_basename (g_get_prgname ());
	}
      openlog (global_ident, 0, fac);
      last_facility = fac;
    }

  syslog (gsk_syslog_level_to_native (level), "%s", message);
}

#else /* !HAVE_SYSLOG */
#include <stdio.h>

/* everyone has syslog, so this crappy implementation will do... */
void
gsk_syslog              (GskSyslogFacility  facility,
			 GskSyslogLevel     level,
			 const char        *message)
{
  fprintf (stderr, "%s\n", message);
}
#endif /* !HAVE_SYSLOG */
