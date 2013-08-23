#include <string.h>
#include <stdlib.h>
#include "../gsktable.h"
#include "../gskstdio.h"
#include "../gskutils.h"
#include "../gskerror.h"
#include "../gskinit.h"

static const char *dir = NULL;
static const char *io_mode = "default";
static const char *op_mode = "replacement";
static const char *input = NULL;
static gboolean create = FALSE;
static gboolean existing = FALSE;
static gboolean no_close = FALSE;

static gboolean
print_op_modes_handler (const gchar    *option_name,
                        const gchar    *value,
                        gpointer        data,
                        GError        **error)
{
  g_printerr ("operation modes:\n");
  g_printerr ("  replacement         Setting a key's value twice\n"
              "                      overrides the old value.\n");
  exit (1);
}
static gboolean
print_io_modes_handler (const gchar    *option_name,
                        const gchar    *value,
                        gpointer        data,
                        GError        **error)
{
  g_printerr ("i/o modes:\n");
  g_printerr ("  cmd_prefixed_hex\n"
              "     Each line of input contains a command and\n"
              "     some keys and/or values.\n"
              "     The commands are:\n"
              "        Q [KEY]           Find the value for a key\n"
              "        A [KEY] [VALUE]   Insert the value for a key\n"
              "        !+ [KEY] [VALUE]  Assert db's entry for KEY is VALUE\n"
              "        !- [KEY]          Assert that there is no value for KEY\n");
  exit (1);
}


static GOptionEntry op_entries[] =
{
  { "dir", 'd', 0, G_OPTION_ARG_FILENAME, &dir,
    "directory to work in", "DIR" },
  { "input", 'i', 0, G_OPTION_ARG_FILENAME, &input,
    "input filename (instead of stdin)", "FILE" },
  { "io-mode", 'I', 0, G_OPTION_ARG_STRING, &io_mode,
    "type of i/o to use", "MODE" },
  { "op-mode", 'O', 0, G_OPTION_ARG_STRING, &op_mode,
    "type of database to implement", "MODE" },
  { "create", 0, 0, G_OPTION_ARG_NONE, &create,
    "create the table; abort if it exists", NULL },
  { "existing", 0, 0, G_OPTION_ARG_NONE, &existing,
    "open an existing table; abort if it does not exist", NULL },
  { "no-close", 0, 0, G_OPTION_ARG_NONE, &no_close,
    "do not cleanup when done", NULL },
  { "help-op-modes", 0, G_OPTION_FLAG_NO_ARG,
    G_OPTION_ARG_CALLBACK, print_op_modes_handler,
    "print the operation modes and exit", NULL },
  { "help-io-modes", 0, G_OPTION_FLAG_NO_ARG,
    G_OPTION_ARG_CALLBACK, print_io_modes_handler,
    "print the i/o modes and exit", NULL },
  { NULL, 0, 0, 0, NULL, NULL, NULL }
};

typedef struct {
  guint len;
  guint8 *data;
} Data;

static gboolean
parse_hex (const char *line,
           guint       n_data,
           Data       *data,
           GError    **error)
{
  guint n = 0;
  while (n < n_data)
    {
      while (g_ascii_isspace (*line))
        line++;
      if (*line == '.')
        {
          data[n].data = NULL;
          data[n].len = 0;
          n++;
          line++;
        }
      else if (*line == 0)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_PARSE,
                       "premature end-of-line");
          return FALSE;
        }
      else if (!g_ascii_isxdigit (*line))
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_PARSE,
                       "expected hex char, got %c", *line);
          return FALSE;
        }
      else
        {
          gsize tmp_size;
          if (n + 1 == n_data)
            {
              data[n].data = gsk_unescape_memory_hex (line, -1, &tmp_size, error);
              if (data[n].data == NULL)
                return FALSE;
              data[n].len = tmp_size;
              line = strchr (line, 0);
            }
          else
            {
              const char *end = line;
              while (*end && !g_ascii_isspace (*end))
                end++;
              data[n].data = gsk_unescape_memory_hex (line, end - line, &tmp_size, error);
              if (data[n].data == NULL)
                return FALSE;
              data[n].len = tmp_size;
              line = end;
            }
          if (data[n].data == NULL)
            return FALSE;
          n++;
        }
    }
  while (g_ascii_isspace (*line))
    line++;
  if (*line != 0)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_PARSE,
                   "trailing garbage at end-of-line");
      return FALSE;
    }
  return TRUE;
}

int main(int argc, char **argv)
{
  GOptionContext *context;
  GskTableOptions *options;
  GskTableNewFlags new_flags = GSK_TABLE_MAY_EXIST|GSK_TABLE_MAY_CREATE;
  GError *error = NULL;
  GskTable *table = NULL;
  FILE *input_fp;

  gsk_init_without_threads (&argc,&argv);

  context = g_option_context_new ("test-gsktable-prog");
  g_option_context_add_main_entries (context, op_entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    g_error ("error parsing options: %s", error->message);

  if (dir == NULL)
    g_error ("missing --dir=DIR");
  if (create && existing)
    g_error ("--create and --existing conflict");
  if (create)
    new_flags = GSK_TABLE_MAY_CREATE;
  else if (existing)
    new_flags = GSK_TABLE_MAY_EXIST;

  options = gsk_table_options_new ();
  if (strcmp (op_mode, "replacement") == 0)
    {
      gsk_table_options_set_replacement_semantics (options);
      if (strcmp (io_mode, "default") == 0)
        io_mode = "cmd_prefixed_hex";
    }
  else
    g_error ("unknown operations mode '%s'", op_mode);

  table = gsk_table_new (dir, options, new_flags, &error);
  if (table == NULL)
    g_error ("gsk_table_new() failed: %s", error->message);

  if (input == NULL || strcmp (input, "-") == 0)
    input_fp = stdin;
  else
    {
      input_fp = fopen (input, "rb");
      if (input_fp == NULL)
        g_error ("error opening %s for reading", input);
    }

  if (strcmp (io_mode, "cmd_prefixed_hex") == 0)
    {
      guint lineno = 1;
      char *line;
      while ((line=gsk_stdio_readline (input_fp)) != NULL)
        {
          Data data[2];
          switch (line[0])
            {
            case '#':
              break;
            case 'Q':           /* query */
              {
                gboolean found;
                guint value_len;
                guint8 *value_data;
                char *hex;
                if (!parse_hex (line+1, 1, data, &error))
                  g_error ("error line %u parsing query binary data: %s",
                           lineno, error->message);
                if (!gsk_table_query (table, data[0].len, data[0].data,
                                      &found, &value_len, &value_data,
                                      &error))
                  g_error ("gsk_table_query failed: %s", error->message);
                if (found)
                  {
                    hex = gsk_escape_memory_hex (value_data, value_len);
                    printf ("%s\n", hex);
                    g_free (hex);
                    g_free (value_data);
                  }
                else
                  printf ("NOT FOUND\n");
                g_free (data[0].data);
                break;
              }
            case 'A':           /* add */
              if (!parse_hex (line+1, 2, data, &error))
                g_error ("error line %u parsing add binary data: %s",
                         lineno, error->message);
              if (!gsk_table_add (table, data[0].len, data[0].data,
                                  data[1].len, data[1].data, &error))
                g_error ("gsk_table_add failed: %s", error->message);
              g_free (data[0].data);
              g_free (data[1].data);
              break;
            case '!':           /* assert */
              {
                gboolean found;
                guint value_len;
                guint8 *value_data;
                gboolean should_exist;
                if (line[1] == '+')
                  should_exist = TRUE;
                else if (line[1] == '-')
                  should_exist = FALSE;
                else
                  g_error ("assert '!' should be followed with + or -");
                if (!parse_hex (line+2, should_exist ? 2 : 1, data, &error))
                  g_error ("error line %u parsing assert binary data: %s",
                           lineno, error->message);
                if (!gsk_table_query (table, data[0].len, data[0].data,
                                      &found, &value_len, &value_data,
                                      &error))
                  g_error ("gsk_table_query failed: %s", error->message);
                if (should_exist && !found)
                  g_error ("assert: should exist, but not found (line %u)",
                           lineno);
                else if (!should_exist && found)
                  g_error ("assert: should not exist, but found (line %u)",
                           lineno);
                else if (found)
                  {
                    if (value_len != data[1].len)
                      g_error ("assert: expected length %u, got length %u (line %u)",
                               data[1].len, value_len, lineno);
                    if (memcmp (value_data, data[1].data, value_len) != 0)
                      g_error ("assert: right length, but wrong binary data (line %u)",
                               lineno);
                    g_free (data[1].data);
                    g_free (value_data);
                  }
                g_free (data[0].data);
                break;
              }
            case 0:
              break;
            default:
              g_error ("unexpected command '%c' in input", line[0]);
            }
          g_free (line);
        }
      lineno++;
    }
  else
    g_error ("unknown io_mode %s", io_mode);

  if (!no_close)
    gsk_table_destroy (table);

  return 0;
}
