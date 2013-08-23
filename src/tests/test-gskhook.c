#include "../gskhook.h"
#include "../gskinit.h"

typedef struct _TestHookObject TestHookObject;
typedef struct _TestHookObjectClass TestHookObjectClass;
GType test_hook_object_get_type(void) G_GNUC_CONST;
#define TEST_TYPE_HOOK_OBJECT			(test_hook_object_get_type ())
#define TEST_HOOK_OBJECT(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), TEST_TYPE_HOOK_OBJECT, TestHookObject))
#define TEST_HOOK_OBJECT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), TEST_TYPE_HOOK_OBJECT, TestHookObjectClass))
#define TEST_HOOK_OBJECT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), TEST_TYPE_HOOK_OBJECT, TestHookObjectClass))
#define TEST_IS_HOOK_OBJECT(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TEST_TYPE_HOOK_OBJECT))
#define TEST_IS_HOOK_OBJECT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), TEST_TYPE_HOOK_OBJECT))

struct _TestHookObjectClass 
{
  GObjectClass object_class;
  void     (*set_poll) (TestHookObject      *object,
		        gboolean             do_polling);
  void     (*shutdown) (TestHookObject      *object);
};
struct _TestHookObject 
{
  GObject      object;
  GskHook      hook;
  gboolean set_polling_true;
  gboolean has_shutdown;

  /* incremented by our trigger function */
  guint trigger_count;

  /* used as a gauge of when the trigger function return FALSE */
  guint max_trigger_count;
};

static GObjectClass *parent_class = NULL;

static void
test_hook_object_set_poll (TestHookObject      *object,
		           gboolean             do_polling)
{
  g_assert (!object->has_shutdown);
  object->set_polling_true = do_polling;
}

static void
test_hook_object_shutdown (TestHookObject      *object)
{
  g_assert (!object->has_shutdown);
  object->has_shutdown = TRUE;
}

static void
test_hook_object_init (TestHookObject *hook_object)
{
  GSK_HOOK_INIT (hook_object,
		 TestHookObject,
		 hook,
		 GSK_HOOK_IS_AVAILABLE,
		 set_poll,
		 shutdown);
  hook_object->max_trigger_count = 1;
}

static void
test_hook_object_class_init (TestHookObjectClass *class)
{
  parent_class = g_type_class_peek_parent (class);
  class->set_poll = test_hook_object_set_poll;
  class->shutdown = test_hook_object_shutdown;
}

GType test_hook_object_get_type()
{
  static GType hook_object_type = 0;
  if (!hook_object_type)
    {
      static const GTypeInfo hook_object_info =
      {
	sizeof(TestHookObjectClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) test_hook_object_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (TestHookObject),
	0,		/* n_preallocs */
	(GInstanceInitFunc) test_hook_object_init,
	NULL		/* value_table */
      };
      hook_object_type = g_type_register_static (G_TYPE_OBJECT,
                                                  "TestHookObject",
						  &hook_object_info, 0);
    }
  return hook_object_type;
}

#define TEST_HOOK_OBJECT_HOOK(test_object)  (&(TEST_HOOK_OBJECT (test_object))->hook)

static gboolean handle_trigger (TestHookObject *object, gpointer data)
{
  g_assert (object->trigger_count < object->max_trigger_count);
  object->trigger_count++;
  return object->trigger_count < object->max_trigger_count;
}

int main (int argc, char **argv)
{
  TestHookObject *object1;
  
  gsk_init_without_threads (&argc, &argv);
  
  object1 = g_object_new (TEST_TYPE_HOOK_OBJECT, NULL);
  g_assert (TEST_HOOK_OBJECT_HOOK (object1) == &object1->hook);
  g_assert (GSK_HOOK_TEST_IS_AVAILABLE (TEST_HOOK_OBJECT_HOOK (object1)));
  g_assert (!GSK_HOOK_TEST_IDLE_NOTIFY (TEST_HOOK_OBJECT_HOOK (object1)));
  g_assert (!GSK_HOOK_TEST_NEVER_BLOCKS (TEST_HOOK_OBJECT_HOOK (object1)));
  g_assert (!gsk_hook_is_trapped (TEST_HOOK_OBJECT_HOOK (object1)));
  g_assert (object1->trigger_count == 0);
  g_assert (object1->max_trigger_count == 1);
  object1->max_trigger_count = 2;
  gsk_hook_trap (TEST_HOOK_OBJECT_HOOK (object1), (GskHookFunc) handle_trigger, NULL, NULL, NULL);
  g_assert (gsk_hook_is_trapped (TEST_HOOK_OBJECT_HOOK (object1)));
  gsk_hook_notify (TEST_HOOK_OBJECT_HOOK (object1));
  g_assert (gsk_hook_is_trapped (TEST_HOOK_OBJECT_HOOK (object1)));
  g_assert (object1->trigger_count == 1);
  g_assert (object1->max_trigger_count == 2);
  gsk_hook_notify (TEST_HOOK_OBJECT_HOOK (object1));
  g_assert (!gsk_hook_is_trapped (TEST_HOOK_OBJECT_HOOK (object1)));

  g_object_unref (object1);
  return 0;
}
