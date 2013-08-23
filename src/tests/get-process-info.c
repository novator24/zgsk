#include "../gskprocessinfo.h"
#include <stdlib.h>

int main (int argc, char **argv)
{
  guint i;
  if (argc == 1)
    g_error ("usage: get-process-info PID...");
  for (i = 1; i < (guint) argc; i++)
    {
      guint pid = atoi (argv[i]);
      GskProcessInfo *info;
      GError *error = NULL;
      if (pid == 0)
        g_error ("error: pid must be nonzero");
      info = gsk_process_info_get (pid, &error);
      if (info == NULL)
        g_error ("error getting info for %u: %s", pid, error->message);
      g_print ("Process %u.\n"
               "   Parent %u.\n"
               "   Process Group %u.\n"
               "   Session Id %u.\n"
               "   TTY %u.\n"
               , info->process_id,
                 info->parent_process_id,
                 info->process_group_id,
                 info->session_id,
                 info->tty_id);
      g_print ("   Self Runtime %lu user, %lu kernel.\n"
               "   Children Runtime: %lu user, %lu kernel\n"
               , info->user_runtime, info->kernel_runtime,
                 info->child_user_runtime, info->child_kernel_runtime);
      g_print ("   Nice level: %u.\n"
               "   Process State: %s.\n"
               "   Virtual Memory Size: %lu.\n"
               "   Resident Memory Size: %lu.\n"
               , info->nice,
                 gsk_process_info_state_name (info->state),
                 info->virtual_memory_size,
                 info->resident_memory_size);
      g_print ("   Exe Filename: %s.\n", info->exe_filename);
      g_print ("\n");
    }
  return 0;
}
