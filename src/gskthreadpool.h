#ifndef __GSK_THREAD_POOL_H_
#define __GSK_THREAD_POOL_H_

#include "gskmainloop.h"

G_BEGIN_DECLS

typedef struct _GskThreadPool GskThreadPool;

typedef gpointer (*GskThreadPoolRunFunc)    (gpointer  run_data);
typedef void     (*GskThreadPoolResultFunc) (gpointer  run_data,
                                             gpointer  result_data);
typedef void     (*GskThreadPoolDestroyFunc)(gpointer  run_data,
                                             gpointer  result_data);

GskThreadPool *gsk_thread_pool_new     (GskMainLoop             *main_loop,
                                        guint                    max_threads);
void           gsk_thread_pool_push    (GskThreadPool           *pool,
                                        GskThreadPoolRunFunc     run,
			                GskThreadPoolResultFunc  handle_result,
                                        gpointer                 run_data,
			                GskThreadPoolDestroyFunc destroy);
void           gsk_thread_pool_destroy (GskThreadPool           *pool,
					GDestroyNotify           destroy,
					gpointer                 destroy_data);

G_END_DECLS

#endif
