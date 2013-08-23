#include <string.h>
#include <stdlib.h>
#include "testobject.h"
#include "../store/gskxmlformat.h"

enum
{
  PROP_0,
  PROP_CHAR,
  PROP_UCHAR,
  PROP_BOOLEAN,
  PROP_INT,
  PROP_UINT,
  PROP_LONG,
  PROP_ULONG,
  PROP_INT64,
  PROP_UINT64,
  PROP_FLOAT,
  PROP_DOUBLE,
  PROP_STRING,
  PROP_OBJECT,

  PROP_CHAR_ARRAY,
  PROP_UCHAR_ARRAY,
  PROP_BOOLEAN_ARRAY,
  PROP_INT_ARRAY,
  PROP_UINT_ARRAY,
  PROP_LONG_ARRAY,
  PROP_ULONG_ARRAY,
  PROP_INT64_ARRAY,
  PROP_UINT64_ARRAY,
  PROP_FLOAT_ARRAY,
  PROP_DOUBLE_ARRAY,
  PROP_STRING_ARRAY,
  PROP_OBJECT_ARRAY
};

static GObjectClass *test_object_parent_class = NULL;

gboolean
test_objects_equal (const TestObject *a,
		    const TestObject *b,
		    gboolean          verbose)
{
  g_return_val_if_fail (IS_TEST_OBJECT (a), FALSE);
  g_return_val_if_fail (IS_TEST_OBJECT (b), FALSE);

#define SIMPLE_TYPE_CASE(t,fmt) \
  if (a->prop_##t != b->prop_##t) \
    { \
      if (verbose) \
	g_warning ("TestObjects differ; " \
		   "a->prop_" #t "=" fmt ", b->prop_" #t "=" fmt, \
		   a->prop_##t, b->prop_##t); \
      return FALSE; \
    }

  SIMPLE_TYPE_CASE (char,    "%c")
  SIMPLE_TYPE_CASE (uchar,   "%c")
  SIMPLE_TYPE_CASE (boolean, "%c")
  SIMPLE_TYPE_CASE (int,     "%d")
  SIMPLE_TYPE_CASE (uint,    "%u")
  SIMPLE_TYPE_CASE (long,    "%ld")
  SIMPLE_TYPE_CASE (ulong,   "%lu")
  SIMPLE_TYPE_CASE (int64,   "%lld")
  SIMPLE_TYPE_CASE (uint64,  "%llu")
  SIMPLE_TYPE_CASE (float,   "%.18e")
  SIMPLE_TYPE_CASE (double,  "%.18e")

#undef SIMPLE_TYPE_CASE

  if (a->prop_string)
    {
      if (b->prop_string == NULL)
	{
	  if (verbose)
	    g_warning ("TestObjects differ; "
		       "a->prop_string=%s, b->prop_string=NULL",
		       a->prop_string);
	  return FALSE;
	}
      if (strcmp (a->prop_string, b->prop_string) != 0)
	{
	  if (verbose)
	    g_warning ("TestObjects differ; "
		       "a->prop_string=%s, b->prop_string=%s",
		       a->prop_string, b->prop_string);
	  return FALSE;
	}
    }
  else if (b->prop_string)
    {
      if (verbose)
	g_warning ("TestObjects differ; a->prop_string=NULL, b->prop_string=%s",
		   b->prop_string);
      return FALSE;
    }

  if (a->prop_object)
    {
      if (b->prop_object == NULL)
	return FALSE;
      if (!test_objects_equal (a->prop_object, b->prop_object, verbose))
	return FALSE;
    }
  else if (b->prop_object)
    return FALSE;

  return TRUE;
}

TestObject *
test_object_random (double p)
{
  TestObject *object;

  object = g_object_new (TEST_OBJECT_TYPE, NULL);

  if (drand48 () < p)
    object->prop_boolean = TRUE;

#define NUMERIC_TYPE_CASE(t,min,max) \
  if (drand48 () < p) \
    object->prop_##t = min + drand48 () * (max - min);

  NUMERIC_TYPE_CASE (char,    'a',  'z')
  NUMERIC_TYPE_CASE (uchar,   'a',  'z')
  NUMERIC_TYPE_CASE (int,    -1e6,  1e6)
  NUMERIC_TYPE_CASE (uint,    0,    1e6)
  NUMERIC_TYPE_CASE (long,   -1e6,  1e6)
  NUMERIC_TYPE_CASE (ulong,   0,    1e6)
  NUMERIC_TYPE_CASE (int64,  -1e16, 1e16)
  NUMERIC_TYPE_CASE (uint64,  0,    1e16)
  NUMERIC_TYPE_CASE (float,  -1e9,  1e9)
  NUMERIC_TYPE_CASE (double, -1e12, 1e12)

#undef NUMERIC_TYPE_CASE

  if (drand48 () < p)
    {
      if (object->prop_string)
	g_free (object->prop_string);
      if (drand48 () < 0.5)
	object->prop_string = g_strdup_printf ("%f", drand48 ());
      else
	{
	  guint len;
	  gchar *str, *end;
	  len = lrand48 () & 0x1fff;
	  str = object->prop_string = g_malloc (len + 1);
	  g_return_val_if_fail (str, NULL);
	  for (end = str + len; str < end; ++str)
	    {
	      /* Try not to run afoul of GskXmlFormat limitations... */
	      do
		*str = lrand48 () & 0x7f;
	      while (*str == 0 || *str == '>');
	    }
	  *str = 0;
	}
    }
  if (drand48 () < p)
    {
      if (object->prop_object)
	g_object_unref (object->prop_object);
      object->prop_object = test_object_random (p);
    }
  return object;
}

static void
test_object_get_property (GObject    *object,
			  guint       property_id,
			  GValue     *value,
			  GParamSpec *pspec)
{
  TestObject *self = TEST_OBJECT (object);

  switch (property_id)
    {
#define SIMPLE_TYPE_CASE(T,t) \
      case PROP_##T: \
        g_value_set_##t (value, self->prop_##t); \
        break;

      SIMPLE_TYPE_CASE (CHAR,    char)
      SIMPLE_TYPE_CASE (UCHAR,   uchar)
      SIMPLE_TYPE_CASE (BOOLEAN, boolean)
      SIMPLE_TYPE_CASE (INT,     int)
      SIMPLE_TYPE_CASE (UINT,    uint)
      SIMPLE_TYPE_CASE (LONG,    long)
      SIMPLE_TYPE_CASE (ULONG,   ulong)
      SIMPLE_TYPE_CASE (INT64,   int64)
      SIMPLE_TYPE_CASE (UINT64,  uint64)
      SIMPLE_TYPE_CASE (FLOAT,   float)
      SIMPLE_TYPE_CASE (DOUBLE,  double)
      SIMPLE_TYPE_CASE (OBJECT,  object)
      SIMPLE_TYPE_CASE (STRING,  string)

#undef SIMPLE_TYPE_CASE

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
test_object_set_property (GObject      *object,
			  guint         property_id,
			  const GValue *value,
			  GParamSpec   *pspec)
{
  TestObject *self = TEST_OBJECT (object);

  switch (property_id) 
    {
#define SIMPLE_TYPE_CASE(T,t) \
      case PROP_##T: \
        self->prop_##t = g_value_get_##t (value); \
        break;

      SIMPLE_TYPE_CASE (CHAR,    char)
      SIMPLE_TYPE_CASE (UCHAR,   uchar)
      SIMPLE_TYPE_CASE (BOOLEAN, boolean)
      SIMPLE_TYPE_CASE (INT,     int)
      SIMPLE_TYPE_CASE (UINT,    uint)
      SIMPLE_TYPE_CASE (LONG,    long)
      SIMPLE_TYPE_CASE (ULONG,   ulong)
      SIMPLE_TYPE_CASE (INT64,   int64)
      SIMPLE_TYPE_CASE (UINT64,  uint64)
      SIMPLE_TYPE_CASE (FLOAT,   float)
      SIMPLE_TYPE_CASE (DOUBLE,  double)

#undef SIMPLE_TYPE_CASE

      case PROP_OBJECT:
	{
	  gpointer object;
	  object = g_value_dup_object (value);
	  g_return_if_fail (object == NULL || IS_TEST_OBJECT (object));
	  self->prop_object = (TestObject *) object;
	}
	break;
      case PROP_STRING:
	self->prop_string = g_value_dup_string (value);
	break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
test_object_finalize (GObject *self_object)
{ 
  TestObject *self = TEST_OBJECT (self_object);
  
  if (self->prop_string)
    g_free (self->prop_string);
  if (self->prop_object)
    g_object_unref (self->prop_object);

  (*test_object_parent_class->finalize) (self_object);
}

static void
test_object_class_init (GObjectClass *object_class)
{ 
  test_object_parent_class = g_type_class_peek_parent (object_class);
  
  object_class->get_property = test_object_get_property;
  object_class->set_property = test_object_set_property;
  object_class->finalize = test_object_finalize;

  g_object_class_install_property (
    object_class,
    PROP_BOOLEAN,
    g_param_spec_boolean ("boolean",
			  "boolean",
			  "boolean",
			  FALSE,
			  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY)
  );

#define NUMERIC_TYPE_CASE(T,t,min,max,def) \
  g_object_class_install_property ( \
    object_class, \
    PROP_##T, \
    g_param_spec_##t (#t, #t, #t, min, max, def, \
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY) \
  );

  NUMERIC_TYPE_CASE (CHAR, char, -128, 127, 0)
  NUMERIC_TYPE_CASE (UCHAR, uchar, 0, 255, 0)

#define NUMERIC_TYPE_CASE_SIGNED(T,t) \
  NUMERIC_TYPE_CASE (T, t, G_MIN##T, G_MAX##T, 0)

#define NUMERIC_TYPE_CASE_UNSIGNED(T,t) \
  NUMERIC_TYPE_CASE (T, t, 0, G_MAX##T, 0)

#define NUMERIC_TYPE_CASE_FLOAT(T,t) \
  NUMERIC_TYPE_CASE (T, t, -G_MAX##T, G_MAX##T, 0.0)

  NUMERIC_TYPE_CASE_SIGNED   (INT,     int)
  NUMERIC_TYPE_CASE_UNSIGNED (UINT,    uint)
  NUMERIC_TYPE_CASE_SIGNED   (LONG,    long)
  NUMERIC_TYPE_CASE_UNSIGNED (ULONG,   ulong)
  NUMERIC_TYPE_CASE_SIGNED   (INT64,   int64)
  NUMERIC_TYPE_CASE_UNSIGNED (UINT64,  uint64)
  NUMERIC_TYPE_CASE_FLOAT    (FLOAT,   float)
  NUMERIC_TYPE_CASE_FLOAT    (DOUBLE,  double)

#undef NUMERIC_TYPE_CASE
#undef NUMERIC_TYPE_CASE_SIGNED
#undef NUMERIC_TYPE_CASE_UNSIGNED

  g_object_class_install_property (
    object_class,
    PROP_STRING,
    g_param_spec_string ("string",
                         "string",
                         "string",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY)
  );
  g_object_class_install_property (
    object_class,
    PROP_OBJECT,
    g_param_spec_object ("object",
			 "object",
			 "object",
			 TEST_OBJECT_TYPE,
			 G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY)
  );
}

GType
test_object_get_type (void)
{ 
  static GType type = 0;
  if (!type)
    {
      static const GTypeInfo type_info =
        {
          sizeof (TestObjectClass),
          (GBaseInitFunc) NULL,
          (GBaseFinalizeFunc) NULL,
          (GClassInitFunc) test_object_class_init,
          NULL,           /* class_finalize */
          NULL,           /* class_data */
          sizeof (TestObject),
          0,              /* n_preallocs */
          (GInstanceInitFunc) NULL,
          NULL            /* value_table */
        };
      type = g_type_register_static (G_TYPE_OBJECT,
				     "TestObject",
				     &type_info,
				     0);
    }
  return type;
}
