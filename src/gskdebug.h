#ifndef __GSK_DEBUG_H_
#define __GSK_DEBUG_H_

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  GSK_DEBUG_IO     		= (1<<0),
  GSK_DEBUG_STREAM 		= (1<<1),
  GSK_DEBUG_STREAM_LISTENER 	= (1<<2),
  GSK_DEBUG_STREAM_DATA         = (1<<3),
  GSK_DEBUG_LIFETIME		= (1<<4),
  GSK_DEBUG_MAIN_LOOP		= (1<<5),
  GSK_DEBUG_DNS			= (1<<6),
  GSK_DEBUG_HOOK		= (1<<7),
  GSK_DEBUG_SSL     		= (1<<8),
  GSK_DEBUG_HTTP     		= (1<<9),
  GSK_DEBUG_FTP     		= (1<<10),
  GSK_DEBUG_REQUEST 		= (1<<11),
  GSK_DEBUG_FD                  = (1<<12),

  GSK_DEBUG_ALL			= 0xffffffff
} GskDebugFlags;
/* note: if you add to this table, you should modify gskinit.c */

/* Depends on whether --enable-gsk-debug was specified to configure.  */
extern const gboolean gsk_debugging_on;

void gsk_debug_set_flags (GskDebugFlags flags);
void gsk_debug_add_flags (GskDebugFlags flags);

/* read-only */
extern GskDebugFlags gsk_debug_flags;

G_END_DECLS

#endif
