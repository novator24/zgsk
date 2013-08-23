#include <string.h>
#include "../gskrbtreemacros.h"
#include "../gskmempool.h"

typedef struct _TreeNode TreeNode;
struct _TreeNode
{
  gboolean red;
  TreeNode *parent;
  TreeNode *child_left, *child_right;
  guint value;
};
#define COMPARE_INT_WITH_TREE_NODE(a,b,rv) rv = ((a<b->value) ? -1 : (a>b->value) ? 1 : 0)
#define COMPARE_TREE_NODES(a,b,rv)         COMPARE_INT_WITH_TREE_NODE(a->value,b,rv)
#define TREE_NODE_IS_RED(node)             ((node)->red)
#define TREE_NODE_SET_IS_RED(node, val)   ((node)->red = (val))

#define TREE(ptop)  (*(ptop)),TreeNode*,TREE_NODE_IS_RED,TREE_NODE_SET_IS_RED, \
                    parent, child_left, child_right, COMPARE_TREE_NODES

#define DUMP_ALL                0

#if DUMP_ALL
#define DUMP_MESSAGE(args) g_message args
#define DUMP_RBCTREE(t) dump_rbctree(t,0)
#else
#define DUMP_MESSAGE(args)
#define DUMP_RBCTREE(t)
#endif

GskMemPoolFixed tree_node_pool = GSK_MEM_POOL_FIXED_STATIC_INIT(sizeof(TreeNode));

static gboolean
add_tree (TreeNode **ptop,
          guint      v)
{
  TreeNode *node = gsk_mem_pool_fixed_alloc (&tree_node_pool);
  TreeNode *extant;
  node->value = v;
  GSK_RBTREE_INSERT (TREE(ptop), node, extant);
  if (extant == NULL)
    return FALSE;
  gsk_mem_pool_fixed_free (&tree_node_pool, node);
  return TRUE;
}

static gboolean
test_tree (TreeNode **ptop,
           guint      v)
{
  TreeNode *found;
  GSK_RBTREE_LOOKUP_COMPARATOR (TREE(ptop), v, COMPARE_INT_WITH_TREE_NODE, found);
  return found != NULL;
}

static gboolean
del_tree (TreeNode **ptop,
          guint      v)
{
  TreeNode *found;
  GSK_RBTREE_LOOKUP_COMPARATOR (TREE(ptop), v, COMPARE_INT_WITH_TREE_NODE, found);
  if (found == NULL)
    return FALSE;
  GSK_RBTREE_REMOVE (TREE(ptop), found);
  gsk_mem_pool_fixed_free (&tree_node_pool, found);
  return TRUE;
}

/* --- RBC Tree Test --- */
/* Add all integers in the range [0, N) to
   the rbc tree, then iterate the tree
   and compute the index for each node. */
typedef struct _RBCTreeNode RBCTreeNode;
struct _RBCTreeNode
{
  guint tn_count;
  RBCTreeNode *tn_left, *tn_right, *tn_parent;
  gboolean red;
  guint value;
};

#define RBCTREENODE_GET_COUNT(n) n->tn_count
#define RBCTREENODE_SET_COUNT(n,v) n->tn_count = v
#define RBCTREE(tp)     (tp), RBCTreeNode*, \
                        TREE_NODE_IS_RED, TREE_NODE_SET_IS_RED, \
                        RBCTREENODE_GET_COUNT, RBCTREENODE_SET_COUNT, \
                        tn_parent, tn_left, tn_right, COMPARE_TREE_NODES

static void
validate_counts (RBCTreeNode *xxx)
{
  if (xxx == NULL)
    return;
  g_assert (xxx->tn_count == (xxx->tn_left ? xxx->tn_left->tn_count : 0)
                           + (xxx->tn_right ? xxx->tn_right->tn_count : 0)
                           + 1);
  validate_counts (xxx->tn_left);
  validate_counts (xxx->tn_right);
}

#if 0
static guint
rbc_tree_node_height (RBCTreeNode *at)
{
  guint lheight = (at->tn_left) ? rbc_tree_node_height (at->tn_left) + 1 : 1;
  guint rheight = (at->tn_right) ? rbc_tree_node_height (at->tn_right) + 1 : 1;
  return MAX (lheight, rheight);
}
#endif

#if DUMP_ALL
static void
dump_rbctree (RBCTreeNode *top, guint indent)
{
  if (top == NULL)
    return;
  dump_rbctree (top->tn_left, indent + 4);
  g_print ("%*s%u (%u) (%s)\n", indent, "", top->value, top->tn_count, top->red ? "red" : "black");
  dump_rbctree (top->tn_right, indent + 4);
}
#endif

static void
test_random_rbcint_tree  (guint n, gboolean do_validate_counts)
{
  RBCTreeNode *arr = g_new (RBCTreeNode, n);
  RBCTreeNode *top = NULL;
  guint i;
  guint mod, m;
  guint *mod_shuffle;

  /* generate a random permutation
     (from http://www.techuser.net/randpermgen.html) */
  for (i = 0; i < n; i++)
    arr[i].value = i;
  for (i = 0; i < n; i++)
    {
      guint r = g_random_int_range (i, n);
      guint tmp = arr[i].value;
      arr[i].value = arr[r].value;
      arr[r].value = tmp;
    }

  for (i = 0; i < n; i++)
    {
      RBCTreeNode *conflict;
      DUMP_MESSAGE (("inserting %u", arr[i].value));
      GSK_RBCTREE_INSERT (RBCTREE (top), arr + i, conflict);
      g_assert (conflict == NULL);
      DUMP_RBCTREE (top);
      if (do_validate_counts)
        validate_counts (top);
    }
  for (i = 0; i < n; i++)
    {
      guint v = arr[i].value;
      RBCTreeNode *n;
      guint tv;
      GSK_RBCTREE_LOOKUP_COMPARATOR (RBCTREE (top), v, COMPARE_INT_WITH_TREE_NODE, n);
      g_assert (n == arr + i);
      GSK_RBCTREE_GET_NODE_INDEX (RBCTREE (top), n, tv);
      //g_message ("node index for value %u, index %u is apparently %u (should == value %u)", v,i,tv,v);
      g_assert (v == tv);
    }

  /* remove the numbers of waves.
     pick a random modulus, then
     shuffle the numbers [0,modulus),
     then remove each set of numbers
     testing at each wave. */
  mod = g_random_int_range (1,11);
  mod_shuffle = g_new (guint, mod);
  for (i = 0; i < mod; i++)
    mod_shuffle[i] = i;
  for (i = 0; i < mod; i++)
    {
      guint r = g_random_int_range (i, mod);
      guint tmp = mod_shuffle[i];
      mod_shuffle[i] = mod_shuffle[r];
      mod_shuffle[r] = tmp;
    }
  for (m = 0; m < mod; m++)
    {
      guint m2;
      for (i = 0; i < n; i++)
        {
          guint v = arr[i].value;
          RBCTreeNode *n;
          guint mm = v % mod;
          guint k;
          for (k = 0; k < m; k++)
            if (mod_shuffle[k] == mm)
              break;
          if (k == m)
            {
              GSK_RBCTREE_LOOKUP_COMPARATOR (RBCTREE (top), v, COMPARE_INT_WITH_TREE_NODE, n);
              g_assert (n == arr + i);
            }
          if (mm == mod_shuffle[m])
            {
              DUMP_MESSAGE (("removing %u", arr[i].value));
              GSK_RBCTREE_REMOVE (RBCTREE (top), arr + i);
              DUMP_RBCTREE (top);
              if (do_validate_counts)
                validate_counts (top);
            }
        }
      for (m2 = m + 1; m2 < mod; m2++)
        {
          guint dmm = mod_shuffle[m2];
          guint v;
          guint pre_good_count = 0;
          for (v = m + 1; v < mod; v++)
            if (mod_shuffle[v] < dmm)
              pre_good_count++;
          for (v = dmm; v < n; v += mod)
            {
              /* the index of v should be ((v-dmm)/mod)*(mod-m-1) + pre_good_count */
              guint expected_index = (v / mod) * (mod - m - 1) + pre_good_count;
              guint actual_index;
              RBCTreeNode *n;
              GSK_RBCTREE_LOOKUP_COMPARATOR (RBCTREE (top), v, COMPARE_INT_WITH_TREE_NODE, n);
              g_assert (n->value == v);
              GSK_RBCTREE_GET_NODE_INDEX (RBCTREE (top), n, actual_index);
              g_assert (expected_index == actual_index);
            }
        }
    }
  g_assert (top == NULL);
  g_free (arr);
  g_free (mod_shuffle);
}

int main()
{
  TreeNode *tree = NULL;
  TreeNode *node;
  guint i;
  g_assert (!add_tree (&tree, 1));
  g_assert (!add_tree (&tree, 2));
  g_assert (!add_tree (&tree, 3));
  g_assert ( add_tree (&tree, 1));
  g_assert ( add_tree (&tree, 2));
  g_assert ( add_tree (&tree, 3));
  g_assert (!test_tree (&tree, 0));
  g_assert ( test_tree (&tree, 1));
  g_assert ( test_tree (&tree, 2));
  g_assert ( test_tree (&tree, 3));
  g_assert (!test_tree (&tree, 4));
  g_assert (!del_tree  (&tree, 0));
  g_assert ( del_tree  (&tree, 2));
  g_assert (!del_tree  (&tree, 4));
  g_assert (!test_tree (&tree, 0));
  g_assert ( test_tree (&tree, 1));
  g_assert (!test_tree (&tree, 2));
  g_assert ( test_tree (&tree, 3));
  g_assert (!test_tree (&tree, 4));
  g_assert ( add_tree (&tree, 1));
  g_assert (!add_tree (&tree, 2));
  g_assert ( add_tree (&tree, 3));
  g_assert ( del_tree  (&tree, 1));
  g_assert ( del_tree  (&tree, 2));
  g_assert ( del_tree  (&tree, 3));
  g_assert (tree == NULL);

  GSK_RBTREE_FIRST (TREE(&tree), node);
  g_assert (node == NULL);
  GSK_RBTREE_LAST (TREE(&tree), node);
  g_assert (node == NULL);

  /* Construct tree with odd numbers 1..999 inclusive */
  for (i = 1; i <= 999; i += 2)
    g_assert (!add_tree (&tree, i));

  GSK_RBTREE_FIRST (TREE(&tree), node);
  g_assert (node != NULL);
  g_assert (node->value == 1);
  GSK_RBTREE_LAST (TREE(&tree), node);
  g_assert (node != NULL);
  g_assert (node->value == 999);

  for (i = 1; i <= 999; i += 2)
    {
      g_assert (test_tree (&tree, i));
      g_assert (!test_tree (&tree, i+1));
    }
  for (i = 0; i <= 999; i++)
    {
      GSK_RBTREE_SUPREMUM_COMPARATOR (TREE(&tree), i, COMPARE_INT_WITH_TREE_NODE, node);
      g_assert (node);
      g_assert (node->value == (i%2)?i:(i+1));
    }
  GSK_RBTREE_SUPREMUM_COMPARATOR (TREE(&tree), 1000, COMPARE_INT_WITH_TREE_NODE, node);
  g_assert (node==NULL);
  for (i = 1; i <= 1000; i++)
    {
      TreeNode *node;
      GSK_RBTREE_INFIMUM_COMPARATOR (TREE(&tree), i, COMPARE_INT_WITH_TREE_NODE, node);
      g_assert (node);
      g_assert (node->value == (i%2)?i:(i-1));
    }
  GSK_RBTREE_INFIMUM_COMPARATOR (TREE(&tree), 0, COMPARE_INT_WITH_TREE_NODE, node);
  g_assert (node==NULL);
  for (i = 1; i <= 999; i += 2)
    g_assert (del_tree (&tree, i));

  /* random rbctree test */
  g_printerr ("Testing RBC-tree macros... ");
  for (i = 0; i < 1000; i++)
    test_random_rbcint_tree (10, TRUE);
  g_printerr (".");
  for (i = 0; i < 100; i++)
    test_random_rbcint_tree (100, TRUE);
  g_printerr (".");
  for (i = 0; i < 50; i++)
    {
      test_random_rbcint_tree (1000, FALSE);
      g_printerr (".");
    }
  for (i = 0; i < 5; i++)
    {
      test_random_rbcint_tree (10000, FALSE);
      g_printerr (".");
    }
  g_printerr (" done.\n");

  return 0;
}

