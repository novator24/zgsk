#include "../http/gskprefixtree.h"

int main()
{
  GskPrefixTree *pt;
  GSList *all;

  pt = NULL;

#define P(pref, old, new) \
  g_assert(gsk_prefix_tree_insert(&pt, pref, GUINT_TO_POINTER(new))==GUINT_TO_POINTER(old))
#define L(val, expected) \
  g_assert(gsk_prefix_tree_lookup(pt, val) == GUINT_TO_POINTER(expected))
#define POP(list, val) \
  G_STMT_START{ g_assert(list->data == GUINT_TO_POINTER(val)); \
                list = g_slist_remove (list, list->data); }G_STMT_END
  P("abc", 0, 1);
  P("abcdef", 0, 2);
  P("abcdefghi", 0, 3);
  P("def", 0, 5);
  L("a", 0);
  L("abcd", 1);
  L("abcdef", 2);
  L("abcdefg", 2);
  L("abcdefghij", 3);
  L("definition", 5);
  all = gsk_prefix_tree_lookup_all (pt, "abcdefghij");
  POP(all, 3); POP(all, 2); POP(all, 1);
  g_assert(all==NULL);
  P("abc", 1, 4);
  L("abcd", 4);
  P("aq", 0, 6);
  L("abcd", 4);
  L("abcdef", 2);
  L("abcdefg", 2);
  L("abcdefghij", 3);
  L("aquaman", 6);

  gsk_prefix_tree_destroy (pt);

  return 0;
}
