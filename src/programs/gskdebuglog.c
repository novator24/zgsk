#include "gskdebuglog.h"
#include "../gskerror.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static gboolean
read_word (GskDebugLog *log,
           guint64     *out)
{
  if (log->is_64bit)
    {
      guint64 val;
      if (fread (&val, 8, 1, log->fp) != 1)
        return FALSE;
      *out = log->little_endian ? GUINT64_FROM_LE (val) : GUINT64_FROM_BE (val);
    }
  else
    {
      guint32 val;
      if (fread (&val, 4, 1, log->fp) != 1)
        return FALSE;
      *out = log->little_endian ? GUINT32_FROM_LE (val) : GUINT32_FROM_BE (val);
    }
  return TRUE;
}

GskDebugLog       *
gsk_debug_log_open (const char *filename,
                    GError    **error)
{
  guint8 data[8];
  FILE *fp;
  GskDebugLog *log;
  guint32 magic_le = GUINT32_TO_LE (GSK_DEBUG_LOG_PACKET_INIT);
  guint32 magic_be = GUINT32_TO_BE (GSK_DEBUG_LOG_PACKET_INIT);
  guint32 zeroes = 0;
  guint64 v;
  fp = fopen (filename, "rb");
  if (fp == NULL)
    {
      g_set_error (error,
                   GSK_G_ERROR_DOMAIN,
                   gsk_error_code_from_errno (errno),
                   "error opening %s: %s", filename, g_strerror (errno));
      return NULL;
    }
  log = g_new (GskDebugLog, 1);
  log->fp = fp;

  if (fread (data, 8, 1, fp) != 1)
    {
      g_set_error (error,
                   GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_END_OF_FILE,
                   "error reading %s: too short", filename);
      return NULL;
    }
  if (memcmp (data, &magic_le, 4) == 0
   && memcmp (data + 4, "\4\3\2\1", 4) == 0)
    {
      log->little_endian = TRUE;
      log->is_64bit = FALSE;
    }
  else if (memcmp (data, &magic_be, 4) == 0
   && memcmp (data + 4, "\1\2\3\4", 4) == 0)
    {
      log->little_endian = FALSE;
      log->is_64bit = FALSE;
    }
  else if (memcmp (data, &zeroes, 4) == 0
   && memcmp (data + 4, &magic_be, 4) == 0)
    {
      log->little_endian = FALSE;
      log->is_64bit = TRUE;
    }
  else if (memcmp (data + 4, &zeroes, 4) == 0
        && memcmp (data, &magic_le, 4) == 0)
    {
      log->little_endian = TRUE;
      log->is_64bit = TRUE;
    }
  else
    {
      g_set_error (error,
                   GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_BAD_FORMAT,
                   "magic invalid: cannot determine big/little 32/64 headers");
      g_free (log);
      fclose (fp);
      return NULL;
    }

  if (log->is_64bit)
    {
      /* read the \1\2\3\4 number */
      if (fread (data, 8, 1, fp) != 1)
        g_error ("debug-log-magic: too short");
      if (log->little_endian)
        g_assert (memcmp (data, "\4\3\2\1\0\0\0\0", 8) == 0);
      else
        g_assert (memcmp (data, "\0\0\0\0\4\3\2\1", 8) == 0);
    }
  if (!read_word (log, &v))
    g_error ("error reading version");
  g_assert (v == 0); /* version 0 only */
  if (!read_word (log, &v))
    g_error ("error reading timestamp");
  log->timestamp = v;
  return log;
}

GskDebugLogPacket *gsk_debug_log_read (GskDebugLog *log)
{
  guint64 magic, data;
  guint i;
  GskDebugLogPacket *rv = g_new (GskDebugLogPacket, 1);

restart:
  if (!read_word (log, &magic))
    return NULL;
  rv->type = magic;
  switch (magic)
    {
    case GSK_DEBUG_LOG_PACKET_MAP:
      {
        GskDebugLogMap map;
        if (!read_word (log, &data))
          g_error ("read-debug-alloc-log: error reading MAP message");
        map.start = data;
        if (!read_word (log, &data))
          g_error ("read-debug-alloc-log: error reading MAP message");
        map.length = data;
        if (!read_word (log, &data))
          g_error ("read-debug-alloc-log: error reading MAP message");
        map.path = g_malloc (data + 1);
        map.path[data] = 0;
        if (fread (map.path, data, 1, log->fp) != 1)
          g_error ("read-debug-alloc-log: error reading MAP message");
        log->n_maps++;
        log->maps = g_renew (GskDebugLogMap, log->maps, log->n_maps);
        log->maps[log->n_maps-1] = map;
      }
      goto restart;
    case GSK_DEBUG_LOG_PACKET_MALLOC:
      if (!read_word (log, &data))
        g_error ("read-debug-alloc-log: error reading MALLOC message");
      rv->info.malloc.n_bytes = data;
      if (!read_word (log, &data))
        g_error ("read-debug-alloc-log: error reading MALLOC message");
      rv->info.malloc.n_contexts = data;
      rv->info.malloc.contexts = g_new (guint64, rv->info.malloc.n_contexts);
      for (i = 0; i < data; i++)
        if (!read_word (log, rv->info.malloc.contexts + i))
          g_error ("read-debug-alloc-log: error reading MALLOC message");
      if (!read_word (log, &rv->info.malloc.allocation))
        g_error ("read-debug-alloc-log: error reading MALLOC message");
      break;
    case GSK_DEBUG_LOG_PACKET_FREE:
      if (!read_word (log, &data))
        g_error ("read-debug-alloc-log: error reading FREE message");
      rv->info.free.n_bytes = data;
      if (!read_word (log, &data))
        g_error ("read-debug-alloc-log: error reading FREE message");
      rv->info.free.n_contexts = data;
      rv->info.free.contexts = g_new (guint64, rv->info.free.n_contexts);
      for (i = 0; i < data; i++)
        if (!read_word (log, rv->info.free.contexts + i))
          g_error ("read-debug-alloc-log: error reading FREE message");
      if (!read_word (log, &rv->info.free.allocation))
        g_error ("read-debug-alloc-log: error reading FREE message");
      break;
    case GSK_DEBUG_LOG_PACKET_REALLOC:
      if (!read_word (log, &data))
        g_error ("read-debug-alloc-log: error reading REALLOC message");
      rv->info.realloc.mem = data;
      if (!read_word (log, &data))
        g_error ("read-debug-alloc-log: error reading REALLOC message");
      rv->info.realloc.size = data;
      break;
    case GSK_DEBUG_LOG_PACKET_TIME:
      if (!read_word (log, &data))
        g_error ("read-debug-alloc-log: error reading TIME message");
      log->timestamp = data;
      goto restart;
    default:
      g_error ("invalid magic in debug-log");
    }
  return rv;
}

void               gsk_debug_log_packet_free (GskDebugLogPacket *packet)
{
  switch (packet->type)
    {
    case GSK_DEBUG_LOG_PACKET_MALLOC:
      g_free (packet->info.malloc.contexts);
      break;
    case GSK_DEBUG_LOG_PACKET_FREE:
      g_free (packet->info.free.contexts);
      break;
    default:
      break;
    }
  g_free (packet);
}

void
gsk_debug_log_close(GskDebugLog *log)
{
  guint i;
  for (i = 0; i < log->n_maps; i++)
    g_free (log->maps[i].path);
  g_free (log->maps);
  fclose (log->fp);
  if (log->context_cache)
    g_hash_table_destroy (log->context_cache);
  g_free (log);
}
