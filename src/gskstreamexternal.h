#ifndef __GSK_STREAM_EXTERNAL_H_
#define __GSK_STREAM_EXTERNAL_H_

#include "gskstream.h"
#include "gskmainloop.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskStreamExternal GskStreamExternal;
typedef struct _GskStreamExternalClass GskStreamExternalClass;
/* --- type macros --- */
GType gsk_stream_external_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_STREAM_EXTERNAL			(gsk_stream_external_get_type ())
#define GSK_STREAM_EXTERNAL(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_STREAM_EXTERNAL, GskStreamExternal))
#define GSK_STREAM_EXTERNAL_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_STREAM_EXTERNAL, GskStreamExternalClass))
#define GSK_STREAM_EXTERNAL_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_STREAM_EXTERNAL, GskStreamExternalClass))
#define GSK_IS_STREAM_EXTERNAL(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_STREAM_EXTERNAL))
#define GSK_IS_STREAM_EXTERNAL_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_STREAM_EXTERNAL))

/* --- callbacks --- */
typedef void (*GskStreamExternalTerminated) (GskStreamExternal   *external,
					     GskMainLoopWaitInfo *wait_info,
					     gpointer             user_data);
typedef void (*GskStreamExternalStderr)     (GskStreamExternal   *external,
					     const char          *error_text,
					     gpointer             user_data);

/* --- structures --- */

struct _GskStreamExternalClass 
{
  GskStreamClass stream_class;
};
struct _GskStreamExternal 
{
  GskStream      stream;

  /* stdin for the process */
  int write_fd;
  GskSource *write_source;
  GskBuffer write_buffer;
  gsize max_write_buffer;

  /* stdout for the process */
  int read_fd;
  GskSource *read_source;
  GskBuffer read_buffer;
  gsize max_read_buffer;

  /* stderr for the process */
  int read_err_fd;
  GskSource *read_err_source;
  GskBuffer read_err_buffer;
  gsize max_err_line_length;

  /* process-termination notification */
  GskSource *process_source;
  glong pid;

  /* user-callback information */
  GskStreamExternalTerminated term_func;
  GskStreamExternalStderr err_func;
  gpointer user_data;
};

typedef enum
{
  GSK_STREAM_EXTERNAL_ALLOCATE_PSEUDOTTY = (1<<2),
  GSK_STREAM_EXTERNAL_SEARCH_PATH        = (1<<3)
} GskStreamExternalFlags;
 


/* --- prototypes --- */
GskStream *gsk_stream_external_new       (GskStreamExternalFlags      flags,
					  const char                 *stdin_filename,
					  const char                 *stdout_filename,
				          GskStreamExternalTerminated term_func,
					  GskStreamExternalStderr     err_func,
				          gpointer                    user_data,
				          const char                 *path,
				          const char                 *argv[],
				          const char                 *env[],
					  GError                    **error);

G_END_DECLS

#endif
