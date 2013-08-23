#include "../gsklistmacros.h"
#include <glib.h>
#include <string.h>
#include <stdlib.h>

/* --- nodes --- */
typedef struct _Node Node;
struct _Node
{
  Node *next, *prev;
  const char *str;
};

Node *
node_alloc (const char *str)
{
  Node *rv = g_new0 (Node, 1);
  rv->str = str;
  return rv;
}

void
node_free (Node *node)
{
  g_free (node);
}

/* int-list nodes */
typedef struct _IntNode IntNode;
struct _IntNode
{
  IntNode *next, *prev;
  guint i;
};

IntNode *
int_node_alloc (guint i)
{
  IntNode *rv = g_new0 (IntNode, 1);
  rv->i = i;
  return rv;
}

void
int_node_free (IntNode *node)
{
  g_free (node);
}


/* compare only the high 16 bits -- used for testing stability */
#define INT_NODE_COMPARE_HIGH16(a,b,rv) \
  G_STMT_START{                                                 \
    guint ai = a->i >> 16;                                      \
    guint bi = b->i >> 16;                                      \
    rv = ((ai < bi) ? -1 : (ai > bi) ? 1 : 0);                  \
  }G_STMT_END



/* used for simulating a stable sort in qsort()-- to match
   with INT_NODE_COMPARE_HIGH16... */
typedef struct _IntPair IntPair;
struct _IntPair
{
  guint index;
  guint value;
};
static int compare_int_pair_by_value_hi16_then_index(gconstpointer a,
                                                     gconstpointer b)
{
  const IntPair *ipa = a;
  const IntPair *ipb = b;
  guint ai = ipa->value >> 16;
  guint bi = ipb->value >> 16;
  if (ai < bi)
    return -1;
  else if (ai > bi)
    return 1;
  else if (ipa->index < ipb->index)
    return -1;
  else if (ipa->index > ipb->index)
    return 1;
  else
    return 0;
}



#define NODE_COMPARE(a,b,rv)   rv = strcmp((a)->str, (b)->str)
#define INT_NODE_COMPARE(a,b,rv)                                \
  G_STMT_START{                                                 \
    rv = ((a->i < b->i) ? -1 : (a->i > b->i) ? 1 : 0);          \
  }G_STMT_END

int main()
{
  Node *n;
  Node *first, *last;

  /* Test STACK stuff */
  first = NULL;
#define GET_STACK() Node*,first,next
  g_assert (GSK_STACK_IS_EMPTY (GET_STACK()));
  GSK_STACK_PUSH (GET_STACK (), node_alloc ("a"));
  GSK_STACK_PUSH (GET_STACK (), node_alloc ("b"));
  GSK_STACK_PUSH (GET_STACK (), node_alloc ("c"));
  g_assert (strcmp (first->str, "c") == 0);
  g_assert (strcmp (first->next->str, "b") == 0);
  g_assert (strcmp (first->next->next->str, "a") == 0);
  g_assert (first->next->next->next == NULL);
  GSK_STACK_GET_BOTTOM (GET_STACK (), n);
  g_assert (strcmp (n->str, "a") == 0);
  GSK_STACK_REVERSE (GET_STACK ());
  g_assert (strcmp (first->str, "a") == 0);
  g_assert (strcmp (first->next->str, "b") == 0);
  g_assert (strcmp (first->next->next->str, "c") == 0);
  g_assert (first->next->next->next == NULL);
  GSK_STACK_POP (GET_STACK (), n);
  g_assert (strcmp (n->str, "a") == 0);
  g_assert (strcmp (first->str, "b") == 0);
  GSK_STACK_INSERT_AFTER (GET_STACK (), first, n);
  GSK_STACK_SORT (GET_STACK (), NODE_COMPARE);
  GSK_STACK_FOREACH (GET_STACK (), n, node_free (n));
  first = last = NULL;
#undef GET_STACK

  /* Test QUEUE stuff */
#define GET_QUEUE() Node*,first,last,next
  g_assert (GSK_QUEUE_IS_EMPTY (GET_QUEUE()));
  GSK_QUEUE_PREPEND (GET_QUEUE (), node_alloc ("a"));
  GSK_QUEUE_PREPEND (GET_QUEUE (), node_alloc ("b"));
  GSK_QUEUE_PREPEND (GET_QUEUE (), node_alloc ("c"));
  g_assert (strcmp (first->str, "c") == 0);
  g_assert (strcmp (first->next->str, "b") == 0);
  g_assert (strcmp (first->next->next->str, "a") == 0);
  g_assert (first->next->next->next == NULL);
  g_assert (last == first->next->next);
  GSK_QUEUE_REVERSE (GET_QUEUE ());
  g_assert (strcmp (first->str, "a") == 0);
  g_assert (strcmp (first->next->str, "b") == 0);
  g_assert (strcmp (first->next->next->str, "c") == 0);
  g_assert (first->next->next->next == NULL);
  GSK_QUEUE_DEQUEUE (GET_QUEUE (), n);
  g_assert (strcmp (n->str, "a") == 0);
  g_assert (strcmp (first->str, "b") == 0);
  first = last = NULL;

  /* Test LIST stuff */
  g_warning ("need LIST tests");

  /* Test sorting more vigorously */
  {
    guint n_nodes = 10000;
    IntNode **nodes = g_new (IntNode *, n_nodes);
    IntNode *at;
    guint i;
#define GET_STACK() IntNode*,nodes[0],next
    for (i = 0; i < n_nodes; i++)
      nodes[i] = int_node_alloc (i);
    for (i = 0; i < n_nodes; i++)
      {
        guint r1 = g_random_int_range (0, n_nodes);
        guint r2 = g_random_int_range (0, n_nodes);
        IntNode *tmp = nodes[r1];
        nodes[r1] = nodes[r2];
        nodes[r2] = tmp;
      }
    for (i = 0; i + 1 < n_nodes; i++)
      nodes[i]->next = nodes[i+1];
    nodes[i]->next = NULL;
    GSK_STACK_SORT (GET_STACK (), INT_NODE_COMPARE);
    at = nodes[0];
    for (i = 0; i < n_nodes; i++)
      {
        g_assert (at->i == i);
        at = at->next;
      }
    g_assert (at == NULL);
#undef GET_STACK
    g_free (nodes);
  }

  /* Test sorting stability */
  {
    guint n_nodes = 10000;
    IntNode **nodes = g_new (IntNode *, n_nodes);
    IntNode *at;
    IntPair *int_pairs = g_new (IntPair, n_nodes);
    guint i;
#define GET_STACK() IntNode*,nodes[0],next
    for (i = 0; i < n_nodes; i++)
      nodes[i] = int_node_alloc (((i / 16) << 16) | g_random_int_range (0,255));
    for (i = 0; i < n_nodes; i++)
      {
        guint r1 = g_random_int_range (0, n_nodes);
        guint r2 = g_random_int_range (0, n_nodes);
        IntNode *tmp = nodes[r1];
        nodes[r1] = nodes[r2];
        nodes[r2] = tmp;
      }
    for (i = 0; i + 1 < n_nodes; i++)
      nodes[i]->next = nodes[i+1];
    nodes[i]->next = NULL;
    for (i = 0; i < n_nodes; i++)
      {
        int_pairs[i].index = i;
        int_pairs[i].value = nodes[i]->i;
      }
    GSK_STACK_SORT (GET_STACK (), INT_NODE_COMPARE_HIGH16);
    qsort (int_pairs, n_nodes, sizeof (IntPair),
           compare_int_pair_by_value_hi16_then_index);
    at = nodes[0];
    for (i = 0; i < n_nodes; i++)
      {
        g_assert (at->i == int_pairs[i].value);
        at = at->next;
      }
    g_assert (at == NULL);
#undef GET_STACK
    g_free (nodes);
    g_free (int_pairs);
  }

  return 0;
}
