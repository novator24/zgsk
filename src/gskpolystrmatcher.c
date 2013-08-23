#include "gskpolystrmatcher.h"

typedef struct _PolystrMatcherNode PolystrMatcherNode;

typedef struct _TransTableEntry TransTableEntry;
struct _TransTableEntry
{
  char next;
  TransTableEntry *child;
};


struct _PolystrMatcherNode
{
  unsigned n_entries;
  union
  {
    TransTableEntry *entries;		/* n_entries > 0 */
    unsigned found_index;
  } info;
};
  
struct _PolystrMatcher
{
  PolystrMatcherNode *top;
  unsigned n_strs;
  unsigned *str_lens;
};

typedef struct _MatchState MatchState;
struct _MatchState
{
  unsigned str_index;
  unsigned n_chars_matched;
};
typedef struct _State State;
struct _State
{
  unsigned n_match_states;
  MatchState match_states[1];	/* sorted by n_chars_matched, then str_index */
};
static guint
hash_state (gconstpointer s)
{
  const State *state = s;
  guint hash = 1;
  unsigned i;
  for (i = 0; i < state->n_match_states; i++)
    {
      hash *= 5003;
      hash += state->match_states[i].str_index * 33;
      hash += state->match_states[i].n_chars_matched;
    }
  return hash;
}
static gboolean
equals_state (gconstpointer a, gconstpointer b)
{
  const State *state_a = a;
  const State *state_b = b;
  guint i;
  if (state_a->n_match_states != state_b->n_match_states)
    return FALSE;
  for (i = 0; i < state_a->n_match_states; i++)
    if (state_a->match_states[i].str_index != state_b->match_states[i].str_index
     || state_a->match_states[i].n_chars_matched != state_b->match_states[i].n_chars_matched)
      return FALSE;
  return TRUE;
}

typedef struct _CharMatchState CharMatchState;
struct _CharMatchState
{
  guint8 c;
  MatchState ms;
};

static int
compare_char_match_states (gconstpointer a,
                           gconstpointer b)
{
  const CharMatchState *ca = a;
  const CharMatchState *cb = b;
  if (ca->c < cb->c) return -1;
  if (ca->c > cb->c) return 1;
  if (ca->ms.n_chars_matched < cb->ms.n_chars_matched) return -1;
  if (ca->ms.n_chars_matched > cb->ms.n_chars_matched) return 1;
  if (ca->ms.str_index < cb->ms.str_index) return -1;
  if (ca->ms.str_index > cb->ms.str_index) return 1;
  return 0;
}

GskPolystrMatcher *
gsk_polystr_matcher_new (unsigned n_strs,
                     char **strs)
{
  GQueue *states_to_finish = g_queue_new ();
  PolystrMatcherNode *top_node = g_slice_new (PolystrMatcherNode);
  GHashTable *match_states = g_hash_table_new (hash_state, equals_state);
  State *top = g_malloc (sizeof (State));
  State *state;
  GArray *buffer = g_array_new (FALSE, FALSE, sizeof (CharMatchState));
  top->n_match_states = 0;
  g_hash_table_insert (match_states, top, top_node);
  g_queue_push_tail (states_to_finish, top);

  while ((state = g_queue_pop_head (states_to_finish)) != NULL)
    {
      PolystrMatcherNode *node = g_hash_table_lookup (match_states, state);
      CharMatchState *tmp;
      unsigned i;
      g_array_set_size (buffer, state->n_match_states + n_strs);
      tmp = (CharMatchState *) buffer->data;

      /* expand each existing match by one char */
      for (i = 0; i < state->n_match_states; i++)
	{
	  MatchState ms = state->match_states[i];
	  if (strs[ms.str_index][ms.n_chars_matched] == 0)
	    {
	      /* we have a match */
	      node->n_entries = 0;
              node->info.found_index = ms.str_index;
              goto continue_with_next_state;
	    }
	  else
	    {
	      tmp[i].c = strs[ms.str_index][ms.n_chars_matched];
	      tmp[i].ms.str_index = ms.str_index;
	      tmp[i].ms.n_chars_matched = ms.n_chars_matched + 1;
	    }
	}

      /* consider all new matches that may begin */
      for (i = 0; i < n_strs; i++)
        {
	  tmp[state->n_match_states + i].c = strs[i][0];
	  tmp[state->n_match_states + i].ms.str_index = i;
	  tmp[state->n_match_states + i].ms.n_chars_matched = 1;
	}
      g_array_sort (buffer, compare_char_match_states);
      prev_char = 0;
      for (i = 0; i < buffer->len; i++)
        {
	  if (prev_char != tmp[i].c)
	    n_trans++;
	  prev_char = tmp[i].c;
	}

      node->n_entries = n_trans;
      guint trans_i = 0;
      for (i = 0; i < buffer->len; )
        {
	  guint8 c = tmp[i].c;
	  unsigned n = 1;
	  while (i + n < buffer->len && tmp[i+n].c == c)
	    n++;
          node->info.entries[trans_i].next = c;
	  node->info.entries[trans_i].child = force_node (n, tmp + i,
	                                        match_states, states_to_finish);
	  i += n;
	  trans_i++;
	}
continue_with_next_state:
    }
  g_hash_table_foreach (match_states, (GHFunc) g_free, NULL);
  g_hash_table_destroy (match_states);

  matcher = g_new (PolystrMatcher, 1);
  matcher->top = top_node;
  matcher->n_strs = n_strs;
  matcher->str_lens = g_new (guint, n_strs);
  for (i = 0; i < n_strs;i++)
    matcher->str_lens[i] = strlen (strs[i]);
  return matcher;
}


gboolean
gsk_polystr_match (GskPolystrMatcher *matcher, const char *str,
                   unsigned *which_out, const char **start_out)
{
  PolystrMatcherNode *at = matcher->top;
  if (at != NULL)
    while (*str)
      {
        TransTableEntry *e;
        e = bsearch (GUINT_TO_POINTER (*str), at->entries,
	             at->n_entries, sizeof (TransTableEntry),
	             compare_char_to_entry);
        if (e == NULL)
	  at = matcher->top;
	else if (e->n_entries == 0)
	  {
	    *which_out = e->info.found_index;
	    *start_out = str - matcher->str_lens[e->info.found_index];
	    return TRUE;
	  }
	else
	  at = e;
        str++;
      }
  return FALSE;
}
