#ifndef __GSK_STREAM_CONCAT_H_
#define __GSK_STREAM_CONCAT_H_

#include "gskstream.h"

G_BEGIN_DECLS

GskStream *gsk_streams_concat_and_unref (GskStream *stream0,
                                         ...);
GskStream *gsk_streams_concat_v         (unsigned    n_streams,
                                         GskStream **streams);
G_END_DECLS

#endif
