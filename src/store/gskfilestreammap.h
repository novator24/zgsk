#ifndef __GSK_FILE_STREAM_MAP_H_
#define __GSK_FILE_STREAM_MAP_H_

/*
 *
 * GskFileStreamMap -- implementation of GskStreamMap that uses the
 * filesystem.
 *
 * Properties:
 *   directory   string   Directory to store files in.
 */

#include "gskstreammap.h"

G_BEGIN_DECLS

typedef GObjectClass             GskFileStreamMapClass;
typedef struct _GskFileStreamMap GskFileStreamMap;

GType gsk_file_stream_map_get_type (void) G_GNUC_CONST;

#define GSK_TYPE_FILE_STREAM_MAP (gsk_file_stream_map_get_type ())
#define GSK_FILE_STREAM_MAP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
			       GSK_TYPE_FILE_STREAM_MAP, \
			       GskFileStreamMap))
#define GSK_FILE_STREAM_MAP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
			    GSK_TYPE_FILE_STREAM_MAP, \
			    GskFileStreamMapClass))
#define GSK_FILE_STREAM_MAP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
			      GSK_TYPE_FILE_STREAM_MAP, \
			      GskFileStreamMapClass))
#define GSK_IS_FILE_STREAM_MAP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_FILE_STREAM_MAP))
#define GSK_IS_FILE_STREAM_MAP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_FILE_STREAM_MAP))

struct _GskFileStreamMap
{
  GObject object;

  char *directory;
};

GskFileStreamMap * gsk_file_stream_map_new (const char *directory);

G_END_DECLS

#endif
