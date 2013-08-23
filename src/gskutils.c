#include "config.h"	/* must be first for 64-bit file-offset support */
#include "gskutils.h"
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include "gskerror.h"
#include "gskerrno.h"

/**
 * gsk_mkdir_p:
 * @dir: the directory to make.
 * @permissions: file creation mode for the directory
 * and its subdirectories.
 * @error: where to put an error, if one occurs.
 *
 * Make a directory and any nonexistant parent directories.
 *
 * This parallels the unix command 'mkdir -p DIR'.
 *
 * returns: whether the directory now exists.
 * Note that this function returns TRUE if the directory
 * existed on startup.
 */
gboolean gsk_mkdir_p (const char *dir,
                      guint       permissions,
		      GError    **error)
{
  guint dir_len = strlen (dir);
  char *dir_buf = g_alloca (dir_len + 1);
  guint cur_len = 0;

  if (g_file_test (dir, G_FILE_TEST_IS_DIR))
    return TRUE;

  /* append to dir_buf any number of consecutive '/'
     characters. */
#define SCAN_THROUGH_SLASHES()  \
  G_STMT_START{                 \
    while (cur_len < dir_len && dir[cur_len] == G_DIR_SEPARATOR) \
      dir_buf[cur_len++] = G_DIR_SEPARATOR; \
  }G_STMT_END

  SCAN_THROUGH_SLASHES();
  while (cur_len < dir_len)
    {
      const char *slash = strchr (dir + cur_len, G_DIR_SEPARATOR);    /* not UTF8 */
      guint new_cur_len;
      if (slash == NULL)
        new_cur_len = dir_len;
      else
        new_cur_len = slash - dir;

      memcpy (dir_buf + cur_len, dir + cur_len, new_cur_len - cur_len);
      dir_buf[new_cur_len] = 0;
      cur_len = new_cur_len;

      if (g_file_test (dir_buf, G_FILE_TEST_IS_DIR))
        ;
      else
        {
          if (mkdir (dir_buf, permissions) < 0)
            {
              if (errno != EEXIST)
                {
                  g_set_error (error, GSK_G_ERROR_DOMAIN,
                               gsk_error_code_from_errno (errno),
                               "error making directory %s: %s",
                               dir_buf, g_strerror (errno));
                  return FALSE;
                }
            }
        }

      SCAN_THROUGH_SLASHES();
    }
  return TRUE;
#undef SCAN_THROUGH_SLASHES
}


/* TODO: actually, this should be TRUE for all known versions
   of linux... that would speed up rm_rf by eliminating an
   extra lstat(2) call. */
#define UNLINK_DIR_RETURNS_EISDIR       FALSE

static gboolean
safe_unlink (const char *dir_or_file,
             const char **failed_op_out,
             int *errno_out)
{
#if ! UNLINK_DIR_RETURNS_EISDIR
  struct stat stat_buf;
  if (lstat (dir_or_file, &stat_buf) < 0)
    {
      *errno_out = errno;
      *failed_op_out = "lstat";
      return FALSE;
    }
  if (S_ISDIR (stat_buf.st_mode))
    {
      *errno_out = EISDIR;
      *failed_op_out = "unlink";
      return FALSE;
    }
#endif
  if (unlink (dir_or_file) < 0)
    {
      *errno_out = errno;
      *failed_op_out = "unlink";
      return FALSE;
    }
  return TRUE;
}

/**
 * gsk_rm_rf:
 * @dir_or_file: the directory or file to delete.
 * @error: optional error return location.
 *
 * Recursively remove a directory or file,
 * similar to 'rm -rf DIR_OR_FILE' on the unix command-line.
 *
 * returns: whether the removal was successful.
 * This routine fails if there is a permission or i/o problem.
 * (It returns TRUE if the file does not exist.)
 * If it fails, and error is non-NULL, *error will hold
 * a #GError object.
 */
gboolean gsk_rm_rf   (const char *dir_or_file,
                      GError    **error)
{
  int e;
  const char *op;
  if (!safe_unlink (dir_or_file, &op, &e))
    {
      if (strcmp (op, "lstat") == 0 && e == ENOENT)
        return TRUE;
      if (e == EISDIR)
        {
          /* scan directory, removing contents recursively */
          GDir *dir = g_dir_open (dir_or_file, 0, error);
          const char *base;
          if (dir == NULL)
            return FALSE;
          while ((base = g_dir_read_name (dir)) != NULL)
            {
              char *fname;

              /* skip . and .. */
              if (base[0] == '.'
               && (base[1] == 0 || (base[1] == '.' && base[2] == 0)))
                continue;

              /* recurse */
              fname = g_strdup_printf ("%s/%s", dir_or_file, base);
              if (!gsk_rm_rf (fname, error))
                {
                  g_free (fname);
                  g_dir_close (dir);
                  return FALSE;
                }
              g_free (fname);
            }
          g_dir_close (dir);

          if (rmdir (dir_or_file) < 0)
            {
              g_set_error (error, GSK_G_ERROR_DOMAIN,
                           gsk_error_code_from_errno (errno),
                           "error running rmdir(%s): %s", dir_or_file,
                           g_strerror (errno));
              return FALSE;
            }
          return TRUE;
        }
      else
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN,
                       gsk_error_code_from_errno (e),
                       "error %s %s: %s", op, dir_or_file, g_strerror (e));
          return FALSE;
        }
    }
  return TRUE;
}

/**
 * gsk_lock_dir:
 * @dir: the directory to lock.
 * @block: block if the directory is locked.
 * @error: optional error return location.
 *
 * Lock the given directory and return
 * the file-descriptor associated with
 * the directory (and the lock).
 *
 * The lock is advisory; anyone can still modify the directory,
 * as long as they have adequate permissions.
 *
 * returns: the new file-descriptor, or -1 if the lock fails.
 */
int       gsk_lock_dir   (const char *dir,
                          gboolean    block,
                          GError    **error)
{
  int fd = open (dir, O_RDONLY);
  if (fd < 0)
    {
      gsk_errno_fd_creation_failed ();
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   gsk_error_code_from_errno (errno),
                   "error opening directory %s for locking: %s",
                   dir, g_strerror (errno));
      return -1;
    }
  gsk_fd_set_close_on_exec (fd, TRUE);
  if (flock (fd, LOCK_EX|(block ? 0 : LOCK_NB)) < 0)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   gsk_error_code_from_errno (errno),
                   "error locking directory %s: %s",
                   dir, g_strerror (errno));
      close (fd);
      return -1;
    }
  return fd;
}

/**
 * gsk_unlock_dir:
 * @lock_rv: the return-value from gsk_lock_dir()
 * @error: optional error return location.
 *
 * Unlock a directory locked with gsk_lock_dir().
 *
 * returns: whether unlocking succeeded.
 */
gboolean  gsk_unlock_dir (int         lock_rv,
                          GError    **error)
{
  if (flock (lock_rv, LOCK_UN) < 0)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   gsk_error_code_from_errno (errno),
                   "error unlocking directory: %s",
                   g_strerror (errno));
      return FALSE;
    }
  close (lock_rv);
  return TRUE;
}


/**
 * gsk_escape_memory:
 * @data: raw data to C-escape.
 * @len: length of raw data in bytes
 *
 * Convert a bunch of memory to something
 * suitable for addition into a C string.
 *
 * returns: a newly allocated string of escaped data.
 */
char *
gsk_escape_memory (gconstpointer    data,
		   guint            len)
{
  GString *out = g_string_new ("");
  guint i;
  for (i = 0; i < len; i++)
    {
      guint8 c = ((guint8 *) data)[i];
      if (!isprint (c) || c <= 27 || c == '"' || c == '\\')
	{
	  switch (c)
	    {
	    case '\t':
	      g_string_append (out, "\\t");
	      break;
	    case '\r':
	      g_string_append (out, "\\r");
	      break;
	    case '\n':
	      g_string_append (out, "\\n");
	      break;
	    case '\\':
	      g_string_append (out, "\\\\");
	      break;
	    case '"':
	      g_string_append (out, "\\\"");
	      break;
	    default:
              {
                /* if the next character is a digit,
                   we must use a 3-digit code.
                   at the end-of-string we use a 3-digit to be careful
                   so that two escaped strings can be concatenated. */
                if (i + 1 < len && g_ascii_isdigit (((guint8*)data)[1]))
                  g_string_sprintfa (out, "\\%03o", c);
                else
                  g_string_sprintfa (out, "\\%o", c);
                break;
              }
	    }
	}
      else
	{
	  g_string_append_c (out, c);
	}
    }
  return g_string_free (out, FALSE);
}


/**
 * gsk_unescape_memory:
 * @quoted: C-string to unquote.
 * @has_quote_marks: whether to strip off double-quotes.
 * @end: where to store the end of the quoted string (right
 * past the last double-quote.
 * @length_out: where to store the length of the
 * unquoted memory.
 * @error: optional error return location.
 *
 * Take a C double-quoted string and make it into a
 * suitable for addition into a C string.
 *
 * returns: a newly allocated string of raw data,
 * with an extra NUL postpended, so that it can
 * be used as a string.
 */
gpointer
gsk_unescape_memory (const char *quoted,
                     gboolean    has_quote_marks,
                     const char**end,
                     guint      *length_out,
                     GError    **error)
{
  /* parse quoted string */
  GString *s = g_string_new ("");
  const char *str = quoted;
  if (has_quote_marks)
    {
      if (*str != '"')
        goto expected_double_quote;
      str++;
    }
  while (*str != '"' && *str != '\0')
    {
      if (*str == '\\')
        {
          str++;
          if (g_ascii_isalpha (*str))
            {
              static const char *pairs
                = "r\r"
                  "n\n"
                  "t\t"
                  ;
              const char *p = pairs;
              while (*p)
                {
                  if (*p == *str)
                    break;
                  p += 2;
                }
              if (*p == 0)
                goto bad_backslashed;
              g_string_append_c (s, p[1]);
            }
          else if (g_ascii_isdigit (*str))
            {
              /* octal: use up to three octal digits */
              char v[4];
              v[0] = *str;
              if (!g_ascii_isdigit (str[1]))
                v[1] = 0;
              else
                {
                  v[1] = str[1];
                  if (!g_ascii_isdigit (str[2]))
                    v[2] = 0;
                  else
                    {
                      v[2] = str[2];
                      v[3] = 0;
                    }
                }
              guint c = strtoul (v, NULL, 8);
              g_string_append_c (s, c);
              str += strlen (v);
            }
          else
            goto bad_backslashed;
        }
      else
        {
          g_string_append_c (s, *str);
          str++;
        }
    }
  if (has_quote_marks)
    {
      if (*str != '"')
        goto expected_double_quote;
      str++;
    }
  if (end)
    *end = str;
  if (length_out)
    *length_out = s->len;
  return g_string_free (s, FALSE);

bad_backslashed:
  g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
               "unknown backslashed character \\%c", *str);
  g_string_free (s, TRUE);
  return NULL;

expected_double_quote:
  if (*str == 0)
    g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                 "end-of-string parsing double-quoted string");
  else
    g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                 "bad character %c instead of double-quote", *str);
  g_string_free (s, TRUE);
  return NULL;
}


/**
 * gsk_escape_memory_hex:
 * @data: raw data to dump as hex
 * @len: length of raw data in bytes
 *
 * Convert a bunch of memory to its hex dump.
 *
 * returns: a newly allocated string of hex digits.
 */
char *gsk_escape_memory_hex (gconstpointer    data,
		             guint            len)
{
  char *out = g_malloc (len * 2 + 1);
  char *at = out;
  const guint8 *in = data;
  static char value_to_hex[16] = { "0123456789abcdef" };
  while (len--)
    {
      guint8 i = *in++;
      *at++ = value_to_hex[i >> 4];
      *at++ = value_to_hex[i & 0xf];
    }
  *at = 0;
  return out;
}

#define IS_HEX_CHAR(c)                             \
                (('0' <= (c) && (c) <= '9')        \
              || ('a' <= (c) && (c) <= 'f')        \
              || ('A' <= (c) && (c) <= 'F'))
#define HEX_VALUE(c)                               \
                (((c) <= '9') ? ((c) - '0')        \
               : ((c) <= 'F') ? ((c) - 'A' + 10)   \
               : ((c) - 'a' + 10))

/**
 * gsk_unescape_memory_hex:
 * @str: the memory dump as a string.
 * @len: the maximum length of the string, or -1 to use NUL-termination.
 * @length_out: length of the returned memory.
 * @error: where to put an error, if one occurs.
 *
 * Converts an even-number of hex digits into a
 * binary set of bytes, and returns the bytes.
 * Even if the data is length 0, you must free the return value.
 *
 * If NULL is returned, it means an error occurred.
 *
 * returns: the binary data.
 */
guint8 *
gsk_unescape_memory_hex (const char  *str,
                         gssize       len,
                         gsize       *length_out,
                         GError     **error)
{
  guint outlen;
  guint8 *rv;
  guint i;
  if (len >= 0)
    {
      for (i = 0; i < (guint)len; i++)
        if (str[i] == 0)
          {
            len = i;
            break;
          }
        else if (IS_HEX_CHAR (str[i]))
          ;
        else
          {
            g_set_error (error, GSK_G_ERROR_DOMAIN,
                         GSK_ERROR_BAD_FORMAT,
                         "invalid char %c in hex string", str[i]);
            return NULL;
          }
    }
  else
    {
      for (i = 0; str[i]; i++)
        if (!IS_HEX_CHAR (str[i]))
          {
            g_set_error (error, GSK_G_ERROR_DOMAIN,
                         GSK_ERROR_BAD_FORMAT,
                         "invalid char %c in hex string", str[i]);
            return NULL;
          }
      len = i;
    }
  if (len % 2 == 1)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_BAD_FORMAT,
                   "hex escaped data should be an even number of nibbles");
      return NULL;
    }
  outlen = len / 2;
  *length_out = outlen;
  rv = g_malloc (MAX (1,outlen));
  for (i = 0; i < outlen; i++)
    rv[i] = (HEX_VALUE (str[2*i+0]) << 4)
          + (HEX_VALUE (str[2*i+1]) << 0);
  return rv;
}

/**
 * gsk_fd_set_close_on_exec:
 * @fd: the file-descriptor to affect.
 * @close_on_exec: whether the close the file-descriptor on exec(2).
 *
 * This function sets the value of the close-on-exec flag
 * for a file-descriprtor.
 *
 * Most files will be closed on the exec system call, but normally 0,1,2
 * (standard input, output and error) are set not to close-on-exec,
 * which is why they are always inherited.
 */
void  gsk_fd_set_close_on_exec (int fd, gboolean close_on_exec)
{
  int fdflags = fcntl (fd, F_GETFD);
  if (close_on_exec)
    fdflags |= FD_CLOEXEC;
  else
    fdflags &= ~FD_CLOEXEC;
  fcntl (fd, F_SETFD, fdflags);
}

/**
 * gsk_fatal_user_error:
 * @format: printf-style format string.
 * @...: printf-style arguments.
 *
 * Print the message the standard-error,
 * plus a terminating newline.
 * Then exit with status 1.
 */
void gsk_fatal_user_error (const char *format,
                           ...)
{
  va_list args;
  va_start (args, format);
  vfprintf (stderr, format, args);
  va_end (args);
  fputc ('\n', stderr);
  exit (1);
}


/**
 * gsk_readn:
 * @fd: the file-descriptor to read from.
 * @buf: the buffer to fill.
 * @len: number of bytes to read from fd.
 *
 * Read data from fd, retrying the read
 * if not enough data is found.  This is only
 * for blocking reads.
 *
 * returns: the number of bytes read or -1 if an error occurs.
 */
gssize
gsk_readn (guint fd, void *buf, gsize len)
{
  gsize rv = 0;
  while (rv < len)
    {
      gssize read_rv = read (fd, (char*)buf + rv, len - rv);
      if (read_rv < 0)
        return read_rv;
      if (read_rv == 0)
        break;
      rv += (gsize) read_rv;
    }
  return rv;
}

/**
 * gsk_writen:
 * @fd: the file-descriptor to write to.
 * @buf: the buffer to read from.
 * @len: number of bytes to write to fd.
 *
 * Write data to fd, retrying the write
 * if not enough data is sent.  This is only
 * for blocking writes.
 *
 * returns: the number of bytes written or -1 if an error occurs.
 */
gssize
gsk_writen (guint fd, const void *buf, gsize len)
{
  gsize rv = 0;
  while (rv < len)
    {
      gssize write_rv = write (fd, (const char*)buf + rv, len - rv);
      if (write_rv < 0)
        return write_rv;
      if (write_rv == 0)
        break;
      rv += (gsize) write_rv;
    }
  return rv;
}


