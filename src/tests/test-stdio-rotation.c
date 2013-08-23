#include "../gsklog.h"
#include "../gskmain.h"
#include "../gskmainloop.h"
#include "../gskinit.h"
#include "../gskutils.h"

static guint rotation_period = 3600;
static guint print_period = 1;
static gboolean use_localtime = FALSE;
static const char *output_template = "output.%s";
static gboolean print_to_stderr = FALSE;

static GOptionEntry op_entries[] =
{
  { "rotation-period", 'r', 0, G_OPTION_ARG_INT, &rotation_period,
    "period with which to rotate the logfile", "SECONDS" },
  { "print-period", 'p', 0, G_OPTION_ARG_INT, &print_period,
    "period with which to print a message", "SECONDS" },
  { "output-template", 'o', 0, G_OPTION_ARG_STRING, &output_template,
    "strftime-compatible output filename format string", "FNAME_TEMPLATE" },
  { "use-localtime", 'l', 0, G_OPTION_ARG_NONE, &use_localtime,
    "use localtime in strftime formatting", NULL },
  { "print-stderr", 'e', 0, G_OPTION_ARG_NONE, &print_to_stderr,
    "use stderr (instead of stdout)", NULL },
  { NULL, 0, 0, 0, NULL, NULL, NULL }
};

  static guint message_count = 0;
static gboolean
handle_timeout (gpointer data)
{
  if (print_to_stderr)
    g_printerr ("message %u\n", message_count);
  else
    g_print ("message %u\n", message_count);
  message_count++;
  return TRUE;
}

int main(int argc, char **argv)
{
  GOptionContext *op_context;
  GError *error = NULL;

  gsk_init_without_threads (&argc, &argv);

  op_context = g_option_context_new (NULL);
  g_option_context_set_summary (op_context, "log tester");
  g_option_context_add_main_entries (op_context, op_entries, NULL);
  if (!g_option_context_parse (op_context, &argc, &argv, &error))
    gsk_fatal_user_error ("error parsing command-line options: %s", error->message);
  g_option_context_free (op_context);

  gsk_log_rotate_stdio_logs (output_template, use_localtime, rotation_period);
  gsk_main_loop_add_timer (gsk_main_loop_default (),
                           handle_timeout, NULL, NULL,
                           print_period * 1000, print_period * 1000);
  return gsk_main_run ();
}
