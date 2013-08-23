#ifndef __GSK_MAIN_LOOP_DEV_POLL_H_
#define __GSK_MAIN_LOOP_DEV_POLL_H_

#include "gskmainlooppollbase.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskMainLoopDevPoll GskMainLoopDevPoll;
typedef struct _GskMainLoopDevPollClass GskMainLoopDevPollClass;

/* --- type macros --- */
GType gsk_main_loop_dev_poll_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MAIN_LOOP_DEV_POLL			(gsk_main_loop_dev_poll_get_type ())
#define GSK_MAIN_LOOP_DEV_POLL(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_MAIN_LOOP_DEV_POLL, GskMainLoopDevPoll))
#define GSK_MAIN_LOOP_DEV_POLL_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_MAIN_LOOP_DEV_POLL, GskMainLoopDevPollClass))
#define GSK_MAIN_LOOP_DEV_POLL_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_MAIN_LOOP_DEV_POLL, GskMainLoopDevPollClass))
#define GSK_IS_MAIN_LOOP_DEV_POLL(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_MAIN_LOOP_DEV_POLL))
#define GSK_IS_MAIN_LOOP_DEV_POLL_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_MAIN_LOOP_DEV_POLL))

/* --- structures --- */
struct _GskMainLoopDevPollClass 
{
  GskMainLoopPollBaseClass	main_loop_class;
};
struct _GskMainLoopDevPoll 
{
  GskMainLoopPollBase		main_loop;
  int                   	dev_poll_fd;
};


G_END_DECLS

#endif
