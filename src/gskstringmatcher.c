#include <string.h>
#include "gskstringmatcher.h"
#include "gskmempool.h"

typedef struct _MatchStateTransition MatchStateTransition;
typedef struct _MatchState MatchState;
typedef struct _ResultList ResultList;

typedef struct _TreeDataPos TreeDataPos;
typedef struct _TreeData TreeData;

struct _MatchStateTransition
{
  guint8 c;
  guint next_state;
  guint result_list_index;              /*0==no_results; 1..N are single matches
                                          others are allocated */
};

struct _MatchState
{
  guint n_transitions;
  MatchStateTransition *transitions;
};

struct _ResultList
{
  guint n_results;
  guint *results;
};

struct _GskStringMatcher
{
  guint n_entries;
  gpointer *entry_data;

  guint n_states;
  MatchState *states;

  guint n_nontriv_result_lists;
  ResultList *nontriv_result_lists;
};

/* used temporarily during construction */
struct _TreeDataPos
{
  guint match_index;
  guint match_pos;
};

struct _TreeData
{
  guint n_pos;
  TreeDataPos *pos;

  guint n_trans;
  MatchStateTransition *trans;
};

static guint tree_data_hash (gconstpointer a)
{
  const TreeData *td = a;
  guint hash = 5003;
  guint i;
  for (i = 0; i < td->n_pos; i++)
    {
      hash *= 33;
      hash += td->pos[i].match_index;
      hash *= 33;
      hash += td->pos[i].match_pos;
    }
  return hash;
}
static gboolean tree_data_equal (gconstpointer a, gconstpointer b)
{
  const TreeData *aa = a;
  const TreeData *bb = b;
  return aa->n_pos == bb->n_pos
      && memcmp (aa->pos, bb->pos, sizeof (TreeDataPos) * aa->n_pos) == 0;
}

GskStringMatcher *gsk_string_matcher_new (guint n_entries,
                                          GskStringMatcherEntry *entries)
{
  guint n_states_finished = 0;
  GskMemPool mem_pool = GSK_MEM_POOL_STATIC_INIT;
  GArray *tmp_pos = g_array_new (FALSE, FALSE, sizeof (TreeDataPos));
  GPtrArray *all_tree_data;
  GHashTable *pos_array_to_index;
  all_tree_data = g_ptr_array_new ();
  pos_array_to_index = g_hash_table_new (tree_data_hash,
                                            tree_data_equal);

  /* add an intiial state */
  TreeData *tree_data;
  tree_data = gsk_mem_pool_alloc (&mem_pool, sizeof (TreeData));
  tree_data->n_pos = 0;
  tree_data->pos = 0;
  g_ptr_array_add (all_tree_data, tree_data);
  g_hash_table_insert (pos_array_to_index, tree_data, GUINT_TO_POINTER (0));

  GArray *tmp_transitions = g_array_new (FALSE, FALSE,
                                         sizeof (MatchStateTransition));

  while (n_states_finished < all_tree_data->len)
    {
      guint c;
      tree_data = all_tree_data->pdata[n_states_finished];
      g_array_set_size (tmp_transitions, 0);

      /* for each of the 256 bytes, see if the byte leads to
         any interesting states (allocating those transition tables) */
      for (c = 1; c < 256; c++)
        {
          TreeData tmp_tree_data;
          MatchStateTransition trans;
          guint i;
          g_array_set_size (tmp_pos, 0);
          for (i = 0; i < tree_data->n_pos; i++)
            {
              ...
            }
          for (i = 0; i < n_entries; i++)
            {
              ...
            }

          if (tmp_pos->len == 0)
            continue;

          /* if any states are at the end of the string,
             gather them into the emission list
             and remove them from the state list */
          for (i = 0; i < tmp_pos->len; )
            {
              TreeDataPos *p = ((TreeDataPos *)tmp_pos->data) + i;
              if (matches[p->match_index].str[p->match_pos] == 0)
                {
                  g_array_append_val (tmp_emissions, p->match_index);
                  g_array_remove_fast (tmp_pos, i);
                }
              else
                {
                  i++;
                }
            }

          /* sort the emissions array */
          ...

          /* get a result id */
          if (tmp_emissions->len == 0)
            result_id = 0;
          else if (tmp_emissions->len == 1)
            result_id = g_array_index (tmp_emissions, guint, 0) + 1;
          else
            {
              ...
            }

          /* sort the new pos array */
          g_array_sort (tmp_pos, ...);

          tmp_tree_data.n_pos = tmp_pos->len;
          tmp_tree_data.pos = (TreeDataPos *) tmp_pos->data;
          if (g_hash_table_lookup_extended (pos_array_to_index,
                                            &tmp_tree_data,
                                            NULL,
                                            &new_state_index_ptr))
            {
              new_state_index = GPOINTER_TO_UINT (new_state_index_ptr);
            }
          else
            {
              /* allocate a new state */
              guint pos_size = tmp_pos->len * sizeof (TreeDataPos);
              new_tree_data.n_pos = tmp_pos->len;
              new_tree_data.pos = gsk_mem_pool_alloc (&mem_pool, pos_size);
              memcpy (new_tree_data.pos, tmp_pos->data, pos_size);

              new_state_index = all_tree_data->len;
              g_ptr_array_add (all_tree_data, new_tree_data);
              g_hash_table_insert (pos_array_to_index,
                                   new_tree_data,
                                   GUINT_TO_POINTER (new_state_index));
            }
          trans.c = c;
          trans.next_state = new_state_index;
          trans.result_list_index = result_id;
          g_array_append_val (tmp_transitions, trans);
        }
      g_assert (tmp_transitions->len > 0);
      tree_data->n_transitions = tmp_transitions->len;
      trans_size = tmp_transitions->len * sizeof (MatchStateTransition);
      tree_data->transitions = gsk_mem_pool_alloc (&mem_pool, trans_size);
      memcpy (tree_data->transitions, tmp_transitions->data, trans_size);
      n_states_finished++;
      total_transitions += tmp_transitions->len;
    }

  /* allocate the string matcher */
  total_size = sizeof (GskStringMatcher)
             + sizeof (gpointer) * n_entries
             + sizeof (guint) * n_entries
             + sizeof (MatchState) * all_tree_data->len
             + sizeof (MatchStateTransition) * total_transitions
             + sizeof (ResultList) * result_lists->len
             + sizeof (guint) * total_result_list_results;

  rv = g_malloc (total_size);
  rv->n_entries = n_entries;
  rv->entries = (gpointer *) (rv + 1);
  rv->match_entry_lens = (guint *) (rv->entries + n_entries);
  rv->n_states = all_tree_data->len;
  rv->states = (MatchState *) (rv->match_entry_lens + n_entries);
  match_transitions = (MatchStateTransition *) (match_states + all_tree_data->len);
  rv->n_result_lists = result_lists->len;
  rv->result_lists = (ResultList *) (match_transitions + total_transitions);
  results = (guint *) (rv->result_lists + result_lists->len);

  for (i = 0; i < n_entries; i++)
    {
      rv->entries[i] = entries[i].entry_data;
      rv->match_entry_lens[i] = strlen (entries[i].str);
    }
  for (i = 0; i < all_tree_data->len; i++)
    {
      tree_data = all_tree_data->pdata[i];
      rv->entries[i].n_transitions = tree_data->n_trans;
      rv->entries[i].transitions = match_transitions;
      memcpy (match_transitions,
              tree_data->trans,
              sizeof (MatchStateTransition) * tree_data->n_trans);
      match_transitions += tree_data->n_trans;
    }
  for (i = 0; i < result_lists->len; i++)
    {
      ResultList list = g_array_new (result_lists, ResultList, i);
      rv->result_lists[i].n_results = list.n_results;
      rv->result_lists[i].results = results;
      memcpy (results, list.results, list.n_results * sizeof (guint));
      results += list.n_results;
    }
  return rv;
}

void              gsk_string_matcher_run (GskStringMatcher *matcher,
                                          const char       *haystack,
                                          GskStringMatcherFunc match_func,
					  gpointer          match_data)
{
  guint state = 0;
  const char *at = haystack;
  while (*at)
    {
      guint8 c = *at++;
      MatchStateTransition *trans = matcher->states[state].transitions;
      guint n_trans = matcher->states[state].n_transitions;

      /* bsearch transitions */
      while (n_trans > 1)
        {
          ...
        }

      /* if not found, goto state 0 */
      if (n_trans == 0 || trans->c != c)
        {
          state = 0;
          continue;
        }

      if (trans->result_list_index > 0)
        {
          guint n_matches;
          guint *matches;
          guint dummy;
          if (trans->result_list_index <= matcher->n_entries)
            {
              n_matches = 1;
              dummy = trans->result_list_index - 1;
              matches = &dummy;
            }
          else
            {
              guint I = trans->result_list_index - matches->n_entries - 1;
              n_matches = matcher->result_lists[I].n_results;
              matches = matcher->result_lists[I].results;
            }
          for (m = 0; m < n_matches; m++)
            if (!func (matches[i],
                       at - haystack - matcher->match_entry_lens[matches[i]],
                       matcher->match_data[matches[i]],
                       match_data))
              return;
        }
      state = trans->new_state_index;
    }
}

void              gsk_string_matcher_free(GskStringMatcher *matcher)
{
  g_free (matcher);
}
