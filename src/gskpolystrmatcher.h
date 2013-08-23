
typedef struct _GskPolystrMatcher GskPolystrMatcher;

#include <glib.h>

GskPolystrMatcher *gsk_polystr_matcher_new (unsigned n_strs,
                                            char **strs);
gboolean            gsk_polystr_match      (GskPolystrMatcher *matcher,
                                            const char *str,
                                            unsigned *which_out,
                                            const char **start_out);


