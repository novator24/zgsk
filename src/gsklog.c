#include "config.h"		/* for 64-bit file support */
#include "gsklog.h"
#ifndef __REENTRANT
#define __REENTRANT     1
#endif
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* for stdout/stderr redirection support */
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include "gskmainloop.h"


#define DEFAULT_TIME_FORMAT     "%Y-%m-%d %H:%M:%S"
#define DEFAULT_MODE            "w"             /* or "a" for append */

typedef struct _Piece Piece;
typedef struct _PrintInfo PrintInfo;
typedef struct _ParsedFormat ParsedFormat;
struct _PrintInfo
{
  const char *domain;
  GLogLevelFlags level;
  const char *message;
};

struct _Piece
{
  void (*print) (Piece *,
                 PrintInfo *info,
                 GString *out);
};

struct _ParsedFormat
{
  guint ref_count;
  char *output_format;
  guint n_pieces;
  Piece **pieces;
};

static void
literal_print (Piece *piece,
               PrintInfo *info,
               GString *out)
{
  g_string_append (out, (char*)(piece + 1));
}

static Piece *
piece_literal (const char *str, gssize len)
{
  Piece *p;
  if (len < 0)
    len = strlen (str);
  p = g_malloc (sizeof (Piece) + len + 1);
  p->print = literal_print;
  memcpy ((char *)(p + 1), str, len);
  ((char *)(p + 1))[len] = '\0';
  return p;
}

static void
message_print (Piece *piece,
               PrintInfo *info,
               GString *out)
{
  g_string_append (out, info->message);
}

static Piece *
piece_message (void)
{
  Piece *p = g_malloc (sizeof (Piece));
  p->print = message_print;
  return p;
}

static void
nmessage_print (Piece *piece,
                PrintInfo *info,
                GString *out)
{
  guint max = * (guint *) (piece + 1);
  guint mlen = strlen (info->message);
  g_string_append_len (out, info->message, MIN (max, mlen));
}

static Piece *
piece_nmessage (guint n)
{
  Piece *p = g_malloc (sizeof (Piece) + sizeof (guint));
  p->print = nmessage_print;
  ((guint *) (p + 1))[0] = n;
  return p;
}

static void
datetime_print (Piece *piece,
                 PrintInfo *info,
                 GString *out)
{
  char buf[512];
  time_t t;
  struct tm tm;
  gboolean *b = (gboolean *) (piece + 1);
  const char *fmt = (char *) (b + 1);
  time (&t);
  if (*b)
    localtime_r (&t, &tm);
  else
    gmtime_r (&t, &tm);
  strftime (buf, sizeof (buf), fmt, &tm);
  g_string_append (out, buf);
}

static Piece *
piece_datetime(gboolean use_localtime, const char *fmt)
{
  Piece *piece = g_malloc (sizeof (Piece) + sizeof (gboolean) + strlen (fmt) + 1);
  gboolean *b = (gboolean *) (piece + 1);
  char *piece_fmt = (char *) (b + 1);
  *b = use_localtime;
  strcpy (piece_fmt, fmt);
  piece->print = datetime_print;
  return piece;
}

static void
domain_print    (Piece *piece,
                 PrintInfo *info,
                 GString *out)
{
  g_string_append (out, info->domain);
}

static Piece *
piece_domain (void)
{
  Piece *piece = g_new (Piece, 1);
  piece->print = domain_print;
  return piece;
}

enum
{
  LEVEL_MODE_LOWERCASE,
  LEVEL_MODE_MIXEDCASE,
  LEVEL_MODE_UPPERCASE,
  LEVEL_MODE_G
};

static struct
{
  GLogLevelFlags level;
  const char *tags[4];
  const char *suffix;
} level_infos[6] =
{
  { G_LOG_LEVEL_ERROR, { "error",   "Error",   "ERROR",   "***ERROR***" },  "!!!" },
  { G_LOG_LEVEL_CRITICAL, { "critical",   "Critical",   "CRITICAL",   "***CRITICAL***" },  "!!!" },
  { G_LOG_LEVEL_WARNING, { "warning",   "Warning",   "WARNING",   "*WARNING*" },  "!" },
  { G_LOG_LEVEL_MESSAGE, { "message",   "Message",   "MESSAGE",   "*Message*" },  "." },
  { G_LOG_LEVEL_INFO, { "info",   "Info",   "INFO",   "Info" },  "." },
  { G_LOG_LEVEL_DEBUG, { "debug",   "Debug",   "DEBUG",   "debug" },  "." }
};

static void
level_prefix_print (Piece *piece,
                    PrintInfo *info,
                    GString *out)
{
  unsigned level;
  for (level = 0; level < G_N_ELEMENTS (level_infos); level++)
    if ((info->level & level_infos[level].level) != 0)
      break;
  if (level == G_N_ELEMENTS (level_infos))
    g_string_append (out, "[unknown log level]");
  else 
    g_string_append (out, level_infos[level].tags[* (unsigned *)(piece + 1)]);
}

static Piece *
piece_level_prefix (unsigned level_mode)
{
  Piece *rv = g_malloc (sizeof (Piece) + sizeof (unsigned));
  rv->print = level_prefix_print;
  * (unsigned *) (rv + 1) = level_mode;
  return rv;
}

static void
level_suffix_print (Piece *piece,
                    PrintInfo *info,
                    GString *out)
{
  unsigned level;
  for (level = 0; level < G_N_ELEMENTS (level_infos); level++)
    if ((info->level & level_infos[level].level) != 0)
      break;
  if (level == G_N_ELEMENTS (level_infos))
    g_string_append (out, ".");
  else 
    g_string_append (out, level_infos[level].suffix);
}

static Piece *
piece_level_suffix (void)
{
  Piece *rv = g_malloc (sizeof (Piece));
  rv->print = level_suffix_print;
  return rv;
}

static Piece *
handle_special_piece (const char *type)
{
  gboolean got_digits = isdigit (type[0]);
  guint count = 0;
  if (got_digits)
    {
      char *end;
      count = strtoul (type, &end, 10);
      type = end;
    }
  if (strcmp (type, "message") == 0)
    {
      if (got_digits)
        return piece_nmessage (count);
      else
        return piece_message ();
    }
  else if (strncmp (type, "localtime:", 10) == 0)
    {
      const char *fmt = type + 10;
      return piece_datetime (TRUE, fmt);
    }
  else if (strncmp (type, "gmtime:", 7) == 0)
    {
      const char *fmt = type + 7;
      return piece_datetime (FALSE, fmt);
    }
  else if (strcmp (type, "localtime") == 0)
    {
      return piece_datetime (TRUE, DEFAULT_TIME_FORMAT);
    }
  else if (strcmp (type, "gmtime") == 0)
    {
      return piece_datetime (FALSE, DEFAULT_TIME_FORMAT);
    }
  else if (strcmp (type, "domain") == 0)
    return piece_domain ();
  else if (strcmp (type, "level") == 0)
    return piece_level_prefix (LEVEL_MODE_LOWERCASE);
  else if (strcmp (type, "Level") == 0)
    return piece_level_prefix (LEVEL_MODE_MIXEDCASE);
  else if (strcmp (type, "LEVEL") == 0)
    return piece_level_prefix (LEVEL_MODE_UPPERCASE);
  else if (strcmp (type, "glevel") == 0)
    return piece_level_prefix (LEVEL_MODE_G);
  else if (strcmp (type, "levelsuffix") == 0)
    return piece_level_suffix ();
  else
    return NULL;
}

static Piece *
handle_special_piece_n (const char *tmp, guint len)
{
  char *msg = g_alloca (len + 1);
  memcpy (msg, tmp, len);
  msg[len] = 0;
  return handle_special_piece (msg);
}

static ParsedFormat *
parse_format (const char *output_format)
{
  GString *literal = g_string_new ("");
  GPtrArray *pieces = g_ptr_array_new ();
  const char *orig_output_format = output_format;
  ParsedFormat *rv;
  while (*output_format)
    {
      if (*output_format != '%')
        {
          g_string_append_c (literal, *output_format);
          output_format++;
        }
      else
        {
          if (output_format[1] == '%')
            {
              g_string_append_c (literal, '%');
              output_format += 2;
            }
          else if (output_format[1] == '{')
            {
              const char *start = output_format + 2;
              const char *end = strchr (start, '}');
              Piece *piece;
              if (end == NULL)
                {
                  g_warning ("missing '}'");
                  return NULL; /* XXX: leaks, needs proper error handling */
                }
              piece = handle_special_piece_n (start, end - start);
              if (piece == NULL)
                {
                  g_warning ("error parsing special log-format token '%.*s' (in context '%s')", (int)(end - start), start, output_format);
                  return NULL;  /* XXX: leaks, needs proper error handling */
                }
              if (literal->len > 0)
                {
                  g_ptr_array_add (pieces, piece_literal (literal->str, literal->len));
                  g_string_set_size (literal, 0);
                }
              g_ptr_array_add (pieces, piece);
              output_format = end + 1;
            }
          else
            {
              g_warning ("error parsing format string, at '%s'", output_format);
              return NULL;   /* XXX: leaks, needs proper error handling */
            }
        }
    }
#if 0   /* this commented code adds a newline, which we no longer need */
  if (literal->len == 0 || literal->str[literal->len-1] != '\n')
    g_string_append_c (literal, '\n');
#endif
  if (literal->len > 0)
    g_ptr_array_add (pieces, piece_literal (literal->str, literal->len));
  g_string_free (literal, TRUE);

  rv = g_new (ParsedFormat, 1);
  rv->ref_count = 1;
  rv->output_format = g_strdup (orig_output_format);
  rv->n_pieces = pieces->len;
  rv->pieces = (Piece **) g_ptr_array_free (pieces, FALSE);
  return rv;
}

static ParsedFormat *
parsed_format_new (const char *output_format)
{
  static GHashTable *output_format_to_parsed_format = NULL;
  ParsedFormat *rv;
  if (output_format == NULL)
    output_format = GSK_LOG_DEFAULT_OUTPUT_FORMAT;
  if (output_format_to_parsed_format == NULL)
    output_format_to_parsed_format = g_hash_table_new (g_str_hash, g_str_equal);
  rv = g_hash_table_lookup (output_format_to_parsed_format, output_format);
  if (rv)
    {
      ++(rv->ref_count);
      return rv;
    }
  else
    {
      rv = parse_format (output_format);
      if (rv == NULL)
        return NULL;
      g_hash_table_insert (output_format_to_parsed_format, rv->output_format, rv);
    }
  return rv;
}

static GHashTable *filename_to_FILE = NULL;

static FILE *
log_file_maybe_open (const char *filename, const char *mode)
{
  FILE *fp;
  if (filename_to_FILE == NULL)
    filename_to_FILE = g_hash_table_new (g_str_hash, g_str_equal);
  if (g_hash_table_lookup_extended (filename_to_FILE, filename, NULL, (gpointer *) &fp))
    return fp;
  fp = fopen (filename, mode);
  if (fp != NULL)
    setlinebuf (fp);
  g_hash_table_insert (filename_to_FILE, g_strdup (filename), fp);
  return fp;
}

/**
 * gsk_log_append:
 * @filename: log filename that should be opened in append-mode,
 * rather than overwritten.
 *
 * Indicate that the given logfile should
 * be appended to, rather than overwritten.
 *
 * This must be invoked before any other references to the logfile.
 */
void gsk_log_append (const char *filename)
{
  log_file_maybe_open (filename, "a");
}

struct _GskLogTrap
{
  const char *domain;
  GLogLevelFlags level_mask;

  ParsedFormat *format;

  gpointer data;
  GskLogTrapFunc func;
  GDestroyNotify destroy;
};

static void
handle_fp (const char *domain,
           GLogLevelFlags   level,
           const char *raw_message,
           const char *formatted_message,
           gpointer    data)
{
  FILE *fp = data;
  fputs (formatted_message, fp);
  fputc ('\n', fp);
}

static GskLogTrap *
trap_new_fp (FILE *fp, ParsedFormat *format)
{
  GskLogTrap *trap = g_new (GskLogTrap, 1);
  trap->data = fp;
  trap->format = format;
  trap->func = handle_fp;
  trap->destroy = NULL;
  return trap;
}

static void
handle_ring_buffer (const char *domain,
                    GLogLevelFlags   level,
                    const char *raw_message,
                    const char *formatted_message,
                    gpointer    data)
{
  gsk_log_ring_buffer_add (data, formatted_message);
}


static GskLogTrap *
trap_new_ring_buffer (GskLogRingBuffer *buffer,
                      ParsedFormat     *format)
{
  GskLogTrap *trap = g_new (GskLogTrap, 1);
  trap->data = buffer;
  trap->format = format;
  trap->func = handle_ring_buffer;
  trap->destroy = NULL;
  return trap;
}

static GskLogTrap *
trap_new_generic (GskLogTrapFunc func,
                  gpointer       data,
                  GDestroyNotify destroy,
                  ParsedFormat  *format)
{
  GskLogTrap *trap = g_new (GskLogTrap, 1);
  trap->data = data;
  trap->format = format;
  trap->func = func;
  trap->destroy = destroy;
  return trap;
}


static GHashTable *domain_to_slist_of_traps = NULL;

static gboolean log_system_initialized = FALSE;

static void
add_trap (const char *domain,
          GLogLevelFlags level_mask,
          GskLogTrap *trap)
{
  GSList *trap_list;
  gpointer key;
  trap->level_mask = level_mask;
  if (domain_to_slist_of_traps == NULL)
    domain_to_slist_of_traps = g_hash_table_new (g_str_hash, g_str_equal);
  if ( g_hash_table_lookup_extended (domain_to_slist_of_traps, domain,
                                     &key, (gpointer *) &trap_list) )
    {
      if (trap_list)
        trap_list = g_slist_append (trap_list, trap);
      else
        g_hash_table_insert (domain_to_slist_of_traps,
                             (gpointer) domain,
                             g_slist_prepend (NULL, trap));
    }
  else
    {
      g_hash_table_insert (domain_to_slist_of_traps,
                           key = g_strdup (domain),
                           g_slist_prepend (NULL, trap));
    }
  trap->domain = key;
}

/**
 * gsk_log_trap_domain_to_file:
 *
 * @domain: the log-domain to trap, as passed to g_log
 * or the gsk_ family of log functions.
 * @filename: the filename to write the log to.
 * @output_format: a string giving the formatting
 * to be used with the given trap.
 * It may contain any of the following strings:
 *    %{message}       the message itself.
 *    %{NNNmessage}    the first NNN characters of message.
 *    %{localtime:FMT} the time/date in local timezone, formatted as per FMT.
 *    %{gmtime:FMT}    the time/date in gm, formatted as per FMT.
 *                     (If :FMT is omitted, a default format string is used)
 *    %{domain}        the log domain.
 *    %{level}         the log level, as 'error', 'message', etc.
 *    %{glevel}        approximately how glib does the level:
 *                     'Debug', 'Info', '*Message*', '***Warning***',
 *                     '***Critical***', '***ERROR***'.
 *    %{Level}, %{LEVEL}  like %{level} with casing differences.
 *    %{levelsuffix}   '.', '!', '!!!' depending on the severity.
 *    %%               a percent symbol.
 */
GskLogTrap *
gsk_log_trap_domain_to_file(const char *domain,
                            GLogLevelFlags level_mask,
                            const char *filename,
                            const char *output_format)
{
  FILE *fp = log_file_maybe_open (filename, DEFAULT_MODE);
  ParsedFormat *format;
  GskLogTrap *trap;
  if (fp == NULL)
    return NULL;
  if (!log_system_initialized)
    gsk_log_init ();
  format = parsed_format_new (output_format);
  if (format == NULL)
    return NULL;
  trap = trap_new_fp (fp, format);
  add_trap (domain, level_mask, trap);
  return trap;
}

GskLogTrap *gsk_log_trap_generic      (const char    *domain,
                                       GLogLevelFlags trap_mask,
                                       const char    *output_format,
                                       GskLogTrapFunc func,
                                       gpointer       data,
                                       GDestroyNotify destroy)
{
  ParsedFormat *format;
  GskLogTrap *trap;
  if (!log_system_initialized)
    gsk_log_init ();
  format = parsed_format_new (output_format);
  if (format == NULL)
    return NULL;
  trap = trap_new_generic (func, data, destroy, format);
  add_trap (domain, trap_mask, trap);
  return trap;
}

static void
ignore_errors  (const char *domain,
                GLogLevelFlags level,
                const char *raw_message,
                const char *formatted_message,
                gpointer    data)
{
}

GskLogTrap *gsk_log_trap_ignore       (const char    *domain,
                                       GLogLevelFlags trap_mask)
{
  if (!log_system_initialized)
    gsk_log_init ();
  return gsk_log_trap_generic (domain, trap_mask, "",
                               ignore_errors, NULL, NULL);
}

GskLogTrap *gsk_log_trap_ring_buffer  (const char    *domain,
                                       GLogLevelFlags trap_mask,
                                       GskLogRingBuffer *buffer,
                                       const char       *output_format)
{
  ParsedFormat *format;
  GskLogTrap *trap;
  if (!log_system_initialized)
    gsk_log_init ();
  format = parsed_format_new (output_format);
  if (format == NULL)
    return NULL;
  trap = trap_new_ring_buffer (buffer, format);
  add_trap (domain, trap_mask, trap);
  return trap;
}


static void
trap_print_using_PrintInfo (GskLogTrap *trap,
                            PrintInfo *info)
{
  GString *out;
  guint i;
  if ((trap->level_mask & info->level) == 0)
    return;
  out = g_string_new ("");
  for (i = 0; i < trap->format->n_pieces; i++)
    {
      Piece *piece = trap->format->pieces[i];
      piece->print (piece, info, out);
    }
  (*trap->func) (trap->domain, info->level,
                 info->message, out->str,
                 trap->data);

  g_string_free (out, TRUE);
}

static void
log_default (const gchar   *log_domain,
             GLogLevelFlags log_level,
             const gchar   *message,
             gpointer       user_data)
{
  GSList *traps = NULL;
  if (log_domain != NULL)
    traps = g_hash_table_lookup (domain_to_slist_of_traps, log_domain);
  if (traps)
    {
      PrintInfo info = {log_domain, log_level, message};
      g_slist_foreach (traps, (GFunc) trap_print_using_PrintInfo, &info);
    }
  else
    g_log_default_handler (log_domain, log_level, message, NULL);
}

void gsk_log_init (void)
{
  if (!log_system_initialized)
    {
      if (domain_to_slist_of_traps == NULL)
        domain_to_slist_of_traps = g_hash_table_new (g_str_hash, g_str_equal);
      g_log_set_default_handler (log_default, NULL);
      log_system_initialized = TRUE;
    }
}

/* --- support for gsk_log_rotate_stdio_logs() --- */
static char *the_output_file_template = NULL;
static gboolean output_use_localtime = FALSE;
static guint output_rotation_period = 3600;


static char *
make_output_filename (guint time)
{
  char buf[4096];
  time_t t = time;
  struct tm tm;
  const char *templ = the_output_file_template;
  GString *str = g_string_new ("");
  t -= t % output_rotation_period;

  while (*templ)
    {
      if (*templ == '%')
        {
          if (templ[1] == 's')
            {
              g_string_append_printf (str, "%u", (guint) t);
              templ += 2;
              continue;
            }
          else if (templ[1] == '%')
            {
              g_string_append (str, "%%");
              templ += 2;
              continue;
            }
        }
      g_string_append_c (str, *templ);
      templ++;
    }

  if (output_use_localtime)
    localtime_r (&t, &tm);
  else
    gmtime_r (&t, &tm);

  strftime (buf, sizeof (buf), str->str, &tm);
  buf[sizeof(buf)-1] = 0;
  g_string_free (str, TRUE);

  return g_strdup (buf);
}

static void
do_stdio_dups (guint time)
{
  char *fname = make_output_filename (time);
  int fd = open (fname, O_CREAT|O_APPEND|O_WRONLY, 0644);
  if (fd < 0)
    {
      g_warning ("error opening %s: %s", fname, g_strerror (errno));
      g_free (fname);
      return;
    }
  fflush (stdout);
  fflush (stderr);
  close (STDOUT_FILENO);
  dup2 (fd, STDOUT_FILENO);
  close (STDERR_FILENO);
  dup2 (fd, STDERR_FILENO);
  close (fd);
  g_free (fname);
}

static gboolean
handle_stdio_rotation_timeout (gpointer data)
{
  guint time = gsk_main_loop_default ()->current_time.tv_sec;
  guint wait = output_rotation_period - time % output_rotation_period;

  do_stdio_dups (time);

  gsk_main_loop_add_timer (gsk_main_loop_default (),
                           handle_stdio_rotation_timeout, NULL, NULL,
                           wait * 1000, -1);
  return FALSE;
}

void
gsk_log_rotate_stdio_logs (const char *output_file_template,
                           gboolean    use_localtime,
                           guint       rotation_period)
{
  guint time = gsk_main_loop_default ()->current_time.tv_sec;
  guint wait;
  g_assert (the_output_file_template == NULL);

  the_output_file_template = g_strdup (output_file_template);
  output_use_localtime = use_localtime;
  output_rotation_period = rotation_period;

  do_stdio_dups (time);
  wait = rotation_period - time % rotation_period;
  gsk_main_loop_add_timer (gsk_main_loop_default (),
                           handle_stdio_rotation_timeout, NULL, NULL,
                           wait * 1000, -1);
}
