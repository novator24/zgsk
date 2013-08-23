#include "config.h"
#include "gskdebugalloc.h"
#include "gskmainloop.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>

/* TODO: portability? */
/* TODO: conditionalize addr2line usage (default to FALSE, until we write the parser) */

#if HAVE_EXECINFO_H
#include <execinfo.h>
#define gsk_backtrace(contexts, max_levels)         backtrace(contexts,max_levels)
#define gsk_backtrace_symbols(contexts, n_contexts) backtrace_symbols(contexts, n_contexts) 
#else
/* TODO: need more useful implementation on other systems... */
static guint
gsk_backtrace (gpointer *contexts, guint n_contexts)
{
  return 0;
}
static char **
gsk_backtrace_symbols(gpointer *contexts, guint n_contexts)
{
  char **rv;
  assert (n_contexts == 0);
  rv = malloc (sizeof (char *));
  if (rv == NULL)
    return NULL;
  memset (rv, 0, sizeof(char*));
  return rv;
}
#endif

typedef struct _AllocationContext AllocationContext;
typedef struct _AllocationHeader AllocationHeader;

struct _AllocationContext
{
  AllocationContext *parent;
  AllocationContext *next_sibling;
  AllocationContext *first_child, *last_child;
  gpointer code_context;
  guint n_blocks_used;
  guint n_bytes_used;
};

struct _AllocationHeader
{
  guint size;
  AllocationContext *context;
  guint8 underrun_detection_magic[4];
};

static AllocationContext root_context =
{
  NULL,
  NULL,
  NULL, NULL,
  NULL,
  0,
  0
};

static guint8 underrun_detection_magic[4] = { 0xf3, 0x1d, 0x77, 0x39 };
static guint8 overrun_detection_magic[4]  = { 0xe5, 0x2c, 0x96, 0xdf };
static guint  stack_depth = 16;
static guint  stack_levels_to_ignore = 1;
static FILE   *output_fp = NULL;

static AllocationContext *
get_allocate_context (guint               n_levels,
                      gpointer           *levels)
{
  AllocationContext *rv = &root_context;
  guint i;
  for (i = 0; i < n_levels; i++)
    {
      gpointer c = levels[i];
      AllocationContext *child;
      for (child = rv->first_child; child != NULL; child = child->next_sibling)
        if (child->code_context == c)
          break;
      if (child == NULL)
        {
          /* allocate context */
          child = malloc (sizeof (AllocationContext));
          child->code_context = c;
          child->parent = rv;
          child->next_sibling = NULL;
          child->first_child = child->last_child = NULL;
          child->n_blocks_used = 0;
          child->n_bytes_used = 0;
          if (rv->last_child)
            rv->last_child->next_sibling = child;
          else
            rv->first_child = child;
          rv->last_child = child;
        }
      rv = child;
    }
  return rv;
}

static gpointer debug_malloc      (gsize    n_bytes);
static gpointer debug_realloc     (gpointer mem,
                                   gsize    n_bytes);
static void     debug_free        (gpointer mem);

/* binary logging of all allocation */
static int log_fd = -1;

static void log_binary (gconstpointer data, guint len)
{
  const guint8 *buf = data;
  guint written = 0;
  while (written < len)
    {
      int rv = write (log_fd, buf + written, len - written);
      if (rv < 0)
        {
          if (errno == EINTR)
            continue;
          g_error ("error writing: %s", g_strerror (errno));
        }
      written += rv;
    }
}

static void log_pointer(gpointer p)
{
  log_binary(&p, sizeof(gpointer));
}

typedef enum
{
  LOG_MAGIC_INIT = 0x542134a,
  LOG_MAGIC_MAP,
  LOG_MAGIC_MALLOC,
  LOG_MAGIC_FREE,
  LOG_MAGIC_REALLOC,
  LOG_MAGIC_TIME
} LogMagic;

static void log_uint (guint i)
{
  log_pointer (GUINT_TO_POINTER (i));
}

void gsk_debug_alloc_open_log (const char *output)
{
  log_fd = open (output, O_WRONLY | O_CREAT | O_EXCL, 0644);
  if (log_fd < 0)
    {
      g_error ("gsk_debug_alloc_open_log: failed!!!: %s", g_strerror (errno));
    }
  else
    {
      time_t t = time(NULL);
      log_uint(LOG_MAGIC_INIT); /* magic */
      log_uint(0x01020304);     /* further magic (determines sizeof(pointer)) */
      log_uint(0);              /* version */
      log_uint(t);              /* timestamp */
    }
}
static gboolean add_time_to_log (gpointer data)
{
  log_uint(LOG_MAGIC_TIME);
  log_uint(GSK_MAIN_LOOP(data)->current_time.tv_sec);
  return TRUE;
}

void gsk_debug_alloc_add_log_time_update_idle (void)
{
  GskMainLoop *main_loop = gsk_main_loop_default ();
  gsk_main_loop_add_idle (main_loop, add_time_to_log, main_loop,NULL);
}

typedef struct _Map Map;
struct _Map
{
  gpointer start;
  gsize len;
  Map *next;
};

static void
check_one_map (Map **inout,
               gpointer data,
               gsize len,
               const char *filename)
{
  Map *map;

  /* stop if have we already encountered this map. */
  for (map = *inout; map; map = map->next)
    if (map->start == data && map->len == len)
      return;

  /* allocate a new Map record */
  map = malloc (sizeof(Map));
  map->start = data;
  map->len = len;
  map->next = *inout;
  *inout = map;

  /* output a new Map log entry */
  log_uint (LOG_MAGIC_MAP);
  log_pointer (data);
  log_uint (len);
  log_uint (strlen (filename));
  log_binary (filename, strlen (filename));
}

static void
reread_proc_self_maps (Map **inout)
{
  FILE *fp = fopen ("/proc/self/maps", "r");
  char buf[4096];
  if (fp == NULL)
    g_error ("error reading /proc/self/maps");

  while (fgets (buf, sizeof (buf), fp) != NULL)
    {
      ///   08048000-08192000 r-xp 00000000 03:01 426215     /usr/bin/vim
      guint64 addr0, addr1;
      char *at = buf;
      addr0 = g_ascii_strtoull (buf, &at, 16);
      if (*at != '-')
        g_error ("/proc/self/maps: expected -");
      addr1 = g_ascii_strtoull (at + 1, &at, 16);
      if (*at != ' ')
        at++;
      if (memcmp ("r-xp", at, 4) != 0)
        continue;
      at = strchr (at, '/');
      g_assert (at != NULL);
      g_strchomp (at);
      check_one_map (inout, (gpointer) (gsize) addr0, (gsize)(addr1 - addr0), at);
    }
  fclose (fp);
}

static void
check_needs_map_entries (guint n_levels, void **contexts)
{
  static Map *maps = NULL;
  guint i;
  for (i = 0; i < n_levels; i++)
    {  
      Map *at = maps;
      while (at != NULL
         && (contexts[i] < at->start
          || contexts[i] >= (gpointer) ((char*)at->start + at->len)))
        at = at->next;
      if (at == NULL)
        reread_proc_self_maps (&maps);
    }
}

static gpointer 
debug_malloc      (gsize    n_bytes)
{
  guint total_levels = stack_depth + stack_levels_to_ignore;
  gpointer *context = g_newa (gpointer, total_levels);
  guint n_levels = gsk_backtrace (context, total_levels);
  AllocationContext *ac;
  AllocationHeader *header;
  if (n_bytes == 0)
    return NULL;
  if (n_levels <= stack_levels_to_ignore)
    n_levels = 0;
  else
    n_levels -= stack_levels_to_ignore;
  context += stack_levels_to_ignore;
  ac = get_allocate_context (n_levels, context);
  ac->n_bytes_used += n_bytes;
  ac->n_blocks_used += 1;


  header = malloc (sizeof (AllocationHeader) + n_bytes + 4);
  assert (header != NULL);
  header->size = n_bytes;
  header->context = ac;
  memcpy (header->underrun_detection_magic, underrun_detection_magic, 4);
  memcpy ((char*)(header + 1) + n_bytes, overrun_detection_magic, 4);

  if (log_fd >= 0)
    {
      guint i;
      check_needs_map_entries (n_levels, context);
      log_uint (LOG_MAGIC_MALLOC);
      log_uint (n_bytes);
      log_uint (n_levels);
      for (i = 0; i < n_levels; i++)
        log_pointer (context[i]);
      log_pointer (header + 1);
    }
  return header + 1;
}

static gpointer 
debug_realloc     (gpointer mem,
                   gsize    n_bytes)
{
#if 0
  AllocationHeader *header = ((AllocationHeader*)mem) - 1;
  guint old_size;
  assert (memcmp (header->underrun_detection_magic, underrun_detection_magic, 4) == 0);
  assert (memcmp ((char*)(header + 1) + header->size, overrun_detection_magic, 4) == 0);
  assert (header->context->n_bytes_used >= header->size);
  old_size = header->size;

  header = realloc (header, sizeof (AllocationHeader) + n_bytes + 4);
  header->size = n_bytes;
  memcpy ((char*)(header + 1) + n_bytes, overrun_detection_magic, 4);

  header->context->n_bytes_used -= old_size;
  header->context->n_bytes_used += n_bytes;

  return header + 1;
#else
  void *rv;
  guint size;
  if (mem)
    {
      AllocationHeader *header = ((AllocationHeader*)mem) - 1;
      assert (memcmp (header->underrun_detection_magic, underrun_detection_magic, 4) == 0);
      size = header->size;
      assert (memcmp ((char*)(header + 1) + size, overrun_detection_magic, 4) == 0);
      assert (header->context->n_bytes_used >= size);
    }
  else
    size = 0;

  if (log_fd >= 0)
    {
      log_uint (LOG_MAGIC_REALLOC);
      log_pointer (mem);
      log_uint (size);
    }

  stack_levels_to_ignore++;
  rv = debug_malloc (n_bytes);
  memcpy (rv, mem, MIN (n_bytes, size));
  debug_free (mem);
  stack_levels_to_ignore--;

  return rv;
#endif
}

static void
debug_free        (gpointer mem)
{
  AllocationHeader *header = ((AllocationHeader*)mem) - 1;
  if (mem == NULL)
    return;
  assert (memcmp (header->underrun_detection_magic, underrun_detection_magic, 4) == 0);
  assert (memcmp ((char*)(header + 1) + header->size, overrun_detection_magic, 4) == 0);
  assert (header->context->n_bytes_used >= header->size);
  memset (header->underrun_detection_magic, 0, 4);
  memset ((char*)(header + 1) + header->size, 0, 4);
  memset (mem, 0xaf, header->size);

  if (log_fd >= 0)
    {
      guint i;
      guint total_levels = stack_depth + stack_levels_to_ignore;
      gpointer *context = g_newa (gpointer, total_levels);
      guint n_levels = gsk_backtrace (context, total_levels);
      log_uint (LOG_MAGIC_FREE);
      if (n_levels < stack_levels_to_ignore)
        n_levels = 0;
      else
        n_levels -= stack_levels_to_ignore;
      context += stack_levels_to_ignore;
      log_uint (header->size);
      log_uint (n_levels);
      for (i = 0; i < n_levels; i++)
        log_pointer (context[i]);
      log_pointer (mem);
    }

  header->context->n_bytes_used -= header->size;
  header->context->n_blocks_used -= 1;
  free (header);
}

static GMemVTable debug_mem_vtable =
{
  debug_malloc,
  debug_realloc,
  debug_free,
  NULL,
  NULL,
  NULL
};

static const char *exe_name;

void gsk_set_debug_mem_vtable (const char *executable_filename)
{
  assert (executable_filename != NULL);
  exe_name = strdup (executable_filename);
  assert (exe_name != NULL);
  g_mem_set_vtable (&debug_mem_vtable);
}

static guint get_num_context_symbols (AllocationContext *context,
                                      guint              depth)
{
  guint rv = 0;
  AllocationContext *child;
  if (context->n_blocks_used > 0)
    rv += depth;
  for (child = context->first_child; child != NULL; child = child->next_sibling)
    rv += get_num_context_symbols (child, depth + 1);
  return rv;
}
static void get_context_symbols (AllocationContext *context,
                                 gpointer         **symbols_at)
{
  AllocationContext *child;
  if (context->n_blocks_used > 0)
    {
      guint n = 0;
      guint i;
      AllocationContext *at = context;
      while (at->parent)
        {
          (*symbols_at)[n++] = at->code_context;
          at = at->parent;
        }

      /* reverse the pointers... */
      for (i = 0; i < n / 2; i++)
        {
          gpointer swap = (*symbols_at)[i];
          (*symbols_at)[i] = (*symbols_at)[n - 1 - i];
          (*symbols_at)[n - 1 - i] = swap;;
        }

      (*symbols_at) += n;
    }
  for (child = context->first_child; child != NULL; child = child->next_sibling)
    get_context_symbols (child, symbols_at);
}

static gboolean
is_executable_symbol (char *symbol, char **addr_start_out)
{
  /* XXX: for now, dont do this, until we know the format of the output */
  return FALSE;
}

static void
resolve_executable_symbols (guint n, char **symbols, gpointer *to_free_out)
{
  char fname[256];
  char addr2line_cmd[512];
  FILE *addr2line;
  FILE *fp;
  char *at;
  guint i;
  guint n_addr_written = 0;
  char *addr;
  struct stat stat_buf;

  static guint seq_no = 0;

  /* make a temporary filename */
  snprintf (fname, sizeof (fname),
            "/tmp/gsk-debug-memdump.tmp.%lu.%u.%u",
            (unsigned long)time(NULL), getpid(), seq_no++);

  /* open addr2line */
  snprintf (addr2line_cmd, sizeof (addr2line_cmd),
            "addr2line --exe=\"%s\" > %s",
            exe_name, fname);
  addr2line = popen (addr2line_cmd, "w");

  /* print addresses to it. */
  for (i = 0; i < n; i++)
    if (is_executable_symbol (symbols[i], &addr))
      {
        fprintf (addr2line, "%s\n", addr);
        n_addr_written++;
      }

  /* close it and suck its output into memory */
  if (pclose (addr2line) != 0)
    assert (0);
  if (stat (fname, &stat_buf) < 0)
    assert (0);
  *to_free_out = malloc (stat_buf.st_size + 1);
  fp = fopen (fname, "rb");
  assert (fp);
  if (stat_buf.st_size != 0
   && fread (*to_free_out, stat_buf.st_size, 1, fp) != 1)
    assert (0);
  ((char*)(*to_free_out))[stat_buf.st_size] = 0; /* NUL-terminate */
  fclose (fp);
  unlink (fname);

  /* sanity check: count the number of newlines to make sure it matches n_addr_written */
  at = *to_free_out;
  for (i = 0; i < n_addr_written; i++)
    {
      at = strchr (at, '\n');
      assert (at);
      at++;
    }
  assert (*at == 0);

  /* overwrite the symbols; chomp newlines */
  at = *to_free_out;
  for (i = 0; i < n; i++)
    if (is_executable_symbol (symbols[i], &addr))
      {
        symbols[i] = at;
        at = strchr (at, '\n');
        assert (at);
        *at++ = 0;
      }
}

static void  print_nonempty_contexts (AllocationContext *context,
                                      guint              depth,
                                      FILE              *fp,
                                      char            ***symbols_inout,
                                      guint             *n_contexts_out,
                                      guint             *n_blocks_out,
                                      guint             *n_bytes_out)
{
  AllocationContext *child;
  if (context->n_blocks_used > 0)
    {
      /* print this context */
      guint i;
      fprintf (fp, "%u bytes allocated in %u blocks from:\n",
               context->n_bytes_used, context->n_blocks_used);
      for (i = 0; i < depth; i++)
        fprintf (fp, "  %s\n", (*symbols_inout)[i]);
      *n_contexts_out += 1;
      *n_blocks_out += context->n_blocks_used;
      *n_bytes_out += context->n_bytes_used;

      *symbols_inout += depth;
    }
  for (child = context->first_child; child != NULL; child = child->next_sibling)
    print_nonempty_contexts (child, depth + 1, fp, symbols_inout, n_contexts_out, n_blocks_out, n_bytes_out);
}
void gsk_print_debug_mem_vtable (void)
{
  guint n_nonempty_contexts;
  gpointer *code_contexts;
  gpointer *code_contexts_at;
  char **symbols;
  char **symbols_at;
  gpointer to_free = NULL;
  guint n_contexts, n_blocks, n_bytes;
  FILE *fp = output_fp ? output_fp : stderr;

  /* iterate the allocation tree, finding the number of blocks to report */
  n_nonempty_contexts = get_num_context_symbols (&root_context, 0);

  /* allocate enough space for all the contexts */
  code_contexts = malloc (sizeof (gpointer) * n_nonempty_contexts);
  code_contexts_at = code_contexts;
  get_context_symbols (&root_context, &code_contexts_at);
  assert (code_contexts_at == code_contexts + n_nonempty_contexts);

  /* get the symbols */
  symbols = gsk_backtrace_symbols (code_contexts, n_nonempty_contexts);

  /* use addr2line to resolve references to this executable */
  resolve_executable_symbols (n_nonempty_contexts, symbols, &to_free);

  /* write preamble */
#ifdef __linux__
  {
    char buf[128];
    FILE *read_fp;
    unsigned long ps = getpagesize ();
    unsigned long size, resident, share, code_size, lib_size, data_size, dirty;
    g_snprintf (buf, sizeof (buf), "/proc/%u/statm", (unsigned) getpid ());
    read_fp = fopen (buf, "r");
    if (fscanf (read_fp, "%lu %lu %lu %lu %lu %lu",
                &size, &resident, &share, &code_size, &lib_size, &data_size,
                &dirty) != 6)
      g_warning ("couldn't parse /proc/%u/statm", (unsigned) getpid ());
    else
      fprintf (fp,
               "rusage: size: %lu\n"
               "rusage: resident: %lu\n"
               "rusage: share: %lu\n"
               "rusage: code-size: %lu\n"
               "rusage: lib-size: %lu\n"
               "rusage: data-size: %lu\n",
               ps * size,
               ps * resident,
               ps * share,
               ps * code_size,
               ps * lib_size,
               ps * data_size);
    fclose (read_fp);
  }
#elif HAVE_GETRUSAGE
  {
    struct rusage ru;
    if (getrusage (RUSAGE_SELF, &ru) == 0)
      {
        fprintf (fp,
                 "rusage: user-time: %u.%06us\n"
                 "rusage: system-time: %u.%06us\n"
                 "rusage: max-rss: %ld\n"
                 "rusage: shared-mem: %ld\n"
                 "rusage: unshared-mem: %ld\n"
                 "rusage: stack: %ld\n"
                 "rusage: page-reclaims: %ld\n"
                 "rusage: page-faults: %ld\n"
                 "rusage: n-swaps: %ld\n"
                 "rusage: block-input ops: %ld\n"
                 "rusage: block-output ops: %ld\n"
                 "rusage: signals-received: %ld\n"
                 "rusage: volutary context switches: %ld\n"
                 "rusage: involutary context switches: %ld\n",
                 (unsigned) ru.ru_utime.tv_sec, (unsigned) ru.ru_utime.tv_usec,
                 (unsigned) ru.ru_stime.tv_sec, (unsigned) ru.ru_stime.tv_usec,
                 (long) ru.ru_maxrss,
                 (long) ru.ru_ixrss,
                 (long) ru.ru_idrss,
                 (long) ru.ru_isrss,
                 (long) ru.ru_minflt,
                 (long) ru.ru_majflt,
                 (long) ru.ru_nswap,
                 (long) ru.ru_inblock,
                 (long) ru.ru_oublock,
                 (long) ru.ru_nsignals,
                 (long) ru.ru_nvcsw,
                 (long) ru.ru_nivcsw);
      }
  }
#endif          /* HAVE_GETRUSAGE */

  /* iterate the tree in the same order, printing all the symbols */
  symbols_at = symbols;
  n_contexts = n_blocks = n_bytes = 0;
  print_nonempty_contexts (&root_context, 0, fp, &symbols_at,
                           &n_contexts, &n_blocks, &n_bytes);

  fprintf(fp, "Summary: %u bytes allocated in %u blocks from %u contexts.\n",
          n_bytes, n_blocks, n_contexts);

  /* clean up */
  free (symbols);
  if (to_free)
    free (to_free);
  
  if (output_fp)
    fclose (output_fp);
  output_fp = NULL;
}

void gsk_set_debug_mem_output_filename (const char *filename)
{
  if (output_fp)
    fclose (output_fp);
  output_fp = fopen (filename, "w");
}

/* --- object lifetime timers --- */
typedef struct
{
  GskSource *source;
  GObject *object;	/* weak reference */
  GskDebugObjectTimedOut func;
  gpointer data;
  GDestroyNotify destroy;
} TimeoutData;
  
static void handle_object_finalized (gpointer data, GObject *where_the_object_was)
{
  TimeoutData *td = data;
  gsk_source_remove (td->source);
  if (td->destroy) 
    td->destroy(td->data);
  g_free (td);
}

static gboolean handle_timeout (gpointer data)
{
  TimeoutData *td = data;
  g_object_weak_unref (td->object, handle_object_finalized, data);
  if (td->func)
    td->func (td->object, td->data);
  else
    g_error ("object %p [%s] exceeded allowed lifetime [data=%p]",
             G_OBJECT (td->object), G_OBJECT_TYPE_NAME (td->object), td->data);
  if (td->destroy) 
    td->destroy(td->data);
  g_free (td);
  return FALSE;
}

void gsk_debug_set_object_timeout (GObject *object,
                                   guint    max_duration_millis,
                                   GskDebugObjectTimedOut func,
                                   gpointer data,
                                   GDestroyNotify destroy)
{
  TimeoutData *td = g_new (TimeoutData, 1);
  td->func = func;
  td->data = data;
  td->destroy = destroy;
  td->object = object;
  td->source = gsk_main_loop_add_timer (gsk_main_loop_default (),
                                        handle_timeout, td, NULL,
                                        max_duration_millis, -1);
  g_object_weak_ref (object, handle_object_finalized, td);
}
