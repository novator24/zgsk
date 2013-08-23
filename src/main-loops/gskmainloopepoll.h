#ifndef __GSK_MAIN_LOOP_EPOLL_H_
#define __GSK_MAIN_LOOP_EPOLL_H_

#include "gskmainlooppollbase.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskMainLoopEpoll GskMainLoopEpoll;
typedef struct _GskMainLoopEpollClass GskMainLoopEpollClass;

/* --- type macros --- */
GType gsk_main_loop_epoll_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MAIN_LOOP_EPOLL			(gsk_main_loop_epoll_get_type ())
#define GSK_MAIN_LOOP_EPOLL(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_MAIN_LOOP_EPOLL, GskMainLoopEpoll))
#define GSK_MAIN_LOOP_EPOLL_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_MAIN_LOOP_EPOLL, GskMainLoopEpollClass))
#define GSK_MAIN_LOOP_EPOLL_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_MAIN_LOOP_EPOLL, GskMainLoopEpollClass))
#define GSK_IS_MAIN_LOOP_EPOLL(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_MAIN_LOOP_EPOLL))
#define GSK_IS_MAIN_LOOP_EPOLL_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_MAIN_LOOP_EPOLL))

/* --- structures --- */
struct _GskMainLoopEpollClass 
{
  GskMainLoopPollBaseClass main_loop_poll_base_class;
};
struct _GskMainLoopEpoll 
{
  GskMainLoopPollBase      main_loop_poll_base;
  int                      fd;
  gpointer                 epoll_events;
};

/* --- prototypes --- */



G_END_DECLS

#endif
