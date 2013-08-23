#ifndef __GSK_TYPES_H_
#define __GSK_TYPES_H_

#include <glib-object.h>

G_BEGIN_DECLS

GType gsk_fd_get_type () G_GNUC_CONST;
GType gsk_param_fd_get_type () G_GNUC_CONST;
#define GSK_TYPE_FD		(gsk_fd_get_type())
#define GSK_TYPE_PARAM_FD	(gsk_param_fd_get_type())

GParamSpec *gsk_param_spec_fd (const char *name,
			       const char *nick,
			       const char *blurb,
			       GParamFlags flags);

G_END_DECLS

#endif
