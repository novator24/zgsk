#ifndef __GSK_DEBUG_ALLOC_H_
#define __GSK_DEBUG_ALLOC_H_

#include <glib-object.h>

G_BEGIN_DECLS

/* this calls g_mem_set_vtable() and must precede any
 * gsk or glib functions! */
void gsk_set_debug_mem_vtable (const char *executable_filename);

/* print the allocations in a manner similar to valgrind */
/* HINT: register this with g_atexit() */
void gsk_print_debug_mem_vtable (void);

void gsk_set_debug_mem_output_filename (const char *filename);


/* --- for logging all allocations --- */

/* ok to call before gsk_init() */
void gsk_debug_alloc_open_log (const char *output);

/* must be called after gsk_init() */
void gsk_debug_alloc_add_log_time_update_idle (void);

/* --- object timeouts --- */

typedef void (*GskDebugObjectTimedOut) (GObject *object, gpointer data);
void gsk_debug_set_object_timeout (GObject *object,
                                   guint    max_duration_millis,
                                   GskDebugObjectTimedOut func,
                                   gpointer data,
                                   GDestroyNotify destroy);

G_END_DECLS

#endif
