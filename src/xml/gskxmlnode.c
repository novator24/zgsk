#include "gskxml.h"
#include "../gskqsortmacro.h"
#include <string.h>

static GHashTable *ns_table = NULL;

#define XML_NODE_MAGIC          0x33ae

G_LOCK_DEFINE_STATIC (ns_table);

static guint
ns_hash (gconstpointer a)
{
  const GskXmlNamespace *ns = a;
  gsize ns_abbrev = GPOINTER_TO_SIZE (ns->abbrev);
  gsize ns_uri = GPOINTER_TO_SIZE (ns->uri);
  return ns_uri ^ (ns_abbrev << 4);
}

static gboolean
ns_equal (gconstpointer a, gconstpointer b)
{
  const GskXmlNamespace *ns_a = a;
  const GskXmlNamespace *ns_b = b;
  return ns_a->abbrev == ns_b->abbrev
      && ns_a->uri == ns_b->uri;
}

GskXmlNamespace *
gsk_xml_namespace_new   (GskXmlString *abbrev, /* may be NULL */
                         GskXmlString *uri)
{
  GskXmlNamespace dummy;
  GskXmlNamespace *rv;
  dummy.abbrev = abbrev;
  dummy.uri = uri;
  G_LOCK (ns_table);
  if (ns_table == NULL)
    ns_table = g_hash_table_new (ns_hash, ns_equal);
  rv = g_hash_table_lookup (ns_table, &dummy);
  if (rv)
    {
      ++(rv->ref_count);
      G_UNLOCK (ns_table);
      return rv;
    }
  else
    {
      rv = g_new (GskXmlNamespace, 1);
      rv->ref_count = 1;
      rv->abbrev = abbrev ? gsk_xml_string_ref (abbrev) : NULL;
      rv->uri = gsk_xml_string_ref (uri);
      g_hash_table_insert (ns_table, rv, rv);
      G_UNLOCK (ns_table);
      return rv;
    }
}

GskXmlNamespace *gsk_xml_namespace_ref   (GskXmlNamespace *ns)
{
  G_LOCK (ns_table);
  ++(ns->ref_count);
  G_UNLOCK (ns_table);
  return ns;
}

void             gsk_xml_namespace_unref (GskXmlNamespace *ns)
{
  G_LOCK (ns_table);
  if (--(ns->ref_count) == 0)
    {
      g_hash_table_remove (ns_table, ns);
      g_free (ns);
    }
  G_UNLOCK (ns_table);
}

GskXmlNode *
gsk_xml_node_new_text    (GskXmlString *text)
{
  GskXmlNode *node = g_new (GskXmlNode, 1);
  node->base.ref_count = 1;
  node->base.magic = XML_NODE_MAGIC;
  node->base.type = GSK_XML_NODE_TYPE_TEXT;
  node->v_text.content = gsk_xml_string_ref (text);
  return node;
}

GskXmlNode *
gsk_xml_node_new_text_c  (const char *str)
{
  GskXmlNode *node = g_new (GskXmlNode, 1);
  node->base.ref_count = 1;
  node->base.magic = XML_NODE_MAGIC;
  node->base.type = GSK_XML_NODE_TYPE_TEXT;
  node->v_text.content = gsk_xml_string_new (str);
  return node;
}


GskXmlNode *
gsk_xml_node_new_element (GskXmlNamespace *ns,
                          GskXmlString *name,
                          guint         n_attrs,
                          GskXmlAttribute*attrs,
                          guint         n_children,
                          GskXmlNode  **children)
{
  guint size = sizeof (GskXmlNodeElement)
             + sizeof (GskXmlAttribute) * n_attrs
             + sizeof (GskXmlNode *) * n_children;

  GskXmlNodeElement *rv;
  guint i;
  guint n_element_children = 0;
  guint8 *at;
  for (i = 0; i < n_children; i++)
    if (children[i]->type == GSK_XML_NODE_TYPE_ELEMENT)
      n_element_children++;
  size += n_element_children * sizeof (GskXmlNode *);

  rv = g_malloc (size);
  rv->base.ref_count = 1;
  rv->base.magic = XML_NODE_MAGIC;
  rv->base.type = GSK_XML_NODE_TYPE_ELEMENT;
  rv->ns = ns ? gsk_xml_namespace_ref (ns) : NULL;
  rv->name = gsk_xml_string_ref (name);
  at = (guint8 *) (rv + 1);
  if (n_attrs)
    {
      rv->attributes = (GskXmlAttribute *) at;
      memcpy (rv->attributes, attrs, sizeof (GskXmlAttribute) * n_attrs);
      for (i = 0; i < n_attrs; i++)
        {
          if (attrs[i].ns != NULL)
            gsk_xml_namespace_ref (attrs[i].ns);
          gsk_xml_string_ref (attrs[i].name);
          gsk_xml_string_ref (attrs[i].value);
        }
#define COMPARE_ATTRIBUTE(a,b,rv)            \
      if (a.ns == NULL)                      \
        {                                    \
          if (b.ns != NULL)                  \
            rv = -1;                         \
          else                               \
            rv = 0;                          \
        }                                    \
      else if (b.ns == NULL)                 \
        rv = 1;                              \
      else if (a.ns->uri < b.ns->uri)        \
        rv = -1;                             \
      else if (a.ns->uri > b.ns->uri)        \
        rv = 1;                              \
      else                                   \
        rv = 0;                              \
      if (rv == 0)                           \
        {                                    \
          if (a.name < b.name)               \
            rv = -1;                         \
          else if (a.name > b.name)          \
            rv = 1;                          \
        }
      GSK_QSORT (rv->attributes, GskXmlAttribute, n_attrs, COMPARE_ATTRIBUTE);
#undef COMPARE_ATTRIBUTE
      at = (guint8*) (rv->attributes + n_attrs);
    }
  else
    rv->attributes = NULL;
  rv->n_attributes = n_attrs;

  rv->n_children = n_children;
  rv->children = (GskXmlNode **) at;
  rv->n_element_children = n_element_children;
  rv->element_children = rv->children + n_children;
  n_element_children = 0;
  for (i = 0; i < n_children; i++)
    {
      rv->children[i] = gsk_xml_node_ref (children[i]);
      if (rv->children[i]->type == GSK_XML_NODE_TYPE_ELEMENT)
        rv->element_children[n_element_children++] = GUINT_TO_POINTER (i);
    }
#define COMPARE_CHILD_BY_INDEX(a,b,rv)                  \
    {                                                   \
      guint i_a = GPOINTER_TO_UINT (a);                 \
      guint i_b = GPOINTER_TO_UINT (b);                 \
      GskXmlNodeElement *child_a = &children[i_a]->v_element; \
      GskXmlNodeElement *child_b = &children[i_b]->v_element; \
      if (child_a->ns == NULL)                          \
        {                                               \
          if (child_b->ns == NULL)                      \
            rv = 0;                                     \
          else                                          \
            rv = -1;                                    \
        }                                               \
      else if (child_b->ns == NULL)                     \
        rv = 1;                                         \
      else if (child_a->ns->uri < child_b->ns->uri)     \
        rv = -1;                                        \
      else if (child_a->ns->uri > child_b->ns->uri)     \
        rv = 1;                                         \
      else                                              \
        rv = 0;                                         \
      if (rv == 0)                                      \
        {                                               \
          if (child_a->name < child_b->name)            \
            rv = -1;                                    \
          else if (child_a->name > child_b->name)       \
            rv = 1;                                     \
        }                                               \
      if (rv == 0)                                      \
        {                                               \
          if (i_a < i_b)                                \
            rv = -1;                                    \
          else if (i_a > i_b)                           \
            rv = 1;                                     \
        }                                               \
    }
  GSK_QSORT (rv->element_children,
             GskXmlNode *,
             n_element_children,
             COMPARE_CHILD_BY_INDEX);
#undef COMPARE_CHILD_BY_INDEX

  for (i = 0; i < n_element_children; i++)
    rv->element_children[i] = rv->children[GPOINTER_TO_UINT (rv->element_children[i])];

  return (GskXmlNode *) rv;
}

GskXmlNode *
gsk_xml_node_new_from_element_with_new_children
                         (GskXmlNode   *node,
                          guint         n_children,
                          GskXmlNode  **children)
{
  g_return_val_if_fail (node->type == GSK_XML_NODE_TYPE_ELEMENT, NULL);

  return gsk_xml_node_new_element (node->v_element.ns,
                                   node->v_element.name,
                                   node->v_element.n_attributes,
                                   node->v_element.attributes,
                                   n_children, children);
}

GskXmlNode *
gsk_xml_node_ref    (GskXmlNode   *node)
{
  g_assert (node->base.ref_count > 0);
  ++(node->base.ref_count);
  return node;
}

void
gsk_xml_node_unref  (GskXmlNode   *node)
{
  guint i;
  g_assert (node->base.ref_count > 0);
  --(node->base.ref_count);
  if (node->base.ref_count == 0)
    {
      switch (node->type)
        {
        case GSK_XML_NODE_TYPE_TEXT:
          gsk_xml_string_unref (node->v_text.content);
          break;

        case GSK_XML_NODE_TYPE_ELEMENT:
          for (i = 0; i < node->v_element.n_attributes; i++)
            {
              gsk_xml_string_unref (node->v_element.attributes[i].name);
              gsk_xml_string_unref (node->v_element.attributes[i].value);
              if (node->v_element.attributes[i].ns)
                gsk_xml_namespace_unref (node->v_element.attributes[i].ns);
            }
          for (i = 0; i < node->v_element.n_children; i++)
            gsk_xml_node_unref (node->v_element.children[i]);
          gsk_xml_string_unref (node->v_element.name);
          break;

        default:
          g_assert_not_reached ();
          break;
        }

      g_free (node);
    }
}

#define MAX_CHILDREN_FOR_STACK          32      /* somewhat conservative */

GskXmlNode *
gsk_xml_node_find_child  (GskXmlNode   *node,
                          GskXmlString *ns_uri,
                          GskXmlString *child_name,
                          guint         instance)
{
  guint lo, n;
  if (node->type != GSK_XML_NODE_TYPE_ELEMENT)
    return NULL;
  lo = 0;
  n = node->v_element.n_element_children;
#define MATCHES_RAW(test, rv)                                   \
  G_STMT_START{                                                 \
    if (test->v_element.ns == NULL)                             \
      rv = (ns_uri == NULL ? 0 : -1);                           \
    else if (test->v_element.ns->uri < ns_uri)                  \
      rv = -1;                                                  \
    else if (test->v_element.ns->uri > ns_uri)                  \
      rv = 1;                                                   \
    else                                                        \
      rv = 0;                                                   \
    if (rv == 0)                                                \
      {                                                         \
        if (test->v_element.name < child_name)                  \
          rv = -1;                                              \
        else if (test->v_element.name > child_name)             \
          rv = 1;                                               \
      }                                                         \
  }G_STMT_END
    
  while (n)
    {
      guint mid = lo + n / 2;
      GskXmlNode *mid_child = node->v_element.element_children[mid];
      /* is mid_child <=> ns_uri.child_name ? */
      int cmp;
      MATCHES_RAW (mid_child, cmp);
      if (cmp == 0)
        {
          if (mid == 0)
            goto found_first;
          else
            {
              GskXmlNode *t2 = node->v_element.element_children[mid-1];
              MATCHES_RAW (t2, cmp);
              if (cmp == 0)
                n /= 2;
              else
                {
                  lo = mid;
                  goto found_first;
                }
            }
        }
      else if (cmp < 0)
        {
          guint new_lo = mid + 1;
          n -= (new_lo - lo);
          lo = new_lo;
        }
      else
        n /= 2;
    }
  return NULL;

found_first:
  if (instance == 0)
    return node->v_element.element_children[lo];
  if (lo + instance >= node->v_element.n_element_children)
    return NULL;
  {
    GskXmlNode *rv = node->v_element.element_children[lo + instance];
    int cmp;
    MATCHES_RAW (rv, cmp);
    if (cmp == 0)
      return rv;
    else
      return NULL;
  }
}

typedef struct
{
  GskXmlString *first;
  GPtrArray *all;
} GetContentInfo;

static void
get_content_recursive (GetContentInfo *info,
                       GskXmlNode     *node)
{
  if (node->type == GSK_XML_NODE_TYPE_TEXT)
    {
      GskXmlString *str = node->v_text.content;
      if (info->first == NULL)
        info->first = str;
      else if (info->all == NULL)
        {
          info->all = g_ptr_array_new ();
          g_ptr_array_add (info->all, info->first);
          g_ptr_array_add (info->all, str);
        }
      else
        g_ptr_array_add (info->all, str);
    }
  else
    {
      guint i;
      for (i = 0; i < node->v_element.n_children; i++)
        get_content_recursive (info, node->v_element.children[i]);
    }
}

GskXmlString *gsk_xml_node_get_content (GskXmlNode *node)
{
  GetContentInfo info = {NULL, NULL};
  get_content_recursive (&info, node);
  if (info.first == NULL)
    return gsk_xml_string_new ("");
  else if (info.all == NULL)
    return gsk_xml_string_ref (info.first);
  else
    {
      GskXmlString *rv = gsk_xml_strings_concat (info.all->len, (GskXmlString**)(info.all->pdata));
      g_ptr_array_free (info.all, TRUE);
      return rv;
    }
}

gpointer gsk_xml_node_cast_check (gpointer node, GskXmlNodeType node_type)
{
  g_return_val_if_fail (((GskXmlNode*)node)->type == node_type, node);
  g_return_val_if_fail (((GskXmlNode*)node)->base.magic == XML_NODE_MAGIC, node);
  return node;
}
gpointer gsk_xml_node_cast_check_any_type (gpointer node)
{
  g_return_val_if_fail (((GskXmlNode*)node)->base.magic == XML_NODE_MAGIC, node);
  return node;
}
