#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <gmodule.h>
#include <glib-object.h>
#include "../gskdebug.h"
#include "../gskinit.h"
#include "../xml/gskxmlprocessor.h"

/* TODO: stringify object
 *       ref/id
 *       signals
 *       syntax errors?
 */

/*
 * TestObject
 */

GType test_object_get_type (void);

#define TEST_OBJECT_TYPE (test_object_get_type ())
#define TEST_OBJECT(object) \
  (G_TYPE_CHECK_INSTANCE_CAST ((object), TEST_OBJECT_TYPE, TestObject))
#define TEST_OBJECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), TEST_OBJECT_TYPE, TestObjectClass))
#define TEST_IS_OBJECT(object) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((object), TEST_OBJECT_TYPE))
#define TEST_IS_OBJECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), TEST_OBJECT_TYPE))
#define TEST_OBJECT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TEST_OBJECT_TYPE, TestObjectClass))

typedef struct _TestObject      TestObject;
typedef struct _TestObjectClass TestObjectClass;

enum
{
  PROPERTY_0,
  PROPERTY_VALUE,
  PROPERTY_CHILD
};

struct _TestObjectClass
{
  GObjectClass object_class;

};

struct _TestObject
{
  GObject object;
  int value;
  TestObject *child;
};

static void test_object_get_property (GObject      *object,
				      guint         property_id,
				      GValue       *value,
				      GParamSpec   *pspec)
{
  TestObject *self = TEST_OBJECT (object);
  switch (property_id)
    {
      case PROPERTY_VALUE:
	g_value_set_int (value, self->value);
	break;
      case PROPERTY_CHILD:
	g_value_set_object (value, self->child);
	break;
      default:
	g_return_if_reached ();
    }
}

static void test_object_set_property (GObject      *object,
				      guint         property_id,
				      const GValue *value,
				      GParamSpec   *param_spec)
{
  TestObject *self = TEST_OBJECT (object);
  switch (property_id)
    {
      case PROPERTY_VALUE:
	self->value = g_value_get_int (value);
	break;
      case PROPERTY_CHILD:
	if (self->child)
	  g_object_unref (self->child);
	self->child = g_value_get_object (value);
	if (self->child)
	  g_object_ref (self->child);
	break;
      default:
	g_return_if_reached ();
    }
}

static void test_object_class_init (TestObjectClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = test_object_get_property;
  object_class->set_property = test_object_set_property;

  g_object_class_install_property (object_class,
				   PROPERTY_VALUE,
				   g_param_spec_int ("value",
						     "nick",
						     "blurb",
						     G_MININT,
						     G_MAXINT,
						     137,
						     G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
				   PROPERTY_CHILD,
				   g_param_spec_object ("child",
							"nick",
							"blurb",
							TEST_OBJECT_TYPE,
							G_PARAM_READWRITE));
}

static void test_object_init (TestObject *self)
{
}

GType test_object_get_type (void)
{
  static GType test_object_type = 0;
  if (!test_object_type)
    {
      static const GTypeInfo test_object_info =
	{
	  sizeof (TestObjectClass),
	  NULL,           /* base_init */
	  NULL,           /* base_finalize */
	  (GClassInitFunc) test_object_class_init,
	  NULL,           /* class_finalize */
	  NULL,           /* class_data */
	  sizeof (TestObject),
	  5,              /* n_preallocs */
	  (GInstanceInitFunc) test_object_init,
	};
      test_object_type =
	g_type_register_static (G_TYPE_OBJECT,
				"TestObject",
				&test_object_info,
				0);
    }
  return test_object_type;
}

static char *test_object_get_xml (TestObject *self)
{
  char *result, *value, *child;

  value = g_strdup_printf ("%d", self->value);

  if (self->child)
    {
      char *child_xml = test_object_get_xml (self->child);
      child = g_strjoin ("", "<child>", child_xml, "</child>", NULL);
      g_free (child_xml);
    }
  else
    child = g_strdup ("");

  result = g_strjoin ("", "<TestObject><value>", value, "</value>",
			  child, "</TestObject>", NULL);
  g_free (value);
  g_free (child);
  return result;
}

/* XML */

static gboolean return_true (GType type, gpointer data) { return TRUE; }

static void handle_value (const GValue *value, gpointer user_data)
{
  GValue *dst = (GValue *) user_data;
  g_return_if_fail (value);
  g_return_if_fail (dst);
  g_value_init (dst, G_VALUE_TYPE (value));
  g_value_copy (value, dst);
}

static const char *tests[] = {
  "<TestObject><value>42</value></TestObject>",

  "<TestObject>"
    "<value>77</value>"
    "<child><TestObject><value>86</value></TestObject></child>"
  "</TestObject>",

  "<TestObject>"
    "<value>77</value>"
    "<child>"
      "<TestObject>"
	"<value>88</value>"
	"<child><TestObject><value>99</value></TestObject></child>"
      "</TestObject>"
    "</child>"
  "</TestObject>",

  "<TestObject>"
    "<value>101</value>"
    "<child>"
      "<TestObject>"
	"<value>102</value>"
	"<child>"
	  "<TestObject>"
	    "<value>103</value>"
	    "<child><TestObject><value>104</value></TestObject></child>"
	  "</TestObject>"
	"</child>"
      "</TestObject>"
    "</child>"
  "</TestObject>",
};

int main (int argc, char *argv[])
{
  GskXmlProcessor *xml_processor;
  GskXmlConfig *config;
  GValue value = { 0 };
  int i;

  gsk_init_without_threads (&argc, &argv);

  config = gsk_xml_config_new (NULL);
  g_return_val_if_fail (config, -1);
  gsk_xml_config_add_type_test (config, return_true, NULL, NULL);
  gsk_xml_config_set_loader (config, gsk_xml_loader_introspective, NULL, NULL);
  xml_processor =
    gsk_xml_processor_new (config, G_TYPE_OBJECT, handle_value, &value, NULL);
  g_return_val_if_fail (xml_processor, -1);

  for (i = 0; i < G_N_ELEMENTS (tests); ++i)
    {
      TestObject *object;
      char *xml;

      if (!gsk_xml_processor_input (xml_processor, tests[i], strlen (tests[i])))
	g_error ("error parsing XML '%s'", tests[i]);
      g_return_val_if_fail (G_VALUE_TYPE (&value) == G_TYPE_OBJECT, -1);
      g_return_val_if_fail (TEST_IS_OBJECT (g_value_get_object (&value)), -1);
      object = TEST_OBJECT (g_value_get_object (&value));
      xml = test_object_get_xml (object);
      g_return_val_if_fail (strcmp (xml, tests[i]) == 0, -1);
      g_free (xml);
      g_value_unset (&value);
    }

  gsk_xml_processor_destroy (xml_processor);
  gsk_xml_config_unref (config);
  return 0;
}
