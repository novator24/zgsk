#include "gskprefixtree.h"
#include <string.h>

static inline GskPrefixTree *
tree_alloc (const char *str)
{
  GskPrefixTree *rv = g_new (GskPrefixTree, 1);
  rv->prefix = g_strdup (str);
  rv->next_sibling = rv->children = NULL;
  rv->has_data = FALSE;
  return rv;
}

static inline GskPrefixTree *
tree_alloc_n (const char *str, guint len)
{
  GskPrefixTree *rv = g_new (GskPrefixTree, 1);
  rv->prefix = g_strndup (str, len);
  rv->next_sibling = rv->children = NULL;
  rv->has_data = FALSE;
  return rv;
}
static inline void
tree_set_prefix (GskPrefixTree *tree,
                  const char *prefix)
{
  char *dup = g_strdup (prefix);
  g_free (tree->prefix);
  tree->prefix = dup;
}

gpointer gsk_prefix_tree_insert       (GskPrefixTree   **tree,
                                       const char       *prefix,
                                       gpointer          data)
{
  g_return_val_if_fail (prefix[0] != 0, NULL);

  /* look for a node that shares some prefix in common with us */
  while (*tree)
    {
      if (prefix[0] == (*tree)->prefix[0])
        {
          const char *tp_at = (*tree)->prefix;
          const char *p_at = prefix;
          while (*p_at != 0 && *p_at == *tp_at)
            {
              p_at++;
              tp_at++;
            }
          if (*p_at == 0 && *tp_at == 0)
            {
              /* *tree is an exact match */
              gpointer rv = (*tree)->has_data ? (*tree)->data : NULL;
              (*tree)->has_data = TRUE;
              (*tree)->data = data;
              return rv;
            }

          if (*tp_at == 0)      /* p_at not empty */
            {
              tree = &((*tree)->children);
              prefix = p_at;
              continue;
            }

          if (*p_at == 0)
            {
              /* pattern is too short for tree: must restructure tree:
                  
                  if tree is 'prevsib's' pointer, and *tree is the crrent node,
                  then
                    
                     prevsib                         prevsib
                       |                                |
                       v                                v
                     *tree -> child        becomes     new  -> *tree  -> child
                       |                                |
                       v                                v
                     nextsib                         nextsib */
              GskPrefixTree *new = tree_alloc_n (prefix, p_at - prefix);
              new->next_sibling = (*tree)->next_sibling;
              (*tree)->next_sibling = NULL;
              new->children = (*tree);
              tree_set_prefix (new->children, tp_at);
              *tree = new;
              new->has_data = TRUE;
              new->data = data;
              return NULL;
            }

          /* else, a mismatch.  we must make a new family of trees.

                  prevsib                   prevsib
                     |                         |
                     v                         v
                   *tree       becomes      common -> *tree(end)
                     |                         |         |
                     v                         v         v
                   nextsib                   nextsib    new */
          {
            GskPrefixTree *common = tree_alloc_n (prefix, p_at - prefix);
            GskPrefixTree *cur = *tree;
            common->next_sibling = cur->next_sibling;
            common->children = cur;
            *tree = common;
            cur->next_sibling = NULL;
            tree = &(cur->next_sibling);
            tree_set_prefix (cur, tp_at);
            prefix += p_at - prefix;

            /* prefix not empty */
          }
        }
      else
        {
          tree = &((*tree)->next_sibling);
        }
    }

  /* create a new node with the remaining characters is in */
  *tree = tree_alloc (prefix);
  (*tree)->has_data = TRUE;
  (*tree)->data = data;
  return NULL;
}


gpointer gsk_prefix_tree_lookup       (GskPrefixTree    *tree,
                                       const char       *str)
{
  gpointer found = NULL;
  GskPrefixTree *at = tree;

  while (*str && at)
    {
      for (         ; at; at = at->next_sibling)
        if (g_str_has_prefix (str, at->prefix))
          {
            str += strlen (at->prefix);
            if (at->has_data)
              found = at->data;
            at = at->children;
            break;
          }
    }
  return found;
}

gpointer gsk_prefix_tree_lookup_exact (GskPrefixTree    *tree,
                                       const char       *str)
{
  gpointer found = NULL;
  GskPrefixTree *at = tree;

  while (*str && at)
    {
      for (         ; at; at = at->next_sibling)
        if (g_str_has_prefix (str, at->prefix))
          {
            str += strlen (at->prefix);
            if (at->has_data)
              found = at->data;
            at = at->children;
            break;
          }
    }
  return *str ? NULL : found;
}

GSList  *gsk_prefix_tree_lookup_all   (GskPrefixTree    *tree,
                                       const char       *str)
{
  GSList *rv = NULL;
  GskPrefixTree *at = tree;

  while (*str && at)
    {
      for (         ; at; at = at->next_sibling)
        if (g_str_has_prefix (str, at->prefix))
          {
            str += strlen (at->prefix);
            if (at->has_data)
              rv = g_slist_prepend (rv, at->data);
            at = at->children;
            break;
          }
    }
  return rv;
}
// good idea, but we don't need it.
//gpointer gsk_prefix_tree_remove       (GskPrefixTree    *tree,
//                                       const char        *prefix)
//{
//  ...
//}
//
void     gsk_prefix_tree_foreach      (GskPrefixTree    *tree,
                                       GFunc              func,
                                       gpointer           func_data)
{
  while (tree)
    {
      if (tree->has_data)
        func (tree->data, func_data);
      gsk_prefix_tree_foreach (tree->children, func, func_data);
      tree = tree->next_sibling;
    }
}
void     gsk_prefix_tree_destroy      (GskPrefixTree    *tree)
{
  while (tree)
    {
      GskPrefixTree *next = tree->next_sibling;
      g_free (tree->prefix);
      gsk_prefix_tree_destroy (tree->children);
      tree = next;
    }
}
