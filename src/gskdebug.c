#include "debug.h"

#ifdef GSK_DEBUG
const gboolean gsk_debugging_on = 1;
#else
const gboolean gsk_debugging_on = 0;
#endif

/**
 * gsk_debug_set_flags:
 * @flags: debug bits to start logging.
 *
 * Set which types of debug logs to emit.
 */
void
gsk_debug_set_flags (GskDebugFlags flags)
{
#ifdef GSK_DEBUG
  gsk_debug_flags = flags;
#endif
}

/**
 * gsk_debug_add_flags:
 * @flags: debug bits to start logging.
 *
 * Add new types of debug logs to emit.
 */
void
gsk_debug_add_flags (GskDebugFlags flags)
{
#ifdef GSK_DEBUG
  gsk_debug_flags |= flags;
#endif
}

GskDebugFlags gsk_debug_flags = 0;
