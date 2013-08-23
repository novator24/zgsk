/*
    GSK - a library to write servers

    Copyright (C) 2001 Dave Benson

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA

    Contact:
        daveb@ffem.org <Dave Benson>
*/

#ifndef __GSK_MAIN_LOOP_H_
#define __GSK_MAIN_LOOP_H_

#include <glib-object.h>

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskMainLoopChange GskMainLoopChange;
typedef struct _GskMainLoopEvent GskMainLoopEvent;
typedef struct _GskMainLoopClass GskMainLoopClass;
typedef struct _GskMainLoop GskMainLoop;
typedef struct _GskMainLoopWaitInfo GskMainLoopWaitInfo;
typedef struct _GskSource GskSource;
typedef struct _GskMainLoopContextList GskMainLoopContextList;

/* --- type macros --- */
GType gsk_main_loop_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MAIN_LOOP		(gsk_main_loop_get_type ())
#define GSK_MAIN_LOOP(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_MAIN_LOOP, GskMainLoop))
#define GSK_MAIN_LOOP_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_MAIN_LOOP, GskMainLoopClass))
#define GSK_MAIN_LOOP_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_MAIN_LOOP, GskMainLoopClass))
#define GSK_IS_MAIN_LOOP(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_MAIN_LOOP))
#define GSK_IS_MAIN_LOOP_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_MAIN_LOOP))

GType gsk_main_loop_wait_info_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MAIN_LOOP_WAIT_INFO    (gsk_main_loop_wait_info_get_type ())
struct _GskMainLoopWaitInfo
{
  int               pid; 
  gboolean          exited;         /* exit(2) or killed by signal? */
  union {
    int             signal;         /* !exited */
    int             exit_status;    /*  exited */
  } d;           
  gboolean          dumped_core;
};
/* HINT: for diagnosing processes that die by unexpected signals,
   use g_strsignal() to convert signal numbers to strings */

typedef enum
{
  GSK_MAIN_LOOP_EVENT_IO,
  GSK_MAIN_LOOP_EVENT_SIGNAL,
  GSK_MAIN_LOOP_EVENT_PROCESS
} GskMainLoopEventType;

struct _GskMainLoopChange
{
  GskMainLoopEventType type;
  union
  {
    struct {
      guint number;
      gboolean add;
    } signal;
    struct {
      guint fd;
      GIOCondition old_events;
      GIOCondition events;
    } io;
    struct {
      gint pid;
      gboolean add;
      gboolean did_exit;
    } process;
  } data;
};

struct _GskMainLoopEvent
{
  GskMainLoopEventType type;
  union
  {
    guint signal;
    struct {
      guint fd;
      GIOCondition events;
    } io;
    GskMainLoopWaitInfo process_wait_info;
  } data;
};

/* --- structures --- */
struct _GskMainLoopClass 
{
  GObjectClass object_class;
  gboolean (*setup)  (GskMainLoop       *main_loop);
  void     (*change) (GskMainLoop       *main_loop,
                      GskMainLoopChange *change);
  guint    (*poll)   (GskMainLoop       *main_loop,
                      guint              max_events_out,
                      GskMainLoopEvent  *events,
                      gint               timeout);
};

struct _GskMainLoop 
{
  GObject      object;

  /* idle functions */
  GskSource     *first_idle;
  GskSource     *last_idle;

  /* timers */
  GskSource     *timers;

  /* i/o handlers by file-descriptor */
  GPtrArray     *read_sources;
  GPtrArray     *write_sources;

  /* lists of sources for each signal */
  GPtrArray     *signal_source_lists;

  /* process-termination handlers (int => (GSList<GskSource>)) */
  GHashTable    *process_source_lists;
  GHashTable    *alive_pids;

  /* the source which is currently running */
  GskSource     *running_source;

  GTimeVal       current_time;

  /* optional thread pool support */
  guint          max_workers;
  gpointer       thread_pool;

  guint          num_sources;
  guint          is_setup : 1;
  guint          is_running : 1;		/*< private >*/
  guint          quit : 1;			/*< public >*/

  gint		 exit_status;

  GskMainLoopEvent *event_array_cache;
  unsigned       max_events;

  /* a list of GMainContext's */
  GskMainLoopContextList *first_context;
  GskMainLoopContextList *last_context;
};

/* --- Callback function typedefs. --- */

/* callback for child-process termination */
typedef void     (*GskMainLoopWaitPidFunc)(GskMainLoopWaitInfo  *info,
                                           gpointer              user_data);

/* callback for an "idle" function -- it runs after all events
 * have been processed.
 */
typedef gboolean (*GskMainLoopIdleFunc)   (gpointer              user_data);

/* callback for receiving a signal */
typedef gboolean (*GskMainLoopSignalFunc) (int                   sig_no,
                                           gpointer              user_data);

/* callback for a period */
typedef gboolean (*GskMainLoopTimeoutFunc)(gpointer              user_data);

/* callback for input or output on a file descriptor */
typedef gboolean (*GskMainLoopIOFunc)     (int                   fd,
                                           GIOCondition          condition,
                                           gpointer              user_data);


/* --- prototypes --- */
/* Create a main loop with selected options. */
typedef enum
{
  GSK_MAIN_LOOP_NEEDS_THREADS = (1 << 0)
} GskMainLoopCreateFlags;

GskMainLoop     *gsk_main_loop_new       (GskMainLoopCreateFlags create_flags);

/* return the per-thread main-loop */
GskMainLoop     *gsk_main_loop_default      (void) G_GNUC_CONST;


/* TIMEOUT is the maximum number of milliseconds to wait,
 * or pass in -1 to block forever.
 */
guint            gsk_main_loop_run          (GskMainLoop       *main_loop,
                                             gint               timeout,
                                             guint             *t_waited_out);
GskSource       *gsk_main_loop_add_idle     (GskMainLoop       *main_loop,
                                             GskMainLoopIdleFunc source_func,
                                             gpointer           user_data,
                                             GDestroyNotify     destroy);
GskSource       *gsk_main_loop_add_signal   (GskMainLoop       *main_loop,
                                             int                signal_number,
                                             GskMainLoopSignalFunc signal_func,
                                             gpointer           user_data,
                                             GDestroyNotify     destroy);
GskSource       *gsk_main_loop_add_waitpid  (GskMainLoop       *main_loop,
                                             int                process_id,
                                           GskMainLoopWaitPidFunc waitpid_func,
                                             gpointer           user_data,
                                             GDestroyNotify     destroy);
GskSource       *gsk_main_loop_add_io       (GskMainLoop       *main_loop,
                                             int                fd,
                                             guint              events,
                                             GskMainLoopIOFunc  io_func,
                                             gpointer           user_data,
                                             GDestroyNotify     destroy);
void             gsk_source_adjust_io       (GskSource         *source,
                                             guint              events);
void             gsk_source_add_io_events   (GskSource         *source,
                                             guint              events);
void             gsk_source_remove_io_events(GskSource         *source,
                                             guint              events);
#define gsk_main_loop_add_timer gsk_main_loop_add_timer64
#define gsk_source_adjust_timer gsk_source_adjust_timer64
GskSource       *gsk_main_loop_add_timer    (GskMainLoop       *main_loop,
                                             GskMainLoopTimeoutFunc timer_func,
                                             gpointer           timer_data,
                                             GDestroyNotify     timer_destroy,
                                             gint64             millis_expire,
                                             gint64             milli_period);
GskSource       *gsk_main_loop_add_timer_absolute
                                            (GskMainLoop       *main_loop,
                                             GskMainLoopTimeoutFunc timer_func,
                                             gpointer           timer_data,
                                             GDestroyNotify     timer_destroy,
                                             int                unixtime,
                                             int                unixtime_micro);
void             gsk_source_adjust_timer    (GskSource         *timer_source,
                                             gint64             millis_expire,
                                             gint64             milli_period);
void             gsk_source_remove          (GskSource         *source);
void             gsk_main_loop_add_context  (GskMainLoop       *main_loop,
					     GMainContext      *context);
void             gsk_main_loop_quit         (GskMainLoop       *main_loop);


gboolean         gsk_main_loop_should_continue
                                            (GskMainLoop       *main_loop);

GskMainLoop *gsk_source_peek_main_loop (GskSource *source);

/*< protected >*/
void gsk_main_loop_destroy_all_sources (GskMainLoop *main_loop);

/* miscellaneous: should probably be private. */
gboolean gsk_main_loop_do_waitpid (int                  pid,
                                   GskMainLoopWaitInfo *wait_info);

/*< private >*/
void _gsk_main_loop_init ();
void _gsk_main_loop_fork_notify ();

/* for binary-compatibility, the library defines gsk_main_loop_add_timer()
   with native-int timeouts.  but people compiling with the latest version
   will use gsk_main_loop_add_timer64.  eventually, we can get rid of this hack,
   and just define gsk_main_loop_add_timer() as the 64-bit function. */
#undef gsk_main_loop_add_timer
#undef gsk_source_adjust_timer
GskSource *gsk_main_loop_add_timer (GskMainLoop*,GskMainLoopTimeoutFunc,gpointer,GDestroyNotify,int,int);
void gsk_source_adjust_timer (GskSource*,int,int);
#define gsk_main_loop_add_timer gsk_main_loop_add_timer64
#define gsk_source_adjust_timer gsk_source_adjust_timer64

G_END_DECLS

#endif
