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

#include "gskdate.h"
#include "gsktimegm.h"
#include <ctype.h>
#include <stdlib.h>

#if 1
#define DEBUG_DATE(x)    g_message x
#else
#define DEBUG_DATE(x)
#endif

#define USE_LOCALTIME_HACK      0


static gboolean inited = FALSE;
static GHashTable *day_of_week_from_name = NULL;
static GHashTable *month_from_name = NULL;
static GHashTable *time_offset_from_name = NULL;

static void add_name3_to_index(GHashTable *table,
                               const char *lc_name3,
			       int         index)
{
  const guint8 *name = (const guint8 *) lc_name3;
  int iname = (((int)name[0]) << 0)
            | (((int)name[1]) << 8)
            | (((int)name[2]) << 16);
  g_hash_table_insert (table, GINT_TO_POINTER (iname), GINT_TO_POINTER (index));
}

static void init_tables ()
{
  if (inited)
    return;
  inited = TRUE;
  day_of_week_from_name = g_hash_table_new (NULL, NULL);
  add_name3_to_index (day_of_week_from_name, "sun", 1);
  add_name3_to_index (day_of_week_from_name, "mon", 2);
  add_name3_to_index (day_of_week_from_name, "tue", 3);
  add_name3_to_index (day_of_week_from_name, "wed", 4);
  add_name3_to_index (day_of_week_from_name, "thu", 5);
  add_name3_to_index (day_of_week_from_name, "fri", 6);
  add_name3_to_index (day_of_week_from_name, "sat", 7);

  month_from_name = g_hash_table_new (NULL, NULL);
  add_name3_to_index (month_from_name, "jan", 1);
  add_name3_to_index (month_from_name, "feb", 2);
  add_name3_to_index (month_from_name, "mar", 3);
  add_name3_to_index (month_from_name, "apr", 4);
  add_name3_to_index (month_from_name, "may", 5);
  add_name3_to_index (month_from_name, "jun", 6);
  add_name3_to_index (month_from_name, "jul", 7);
  add_name3_to_index (month_from_name, "aug", 8);
  add_name3_to_index (month_from_name, "sep", 9);
  add_name3_to_index (month_from_name, "oct", 10);
  add_name3_to_index (month_from_name, "nov", 11);
  add_name3_to_index (month_from_name, "dec", 12);

  time_offset_from_name = g_hash_table_new (NULL, NULL);
  add_name3_to_index (time_offset_from_name, "gmt", 0);
  add_name3_to_index (time_offset_from_name, "pst", -8 * 60);
  add_name3_to_index (time_offset_from_name, "pdt", -7 * 60);
  add_name3_to_index (time_offset_from_name, "mst", -7 * 60);
  add_name3_to_index (time_offset_from_name, "mdt", -6 * 60);
  add_name3_to_index (time_offset_from_name, "cst", -6 * 60);
  add_name3_to_index (time_offset_from_name, "cdt", -5 * 60);
  add_name3_to_index (time_offset_from_name, "est", -5 * 60);
  add_name3_to_index (time_offset_from_name, "edt", -4 * 60);
}



static int get_day_of_week (const char *name)
{
  int i;
  i = (guint8) (tolower (name[0]));
  i |= (int) ((guint8) (tolower (name[1]))) << 8;
  i |= (int) ((guint8) (tolower (name[2]))) << 16;
  return GPOINTER_TO_INT (g_hash_table_lookup (day_of_week_from_name,
                                               GINT_TO_POINTER (i)));
}

static int get_month (const char *name)
{
  int i;
  i = (guint8) (tolower (name[0]));
  i |= (int) ((guint8) (tolower (name[1]))) << 8;
  i |= (int) ((guint8) (tolower (name[2]))) << 16;
  return GPOINTER_TO_INT (g_hash_table_lookup (month_from_name,
                                               GINT_TO_POINTER (i)));
}

static gboolean parse_military_time (const char    *time_str,
                                     int           *hour_out,
                                     int           *minute_out,
                                     int           *second_out)
{
  if (time_str[0] == '\0' || time_str[1] == '\0'
   || time_str[2] != ':'
   || time_str[3] == '\0' || time_str[4] == '\0'
   || time_str[5] != ':'
   || time_str[6] == '\0' || time_str[7] == '\0')
    {
      DEBUG_DATE (("invalid format for military time: expected HH:MM:SS"));
      return FALSE;
    }

  *hour_out = strtol (time_str + 0, NULL, 10);
  *minute_out = strtol (time_str + 3, NULL, 10);
  *second_out = strtol (time_str + 6, NULL, 10);
  return TRUE;
}

static int parse_timezone (const char *timezone_str)
{
  int i;
  while (*timezone_str != '\0' && isspace (*timezone_str))
    timezone_str++;
  
  if (*timezone_str == '-' || *timezone_str == '+' || isdigit (*timezone_str))
    {
      /* parse -0600 style timezone */
      gboolean positive = TRUE;
      char hour[3], min[3];
      int rv;
      if (*timezone_str == '-')
        {
	  positive = FALSE;
	  timezone_str++;
	}
      else if (*timezone_str == '+')
        {
	  positive = TRUE;
	  timezone_str++;
	}
      if (timezone_str[0] == 0 || timezone_str[1] == 0
       || timezone_str[2] == 0 || timezone_str[3] == 0)
	{
	  /* XXX: error handling */
	  return 0;
	}
      hour[0] = timezone_str[0];
      hour[1] = timezone_str[1];
      hour[2] = '\0';
      min[0] = timezone_str[2];
      min[1] = timezone_str[3];
      min[2] = '\0';
      rv = (int) (strtol (hour, NULL, 10) * 60 + strtol (min, NULL, 10));
      if (!positive)
        rv = (-rv);
      return rv;
    }

  if (timezone_str[0] == 0 || timezone_str[1] == 0 || timezone_str[2] == 0)
    {
      /* XXX: error handling */
      return 0;
    }
  i = (guint8) (tolower (timezone_str[0]));
  i |= (int) ((guint8) (tolower (timezone_str[1]))) << 8;
  i |= (int) ((guint8) (tolower (timezone_str[2]))) << 16;
  return GPOINTER_TO_INT (g_hash_table_lookup (time_offset_from_name,
                                               GINT_TO_POINTER (i)));
}
  

/* rfc 822, obsoleted by rfc 1123:
 *     Sun, 06 Nov 1994 08:49:37 GMT
 */
static gboolean parse_1123 (const char *date,
                            struct tm  *out,
                            int        *tzoffset_out)
{
  int day_of_week;
  int day_of_month;
  int month;
  int year;
  int hour, minute, second;
  int index = 0;
  char *endp;

  day_of_week = get_day_of_week (date);
  if (day_of_week == 0)
    {
      DEBUG_DATE (("parse_1123: couldn't get day-of-week"));
      return FALSE;
    }
  if (date[3] != ',')
    {
      DEBUG_DATE (("parse_1123: missing ',' after day-of-week"));
      return FALSE;
    }
  index = 4;
  while (date[index] != '\0' && isspace (date[index]))
    index++;
  day_of_month = strtol (date + index, &endp, 10);
  if (endp == date + index)
    {
      DEBUG_DATE (("parse_1123: day-of-month number invalid"));
      return FALSE;
    }
  index = endp - date;
  while (date[index] != '\0' && isspace (date[index]))
    index++;
  month = get_month (date + index);
  index += 4;
  while (date[index] != '\0' && isspace (date[index]))
    index++;
  year = strtol (date + index, &endp, 10);
  if (endp == date + index)
    {
      DEBUG_DATE (("parse_1123: year number invalid"));
      return FALSE;
    }
  index = endp - date;
  while (date[index] != '\0' && isspace (date[index]))
    index++;
  if (!parse_military_time (date + index, &hour, &minute, &second))
    {
      DEBUG_DATE (("parse_1123: parse military time failed"));
      return FALSE;
    }
  index += 8;
  while (date[index] != '\0' && isspace (date[index]))
    index++;
  *tzoffset_out = parse_timezone (date + index);

  /* hm, correct for likely y2k annoyances */
  if (year < 1900)
    year += 1900;

  out->tm_sec = second;
  out->tm_min = minute;
  out->tm_hour = hour;
  out->tm_mday = day_of_month;
  out->tm_mon = month - 1;
  out->tm_year = year - 1900;
  out->tm_wday = day_of_week - 1;		/* ignored */
  out->tm_isdst = 0;
  return TRUE;
}

/* rfc 850, obsoleted by rfc 1036:
 *     Sunday, 06-Nov-94 08:49:37 GMT
 */
static gboolean parse_1036 (const char *date,
                            struct tm  *out,
                            int        *tzoffset_out)
{
  int day_of_week;
  int day_of_month;
  int month;
  int year;
  int hour, minute, second;
  int index = 0;

  day_of_week = get_day_of_week (date);
  if (day_of_week == 0)
    {
      DEBUG_DATE (("parse_1036: couldn't get day-of-week"));
      return FALSE;
    }
  while (date[index] != '\0' && isalpha (date[index]))
    index++;
  if (date[index] != ',')
    {
      DEBUG_DATE (("parse_1036: missing ',' after day-of-week"));
      return FALSE;
    }
  index++;
  while (date[index] != '\0' && isspace (date[index]))
    index++;
  day_of_month = strtol (date + index, NULL, 10);
  if (date[index + 2] != '-' || date[index + 6] != '-')
    {
      DEBUG_DATE (("parse_1036: missing '-' after day-of-month or month"));
      return FALSE;
    }
  month = get_month (date + index + 3);
  if (month == 0)
    return FALSE;
  year = strtol (date + index + 7, NULL, 10);
  if (year < 1900)
    year += 1900;
  index += 9;
  while (date[index] != '\0' && isdigit (date[index]))
    index++;
  while (date[index] != '\0' && isspace (date[index]))
    index++;
  if (!parse_military_time (date + index, &hour, &minute, &second))
    {
      DEBUG_DATE (("parse_1123: parse military time failed"));
      return FALSE;
    }
  index += 8;
  while (date[index] != '\0' && isspace (date[index]))
    index++;
  *tzoffset_out = parse_timezone (date + index);

  out->tm_sec = second;
  out->tm_min = minute;
  out->tm_hour = hour;
  out->tm_mday = day_of_month;
  out->tm_mon = month - 1;
  out->tm_year = year - 1900;
  out->tm_wday = day_of_week - 1;		/* ignored */
  out->tm_isdst = 0;
  return TRUE;
}

/* ansi c's asctime () format:
 *     Sun Nov  6 08:49:37 1994
 */
static gboolean parse_ansi_c (const char *date,
                              struct tm  *out,
			      int        *tzoffset_out)
{
  int day_of_week;
  int day_of_month;
  int month;
  int year;
  int hour, minute, second;
  int index = 0;
  char *endp;

  /* XXX: i guess assuming the local timezone would be best ? */
  *tzoffset_out = 0;

  day_of_week = get_day_of_week (date);
  if (day_of_week == 0)
    {
      DEBUG_DATE (("parse_ansi_c: couldn't get day-of-week"));
      return FALSE;
    }
  index = 3;
  while (date[index] != '\0' && isspace (date[index]))
    index++;
  month = get_month (date + index);
  if (month == 0)
    {
      DEBUG_DATE (("parse_ansi_c: couldn't get month"));
      return FALSE;
    }
  index += 3;
  while (date[index] != '\0' && isspace (date[index]))
    index++;
  day_of_month = strtol (date + index, &endp, 10);
  if (date + index == endp)
    {
      DEBUG_DATE (("parse_ansi_c: couldn't get day-of-month"));
      return FALSE;
    }
  index = endp - date;
  while (date[index] != '\0' && isspace (date[index]))
    index++;
  
  if (!parse_military_time (date + index, &hour, &minute, &second))
    {
      DEBUG_DATE (("parse_ansi_c: parse military time failed"));
      return FALSE;
    }

  index += 8;
  while (date[index] != '\0' && isspace (date[index]))
    index++;
  year = strtol (date + index, NULL, 10);

  if (year == 0)
    {
      DEBUG_DATE (("parse_ansi_c: year number invalid"));
      return FALSE;
    }

  out->tm_sec = second;
  out->tm_min = minute;
  out->tm_hour = hour;
  out->tm_mday = day_of_month;
  out->tm_mon = month - 1;
  out->tm_year = year - 1900;
  out->tm_wday = day_of_week - 1;		/* ignored */
  out->tm_isdst = 0;
  return TRUE;
}

static guint
parse_nums (const char *at,
	    guint      *n_chars_used,
	    char        sep,
	    guint      *nums_out,
	    guint      *digits_per_num_out,
	    guint       max_nums)
{
  guint num = 0;
  guint num_digits = 0;
  guint n_nums = 0;
  guint n_chars = 0;
  gboolean last_was_digit = FALSE;
  while (at[n_chars]
      && at[n_chars] != ' '
      && n_nums < 4)
    {
      if (isdigit (at[n_chars]))
	{
	  if (last_was_digit)
	    num *= 10;
	  else
	    last_was_digit = TRUE;
	  num += at[n_chars] - '0';
	  num_digits++;
	}
      else if (last_was_digit)
	{
	  nums_out[n_nums] = num;
	  digits_per_num_out[n_nums] = num_digits;
	  n_nums++;
	  num = 0;
	  num_digits = 0;
	  last_was_digit = FALSE;
	}

      if (!last_was_digit && at[n_chars] != sep)
	{
	  break;
	}
      n_chars++;
    }
  if (last_was_digit)
    {
      nums_out[n_nums] = num;
      digits_per_num_out[n_nums] = num_digits;
      n_nums++;
    }
  *n_chars_used = n_chars;
  return n_nums;
}

/* See http://www.cl.cam.ac.uk/%7Emgk25/iso-time.html */
static gboolean
parse_iso8601 (const char *date,
	       struct tm  *out,
	       int        *tzoffset_out)
{
  guint n_date_chars = 0;
  guint nums[5];
  guint digits_per_num[5];
  guint year;
  guint month = 1;
  guint day = 1;
  gint day_of_year = -1;
  guint hours = 0;
  guint minutes = 0;
  guint seconds = 0;
  guint timezone = 0;
  guint n_nums = parse_nums (date, &n_date_chars, '-', nums, digits_per_num, 5);
  guint n_digits = 0;
  guint i;
  for (i = 0; i < n_nums; i++)
    n_digits += digits_per_num[i];
  if (n_digits == 2)
    {
      /* YY */
      if (n_nums == 1)
	year = 1900 + nums[0];
      else
	{
	  DEBUG_DATE(("got 2-char date spec with wrong number of numbers"));
	  return FALSE;
	}
    }
  else if (n_digits == 4)
    {
      /* YYYY or YY:MM */
      if (n_nums == 1)
	year = nums[0];
      else if (n_nums == 2)
	{
	  if (digits_per_num[0] != 2)
            {
	      DEBUG_DATE(("got bad 4 digit date spec"));
	      return FALSE;
	    }
	  year = 1900 + nums[0];
	  month = nums[1];
	}
      else
	{
	  DEBUG_DATE(("got 4 char date, with non-2 numbers"));
	  return FALSE;
	}
    }
  else if (n_digits == 6)
    {
      /* YYYY:MM or YY:MM:DD */
      if (n_nums == 2)
	{
	  if (digits_per_num[0] != 4)
	    {
	      DEBUG_DATE(("6 digits, 2 parts, expected first to be year"));
	      return FALSE;
	    }
	  year = nums[0];
	  month = nums[1];
	}
      else if (n_nums == 3)
	{
	  year = 1900 + nums[0];
	  month = nums[1];
	  day = nums[2];
	}
      else
	return FALSE;
    }
  else if (n_date_chars == 7)
    {
      /* YYYY:DOY or YYYYDOY */
      if (n_nums == 1)
	{
	  year = nums[0] / 1000;
	  day_of_year = nums[0] % 1000;
	}
      else if (n_nums == 2)
	{
	  if (digits_per_num[0] != 4)
	    {
	      DEBUG_DATE(("7 digit date must be YEAR:DAT-OF-YEAR"));
	      return FALSE;
	    }
	  year = nums[0];
	  day_of_year = nums[1];
	}
      else
	{
	  DEBUG_DATE(("got 7 digit, %u number date spec", n_nums));
	  return FALSE;
	}
    }
  else if (n_digits == 8)
    {
      /* YYYYMMDD or YYYY:MM:DD */
      if (n_nums == 1)
	{
	  year = nums[0] / 10000;
	  month = nums[0] / 100 % 100;
	  day = nums[0] % 100;
	}
      else if (n_nums == 3)
	{
	  if (digits_per_num[0] != 4 || digits_per_num[1] != 2)
	    {
	      DEBUG_DATE(("got 8 digit date, with 3 number lengths %u,%u,%u; not allowed",
			  digits_per_num[0], digits_per_num[1], digits_per_num[2]));
	      return FALSE;
	    }
	  year = nums[0];
	  month = nums[1];
	  day = nums[2];
	}
      else
	{
	  DEBUG_DATE(("got 8 digit date, with %u parts", n_nums));
	  return FALSE;
	}
    }
  else
    return FALSE;

  if (date[n_date_chars] == 'T')
    n_date_chars++;
  else if (date[n_date_chars] == ' ')
    {
      /* skip whitespace */
      while (date[n_date_chars] && isspace (date[n_date_chars]))
	n_date_chars++;
      if (date[n_date_chars] != 0 && !isdigit (date[n_date_chars]))
	{
	  DEBUG_DATE (("parse_iso8601: bad character after day portion and whitespace"));
	  return FALSE;
	}
    }
  else
    {
      DEBUG_DATE (("parse_iso8601: bad character after day portion"));
      return FALSE;
    }

  if (date[n_date_chars] != '\0')
    {
      /* Parse time portion */
      const char *time = date + n_date_chars;
      guint n_time_chars;
      n_nums = parse_nums (time, &n_time_chars, ':', nums, digits_per_num, 5);
      if (n_nums == 3)
	{
	  hours = nums[0];
	  minutes = nums[1];
	  seconds = nums[2];
	}
      else if (n_nums == 2)
	{
	  hours = nums[0];
	  minutes = nums[1];
	}
      else if (n_nums == 1)
	{
	  if (digits_per_num[0] == 2)
	    hours = nums[0];
	  else if (digits_per_num[0] == 4)
	    {
	      hours = nums[0] / 100;
	      minutes = nums[0] % 100;
	    }
	  else if (digits_per_num[0] == 6)
	    {
	      hours = nums[0] / 100 / 100;
	      minutes = nums[0] / 100 % 100;
	      seconds = nums[0] % 100;
	    }
	  else
	    {
	      DEBUG_DATE(("time spec had 1 number with %u digits", digits_per_num[0]));
	      return FALSE;
	    }
	}
      else
	{
	  DEBUG_DATE(("time spec had %u numbers", n_nums));
	  return FALSE;
	}

      if (time[n_time_chars] == '.')
	{
	  /* fraction of seconds... skip */
	  n_time_chars++;
	  while (time[n_time_chars] && isdigit (time[n_time_chars]))
	    n_time_chars++;
	}

      if (time[n_time_chars] == 'Z')
	{
	  /* fine, UTC which is default */
	}
      else if (time[n_time_chars] == '-'
            || time[n_time_chars] == '+')
	{
	  /* timezone.  either +HH:MM or +HH or +HHMM or '-' variants */
	  int pm = time[n_time_chars] == '+' ? 1 : -1;
	  int hoffset = 0;
	  int moffset = 0;
	  n_time_chars++;
	  if (!isdigit (time[n_time_chars])
	   || !isdigit (time[n_time_chars+1]))
	   return FALSE;
	  hoffset = (time[n_time_chars] - '0') * 10 + (time[n_time_chars+1] - '0');
	  n_time_chars += 2;
	  if (time[n_time_chars] == ':')
	    n_time_chars++;
	  if (time[n_time_chars]
           && isdigit (time[n_time_chars])
	   && isdigit (time[n_time_chars+1]))
	    moffset = (time[n_time_chars] - '0') * 10 + (time[n_time_chars+1] - '0');
	  timezone = pm * (hoffset * 60 + moffset);
	}
      else if (time[n_time_chars] != 0 && !isspace (time[n_time_chars]))
	{
	  DEBUG_DATE (("parse_iso8601: bad character after time portion"));
	  return FALSE;
	}
    }

  /* fill out out and tzoffset_out */
  *tzoffset_out = timezone;

  out->tm_year = year - 1900;
  out->tm_mon = month - 1;
  out->tm_mday = day;
  if (day_of_year >= 0)
    {
      out->tm_yday = day_of_year;

      /* how to compute tm_mday, tm_mon??? */
      out->tm_mday = 0;
      g_warning ("need day-of-year to day-of-month and month routine");
    }

  /* TODO: compute wday... */

  out->tm_min = minutes;
  out->tm_hour = hours;
  out->tm_sec = seconds;

  return TRUE;
}


/**
 * gsk_date_parse:
 * @date_str: the string containing a date.
 * @tm_out: location to store the time, as a struct tm.
 * (That is, all the fields are broken out).
 * @tzoffset_out: location to store the timezone offset.
 * (offset stored in minutes)
 * @formats_allowed: bitwise-OR of all the allowed date formats.
 * The parser will try to find a date in any of the allowed formats.
 *
 * Parse a date to a struct tm.
 *
 * returns: whether the date was successfully parsed.
 */
gboolean gsk_date_parse            (const char        *date_str,
                                    struct tm         *tm_out,
				    int               *tzoffset_out,
				    GskDateFormatMask  allowed)
{
  init_tables ();
  if (date_str[0] == '\0'
   || date_str[1] == '\0'
   || date_str[2] == '\0'
   || date_str[3] == '\0')
    return FALSE;
  if (isalpha (date_str[0]) && isupper (date_str[0])
   && isalpha (date_str[1]) && islower (date_str[1])
   && isalpha (date_str[2]) && islower (date_str[2]))
    {
      if (isspace (date_str[3])
       && (allowed & GSK_DATE_FORMAT_ANSI_C) == GSK_DATE_FORMAT_ANSI_C)
        return parse_ansi_c (date_str, tm_out, tzoffset_out);
      if (date_str[3] == ','
       && (allowed & GSK_DATE_FORMAT_1123) == GSK_DATE_FORMAT_1123)
        return parse_1123 (date_str, tm_out, tzoffset_out);
      if (isalpha (date_str[3]) && islower (date_str [3])
       && (allowed & GSK_DATE_FORMAT_1036) == GSK_DATE_FORMAT_1036)
        return parse_1036 (date_str, tm_out, tzoffset_out);
    }
  if (isdigit (date_str[0])
   && isdigit (date_str[1])
   && (allowed & GSK_DATE_FORMAT_ISO8601) == GSK_DATE_FORMAT_ISO8601)
    {
      return parse_iso8601 (date_str, tm_out, tzoffset_out);
    }
  return FALSE;
}


/**
 * gsk_date_parse_timet:
 * @date_str: the string containing a date.
 * @out: location to store the time, as a unix time.
 * That is, the time since the start of 1970 GMT.
 * @formats_allowed: bitwise-OR of all the allowed date formats.
 * The parser will try to find a date in any of the allowed formats.
 *
 * Parse a date to a unix time.
 *
 * returns: whether the date was successfully parsed.
 */
gboolean gsk_date_parse_timet      (const char        *date_str,
                                    time_t            *out,
				    GskDateFormatMask  formats_allowed)
{
  struct tm tm_out;
  int tz_offset;
#if USE_LOCALTIME_HACK
  struct tm new_tm;
#endif
  if (!gsk_date_parse (date_str, &tm_out, &tz_offset, formats_allowed))
    return FALSE;
  
#if USE_LOCALTIME_HACK
  *out = mktime (&tm_out);
  localtime_r (out, &new_tm);
  if (new_tm.tm_isdst)
    *out += 3600;
  *out -= timezone;
#else
  *out = gsk_timegm (&tm_out);
  if (*out == ((time_t)-1))
    return FALSE;
#endif

  *out -= tz_offset * 60;
  return TRUE;
}

static const char *day_of_week_to_full_name[] =
{
  "Sunday",
  "Monday",
  "Tuesday",
  "Wednesday",
  "Thursday",
  "Friday",
  "Saturday"
};

static const char *day_of_week_to_three_letter_stud_capped[] =
{
  "Sun",
  "Mon",
  "Tue",
  "Wed",
  "Thu",
  "Fri",
  "Sat"
};

static const char *month_to_three_letter_stud_capped[] =
{
  "Jan",
  "Feb",
  "Mar",
  "Apr",
  "May",
  "Jun",
  "Jul",
  "Aug",
  "Sep",
  "Oct",
  "Nov",
  "Dec"
};

/**
 * gsk_date_print:
 * @tm: the time, separated into pieces.
 * (All fields, even derived fields like tm_wday, must be set.)
 * @date_str_out: buffer to fill with the date as a string.
 * @date_str_max_len: the length of @date_str_out.  This should be 80 or greater
 * to prevent clipping.
 * @format: which presentation of the date to use.
 *
 * Print the date to a buffer, in the format requested.
 */
void     gsk_date_print            (const struct tm   *tm,
                                    char              *date_str_out,
				    int                date_str_max_len,
				    GskDateFormatMask  format)
{
  if ((format & GSK_DATE_FORMAT_1036) == GSK_DATE_FORMAT_1036)
    {
      g_snprintf (date_str_out, date_str_max_len,
                  "%s, %02d-%s-%d %02d:%02d:%02d GMT",
		  day_of_week_to_full_name[tm->tm_wday],
		  tm->tm_mday,
		  month_to_three_letter_stud_capped[tm->tm_mon],
		  tm->tm_year,
		  tm->tm_hour,
		  tm->tm_min,
		  tm->tm_sec);
      return;
    }
  if ((format & GSK_DATE_FORMAT_1123) == GSK_DATE_FORMAT_1123)
    {
      g_snprintf (date_str_out, date_str_max_len,
                  "%s, %02d %s %d %02d:%02d:%02d GMT",
		  day_of_week_to_three_letter_stud_capped[tm->tm_wday],
		  tm->tm_mday,
		  month_to_three_letter_stud_capped[tm->tm_mon],
		  tm->tm_year + 1900,
		  tm->tm_hour,
		  tm->tm_min,
		  tm->tm_sec);
      return;
    }
  if ((format & GSK_DATE_FORMAT_ANSI_C) == GSK_DATE_FORMAT_ANSI_C)
    {
      g_snprintf (date_str_out, date_str_max_len,
                  "%s %s %2d %02d:%02d:%02d %d GMT",
		  day_of_week_to_three_letter_stud_capped[tm->tm_wday],
		  month_to_three_letter_stud_capped[tm->tm_mon],
		  tm->tm_mday,
		  tm->tm_hour,
		  tm->tm_min,
		  tm->tm_sec,
		  tm->tm_year + 1900);
      return;
    }
  if ((format & GSK_DATE_FORMAT_ISO8601) == GSK_DATE_FORMAT_ISO8601)
    {
      /* Use the compact 8601 representation.
         See http://www.cl.cam.ac.uk/%7Emgk25/iso-time.html */
      g_snprintf (date_str_out, date_str_max_len,
		  "%04u%02u%02uT%02u%02u%02uZ",
		  tm->tm_year + 1900,
		  tm->tm_mon + 1,
		  tm->tm_mday,
		  tm->tm_hour,
		  tm->tm_min,
		  tm->tm_sec);
      return;
    }

  g_warning ("gsk_date_print: GSK_DATE_FORMAT_* expected");
  g_snprintf (date_str_out, date_str_max_len, "error");
}

/**
 * gsk_date_print_timet:
 * @t: the time, as per unix tradition.  That is, this is the
 * time since the beginning of 1970 GMT.
 * @date_str_out: buffer to fill with the date as a string.
 * @date_str_max_len: the length of @date_str_out.  This should be 80 or greater
 * to prevent clipping.
 * @format: which presentation of the date to use.
 *
 * Print the date to a buffer, in the format requested.
 */
void     gsk_date_print_timet      (time_t             t,
                                    char              *date_str_out,
				    int                date_str_max_len,
				    GskDateFormatMask  format)
{
  /* XXX: there is a gmtime_r function we should use on some systems. */
  struct tm *tm_out;
  tm_out = gmtime (&t);
  g_return_if_fail (tm_out != NULL);
  gsk_date_print (tm_out, date_str_out, date_str_max_len, format);
}
