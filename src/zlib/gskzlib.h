#ifndef __GSK_ZLIB_COMMON_H_
#define __GSK_ZLIB_COMMON_H_

#include "../gskerror.h"

G_BEGIN_DECLS

GskErrorCode gsk_zlib_error_to_gsk_error(gint zlib_error_rv);
const char * gsk_zlib_error_to_message  (gint zlib_error_rv);

G_END_DECLS

#endif
