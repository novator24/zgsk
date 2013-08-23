#ifndef __GSK_PREFIX_TREE_H_
#define __GSK_PREFIX_TREE_H_

#include <glib.h>

G_BEGIN_DECLS

/* Private class.
 *
 * This stores a tree of prefixes,
 * for efficient prefix lookup.
 */

typedef struct _GskPrefixTree GskPrefixTree;
struct _GskPrefixTree
{
  char *prefix;
  GskPrefixTree *next_sibling;
  GskPrefixTree *children;

  gboolean has_data;
  gpointer data;
};

/* note: a NULL GskPrefixTree* is an empty tree. */

/* returns the pointer that we replaced, or NULL */
gpointer gsk_prefix_tree_insert       (GskPrefixTree   **tree,
                                       const char       *prefix,
                                       gpointer          data);

gpointer gsk_prefix_tree_lookup       (GskPrefixTree    *tree,
                                       const char       *str);
gpointer gsk_prefix_tree_lookup_exact (GskPrefixTree    *tree,
                                       const char       *str);
GSList  *gsk_prefix_tree_lookup_all   (GskPrefixTree    *tree,
                                       const char       *str);
gpointer gsk_prefix_tree_remove       (GskPrefixTree    *tree,
                                       const char        *prefix);
void     gsk_prefix_tree_foreach      (GskPrefixTree    *tree,
                                       GFunc              func,
                                       gpointer           func_data);
void     gsk_prefix_tree_destroy      (GskPrefixTree    *tree);


G_END_DECLS

#endif
