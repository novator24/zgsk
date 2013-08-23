#include "../xml/gskxml.h"
#include <string.h>

/* trivial parsing test */
static void
test_basics ()
{
  GskXmlNode *node, *child;
  GskXmlString *str;
  GskXmlString *k_str;
  char *c_str;

  node = gsk_xml_parse_data ("<hi>mom</hi>", -1, 0, NULL);
  g_assert (node);
  g_assert (node->type == GSK_XML_NODE_TYPE_ELEMENT);
  g_assert (strcmp ((char*)node->v_element.name, "hi") == 0);
  g_assert (node->v_element.n_children == 1);
  g_assert (node->v_element.children[0]->type == GSK_XML_NODE_TYPE_TEXT);
  g_assert (strcmp ((char*)node->v_element.children[0]->v_text.content, "mom") == 0);
  gsk_xml_node_unref (node);

  node = gsk_xml_parse_data ("<hi><a>AAA</a><b>BBB</b><a>aaa2</a></hi>", -1, 0, NULL);
  g_assert (node);
  g_assert (node->type == GSK_XML_NODE_TYPE_ELEMENT);
  g_assert (strcmp ((char*)node->v_element.name, "hi") == 0);
  g_assert (node->v_element.n_children == 3);
  k_str = gsk_xml_string_new ("c");
  child = gsk_xml_node_find_child (node, NULL, k_str, 0);
  gsk_xml_string_unref (k_str);
  g_assert (child == NULL);
  k_str = gsk_xml_string_new ("a");
  child = gsk_xml_node_find_child (node, NULL, k_str, 0);
  g_assert (child != NULL);
  g_assert (child->type == GSK_XML_NODE_TYPE_ELEMENT);
  g_assert (child->v_element.n_children == 1);
  str = gsk_xml_node_get_content (child->v_element.children[0]);
  g_assert (strcmp ((char*)str, "AAA") == 0);
  gsk_xml_string_unref (str);
  child = gsk_xml_node_find_child (node, NULL, k_str, 1);
  g_assert (child != NULL);
  g_assert (child->type == GSK_XML_NODE_TYPE_ELEMENT);
  g_assert (child->v_element.n_children == 1);
  str = gsk_xml_node_get_content (child->v_element.children[0]);
  g_assert (strcmp ((char*)str, "aaa2") == 0);
  gsk_xml_string_unref (str);
  gsk_xml_string_unref (k_str);

  k_str = gsk_xml_string_new ("b");
  child = gsk_xml_node_find_child (node, NULL, k_str, 0);
  g_assert (child != NULL);
  g_assert (child->type == GSK_XML_NODE_TYPE_ELEMENT);
  g_assert (child->v_element.n_children == 1);
  str = gsk_xml_node_get_content (child->v_element.children[0]);
  g_assert (strcmp ((char*)str, "BBB") == 0);
  gsk_xml_string_unref (str);
  gsk_xml_node_unref (node);
  gsk_xml_string_unref (k_str);

  str = gsk_xml_string_new ("&");
  child = gsk_xml_node_new_text (str);
  k_str = gsk_xml_string_new ("b");
  node = gsk_xml_node_new_element (NULL, k_str, 0, NULL, 1, &child);
  gsk_xml_string_unref (str);
  gsk_xml_string_unref (k_str);
  gsk_xml_node_unref (child);
  c_str = gsk_xml_to_string (node, FALSE);
  g_strstrip (c_str);
  g_assert (strcmp (c_str, "<b>&amp;</b>") == 0);
  g_free (c_str);
  gsk_xml_node_unref (node);
}

/* --- XML-Object mapping --- */
typedef GObjectClass MyObjectAClass;
typedef MyObjectAClass MyObjectBClass;
typedef struct {
  GObject base_instance;
  guint a;
} MyObjectA;
typedef struct {
  MyObjectA base_instance;
  guint b;
} MyObjectB;
GType my_object_a_get_type (void);
GType my_object_b_get_type (void);
G_DEFINE_TYPE(MyObjectA, my_object_a, G_TYPE_OBJECT);
G_DEFINE_TYPE(MyObjectB, my_object_b, my_object_a_get_type ());
enum
{
  A_PROP_0,
  A_PROP_A
};
enum
{
  B_PROP_0,
  B_PROP_B
};

static void
my_object_a_get_property (GObject      *object,
                          guint         property_id,
                          GValue       *value,
                          GParamSpec   *pspec)
{
  switch (property_id)
    {
    case A_PROP_A:
      g_value_set_uint (value, ((MyObjectA*)object)->a);
      break;
    }
}
static void
my_object_a_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  switch (property_id)
    {
    case A_PROP_A:
      ((MyObjectA*)object)->a = g_value_get_uint (value);
      break;
    }
}

static void my_object_a_class_init (MyObjectAClass *class)
{
  GObjectClass *oclass = G_OBJECT_CLASS (class);
  oclass->get_property = my_object_a_get_property;
  oclass->set_property = my_object_a_set_property;
  g_object_class_install_property (oclass,
                                   A_PROP_A,
                                   g_param_spec_uint ("a", "a", "a",
                                                      0,100000,0,
                                                      G_PARAM_READWRITE));
}
static void my_object_a_init (MyObjectA *object)
{
}
static void
my_object_b_get_property (GObject      *object,
                          guint         property_id,
                          GValue       *value,
                          GParamSpec   *pspec)
{
  switch (property_id)
    {
    case B_PROP_B:
      g_value_set_uint (value, ((MyObjectB*)object)->b);
      break;
    }
}
static void
my_object_b_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  switch (property_id)
    {
    case B_PROP_B:
      ((MyObjectB*)object)->b = g_value_get_uint (value);
      break;
    }
}

static void my_object_b_class_init (MyObjectBClass *class)
{
  GObjectClass *oclass = G_OBJECT_CLASS (class);
  oclass->get_property = my_object_b_get_property;
  oclass->set_property = my_object_b_set_property;
  g_object_class_install_property (oclass,
                                   A_PROP_A,
                                   g_param_spec_uint ("b", "b", "b",
                                                      0,100000,0,
                                                      G_PARAM_READWRITE));
}
static void my_object_b_init (MyObjectB *object)
{
}

static void test_context (void)
{
  GskXmlContext *context = gsk_xml_context_global ();
  GskXmlNode *node;
  GObject *object;
  g_type_class_ref (my_object_b_get_type ());

  node = gsk_xml_parse_data ("<MyObjectA><a>444</a></MyObjectA>", -1, 0, NULL);
  g_assert (node);
  object = gsk_xml_context_deserialize_object (context, G_TYPE_OBJECT, node, NULL);
  g_assert (object != NULL);
  g_assert (G_OBJECT_TYPE (object) == my_object_a_get_type ());
  g_assert (((MyObjectA*)object)->a == 444);
  gsk_xml_node_unref (node);
  g_object_unref (object);
}

int main()
{
  gsk_xml_string_init ();
  g_type_init ();
  test_basics ();
  test_context ();
  return 0;
}
