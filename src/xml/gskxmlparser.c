#include <string.h>
#include <stdlib.h>
#include "gskxmlparser.h"

/* --- GskXmlParserConfig --- */
typedef struct _GskXmlParserStateTrans GskXmlParserStateTrans;
typedef struct _GskXmlParserState GskXmlParserState;
typedef struct _NsInfo NsInfo;

struct _GskXmlParserStateTrans
{
  char *name;
  GskXmlParserState *new_state;
};

struct _GskXmlParserState
{
  guint n_trans;
  GskXmlParserStateTrans *trans;

  GskXmlParserState *fallback_state;

  guint n_emit_indices;
  guint *emit_indices;
};
  
struct _NsInfo
{
  char *abbrev;
  char *url;
};

struct _GskXmlParserConfig
{
  GskXmlParserState *init;
  guint ref_count;
  guint done : 1;
  guint ignore_ns_tag : 1;
  guint passthrough_unknown_ns : 1;

  GPtrArray *paths;
  GArray *ns_array;             /* of NsInfo */
};



GskXmlParserConfig *
gsk_xml_parser_config_new (void)
{
  GskXmlParserConfig *config = g_new (GskXmlParserConfig, 1);
  config->init = NULL;
  config->ref_count = 1;
  config->done = 0;
  config->ignore_ns_tag = 0;
  config->passthrough_unknown_ns = 0;

  config->paths = g_ptr_array_new ();
  return config;
}

static GskXmlParserConfig *real_new_by_depth (guint depth)
{
  GskXmlParserConfig *n = gsk_xml_parser_config_new ();
  char *str;
  guint i;
  gsk_xml_parser_config_set_flags (n, GSK_XML_PARSER_IGNORE_NS_TAGS);
  str = g_malloc (depth * 2 + 2);
  for (i = 0; i < depth; i++)
    {
      str[2*i+0] = '*';
      str[2*i+1] = '/';
    }
  str[2*i+0] = '*';
  str[2*i+1] = 0;
  if (gsk_xml_parser_config_add_path (n, str, NULL) < 0)
    g_assert_not_reached ();
  gsk_xml_parser_config_done (n);
  return n;
}

GskXmlParserConfig *
gsk_xml_parser_config_new_by_depth (guint depth)
{
  static GskXmlParserConfig *by_depths[32];
  if (G_LIKELY (depth < G_N_ELEMENTS (by_depths)))
    {
      if (by_depths[depth] == NULL)
        by_depths[depth] = real_new_by_depth (depth);
      return gsk_xml_parser_config_ref (by_depths[depth]);
    }
  else
    return real_new_by_depth (depth);
}

GskXmlParserConfig *
gsk_xml_parser_config_ref (GskXmlParserConfig *config)
{
  g_return_val_if_fail (config->done, NULL);
  g_return_val_if_fail (config->ref_count > 0, NULL);
  ++(config->ref_count);
  return config;
}

static void
free_state_recursive (GskXmlParserState *state)
{
  guint i;
  for (i = 0; i < state->n_trans; i++)
    {
      g_free (state->trans[i].name);
      free_state_recursive (state->trans[i].new_state);
    }
  g_free (state->trans);
  if (state->fallback_state)
    free_state_recursive (state->fallback_state);
  g_free (state);
}

void
gsk_xml_parser_config_unref    (GskXmlParserConfig *config)
{
  g_return_if_fail (config->ref_count > 0);
  if (--(config->ref_count) == 0)
    {
      free_state_recursive (config->init);
      g_ptr_array_foreach (config->paths, (GFunc) g_free, NULL);
      g_ptr_array_free (config->paths, TRUE);
      g_free (config);
    }
}


gint
gsk_xml_parser_config_add_path (GskXmlParserConfig *config,
                                const char         *path,
                                GError            **error)
{
  guint rv = config->paths->len;
  g_ptr_array_add (config->paths, g_strdup (path));
  return rv;
}

void
gsk_xml_parser_config_add_ns   (GskXmlParserConfig *config,
                                const char         *abbrev,
                                const char         *url)
{
  NsInfo ns_info;
  g_return_if_fail (!config->done);
  ns_info.abbrev = g_strdup (abbrev);
  ns_info.url = g_strdup (url);
  g_array_append_val (config->ns_array, ns_info);
}

#define ITERATE_THROUGH_FLAGS_AND_BITS() \
  VISIT (GSK_XML_PARSER_IGNORE_NS_TAGS, ignore_ns_tag) \
  VISIT (GSK_XML_PARSER_PASSTHROUGH_UNKNOWN_NS, passthrough_unknown_ns)

void
gsk_xml_parser_config_set_flags(GskXmlParserConfig *config,
                                GskXmlParserFlags   flags)
{
  g_return_if_fail (!config->done);

#define VISIT(flag, bit) config->bit = (flags & flag) ? 1 : 0;
  ITERATE_THROUGH_FLAGS_AND_BITS()
#undef VISIT
}

GskXmlParserFlags
gsk_xml_parser_config_get_flags(GskXmlParserConfig *config)
{
  GskXmlParserFlags rv = 0;
#define VISIT(flag, bit) { if (config->bit) rv |= flag; }
  ITERATE_THROUGH_FLAGS_AND_BITS()
#undef VISIT
  return rv;
}

typedef struct _PathIndex PathIndex;
struct _PathIndex
{
  guint path_len;
  const char *path_start;
  guint orig_index;
  gboolean is_start;            /* only set up after sorting by path */
};

static int
compare_path_index_by_str (gconstpointer a, gconstpointer b)
{
  const PathIndex *a_pi = a;
  const PathIndex *b_pi = b;
  int rv;
  if (a_pi->path_len < b_pi->path_len)
    {
      rv = memcmp (a_pi->path_start, b_pi->path_start, a_pi->path_len);
      if (rv == 0)
        rv = -1;
    }
  else if (a_pi->path_len > b_pi->path_len)
    {
      rv = memcmp (a_pi->path_start, b_pi->path_start, b_pi->path_len);
      if (rv == 0)
        rv = 1;
    }
  else /* a_pi->path_len == b_pi->path_len */
    rv = memcmp (a_pi->path_start, b_pi->path_start, a_pi->path_len);
  return rv;
}

static int
bsearch_path_index_array (guint n_pi,
                          PathIndex *pi,
                          const char *str,
                          guint str_len)
{
  PathIndex dummy;
  PathIndex *rv;
  dummy.path_start = str;
  dummy.path_len = str_len;
  rv = bsearch (&dummy, pi, n_pi, sizeof (PathIndex),
                compare_path_index_by_str);
  if (rv == NULL)
    return -1;
  while (rv > pi && compare_path_index_by_str (&dummy, rv - 1) == 0)
    rv--;
  return rv - pi;
}

static void
branch_states_recursive (GskXmlParserState *state,
                         guint              n_paths,
                         char             **paths)
{
  char **next_paths =  g_newa (char *, n_paths);
  char **state_next_paths =  g_newa (char *, n_paths);
  PathIndex *indices = g_newa (PathIndex, n_paths);
  guint i;
  guint n_indices = 0;
  guint n_trans;
  guint n_star_trans = 0, n_star_outputs = 0;
  int star_trans_start = -1;
  guint o;
  for (i = 0; i < n_paths; i++)
    {
      const char *end;
      if (paths[i] == NULL)
        next_paths[i] = NULL;
      else if ((end=strchr (paths[i], '/')) != NULL)
        {
          indices[n_indices].path_start = paths[i];
          indices[n_indices].path_len = end - paths[i];
          indices[n_indices].orig_index = i;
          n_indices++;
          //got_trans = TRUE;          // needed?
          next_paths[i] = (char *) end + 1;
          if (end == paths[i] + 1 && paths[i][0] == '*')
            n_star_trans++;
        }
      else
        {
          indices[n_indices].path_start = paths[i];
          indices[n_indices].path_len = strlen (paths[i]);
          indices[n_indices].orig_index = i;
          n_indices++;
          //got_output = TRUE;          // needed?
          next_paths[i] = NULL;
          if (strcmp (paths[i], "*") == 0)
            n_star_outputs++;
        }
    }
  if (n_indices == 0)
    {
      state->n_trans = 0;
      state->trans = NULL;
      return;
    }

  qsort (indices, n_indices, sizeof (PathIndex), compare_path_index_by_str);
  n_trans = 0;
  for (i = 0; i < n_indices; i++)
    if (i == 0
      || indices[i-1].path_len != indices[i].path_len
      || memcmp (indices[i-1].path_start,
                 indices[i].path_start,
                 indices[i].path_len) != 0)
      {
        indices[i].is_start = TRUE;
        if (indices[i].path_len == 1 && indices[i].path_start[0] == '*')
          star_trans_start = i;
        else
          n_trans++;
      }
    else
      {
        indices[i].is_start = FALSE;
      }

  state->trans = g_new (GskXmlParserStateTrans, n_trans);
  o = 0;                /* index into state->trans for outputting transitions */
  for (i = 0; i < n_indices; )
    {
      guint end;
      guint n_outputs = n_star_outputs;
      GskXmlParserState *new_state;
      guint j;
      guint eio = 0;            /* emit_indices output-index */
      if (next_paths[indices[i].orig_index] == NULL)
        n_outputs++;
      for (end = i + 1; end < n_indices && !indices[end].is_start; end++)
        if (next_paths[indices[end].orig_index] == NULL)
          n_outputs++;

      /* init state->trans[o] */
      state->trans[o].name = g_strdup (paths[indices[i].orig_index]);
      new_state = state->trans[o].new_state = g_new (GskXmlParserState, 1);

      /* write state->trans[o].new_state->n_emit_indices,emit_indices */
      new_state->n_emit_indices = n_outputs;
      new_state->emit_indices = g_new (guint, n_outputs);
      for (j = i; j < end; j++)
        if (next_paths[indices[j].orig_index] == NULL)
          new_state->emit_indices[eio++] = indices[j].orig_index;
      /* write "*" outputs */
      if (n_star_outputs)
        {
          for (j = star_trans_start;
               j < n_indices
               && indices[j].path_len == 1
               && indices[j].path_start[0] == '*';
               j++)
            if (next_paths[indices[j].orig_index] == NULL)
              new_state->emit_indices[eio++] = indices[j].orig_index;
        }
      g_assert (eio == n_outputs);

      /* create state_next_paths by copying next_paths
       * for the original indices that begin with the current
       * path component */
      memset (state_next_paths, 0, sizeof (char *) * n_paths);
      for (j = i; j < end; j++)
        {
          guint oi = indices[j].orig_index;
          state_next_paths[oi] = next_paths[oi];
        }

      /* recurse on state to fill in state->trans[o].state's
       * transitions and fallback state. */
      branch_states_recursive (new_state, n_paths, state_next_paths);

      o++;
      i = end;
    }
  g_assert (o == n_trans);

  /* initialize fallback state */
  if (n_star_outputs || n_star_trans)
    {
      guint j;
      guint eio = 0;            /* emit_indices output-index */
      state->fallback_state = g_new (GskXmlParserState, 1);
      state->fallback_state->n_emit_indices = n_star_outputs;
      state->fallback_state->emit_indices = g_new (guint, n_star_outputs);

      /* find the first '*' in paths[indices[i].orig_index]
         (which is sorted relative to i) */
      memset (state_next_paths, 0, sizeof (char *) * n_paths);
      for (j = star_trans_start;
           j < n_indices
           && indices[j].path_len == 1
           && indices[j].path_start[0] == '*';
           j++)
        {
          guint oi = indices[j].orig_index;
          if (next_paths[oi] == NULL)
            state->fallback_state->emit_indices[eio++] = oi;
          state_next_paths[oi] = next_paths[oi];
        }

      if (n_star_trans)
        {
          branch_states_recursive (state->fallback_state,
                                   n_paths, state_next_paths);
        }
      else
        {
          state->fallback_state->n_trans = 0;
          state->fallback_state->trans = 0;
        }
    }
}

void
gsk_xml_parser_config_done(GskXmlParserConfig *config)
{
  g_return_if_fail (config != NULL);
  g_return_if_fail (!config->done);
  g_return_if_fail (config->ref_count > 0);
  config->done = 1;
  config->init = g_new (GskXmlParserState, 1);
  config->init->n_emit_indices = 0;
  config->init->emit_indices = NULL;

  branch_states_recursive (config->init,
                           config->paths->len,
                           (char **) config->paths->pdata);
}


/* --- GskXmlParser --- */
typedef struct _ParseLevel ParseLevel;
struct _ParseLevel
{
  GskXmlParserState *state;     /* NULL if past known state */
  GPtrArray *children;          /* NULL if discardable */
  guint n_ns;
  char **ns_map;                /* prefix to canon prefix */
  ParseLevel *up;
};
struct _GskXmlParser
{
  GMarkupParseContext *parse_context;
  ParseLevel *level;
  GskXmlParserConfig *config;
};

/* --- without namespace support --- */
static void 
without_ns__start_element  (GMarkupParseContext *context,
                            const gchar         *element_name,
                            const gchar        **attribute_names,
                            const gchar        **attribute_values,
                            gpointer             user_data,
                            GError             **error)
{
  ...
}

static void 
without_ns__end_element(GMarkupParseContext *context,
                        const gchar         *element_name,
                        gpointer             user_data,
                        GError             **error)
{
  ...
}

static void 
without_ns__text       (GMarkupParseContext *context,
                        const gchar         *text,
                        gsize                text_len,  
                        gpointer             user_data,
                        GError             **error)
{
  ...
}


static void 
without_ns__passthrough(GMarkupParseContext *context,
                        const gchar         *passthrough_text,
                        gsize                text_len,  
                        gpointer             user_data,
                        GError             **error)
{
  ...
}

/* --- with namespace support --- */
static void 
with_ns__start_element  (GMarkupParseContext *context,
                         const gchar         *element_name,
                         const gchar        **attribute_names,
                         const gchar        **attribute_values,
                         gpointer             user_data,
                         GError             **error)
{
  ...
}

static void 
with_ns__end_element(GMarkupParseContext *context,
                     const gchar         *element_name,
                     gpointer             user_data,
                     GError             **error)
{
  ...
}

static void 
with_ns__text       (GMarkupParseContext *context,
                     const gchar         *text,
                     gsize                text_len,  
                     gpointer             user_data,
                     GError             **error)
{
  ...
}


static void 
with_ns__passthrough(GMarkupParseContext *context,
                     const gchar         *passthrough_text,
                     gsize                text_len,  
                     gpointer             user_data,
                     GError             **error)
{
  ...
}

static GMarkupParser without_ns_parser =
{
  without_ns__start_element,
  without_ns__end_element,
  without_ns__text,
  without_ns__passthrough,
  NULL                          /* no error handler */
};
static GMarkupParser with_ns_parser =
{
  with_ns__start_element,
  with_ns__end_element,
  with_ns__text,
  with_ns__passthrough,
  NULL                          /* no error handler */
};

GskXmlParser *
gsk_xml_parser_new_take (GskXmlParserConfig *config)
{
  GskXmlParser *parser;
  GMarkupParser *p;
  g_return_val_if_fail (config->done, NULL);
  parser = g_slice_new (GskXmlParser);
  p = config->ignore_ns_tag ? &without_ns_parser : &with_ns_parser;
  parser->parse_context = g_markup_parse_context_new (p, 0, parser, NULL);
  parser->level = NULL;
  parser->config = config;
  return parser;
}

GskXmlParser *
gsk_xml_parser_new (GskXmlParserConfig *config)
{
  return gsk_xml_parser_new_take (gsk_xml_parser_config_ref (config));
}


GskXmlParser *
gsk_xml_parser_new_by_depth (guint               depth)
{
  GskXmlParserConfig *config = gsk_xml_parser_config_new_by_depth (depth);
  return gsk_xml_parser_new_take (config);
}

GskXml *
gsk_xml_parser_dequeue      (GskXmlParser       *parser,
                             guint               index)
{
  ...
}

gboolean
gsk_xml_parser_feed         (GskXmlParser       *parser,
                             const guint8       *xml_data,
                             gssize              len,
                             GError            **error)
{
  return g_markup_parse_context_parse (parser->parse_context,
                                       (const char *) xml_data, len, error);
}

gboolean
gsk_xml_parser_feed_file    (GskXmlParser       *parser,
                             const char         *filename,
                             GError            **error)
{
  ...
}

void
gsk_xml_parser_free         (GskXmlParser       *parser)
{
  ...

  gsk_xml_parser_config_unref (parser->config);
}

