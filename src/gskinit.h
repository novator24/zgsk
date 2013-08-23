#ifndef __GSK_INIT_H_
#define __GSK_INIT_H_

#include <glib.h>

G_BEGIN_DECLS

typedef struct _GskInitInfo GskInitInfo;
struct _GskInitInfo
{
  char *prgname;
  guint needs_threads : 1;
};

GskInitInfo *gsk_init_info_new  (void);
void         gsk_init_info_free (GskInitInfo *info);
void         gsk_init           (int         *argc,
				 char      ***argv,
				 GskInitInfo *info);
void gsk_init_without_threads   (int         *argc,
				 char      ***argv);

/* note: we *always* permit your programs to fork(),
   see gskfork.h for caveats. */

#define gsk_init_get_support_threads()	((gsk_init_flags & _GSK_INIT_SUPPORT_THREADS) == _GSK_INIT_SUPPORT_THREADS)

#ifndef GSK_DISABLE_DEPRECATED
void gsk_init_info_get_defaults (GskInitInfo *info);
#endif

/* --- implementation details --- */
void gsk_init_info_parse_args   (GskInitInfo *in_out,
				 int         *argc,
				 char      ***argv);
void gsk_init_raw               (GskInitInfo *info);

typedef enum
{
  _GSK_INIT_SUPPORT_THREADS = (1<<0)
} _GskInitFlags;
extern _GskInitFlags gsk_init_flags;

extern gpointer gsk_main_thread;

#define GSK_IS_MAIN_THREAD()		(gsk_main_thread == NULL || (g_thread_self () == gsk_main_thread))

G_END_DECLS

#endif
