#include "../gskmodule.h"
#include <unistd.h>
#include <stdio.h>


int main()
{
  GError *error = NULL;
  char *name = g_strdup_printf ("test-gskmodule-%u-%d.c",
                                getpid(), g_random_int ());
  char *program_output = (char*)1;
  GskCompileContext *context;
  GskModule *module;
  unsigned (*func)(unsigned);
  FILE *fp = fopen (name, "w");
  if (fp == NULL)
    g_error ("error opening tmp: %s", error->message);
  fprintf (fp, "unsigned stupid_factorial(unsigned Z)\n"
               "{\n"
               "  unsigned i;\n"
               "  unsigned rv = 1;\n"
               "  for (i = 2; i <= Z; i++)\n"
               "    rv *= i;\n"
               "  return rv;\n"
               "}\n");
  fclose (fp);

  context = gsk_compile_context_new ();
  gsk_compile_context_set_verbose (context, TRUE);
  module = gsk_module_compile (context, 1, &name, 0,
                               TRUE, &program_output, &error);
  if (module == NULL)
    g_error ("error compiling module: %s\n%s",
             error->message,
             program_output);
  //g_message ("output of compilation: %s", program_output);
  g_free (program_output);

  func = gsk_module_lookup (module, "stupid_factorial");
  g_assert (func != NULL);
  g_assert (func(1) == 1);
  g_assert (func(2) == 2);
  g_assert (func(3) == 6);
  g_assert (func(4) == 24);
  g_assert (func(5) == 120);
  g_assert (func(6) == 720);
  gsk_module_unref (module);

  return 0;
}
