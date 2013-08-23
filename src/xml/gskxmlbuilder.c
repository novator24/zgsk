#include <string.h>
#include <stdlib.h>
#include "gskxml.h"
#include "../gskghelpers.h"

typedef struct _BuilderNode BuilderNode;
struct _BuilderNode
{
  GskXmlNamespace *ns;
  GskXmlString *name;
  guint n_attrs;
  GskXmlAttribute *attrs;
  BuilderNode *to_root;
  GPtrArray   *children;

  GskXmlNamespace *default_ns;
  guint n_ns;
  GskXmlNamespace **ns_by_abbrev;
};

struct _GskXmlBuilder
{
  BuilderNode *root;         /* of BuilderNode */
  BuilderNode *cur;          /* points to head of 'root' list */
  GSList      *first_doc;
  GSList      *last_doc;
  gboolean     namespace_support;
};

GskXmlBuilder *gsk_xml_builder_new (GskXmlParseFlags flags)
{
  GskXmlBuilder *builder = g_new (GskXmlBuilder, 1);
  builder->root = NULL;
  builder->cur = NULL;
  builder->first_doc = builder->last_doc = NULL;
  builder->namespace_support = ((flags & GSK_XML_PARSE_WITHOUT_NAMESPACE_SUPPORT) == 0);
  return builder;
}

static int
compare_nspointer_by_abbrev (gconstpointer a,
                             gconstpointer b)
{
  GskXmlNamespace *ns_a = * (GskXmlNamespace **) a;
  GskXmlNamespace *ns_b = * (GskXmlNamespace **) b;
  GskXmlString *abbrev_a = ns_a->abbrev;
  GskXmlString *abbrev_b = ns_b->abbrev;
  return (abbrev_a < abbrev_b) ? -1
       : (abbrev_a > abbrev_b) ? +1
       : 0;
}

/* merge parent namespace abbreviates with 'namespaces'.

   do this by sorting namespaces by 'abbrev' pointer,
   then iterating parent_ns_by_abbrev,
   using namespaces[i] */
static GskXmlNamespace **
merge_namespace_tables (guint parent_n_ns,
                        GskXmlNamespace **parent_ns,
                        guint             n_namespaces,
                        GskXmlNamespace **namespaces,
                        guint            *n_rv_out)
{
  guint i;
  GskXmlNamespace **rv;
  guint n_rv;
  guint i_parent, i_namespaces;

  qsort (namespaces, n_namespaces, sizeof (GskXmlNamespace *),
         compare_nspointer_by_abbrev);

  /* ensure there are no duplicate namespaces,
     optimizing for the case where there are not. */
  for (i = 0; i + 1 < n_namespaces; i++)
    if (G_UNLIKELY (namespaces[i]->abbrev == namespaces[i+1]->abbrev))
      {
        /* ok, slow case: uniq namespaces */
        guint o = i;

        while (i < n_namespaces)
          {
            guint end_run;
            GskXmlNamespace *ns;

            /* find the last similarly abbreviated ns */
            for (end_run = i;
                 end_run + 1 < n_namespaces
                 && namespaces[end_run]->abbrev == namespaces[end_run+1]->abbrev;
                 end_run++)
              ;

            /* replace the run with a single namespace */
            ns = gsk_xml_namespace_ref (namespaces[i]);
            for (   ; i <= end_run; i++)
              gsk_xml_namespace_unref (namespaces[i]);
            namespaces[o++] = ns;
          }
        n_namespaces = o;
      }

  /* collate parent_ns_by_abbrev with n_namespaces */
  rv = g_new (GskXmlNamespace *, n_namespaces + parent_n_ns);
  n_rv = 0;
  for (i_parent = i_namespaces = 0;
       i_parent < parent_n_ns || i_namespaces < n_namespaces;
      )
    {
      guint op;
      if (i_parent == parent_n_ns)
        op = 0;         /* use namespaces */
      else if (i_namespaces == n_namespaces)
        op = 1;         /* use parent_ns */
      else if (parent_ns[i_parent]->abbrev < namespaces[i_namespaces]->abbrev)
        op = 1;         /* use parent_ns */
      else if (parent_ns[i_parent]->abbrev > namespaces[i_namespaces]->abbrev)
        op = 0;         /* use namespaces */
      else
        op = 2;

      switch (op)
        {
        case 0:
          rv[n_rv++] = namespaces[i_namespaces];
          gsk_xml_namespace_ref (namespaces[i_namespaces]);
          i_namespaces++;
          break;
        case 1:
          rv[n_rv++] = parent_ns[i_parent];
          gsk_xml_namespace_ref (parent_ns[i_parent]);
          i_parent++;
          break;
        case 2:
          /* XXX: what to do if the namespace abbreviations conflict!?!??! */
          rv[n_rv++] = parent_ns[i_parent];
          gsk_xml_namespace_ref (parent_ns[i_parent]);
          i_parent++;
          i_namespaces++;
          break;
        }
    }
  *n_rv_out = n_rv;
  return rv;
}

static GskXmlNamespace *
bsearch_ns_array (guint n, GskXmlNamespace **ns,
                  GskXmlString *abbrev)
{
  guint lo = 0, count = n;
  while (count)
    {
      guint mid = lo + count / 2;
      if (ns[mid]->abbrev < abbrev)
        {
          guint new_lo = mid + 1;
          count -= (new_lo - lo);
          lo = new_lo;
        }
      else if (ns[mid]->abbrev > abbrev)
        {
          count /= 2;
        }
      else
        return ns[mid];
    }
  return NULL;
}

static void
try_ns_support (guint n_ns,
                GskXmlNamespace **ns_by_abbrev,
                GskXmlNamespace *default_ns,
                GskXmlString **name_inout,
                GskXmlNamespace **ns_inout)
{
  GskXmlString *old_name = *name_inout;
  const char *name_str = (char*) old_name;
  const char *colon = strchr (name_str, ':');
  g_assert (*ns_inout == NULL);
  if (colon != NULL)
    {
      GskXmlString *ns_name = gsk_xml_string_new_len (name_str, colon - name_str);
      GskXmlNamespace *ns = bsearch_ns_array (n_ns, ns_by_abbrev, ns_name);
      if (ns)
        {
          *ns_inout = ns;
          *name_inout = gsk_xml_string_new (colon + 1);
          gsk_xml_string_unref (old_name);
        }
      else
        {
          /* XXX: error handling??? */
        }
      gsk_xml_string_unref (ns_name);
    }
  else
    {
      if (default_ns)
        *ns_inout = default_ns;
    }
}

void           gsk_xml_builder_start (GskXmlBuilder *builder,
                                      GskXmlString  *name,
                                      guint          n_attrs,
                                      GskXmlString **attrs)
{
  BuilderNode *bn = g_new (BuilderNode, 1);
  guint i;
  bn->name = gsk_xml_string_ref (name);
  bn->n_attrs = n_attrs;
  bn->attrs = g_new (GskXmlAttribute, n_attrs);
  bn->ns_by_abbrev = NULL;
  bn->default_ns = NULL;
  bn->ns = NULL;
  bn->to_root = NULL;
  if (builder->namespace_support)
    {
      /* look for 'xmlns' and 'xmlns:' entries. */

      /* the number of attributes found,
         disclusing xmlns attributes. */
      guint n_real_attrs = 0;

      /* array of namespace attributes found,
         optimized for small numbers of namespaces. */
      guint n_ns_entries = 0;
      guint n_ns_entries_alloced = 16;
      GskXmlNamespace **ns_entries = g_newa (GskXmlNamespace *, n_ns_entries_alloced);
      gboolean must_free_ns_entries = FALSE;

      for (i = 0; i < n_attrs * 2; i+=2)
        {
          if (memcmp ((char*)(attrs[i]), "xmlns", 5) == 0)
            {
              const char *suffix = (char*)(attrs[i]) + 5;
              if (suffix[0] == '\0')
                {
                  /* xmlns=... */
                  if (bn->default_ns != NULL)
                    {
                      /* um, shouldn't happen in wellformed invocation
                         (since behavior with duplicate attributes is undefined) */
                      gsk_xml_namespace_unref (bn->default_ns);
                    }
                  bn->default_ns = gsk_xml_namespace_new (NULL, attrs[i+1]);
                  continue;
                }
              else if (suffix[0] == ':' && suffix[1] != '\0')
                {
                  /* xmlns:something=... */
                  GskXmlString *abbrev = gsk_xml_string_new (suffix+1);
                  if (G_UNLIKELY (n_ns_entries == n_ns_entries_alloced))
                    {
                      /* too many namespaces.  must reallocate */
                      guint new_alloced = n_ns_entries_alloced * 2;
                      GskXmlNamespace **new = g_new (GskXmlNamespace *, new_alloced);
                      memcpy (new, ns_entries, sizeof (GskXmlNamespace*) * n_ns_entries_alloced);
                      if (must_free_ns_entries)
                        g_free (ns_entries);
                      ns_entries = new;
                      must_free_ns_entries = TRUE;
                      n_ns_entries_alloced = new_alloced;
                    }
                  ns_entries[n_ns_entries++] = gsk_xml_namespace_new (abbrev, attrs[i+1]);
                  gsk_xml_string_unref (abbrev);
                  continue;
                }
              else
                {
                  /* fall through to normal attribute code. */
                }
            }

          /* ok, it's a real attribute */
          bn->attrs[n_real_attrs].name = gsk_xml_string_ref (attrs[i]);
          bn->attrs[n_real_attrs].value = gsk_xml_string_ref (attrs[i+1]);
          bn->attrs[n_real_attrs].ns = NULL;
          n_real_attrs++;
        }

      bn->n_attrs = n_real_attrs;

      /* if no "xmlns" attribute found,
         inherit default namespace from parent. */
      if (bn->default_ns == NULL
       && builder->cur != NULL
       && builder->cur->default_ns != NULL)
        bn->default_ns = gsk_xml_namespace_ref (builder->cur->default_ns);

      /* merge parent's namespace table
         with the new namespaces */
      {
        guint cur_n;
        GskXmlNamespace **cur_ns;
        if (builder->cur)
          {
            cur_n = builder->cur->n_ns;
            cur_ns = builder->cur->ns_by_abbrev;
          }
        else
          {
            cur_n = 0;
            cur_ns = NULL;
          }
        bn->ns_by_abbrev = merge_namespace_tables (cur_n, cur_ns,
                                                   n_ns_entries, ns_entries,
                                                   &bn->n_ns);
      }
    }
  else
    {
      bn->n_attrs = n_attrs;
      for (i = 0; i < n_attrs; i++)
        {
          bn->attrs[i].name = gsk_xml_string_ref (attrs[2*i+0]);
          bn->attrs[i].value = gsk_xml_string_ref (attrs[2*i+1]);
          bn->attrs[i].ns = NULL;
        }
      bn->n_ns = 0;
      bn->ns_by_abbrev = NULL;
    }

  bn->children = NULL;
  if (builder->root == NULL)
    {
      builder->root = builder->cur = bn;
    }
  else
    {
      bn->to_root = builder->cur;
      builder->cur = bn;
    }


  if (builder->namespace_support)
    {
      /* resolve attribute namespaces */
      for (i = 0; i < bn->n_attrs; i++)
        {
          try_ns_support (bn->n_ns, bn->ns_by_abbrev, bn->default_ns,
                          &bn->attrs[i].name,
                          &bn->attrs[i].ns);

        }
      
      /* resolve 'name' namespace */
      try_ns_support (bn->n_ns, bn->ns_by_abbrev, bn->default_ns,
                      &bn->name,
                      &bn->ns);
    }
  else
    {
      bn->ns = NULL;
    }
}

/* XXX: optimize this!!! */
void           gsk_xml_builder_start_c (GskXmlBuilder *builder,
                                        const char    *name,
                                        guint          n_attrs,
                                        char         **attrs)
{
  GskXmlString **x_attrs = g_newa (GskXmlString *, n_attrs * 2);
  GskXmlString *x_name = gsk_xml_string_new (name);
  guint i;
  for (i = 0; i < n_attrs * 2; i++)
    x_attrs[i] = gsk_xml_string_new (attrs[i]);
  gsk_xml_builder_start (builder, x_name, n_attrs, x_attrs);
  for (i = 0; i < n_attrs * 2; i++)
    gsk_xml_string_unref (x_attrs[i]);
  gsk_xml_string_unref (x_name);
}

void           gsk_xml_builder_start_ns (GskXmlBuilder *builder,
                                         GskXmlString  *name_ns,
                                         GskXmlString  *name,
                                         guint          n_attrs,
                                         GskXmlRawAttribute *attrs)
{
  BuilderNode *bn = g_new (BuilderNode, 1);
  guint i;
  bn->name = gsk_xml_string_ref (name);
  bn->n_attrs = n_attrs;
  bn->attrs = g_new (GskXmlAttribute, n_attrs);
  bn->ns_by_abbrev = NULL;
  bn->default_ns = NULL;

  if (builder->namespace_support)
    {
      /* look for 'xmlns' and 'xmlns:' entries. */

      /* the number of attributes found,
         disclusing xmlns attributes. */
      guint n_real_attrs = 0;

      /* array of namespace attributes found,
         optimized for small numbers of namespaces. */
      guint n_ns_entries = 0;
      guint n_ns_entries_alloced = 16;
      GskXmlNamespace **ns_entries = g_newa (GskXmlNamespace *, n_ns_entries_alloced);
      gboolean must_free_ns_entries = FALSE;

      for (i = 0; i < n_attrs; i++)
        {
          if (gsk_xml_string__xmlns == attrs[i].ns_abbrev)
            {
              /* xmlns:something=... */
              if (G_UNLIKELY (n_ns_entries == n_ns_entries_alloced))
                {
                  /* too many namespaces.  must reallocate */
                  guint new_alloced = n_ns_entries_alloced * 2;
                  GskXmlNamespace **new = g_new (GskXmlNamespace *, new_alloced);
                  memcpy (new, ns_entries, sizeof (GskXmlNamespace*) * n_ns_entries_alloced);
                  if (must_free_ns_entries)
                    g_free (ns_entries);
                  ns_entries = new;
                  must_free_ns_entries = TRUE;
                  n_ns_entries_alloced = new_alloced;
                }
              ns_entries[n_ns_entries++] = gsk_xml_namespace_new (attrs[i].name, attrs[i].value);
            }
          else if (attrs[i].ns_abbrev == NULL && attrs[i].name == gsk_xml_string__xmlns)
            {
              /* xmlns=... */
              if (bn->default_ns != NULL)
                {
                  /* um, shouldn't happen in wellformed invocation
                     (since behavior with duplicate attributes is undefined) */
                  gsk_xml_namespace_unref (bn->default_ns);
                }
              bn->default_ns = gsk_xml_namespace_new (NULL, attrs[i].value);
            }
          else
            {
              /* ok, it's a real attribute */
              bn->attrs[n_real_attrs].name = gsk_xml_string_ref (attrs[i].name);
              bn->attrs[n_real_attrs].value = gsk_xml_string_ref (attrs[i].value);
              bn->attrs[n_real_attrs].ns = (GskXmlNamespace *) attrs[i].ns_abbrev;
              n_real_attrs++;
            }
        }

      bn->n_attrs = n_real_attrs;

      /* if no "xmlns" attribute found,
         inherit default namespace from parent. */
      if (bn->default_ns == NULL
       && builder->cur != NULL
       && builder->cur->default_ns != NULL)
        bn->default_ns = gsk_xml_namespace_ref (builder->cur->default_ns);

      /* merge parent's namespace table
         with the new namespaces */
      bn->ns_by_abbrev = merge_namespace_tables (builder->cur->n_ns,
                                                 builder->cur->ns_by_abbrev,
                                                 n_ns_entries, ns_entries,
                                                 &bn->n_ns);
    }

  bn->children = NULL;
  if (builder->root == NULL)
    {
      builder->root = builder->cur = bn;
    }
  else
    {
      bn->to_root = builder->cur;
      builder->cur = bn;
    }


  /* resolve attribute namespaces */
  for (i = 0; i < bn->n_attrs; i++)
    {
      GskXmlString *ns_abbrev = (GskXmlString *) bn->attrs[i].ns;
      if (ns_abbrev == NULL)
        bn->attrs[i].ns = bn->default_ns;
      else
        bn->attrs[i].ns = bsearch_ns_array (bn->n_ns, bn->ns_by_abbrev, ns_abbrev);
    }
      
  /* resolve 'name' namespace */
  if (name_ns == NULL)
    bn->ns = bn->default_ns;
  else
    bn->ns = bsearch_ns_array (bn->n_ns, bn->ns_by_abbrev, name_ns);
}

void           gsk_xml_builder_text  (GskXmlBuilder *builder,
                                      GskXmlString  *str)
{
  g_return_if_fail (builder->root != NULL);
  if (builder->cur->children == NULL)
    builder->cur->children = g_ptr_array_new ();
  g_ptr_array_add (builder->cur->children,
                   gsk_xml_node_new_text (str));
}

void           gsk_xml_builder_add_node  (GskXmlBuilder *builder,
                                          GskXmlNode    *node)
{
  g_return_if_fail (builder->root != NULL);
  if (builder->cur->children == NULL)
    builder->cur->children = g_ptr_array_new ();
  g_ptr_array_add (builder->cur->children, gsk_xml_node_ref (node));
}

GskXmlNode    *gsk_xml_builder_end   (GskXmlBuilder *builder,
                                      GskXmlString  *name)
{
  BuilderNode *to_finish;
  GskXmlNode *node;
  guint i;
  g_return_val_if_fail (builder->cur != NULL, NULL);
  if (name)
    g_return_val_if_fail (builder->cur->name == name, NULL);

  /* make cur into an xml node */
  to_finish = builder->cur;
  builder->cur = builder->cur->to_root;
  node = gsk_xml_node_new_element (to_finish->ns,
                                   to_finish->name,
                                   to_finish->n_attrs,
                                   to_finish->attrs,
                                   to_finish->children->len,
                                   (GskXmlNode **) to_finish->children->pdata);

  /* free to_finish */
  gsk_xml_string_unref (to_finish->name);
  for (i = 0; i < to_finish->n_attrs; i++)
    {
      /* NOTE: ns is NOT referenced by the attributes! */
      gsk_xml_string_unref (to_finish->attrs[i].name);
      gsk_xml_string_unref (to_finish->attrs[i].value);
    }
  g_free (to_finish->attrs);
  if (to_finish->children != NULL)
    {
      for (i = 0; i < to_finish->children->len; i++)
        gsk_xml_node_unref (to_finish->children->pdata[i]);
      g_ptr_array_free (to_finish->children, FALSE);
    }
  g_free (to_finish);

  /* add new node to parent */
  if (builder->cur == NULL)
    {
      /* this is a new doc */
      if (builder->first_doc == NULL)
        builder->first_doc = builder->last_doc = g_slist_prepend (NULL, node);
      else
        {
          builder->last_doc = g_slist_append (builder->last_doc, node)->next;
        }
    }
  else
    {
      if (builder->cur->children == NULL)
        builder->cur->children = g_ptr_array_new ();
      g_ptr_array_add (builder->cur->children, node);
    }
  return node;
}
GskXmlNode    *gsk_xml_builder_get_doc(GskXmlBuilder *builder)
{
  GskXmlNode *rv;
  if (builder->first_doc == NULL)
    return NULL;
  rv = builder->first_doc->data;
  builder->first_doc = g_slist_remove (builder->first_doc, rv);
  if (builder->first_doc == NULL)
    builder->last_doc = NULL;
  return rv;
}

void
gsk_xml_builder_free (GskXmlBuilder *builder)
{
  BuilderNode *node;
  g_slist_foreach (builder->first_doc, (GFunc) gsk_xml_node_unref, NULL);
  g_slist_free (builder->first_doc);
  for (node = builder->cur; node; )
    {
      BuilderNode *next = node->to_root;
      guint i;
      gsk_xml_string_unref (node->name);
      for (i = 0; i < node->n_attrs; i++)
        {
          gsk_xml_string_unref (node->attrs[i].name);
          gsk_xml_string_unref (node->attrs[i].value);
        }
      gsk_g_ptr_array_foreach (node->children, (GFunc) gsk_xml_node_unref, NULL);
      g_ptr_array_free (node->children, TRUE);
      if (node->default_ns)
        gsk_xml_namespace_unref (node->default_ns);
      for (i = 0; i < node->n_ns; i++)
        gsk_xml_namespace_unref (node->ns_by_abbrev[i]);
      g_free (node->ns_by_abbrev);
      g_free (node);
      node = next;
    }

  g_free (builder);
}
