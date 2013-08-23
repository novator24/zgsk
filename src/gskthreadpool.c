#include "gskthreadpool.h"
#include "gskerrno.h"
#include "gskghelpers.h"
#include <unistd.h>
#include <errno.h>

typedef struct _TaskInfo TaskInfo;
typedef struct _ThreadInfo ThreadInfo;

struct _GskThreadPool
{

  /*< private >*/
  GskSource *wakeup_source;
  int wakeup_read_fd;
  int wakeup_write_fd;

  guint num_threads;
  guint max_threads;

  GCond *task_available;
  GMutex *task_available_mutex;

  GMutex *lock;
  GQueue *unstarted_tasks;
  GQueue *done_results;
  GQueue *idle_threads;

  gboolean destroy_pending;
  GDestroyNotify destroy_notify;
  gpointer destroy_data;
};

/* Per task information */
struct _TaskInfo
{
  GskThreadPoolRunFunc     run;
  GskThreadPoolResultFunc  handle_result;
  gpointer                 run_data;
  gpointer                 result_data;
  GskThreadPoolDestroyFunc destroy;
};

/* Per GThread information */
struct _ThreadInfo
{
  GskThreadPool           *pool;
  GThread                 *thread;
  GCond                   *cond;
  TaskInfo                *running_task;
  gboolean                 cancelled;
};

static void
destroy_now (GskThreadPool *pool)
{
  if (pool->wakeup_source != NULL)
    gsk_source_remove (pool->wakeup_source);

  g_mutex_free (pool->lock);
  g_queue_free (pool->done_results);
  g_queue_free (pool->idle_threads);
  if (pool->destroy_notify)
    (*pool->destroy_notify) (pool->destroy_data);
  g_free (pool);
}


static gboolean
handle_wakeup_fd_pinged (int fd, GIOCondition condition, gpointer data)
{
  GskThreadPool *pool = data;
  char buf[4096];
  int rv = read (pool->wakeup_read_fd, buf, sizeof (buf));
  TaskInfo *task_info;
  if (rv == 0)
    {
      /* end-of-file??? */
      g_message ("got eof from pipe");
      return TRUE;
    }
  else if (rv < 0)
    {
      int e = errno;
      if (!gsk_errno_is_ignorable (e))
	{
	  g_warning ("error reading wakeup pipe: %s", g_strerror (e));
	  return TRUE;
	}
    }
  g_mutex_lock (pool->lock);
  while ((task_info = g_queue_pop_head (pool->done_results)) != NULL)
    {
      g_mutex_unlock (pool->lock);
      (*task_info->handle_result) (task_info->run_data, task_info->result_data);
      if (task_info->destroy != NULL)
	(*task_info->destroy) (task_info->run_data, task_info->result_data);
      g_free (task_info);
      g_mutex_lock (pool->lock);
    }
  g_mutex_unlock (pool->lock);

  if (pool->destroy_pending && pool->num_threads == 0)
    return FALSE;

  return TRUE;
}

static void
wakefd_source_destroyed (gpointer data)
{
  GskThreadPool *pool = data;
  pool->wakeup_source = NULL;

  if (pool->destroy_pending && pool->num_threads == 0)
    destroy_now (pool);
}

/**
 * gsk_thread_pool_new:
 * @main_loop: the main loop that will manage the thread pool.
 * @max_threads: maximum number of threads that may be used by
 * this thread pool, or 0 to indicate that there is no limit.
 *
 * Make a new thread pool.  A thread pool is a way of recycling threads
 * to reduce thread construction costs.
 *
 * returns: the newly allocated thread pool.
 */
GskThreadPool *gsk_thread_pool_new     (GskMainLoop             *main_loop,
                                        guint                    max_threads)
{
  GskThreadPool *thread_pool;
  int pipe_fds[2];
  if (pipe (pipe_fds) < 0)
    {
      g_error ("error creating pipe: %s", g_strerror (errno));
    }

  gsk_fd_set_nonblocking (pipe_fds[0]);

  thread_pool = g_new (GskThreadPool, 1);
  thread_pool->wakeup_read_fd = pipe_fds[0];
  thread_pool->wakeup_write_fd = pipe_fds[1];
  thread_pool->wakeup_source = gsk_main_loop_add_io (main_loop,
						     pipe_fds[0],
						     G_IO_IN,
						     handle_wakeup_fd_pinged,
						     thread_pool,
						     wakefd_source_destroyed);
  thread_pool->num_threads = 0;
  thread_pool->max_threads = max_threads;
  thread_pool->destroy_pending = FALSE;
  thread_pool->lock = g_mutex_new ();
  thread_pool->idle_threads = g_queue_new ();
  thread_pool->unstarted_tasks = g_queue_new ();
  thread_pool->done_results = g_queue_new ();
  return thread_pool;
}

static void
write_byte (int fd)
{
  char zero = 0;
  write (fd, &zero, 1);
}

static gpointer
the_thread_func (gpointer data)
{
  ThreadInfo *thread_info = data;
  GskThreadPool *pool = thread_info->pool;
  while (thread_info->running_task
      && !thread_info->cancelled
      && !pool->destroy_pending)
    {
      TaskInfo *task = thread_info->running_task;
      task->result_data = (*task->run) (task->run_data);

      g_mutex_lock (pool->lock);
      g_queue_push_tail (pool->done_results, task);
      write_byte (pool->wakeup_write_fd);
      thread_info->running_task = g_queue_pop_head (pool->unstarted_tasks);
      if (thread_info->running_task == NULL)
	{
	  g_queue_push_tail (pool->idle_threads, thread_info);
	  while (!pool->destroy_pending
             && !thread_info->cancelled
             && thread_info->running_task == NULL)
	    {
	      g_cond_wait (thread_info->cond, pool->lock);
	    }
	}
      g_mutex_unlock (pool->lock);
    }

  g_mutex_lock (pool->lock);
  --pool->num_threads;
  g_mutex_unlock (pool->lock);

  write_byte (pool->wakeup_write_fd);

  g_cond_free (thread_info->cond);
  g_free (thread_info);

  return NULL;
}

/**
 * gsk_thread_pool_push:
 * @pool: the pool to add the new task to.
 * @run: function to invoke in the other thread.
 * @handle_result: function to invoke in the main-loop's thread.
 * It is invoked with both @run_data and the return value from @run.
 * @run_data: data to pass to both @run and @handle_result and @destroy.
 * @destroy: function to be invoked once everything else is done,
 * with both @run_data and the return value from @run.
 *
 * Add a new task for the thread-pool.
 *
 * The @run function should be the slow function that must
 * be run in a background thread.
 *
 * The @handle_result function will be called in the current
 * thread (which must be the same as the thread of the main-loop
 * that was used to construct this pool) with the return
 * value of @run.
 *
 * The @destroy function will be invoked in the main thread,
 * after @run and @handle_result are done.
 */
void
gsk_thread_pool_push   (GskThreadPool           *pool,
			GskThreadPoolRunFunc     run,
			GskThreadPoolResultFunc  handle_result,
			gpointer                 run_data,
			GskThreadPoolDestroyFunc destroy)
{
  TaskInfo *info = g_new (TaskInfo, 1);
  ThreadInfo *thread_info;
  g_return_if_fail (pool->destroy_pending == FALSE);
  info->run = run;
  info->handle_result = handle_result;
  info->run_data = run_data;
  info->destroy = destroy;

  g_mutex_lock (pool->lock);
  thread_info = g_queue_pop_head (pool->idle_threads);

  if (thread_info != NULL)
    {
      thread_info->running_task = info;
      g_cond_signal (thread_info->cond);
    }
  else if (pool->max_threads == 0 || pool->num_threads < pool->max_threads)
    {
      GError *error = NULL;
      thread_info = g_new (ThreadInfo, 1);
      thread_info->pool = pool;
      thread_info->cond = g_cond_new ();
      thread_info->running_task = info;
      thread_info->cancelled = FALSE;
      thread_info->thread = g_thread_create (the_thread_func, thread_info, TRUE, &error);
      if (thread_info->thread == NULL)
	{
	  /* uh, destroy thread_info and print a warning. */
	  g_message ("error creating thread: %s", error->message);
	  g_cond_free (thread_info->cond);
	  g_free (thread_info);
	  thread_info = NULL;
	}
      else
	pool->num_threads++;
    }
  if (thread_info == NULL)
    g_queue_push_tail (pool->unstarted_tasks, info);
  g_mutex_unlock (pool->lock);
}

/**
 * gsk_thread_pool_destroy:
 * @pool: the pool to destroy.
 * @destroy: function to invoke when the thread-pool is really done.
 * @destroy_data: data to pass to @destroy when all the threads in
 * the thread pool as gone.
 *
 * Destroy a thread-pool.
 * This may take some time,
 * so you may register a handler that will be 
 * called from the main thread once the thread-pool
 * is destructed.  (The memory is not yet deallocated though,
 * so that hash-tables keyed off the thread-pool
 * will have no race condition)
 */
void
gsk_thread_pool_destroy (GskThreadPool           *pool,
			 GDestroyNotify           destroy,
			 gpointer                 destroy_data)
{
  ThreadInfo *info;
  gboolean do_destroy;
  g_return_if_fail (pool->destroy_pending == FALSE);
  pool->destroy_pending = TRUE;
  pool->destroy_notify = destroy;
  pool->destroy_data = destroy_data;
  g_mutex_lock (pool->lock);
  while ((info = g_queue_pop_head (pool->idle_threads)) != NULL)
    {
      /* Destroy the idle thread. */
      info->cancelled = TRUE;
      g_cond_signal (info->cond);
    }
  do_destroy = pool->num_threads == 0;
  g_mutex_unlock (pool->lock);

  if (do_destroy)
    destroy_now (pool);
}
