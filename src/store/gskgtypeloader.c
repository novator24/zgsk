#include <string.h>
#include <gmodule.h>
#include "../gskerror.h"
#include "gskgtypeloader.h"

typedef struct _TypeTest TypeTest;

struct _TypeTest
{
  gpointer test_data;
  GskTestTypeFunc test_func;
  TypeTest *next;
  GDestroyNotify destroy;
};

struct _GskGtypeLoader
{
  int ref_count;
  TypeTest *first_test;
  TypeTest *last_test;

  GskLoadTypeFunc load_type_func;
  gpointer load_type_data;
  GDestroyNotify load_type_destroy;
};

GskGtypeLoader *
gsk_gtype_loader_new (void)
{
  GskGtypeLoader *loader;

  loader = g_new0 (GskGtypeLoader, 1);
  loader->ref_count = 1;
  return loader;
}

static gboolean
test_type_is_a (GType type, gpointer is_a_type)
{
  return g_type_is_a (type, (GType) GPOINTER_TO_UINT (is_a_type));
}

void
gsk_gtype_loader_add_type (GskGtypeLoader *loader, GType type)
{
  gsk_gtype_loader_add_test (loader,
			     test_type_is_a,
			     GUINT_TO_POINTER (type),
			     NULL);
}

void
gsk_gtype_loader_add_test (GskGtypeLoader  *loader,
			   GskTestTypeFunc  type_func,
			   gpointer         test_data,
			   GDestroyNotify   test_destroy)
{
  TypeTest *test = g_new (TypeTest, 1);
  test->test_data = test_data;
  test->test_func = type_func;
  test->destroy = test_destroy;
  test->next = NULL;
  if (loader->last_test != NULL)
    loader->last_test->next = test;
  else
    loader->first_test = test;
  loader->last_test = test;
}

gboolean
gsk_gtype_loader_test_type (GskGtypeLoader *loader, GType type)
{
  TypeTest *test;
  for (test = loader->first_test; test != NULL; test = test->next)
    if ((*test->test_func) (type, test->test_data))
      return TRUE;
  return FALSE;
}

void
gsk_gtype_loader_set_loader (GskGtypeLoader      *loader,
			     GskLoadTypeFunc  load_type_func,
			     gpointer           load_type_data,
			     GDestroyNotify     load_type_destroy)
{
  if (loader->load_type_destroy != NULL)
    (*loader->load_type_destroy) (loader->load_type_data);
  loader->load_type_func = load_type_func;
  loader->load_type_data = load_type_data;
  loader->load_type_destroy = load_type_destroy;
}

GType
gsk_gtype_loader_load_type (GskGtypeLoader  *loader,
			  const char    *type_name,
			  GError       **error)
{
  if (loader->load_type_func == NULL)
    return g_type_from_name (type_name);
  else
    return (*loader->load_type_func) (type_name, loader->load_type_data, error);
}

void
gsk_gtype_loader_ref (GskGtypeLoader *loader)
{
  g_return_if_fail (loader->ref_count > 0);
  ++loader->ref_count;
}

void
gsk_gtype_loader_unref (GskGtypeLoader *loader)
{
  g_return_if_fail (loader->ref_count > 0);
  if (--loader->ref_count == 0)
    {
      while (loader->first_test != NULL)
	{
	  TypeTest *test = loader->first_test;

	  loader->first_test = test->next;
	  if (loader->first_test == NULL)
	    loader->last_test = NULL;

	  if (test->destroy)
	    (*test->destroy) (test->test_data);

	  g_free (test);
	}
      g_free (loader);
    }
}

static gboolean
return_true (GType type, gpointer data)
{
  (void) type;
  (void) data;
  return TRUE;
}

static GskGtypeLoader *default_config = NULL;

GskGtypeLoader *
gsk_gtype_loader_default (void)
{
  if (!default_config)
    {
      default_config = gsk_gtype_loader_new ();
      gsk_gtype_loader_add_test (default_config, return_true, NULL, NULL);
      gsk_gtype_loader_set_loader (default_config,
				 gsk_load_type_introspective,
				 NULL,
				 NULL);
    }
  gsk_gtype_loader_ref (default_config);
  return default_config;
}

GType
gsk_load_type_introspective (const char  *type_name,
			     gpointer     unused,
			     GError     **error)
{
  static gboolean self_inited = FALSE;
  static GModule *self_module = NULL;
  guint index = 0;
  GType type;
  GString *func_name;
  gpointer symbol;

  (void) unused;

  type = g_type_from_name (type_name);
  if (type != G_TYPE_INVALID)
    return type;

  /* Transform `GObject' into `g_object_get_type',
   * which should be a function that returns a GType,
   * if we're lucky...
   */
  func_name = g_string_new ("");
  while (type_name[index] != '\0')
    {
      if ('A' <= type_name[index] && type_name[index] <= 'Z')
	{
	  if (index > 0)
	    g_string_append_c (func_name, '_');
	  g_string_append_c (func_name, g_ascii_tolower (type_name[index]));
	}
      else
	g_string_append_c (func_name, type_name[index]);
      ++index;
    }
  g_string_append (func_name, "_get_type");

  if (!self_inited)
    {
      self_inited = TRUE;
      self_module = g_module_open (NULL, G_MODULE_BIND_LAZY);
      if (self_module == NULL)
	{
	  g_set_error (error,
		       GSK_G_ERROR_DOMAIN,
		       GSK_ERROR_UNKNOWN,
		       "g_module_open: %s",
		       g_module_error ());
	  goto DONE;
	}
    }
  if (g_module_symbol (self_module, func_name->str, &symbol))
    {
      GType (*func) () = (GType (*)()) symbol;
      const char *name;
      GTypeClass *klass;

      type = (*func) ();
      name = g_type_name (type);
      if (name == NULL)
	{
	  g_set_error (error,
		       GSK_G_ERROR_DOMAIN,
		       GSK_ERROR_UNKNOWN,
		       "called %s, didn't get a valid GType",
		       func_name->str);
	  type = G_TYPE_INVALID;
	  goto DONE;
	}
      if (strcmp (name, type_name) != 0)
	{
	  g_set_error (error,
		       GSK_G_ERROR_DOMAIN,
		       GSK_ERROR_UNKNOWN,
		       "called %s: got %s instead of %s",
		       func_name->str,
		       name,
		       type_name);
	  type = G_TYPE_INVALID;
	  goto DONE;
	}

      /* Sometimes the registrations in the class_init are vital. */
      klass = g_type_class_ref (type);
      g_type_class_unref (klass);
    }
  else
    {
      g_set_error (error,
		   GSK_G_ERROR_DOMAIN,
		   GSK_ERROR_UNKNOWN,
		   "couldn't find symbol %s: %s",
		   func_name->str,
		   g_module_error ());
    }

DONE:
  g_string_free (func_name, TRUE);
  return type;
}
