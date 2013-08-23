#ifndef __GSK_STREAM_FD_H_
#define __GSK_STREAM_FD_H_

#include "gskstream.h"
#include "gsksocketaddresssymbolic.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskStreamFd GskStreamFd;
typedef struct _GskStreamFdClass GskStreamFdClass;

/* --- type macros --- */
GType gsk_stream_fd_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_STREAM_FD			(gsk_stream_fd_get_type ())
#define GSK_STREAM_FD(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_STREAM_FD, GskStreamFd))
#define GSK_STREAM_FD_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_STREAM_FD, GskStreamFdClass))
#define GSK_STREAM_FD_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_STREAM_FD, GskStreamFdClass))
#define GSK_IS_STREAM_FD(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_STREAM_FD))
#define GSK_IS_STREAM_FD_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_STREAM_FD))

#define GSK_STREAM_FD_GET_FD(stream)	(GSK_STREAM_FD (stream)->fd)

#define GSK_STREAM_FD_USE_GLIB_MAIN_LOOP	0

#if !GSK_STREAM_FD_USE_GLIB_MAIN_LOOP
#include "gskmainloop.h"
#endif

/* --- structures --- */
struct _GskStreamFdClass 
{
  GskStreamClass stream_class;
};
struct _GskStreamFd 
{
  GskStream      stream;

  /* read-only */
  guint is_pollable : 1;
  guint is_shutdownable : 1;
  guint is_resolving_name : 1;
  guint failed_name_resolution : 1;

  int fd;
  gushort post_connecting_events;
#if GSK_STREAM_FD_USE_GLIB_MAIN_LOOP
  GPollFD poll_fd;
  GSource *source;
#else
  GskSource *source;
#endif
};

/* --- prototypes --- */
typedef enum
{
  GSK_STREAM_FD_IS_READABLE     = (1<<0),
  GSK_STREAM_FD_IS_WRITABLE     = (1<<1),
  GSK_STREAM_FD_IS_READWRITE    = GSK_STREAM_FD_IS_READABLE
                                | GSK_STREAM_FD_IS_WRITABLE,
  GSK_STREAM_FD_IS_POLLABLE     = (1<<2),
  GSK_STREAM_FD_IS_SHUTDOWNABLE = (1<<3),
  GSK_STREAM_FD_FOR_NEW_SOCKET  = GSK_STREAM_FD_IS_READWRITE
                                | GSK_STREAM_FD_IS_POLLABLE
			        | GSK_STREAM_FD_IS_SHUTDOWNABLE
} GskStreamFdFlags;

GskStream   *gsk_stream_fd_new             (gint            fd,
                                            GskStreamFdFlags flags);
GskStreamFdFlags gsk_stream_fd_flags_guess (gint            fd);
GskStream   *gsk_stream_fd_new_auto        (gint            fd);


GskStream   *gsk_stream_fd_new_connecting  (gint            fd);
GskStream   *gsk_stream_fd_new_from_symbolic_address (GskSocketAddressSymbolic *symbolic,
                                                      GError                  **error);

/* reading/writing from/to a file */
GskStream   *gsk_stream_fd_new_read_file   (const char     *filename,
					    GError        **error);
GskStream   *gsk_stream_fd_new_write_file  (const char     *filename,
					    gboolean        may_create,
					    gboolean        should_truncate,
					    GError        **error);
GskStream   *gsk_stream_fd_new_create_file (const char     *filename,
					    gboolean        may_exist,
					    GError        **error);


/*< private >*/
GskStream * gsk_stream_fd_new_open (const char     *filename,
			            guint           open_flags,
			            guint           permission,
			            GError        **error);

gboolean    gsk_stream_fd_pipe     (GskStream     **read_side_out,
                                    GskStream     **write_side_out,
			            GError        **error);

gboolean    gsk_stream_fd_duplex_pipe (GskStream     **side_a_out,
                                       GskStream     **side_b_out,
			               GError        **error);
gboolean    gsk_stream_fd_duplex_pipe_fd (GskStream     **side_a_out,
                                          int            *side_b_fd_out,
			                  GError        **error);

G_END_DECLS

#endif
