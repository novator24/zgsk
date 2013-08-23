#include "gsktimegm.h"

#define EPOCH_YEAR 1970  /* ah those were the days */

/**
 * gsk_timegm:
 * @t: the time to treat as UTC time.
 *
 * Convert a broken down time representation
 * in UTC (== Grenwich Mean Time, GMT)
 * to the number of seconds since the Great Epoch.
 *
 * returns: the number of seconds since the beginning of 1970
 * in Grenwich Mean Time.  (This is also known as unix time)
 */

time_t
gsk_timegm(const struct tm *t)
{
  static const guint month_starts_in_days[12]
                                 = { 0,         /* jan has 31 */
                                     31,        /* feb has 28 */
                                     59,        /* mar has 31 */
                                     90,        /* apr has 30 */
                                     120,       /* may has 31 */
                                     151,       /* jun has 30 */
                                     181,       /* jul has 31 */
                                     212,       /* aug has 31 */
                                     243,       /* sep has 30 */
                                     273,       /* oct has 31 */
                                     304,       /* nov has 30 */
                                     334,       /* dec has 31 */
                                     /*365*/ };
  guint year = 1900 + t->tm_year;
  guint days_since_epoch, secs_since_midnight;

  /* we need to find the number of leap years between 1970
     and ly_year inclusive.  Therefore, the current year
     is included only if the date falls after Feb 28. */
  gboolean before_leap = (t->tm_mon <= 1);  /* jan and feb are before leap */
  guint ly_year = year - (before_leap ? 1 : 0);

  /* Number of leap years before the date in question, since epoch.
   *
   * There is a leap year every 4 years, except every 100 years,
   * except every 400 years.  see "Gregorian calendar" on wikipedia.
   */
  guint n_leaps = ((ly_year / 4) - (EPOCH_YEAR / 4))
                - ((ly_year / 100) - (EPOCH_YEAR / 100))
                + ((ly_year / 400) - (EPOCH_YEAR / 400));

  if ((guint)t->tm_mon >= 12
   || t->tm_mday < 1 || t->tm_mday > 31
   || (guint)t->tm_hour >= 24
   || (guint)t->tm_min >= 60
   || (guint)t->tm_sec >= 61)   /* ??? are leap seconds meaningful in gmt */
    return (time_t) -1;

  days_since_epoch = (year - EPOCH_YEAR) * 365
                   + n_leaps
                   + month_starts_in_days[t->tm_mon]
                   + (t->tm_mday - 1);
  secs_since_midnight = t->tm_hour * 3600
                      + t->tm_min * 60
                      + t->tm_sec;
  return (guint64) days_since_epoch * 86400ULL
       + (guint64) secs_since_midnight;
}
