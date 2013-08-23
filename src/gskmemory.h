#ifndef __GSK_MEMORY_STREAMS_H_
#define __GSK_MEMORY_STREAMS_H_

#include "gskstream.h"

G_BEGIN_DECLS

/* --- streams which can be read from --- */
/* NOTE: buffer will be drained */
GskStream *gsk_memory_buffer_source_new (GskBuffer              *buffer);

GskStream *gsk_memory_slab_source_new   (gconstpointer           data,
					 guint                   data_len,
					 GDestroyNotify          destroy,
					 gpointer                destroy_data);
GskStream *gsk_memory_source_new_printf (const char             *format,
					 ...) G_GNUC_PRINTF(1,2);
GskStream *gsk_memory_source_new_vprintf(const char             *format,
                                         va_list                 args);
GskStream *gsk_memory_source_static_string (const char *str);
GskStream *gsk_memory_source_static_string_n (const char *str,
					      guint       length);

/* --- streams which can be written to --- */

/* callback to be invoked when 'shutdown_write' is called;
 * you may use any methods on buffer you want -- it will
 * be destructed immediately after this callback.
 */
typedef void (*GskMemoryBufferCallback) (GskBuffer              *buffer,
					 gpointer                data);
GskStream *gsk_memory_buffer_sink_new   (GskMemoryBufferCallback callback,
					 gpointer                data,
					 GDestroyNotify          destroy);


G_END_DECLS

#endif
