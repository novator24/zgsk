#include "gskdebuglog.h"
#include <stdlib.h>
#include <string.h>

static void
usage (void)
{
  g_printerr ("usage: gsk-debug-alloc-tool LOGFILE OPERATION\n\n"
      "Analyze the output of gsk_debug_alloc_open_log().\n\n"
      "Operations:\n"
      "  usage-stats     Get min/max/average memory usage for your process.\n"
      );
  exit (1);
}

static void
do_usage_stats (GskDebugLog *log)
{
  guint64 usage = 0;
  guint64 max_usage = 0;
  GskDebugLogPacket *entry;
  while ((entry=gsk_debug_log_read (log)) != NULL)
    {
      if (entry->type == GSK_DEBUG_LOG_PACKET_MALLOC)
        {
          usage += entry->info.malloc.n_bytes;
          if (usage > max_usage)
            max_usage = usage;
        }
      else if (entry->type == GSK_DEBUG_LOG_PACKET_FREE)
        usage -= entry->info.free.n_bytes;
      gsk_debug_log_packet_free (entry);
    }
  g_print ("peak memory usage: %llu\n", max_usage);
}

int main(int argc, char **argv)
{
  const char *logfile = NULL;
  const char *op = NULL;
  GskDebugLog *log = NULL;
  guint i;
  void (*op_func) (GskDebugLog *) = NULL;
  GError *error = NULL;

  for (i = 1; i < (guint) argc; i++)
    {
      if (argv[i][0] == '-')
	{
	  usage ();
	}
      else if (logfile == NULL)
	logfile = argv[i];
      else if (op == NULL)
	{
	  op = argv[i];
	  if (strcmp (op, "usage-stats") == 0)
	    op_func = do_usage_stats;
          else
            g_error ("error: unknown operation %s", op);
	}
      else
	usage ();
    }
  if (op == NULL)
    usage ();

  log = gsk_debug_log_open (logfile, &error);
  if (log == NULL)
    g_error ("error opening debug log: %s", error->message);
  op_func (log);
  return 0;
}
