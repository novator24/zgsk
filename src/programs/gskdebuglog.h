#ifndef __GSK_DEBUG_MEMORY_LOG_H_
#define __GSK_DEBUG_MEMORY_LOG_H_

#include <glib.h>
#include <stdio.h>

G_BEGIN_DECLS

typedef struct _GskDebugLogMap GskDebugLogMap;
typedef struct _GskDebugLog GskDebugLog;

struct _GskDebugLog
{
  FILE *fp;
  gboolean is_64bit;
  gboolean little_endian;

  guint64 timestamp;

  guint n_maps;
  GskDebugLogMap *maps;

  GHashTable *context_cache;
};

typedef struct _GskDebugLogContext GskDebugLogContext;
struct _GskDebugLogContext
{
  guint64 address;      /* the key */
  char *desc;           /* the value */
};

struct _GskDebugLogMap
{
  guint64 start, length;
  char *path;
};


typedef enum
{
  GSK_DEBUG_LOG_PACKET_INIT = 0x542134a,
  GSK_DEBUG_LOG_PACKET_MAP,
  GSK_DEBUG_LOG_PACKET_MALLOC,
  GSK_DEBUG_LOG_PACKET_FREE,
  GSK_DEBUG_LOG_PACKET_REALLOC,
  GSK_DEBUG_LOG_PACKET_TIME
} GskDebugLogPacketType;


typedef struct
{
  GskDebugLogPacketType type;
  union {
    struct {
      guint n_bytes, n_contexts;
      guint64 *contexts;
      guint64 allocation;
    } malloc;
    struct {
      guint n_bytes, n_contexts;
      guint64 *contexts;
      guint64 allocation;
    } free;
    struct {
      guint64 mem;
      guint64 size;
    } realloc;
  } info;
} GskDebugLogPacket;

GskDebugLog       *gsk_debug_log_open (const char *filename,
                                       GError    **error);
GskDebugLogPacket *gsk_debug_log_read (GskDebugLog *log);
void               gsk_debug_log_packet_free (GskDebugLogPacket *);
void               gsk_debug_log_rewind (GskDebugLog *log);
void               gsk_debug_log_close(GskDebugLog *);


G_END_DECLS

#endif
