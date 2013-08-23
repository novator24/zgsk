#ifndef __GSK_STRING_MATCHER_H_
#define __GSK_STRING_MATCHER_H_

typedef struct _GskStringMatcherEntry GskStringMatcherEntry;
typedef struct _GskStringMatcher GskStringMatcher;

#include <glib.h>

typedef enum
{
  GSK_STRING_MATCHER_ENTRY_FLAGS__RESERVED
} GskStringMatcherEntryFlags;

struct _GskStringMatcherEntry
{
  GskStringMatcherEntryFlags flags;
  const char *str;
  gpointer entry_data;
};
  
typedef gboolean (*GskStringMatcherFunc)(guint match_index,
                                         guint match_start_offset,
					 gpointer entry_data,
					 gpointer match_data);

GskStringMatcher *gsk_string_matcher_new (guint n_entries,
                                          GskStringMatcherEntry *entries);

void              gsk_string_matcher_run (GskStringMatcher *matcher,
                                          const char       *haystack,
                                          GskStringMatcherFunc match_func,
					  gpointer          match_data);

void              gsk_string_matcher_free(GskStringMatcher *matcher);

#endif
