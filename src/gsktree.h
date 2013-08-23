#ifndef __GSK_TREE_H_
#define __GSK_TREE_H_

#include <glib.h>

G_BEGIN_DECLS

typedef struct _GskTreeNode GskTreeNode;
typedef struct _GskTree GskTree;
GskTree      *gsk_tree_new             (GCompareFunc     compare);
GskTree      *gsk_tree_ref             (GskTree         *tree);
void          gsk_tree_unref           (GskTree         *tree);
GskTree      *gsk_tree_new_full        (GCompareDataFunc compare,
                                        gpointer         compare_data,
                                        GDestroyNotify   key_destroy_func,
                                        GDestroyNotify   value_destroy_func);
void          gsk_tree_insert          (GskTree         *tree,
                                        gpointer         key,
                                        gpointer         value);
void          gsk_tree_replace         (GskTree         *tree,
                                        gpointer         key,
                                        gpointer         value);
gpointer      gsk_tree_lookup          (GskTree         *tree,
				        gpointer         key);
void          gsk_tree_remove          (GskTree         *tree,
					gpointer         key);
guint         gsk_tree_n_nodes         (GskTree         *tree);
                                       

/* iteration.
 *
 *  note that insert/remove/replace are allowed on any
 *  node during any number of iterations.  _prev/_next will
 *  never return a removed node but they will return nodes that
 *  were inserted after the iteration began.
 *
 *  When a node is returned to you it has a use_count that is
 *  incremented.  Functions that take a GskTreeNode decrement
 *  that use count.  If you wish to abort iteration prematurely
 *  you should call gsk_tree_node_unvisit(); if you wish to
 *  come back to a certain point you might use gsk_tree_node_visit(),
 *  but that's rare.
 *
 *  Also note that we don't hold a reference to the tree!
 *  Hence make sure you gsk_tree_ref/unref around iteration
 *  if you don't own the tree.
 */
GskTreeNode  *gsk_tree_node_first      (GskTree         *tree);
GskTreeNode  *gsk_tree_node_last       (GskTree         *tree);
GskTreeNode  *gsk_tree_node_find       (GskTree         *tree,
					gpointer         search_key);
GskTreeNode  *gsk_tree_node_next       (GskTree         *tree,
                                        GskTreeNode     *node);
GskTreeNode  *gsk_tree_node_prev       (GskTree         *tree,
                                        GskTreeNode     *node);
gpointer      gsk_tree_node_peek_key   (GskTreeNode     *node);
gpointer      gsk_tree_node_peek_value (GskTreeNode     *node);
gboolean      gsk_tree_node_is_removed (GskTreeNode     *node);
void          gsk_tree_node_visit      (GskTree         *tree,
                                        GskTreeNode     *node);
void          gsk_tree_node_unvisit    (GskTree         *tree,
                                        GskTreeNode     *node);

gboolean      gsk_tree_validate        (GskTree         *tree);

G_END_DECLS

#endif
