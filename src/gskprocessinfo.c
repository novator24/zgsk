#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdlib.h>

#if !defined(PAGE_SIZE) && !defined(PAGESIZE)
#include <sys/user.h>
#endif

#include "gskprocessinfo.h"
#include "gskerror.h"
#include "gskerrno.h"
#include "gskstdio.h"
#include "gskmacros.h"

#define LPAREN          '('
#define RPAREN          ')'

/* we assume that PAGE_SIZE is defined by <limits.h>.
   just in case, define it to the value of PAGESIZE,
   if that'll fix things. */
#if !defined(PAGE_SIZE) && defined(PAGESIZE)
#define PAGE_SIZE PAGESIZE
#endif

/* Define 'get_page_size()' */
#ifdef PAGE_SIZE
# define get_page_size()	PAGE_SIZE
#else

  /* Define SYSCONF_PAGESIZE_MACRO */
# ifdef _SC_PAGESIZE
#  define SYSCONF_PAGESIZE_MACRO	_SC_PAGESIZE
# elif defined(_SC_PAGE_SIZE)
#  define SYSCONF_PAGESIZE_MACRO	_SC_PAGE_SIZE
# else
#  error no way to find PAGE_SIZE
# endif

static guint get_page_size (void)
{
  static guint rv = 0;
  if (rv == 0)
    {
      rv = sysconf (SYSCONF_PAGESIZE_MACRO);
      g_assert (rv != 0);
    }
  return rv;
}
#endif	/* !defined(PAGE_SIZE) */

GskProcessInfo *
gsk_process_info_get (guint          pid,
                      GError       **error)
{
  GskProcessInfo *rv;
  char *filename = g_strdup_printf ("/proc/%u/stat", pid);
  FILE *fp = fopen (filename, "r");
  char *content;
  const char *at;
  const char *end_name;
  gint dummy_int;
  guint dummy_uint;
  if (fp == NULL)
    {
      /* TODO: remap some error codes,
         like ENOENT to GSK_ERROR_PROCESS_NOT_FOUND. */
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   gsk_error_code_from_errno (errno),
                   "error opening %s: %s",
                   filename, g_strerror (errno));
      g_free (filename);
      return NULL;
    }
  content = gsk_stdio_readline (fp);
  if (content == NULL)
    {
      fclose (fp);
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_NO_DOCUMENT,
                   "expected line of text in %s", filename);
      g_free (filename);
      return NULL;
    }
  fclose (fp);
  fp = NULL;

  rv = g_new (GskProcessInfo, 1);
  rv->exe_filename = NULL;

  at = content;

#define ERROR_CLEANUP_AND_RETURN()                      \
  G_STMT_START{                                         \
    g_free (rv);                                        \
    g_free (rv->exe_filename);                          \
    g_free (filename);                                  \
    g_free (content);                                   \
    return NULL;                                        \
  }G_STMT_END
#define PARSE_ERROR(type)                               \
  G_STMT_START{                                         \
    g_set_error (error, GSK_G_ERROR_DOMAIN,             \
                 GSK_ERROR_END_OF_FILE,                 \
                 "error parsing %s from %s",            \
                 type, filename);                       \
    ERROR_CLEANUP_AND_RETURN();                         \
  }G_STMT_END
#define PARSE_ERROR_EXPECTED(what)                      \
  G_STMT_START{                                         \
    g_set_error (error, GSK_G_ERROR_DOMAIN,             \
                 GSK_ERROR_END_OF_FILE,                 \
                 "expected %s in %s",                   \
                 what, filename);                       \
    ERROR_CLEANUP_AND_RETURN();                         \
  }G_STMT_END
#define PARSE_INT(lvalue, base)                         \
  G_STMT_START{                                         \
    char *end;                                          \
    lvalue = strtol (at, &end, base);                   \
    if (at == end)                                      \
      PARSE_ERROR ("int");                              \
    at = end;                                           \
    GSK_SKIP_WHITESPACE (at);                           \
  }G_STMT_END
#define PARSE_UINT(lvalue, base)                        \
  G_STMT_START{                                         \
    char *end;                                          \
    lvalue = strtoul (at, &end, base);                  \
    if (at == end)                                      \
      PARSE_ERROR ("uint");                             \
    at = end;                                           \
    GSK_SKIP_WHITESPACE (at);                           \
  }G_STMT_END

  /* 'pid' */
  PARSE_INT (rv->process_id, 0);

  /* 'comm' */
  if (*at != LPAREN)
    PARSE_ERROR_EXPECTED ("left-paren");
  at++;
  end_name = strchr (at, RPAREN);
  if (end_name == NULL)
    PARSE_ERROR_EXPECTED ("right-paren");
  rv->exe_filename = g_strndup (at, end_name - at);
  at = end_name + 1;
  GSK_SKIP_WHITESPACE (at);

  /* 'state' */
  if (*at == 0 || strchr ("RSDZTW", *at) == NULL)
    PARSE_ERROR_EXPECTED ("a valid process state, one of R,S,D,Z,T,W");
  rv->state = *at;
  at++;
  GSK_SKIP_WHITESPACE (at);

  /* 'ppid' */
  PARSE_INT (rv->parent_process_id, 0);

  /* 'pgrp' */
  PARSE_INT (rv->process_group_id, 0);

  /* 'session' */
  PARSE_INT (rv->session_id, 0);

  /* 'tty_nr' */
  PARSE_INT (rv->tty_id, 0);

  /* 'tpgid' */
  PARSE_INT (dummy_int, 0);

  /* 'flags' */
  PARSE_INT (dummy_int, 8);

  /* 'minflt' */
  PARSE_UINT (dummy_uint, 0);
  /* 'cminflt' */
  PARSE_UINT (dummy_uint, 0);
  /* 'majflt' */
  PARSE_UINT (dummy_uint, 0);
  /* 'cmajflt' */
  PARSE_UINT (dummy_uint, 0);

  /* 'utime' */
  PARSE_UINT (rv->user_runtime, 0);
  /* 'stime' */
  PARSE_UINT (rv->kernel_runtime, 0);
  /* 'cutime' */
  PARSE_UINT (rv->child_user_runtime, 0);
  /* 'cstime' */
  PARSE_UINT (rv->child_kernel_runtime, 0);

  /* 'priority' */
  PARSE_INT (dummy_int, 0);

  /* 'nice' */
  PARSE_INT (rv->nice, 0);

  /* '0' */
  PARSE_INT (dummy_int, 0);

  /* 'itrealvalue */
  PARSE_INT (dummy_int, 0);
  /* 'starttime' */
  PARSE_INT (dummy_int, 0);

  /* 'vsize' */
  PARSE_UINT (rv->virtual_memory_size, 0);

  /* 'rss' */
  PARSE_UINT (dummy_uint, 0);
  rv->resident_memory_size = dummy_uint * get_page_size ();

  /* 'rlim' */
  PARSE_UINT (dummy_uint, 0);

  /* TODO: there's more fields, but we don't much care.
     also the info in 'statm' is worth checking out */

  g_free (filename);
  g_free (content);

  return rv;
}

void
gsk_process_info_free(GskProcessInfo *info)
{
  g_free (info->exe_filename);
  g_free (info);
}

const char *gsk_process_info_state_name (GskProcessInfoState state)
{
  switch (state)
    {
    case GSK_PROCESS_INFO_RUNNING:
      return "running";
    case GSK_PROCESS_INFO_SLEEPING:
      return "sleeping";
    case GSK_PROCESS_INFO_DISK:
      return "disk-wait";
    case GSK_PROCESS_INFO_ZOMBIE:
      return "zombie";
    case GSK_PROCESS_INFO_TRACED:
      return "traced";
    case GSK_PROCESS_INFO_PAGING:
      return "paging";
    default:
      return "**unknown process state**";
    }
}
