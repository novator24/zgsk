/* Insert header here. */
#ifndef __GSK_MAIN_LOOP_SIGIO_H_
#define __GSK_MAIN_LOOP_SIGIO_H_

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskMainLoopSigio GskMainLoopSigio;
typedef struct _GskMainLoopSigioClass GskMainLoopSigioClass;
/* --- type macros --- */
GType gsk_main_loop_sigio_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_MAIN_LOOP_SIGIO			(gsk_main_loop_sigio_get_type ())
#define GSK_MAIN_LOOP_SIGIO(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_MAIN_LOOP_SIGIO, GskMainLoopSigio))
#define GSK_MAIN_LOOP_SIGIO_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_MAIN_LOOP_SIGIO, GskMainLoopSigioClass))
#define GSK_MAIN_LOOP_SIGIO_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_MAIN_LOOP_SIGIO, GskMainLoopSigioClass))
#define GSK_IS_MAIN_LOOP_SIGIO(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_MAIN_LOOP_SIGIO))
#define GSK_IS_MAIN_LOOP_SIGIO_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_MAIN_LOOP_SIGIO))

/* --- structures --- */
struct _GskMainLoopSigioClass 
{
  GskMainLoopClass main_loop_class;
};
struct _GskMainLoopSigio 
{
  GskMainLoop      main_loop;
};
/* --- prototypes --- */
G_END_DECLS

#endif
