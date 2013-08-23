
#include "config.h"
#include "gskdebug.h"

/* Implement _GSK_DEBUG_PRINTF to output a g_message in
 * accordance with certain GskDebugFlags.
 */
#ifdef GSK_DEBUG
#define _GSK_DEBUG_PRINTF(flags,args) 				\
	G_STMT_START{						\
	  if ((gsk_debug_flags & (flags)) != 0)			\
	    g_message args;					\
	}G_STMT_END
#else
#define _GSK_DEBUG_PRINTF(flags,args)
#endif

#ifdef GSK_DEBUG
#define GSK_IS_DEBUGGING(short_name)	((gsk_debug_flags & GSK_DEBUG_ ## short_name) == GSK_DEBUG_ ## short_name)
#else
#define GSK_IS_DEBUGGING(short_name)	FALSE
#endif

