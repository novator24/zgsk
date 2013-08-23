#ifndef __TEST_OBJECT_H_
#define __TEST_OBJECT_H_

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef GObjectClass       TestObjectClass;
typedef struct _TestObject TestObject;

GType test_object_get_type (void) G_GNUC_CONST;

#define TEST_OBJECT_TYPE (test_object_get_type ())
#define TEST_OBJECT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), TEST_OBJECT_TYPE, TestObject))
#define IS_TEST_OBJECT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TEST_OBJECT_TYPE))

struct _TestObject
{ 
  GObject object;
  
  gchar       prop_char;
  guchar      prop_uchar;
  gboolean    prop_boolean;
  gint        prop_int;
  guint       prop_uint;
  glong       prop_long;
  gulong      prop_ulong;
  gint64      prop_int64;
  guint64     prop_uint64;
  gfloat      prop_float;
  gdouble     prop_double;
  gchar      *prop_string;
  TestObject *prop_object;
};

TestObject * test_object_random (double child_probability);

gboolean     test_objects_equal (const TestObject *a,
				 const TestObject *b,
				 gboolean          verbose);

G_END_DECLS

#endif
