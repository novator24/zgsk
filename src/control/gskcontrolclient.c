#include "gskcontrolclient.h"
#include "../http/gskhttpclient.h"
#include "../gskstreamclient.h"
#include "../gskmemory.h"
#include "../gskstdio.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

struct _GskControlClient
{
  GskSocketAddress *address;

  char *prompt;
  guint cmd_no;

  guint add_newlines_as_needed : 1;

  GskControlClientErrorFunc error_func;
  gpointer error_func_data;

  int last_argc;
  char **last_argv;
};

static void
abort_on_error (GError *error, gpointer data)
{
  GskControlClient *cc = data;
  g_error ("control-client error: %s: %s",
           gsk_socket_address_to_string (cc->address),
           error->message);
}

/**
 * gsk_control_client_new:
 * @server: the location of the server to contact.
 *
 * Allocates a new control client.
 *
 * This may invoke the main-loop to answer a few questions.
 *
 * returns: the newly allocated control client.
 */
GskControlClient *
gsk_control_client_new (GskSocketAddress *server)
{
  GskControlClient *cc = g_new (GskControlClient, 1);
  cc->address = server ? g_object_ref (server) : NULL;
  cc->prompt = NULL;
  cc->cmd_no = 1;
  cc->add_newlines_as_needed = 1;
  cc->error_func = abort_on_error;
  cc->error_func_data = cc;
  return cc;
}

/**
 * gsk_control_client_set_flag:
 * @cc:
 * @flag:
 * @value:
 *
 * not used yet.
 */
void
gsk_control_client_set_flag (GskControlClient *cc,
                             GskControlClientFlag flag,
                             gboolean             value)
{
  switch (flag)
    {
    case GSK_CONTROL_CLIENT_ADD_NEWLINES_AS_NEEDED:
      cc->add_newlines_as_needed = value ? 1 : 0;
      break;
    default:
      g_return_if_reached ();
    }
}

/**
 * gsk_control_client_get_flag:
 * @cc:
 * @flag:
 * returns:
 *
 * not used yet.
 */
gboolean
gsk_control_client_get_flag (GskControlClient *cc,
                             GskControlClientFlag flag)
{
  switch (flag)
    {
    case GSK_CONTROL_CLIENT_ADD_NEWLINES_AS_NEEDED:
      return cc->add_newlines_as_needed;
    default:
      g_return_val_if_reached (FALSE);
    }
}

/**
 * gsk_control_client_set_prompt:
 * @cc: the control client to affect.
 * @prompt_format: format string.
 *
 * Set the prompt format string.
 */
void
gsk_control_client_set_prompt(GskControlClient *cc,
                              const char       *prompt_format)
{
  char *to_free = cc->prompt;
  cc->prompt = g_strdup (prompt_format);
  g_free (to_free);
}

typedef struct _GetServerFileStatus GetServerFileStatus;
struct _GetServerFileStatus
{
  gboolean done;
  GError *error;
  gpointer *contents_out;
  gsize *length_out;
};

static void
buffer_callback_get_server_file (GskBuffer *buffer, gpointer data)
{
  GetServerFileStatus *fs = data;
  *(fs->length_out) = buffer->size;
  *(fs->contents_out) = g_malloc (buffer->size);
  gsk_buffer_read (buffer, *(fs->contents_out), buffer->size);
  fs->done = TRUE;
}

static void
handle_get_server_file_response (GskHttpRequest  *request,
                                 GskHttpResponse *response,
                                 GskStream       *input,
                                 gpointer         hook_data)
{
  GetServerFileStatus *fs = hook_data;
  GskStream *stream;
  if (response->status_code != GSK_HTTP_STATUS_OK)
    {
      fs->error = g_error_new (GSK_G_ERROR_DOMAIN,
                               GSK_ERROR_HTTP_NOT_FOUND,
                               "got status code %u from server",
                               response->status_code);
      fs->done = TRUE;
      return;
    }
  stream = gsk_memory_buffer_sink_new (buffer_callback_get_server_file, fs, NULL);
  gsk_stream_attach (input, stream, NULL);
  g_object_unref (stream);
}

static gboolean
get_server_file (GskControlClient *cc,
                 const char       *path,
                 gpointer         *contents_out,
                 gsize            *length_out,
                 GError          **error)
{
  GskHttpRequest *request;
  GetServerFileStatus status = { FALSE, NULL, contents_out, length_out };
  GskHttpClient *http_client;
  GskStream *client_stream;
  char *p = g_strdup_printf ("/run.txt?command=cat&arg1=%s", path);
  http_client = gsk_http_client_new ();
  request = gsk_http_request_new (GSK_HTTP_VERB_GET, p);
  g_free (p);
  gsk_http_client_request (http_client, request, NULL,
                           handle_get_server_file_response, &status,
                           NULL);

  client_stream = gsk_stream_new_connecting (cc->address, error);
  if (client_stream == NULL)
    return FALSE;
  if (!gsk_stream_attach_pair (GSK_STREAM (http_client), client_stream, error))
    return FALSE;
  g_object_unref (request);
  g_object_unref (client_stream);
  g_object_unref (http_client);

  while (!status.done)
    gsk_main_loop_run (gsk_main_loop_default (), -1, NULL);
  if (status.error)
    {
      *error = status.error;
      return FALSE;
    }
  return TRUE;
}

/**
 * gsk_control_client_get_prompt:
 * @cc: the control client whose prompt should be obtained.
 * returns: the new prompt string.
 *
 * Get a newly allocated prompt string.
 * This is used by the user-interface program;
 * typically readline() is employed to use this prompt.
 */
char *
gsk_control_client_get_prompt_string(GskControlClient *cc)
{
  GString *rv;
  guint i;
  if (cc->prompt == NULL)
    {
      /* request prompt string from server. */
      gpointer content; gsize len;
      GError *error = NULL;
      if (get_server_file (cc, "/server/client-prompt", &content, &len, &error))
        {
          cc->prompt = g_strndup (content, len);
          g_free (content);
        }
      else
        {
          g_message ("error: %s", error->message);
          g_error_free (error);
          cc->prompt = g_strdup ("");
        }
    }
  rv = g_string_new ("");
  for (i = 0; cc->prompt[i] != 0; i++)
    {
      if (cc->prompt[i] == '%')
        {
          switch (cc->prompt[++i])
            {
            case 'n':
              g_string_append_printf (rv, "%u", cc->cmd_no);
              break;
            case '%':
              g_string_append_c (rv, '%');
              break;
            default:
              /* discard */
              g_warning ("bad prompt string '%s'", cc->prompt);
              break;
            }
        }
      else
        g_string_append_c (rv, cc->prompt[i]);
    }
  return g_string_free (rv, FALSE);
}

static void
set_server_address (GskControlClient *client,
                    const char       *path)
{
  if (client->address)
    {
      g_warning ("already had address");
      return;
    }
  client->address = gsk_socket_address_local_new (path);
}

static void
run_script (GskControlClient *cc,
            const char       *filename)
{
  FILE *fp = fopen (filename, "r");
  guint old_cmd_no;
  char *line;
  if (fp == NULL)
    {
      GError *error = g_error_new (GSK_G_ERROR_DOMAIN,
                                   gsk_error_code_from_errno (errno),
                                   "error opening file: %s",
                                   g_strerror (errno));
      if (cc->error_func)
        cc->error_func (error, cc->error_func_data);
      g_error_free (error);
      return;
    }
  old_cmd_no = cc->cmd_no;
  while ((line=gsk_stdio_readline (fp)) != NULL)
    {
      g_strstrip (line);
      if (line[0] == 0 || line[0] == '#')
        {
          g_free (line);
          continue;
        }
      gsk_control_client_run_command_line (cc, line);
      g_free (line);
    }
  cc->cmd_no = old_cmd_no;
  fclose (fp);
}

#define TEST_OPTION_FLAG(flags, opt) \
  ((flags&GSK_CONTROL_CLIENT_OPTIONS_##opt) == GSK_CONTROL_CLIENT_OPTIONS_##opt)
/**
 * gsk_control_client_parse_command_line_args:
 * @cc: the control client to affect.
 * @argc_inout: a reference to the 'argc' which was passed into main.
 * @argv_inout: a reference to the 'argv' which was passed into main.
 * @flags: bitwise-OR'd flags telling which command-line arguments to parse.
 * returns: whether to parse commands from stdin.
 *
 * Parse standard command-line options.
 *
 * During this parsing, some remote commands may be run.
 * For example -e flags cause instructions to be executed.
 * Therefore, this may reinvoke the mainloop.
 */
gboolean
gsk_control_client_parse_command_line_args (GskControlClient *cc,
                                            int              *argc_inout,
                                            char           ***argv_inout,
                                            GskControlClientOptionFlags flags)
{
  int i;
  gboolean user_specified_interactive = FALSE;
  gboolean ran_commands = FALSE;
#define SWALLOW_ARG(count)              \
    { memmove ((*argv_inout) + i,       \
               (*argv_inout) + (i + count), \
               sizeof(char*) * (*argc_inout + 1 - (i + count))); \
      *argc_inout -= count; }

  for (i = 1; i < *argc_inout; )
    {
      const char *arg = (*argv_inout)[i];
      if (TEST_OPTION_FLAG (flags, RANDOM))
        {
          if (strcmp (arg, "--exact-newlines") == 0)
            {
              SWALLOW_ARG(1);
              cc->add_newlines_as_needed = 0;
              continue;
            }
        }
      if (TEST_OPTION_FLAG (flags, INTERACTIVE))
        {
          if (strcmp (arg, "-i") == 0
           || strcmp (arg, "--interactive") == 0)
            {
              user_specified_interactive = TRUE;
              continue;
            }
        }
      if (TEST_OPTION_FLAG (flags, INLINE_COMMAND))
        {
          if (strcmp (arg, "-e") == 0)
            {
              const char *cmd = (*argv_inout)[i+1];
              SWALLOW_ARG (2);
              gsk_control_client_run_command_line (cc, cmd);
              ran_commands = TRUE;
              continue;
            }
        }
      if (TEST_OPTION_FLAG (flags, SOCKET))
        {
          if (strcmp (arg, "--socket") == 0)
            {
              const char *path = (*argv_inout)[i+1];
              SWALLOW_ARG (2);
              set_server_address (cc, path);
              continue;
            }
          if (g_str_has_prefix (arg, "--socket="))
            {
              const char *path = strchr (arg, '=') + 1;
              SWALLOW_ARG (1);
              set_server_address (cc, path);
              continue;
            }
        }
      if (TEST_OPTION_FLAG (flags, SCRIPTS))
        {
          if (strcmp (arg, "-f") == 0)
            {
              const char *file = (*argv_inout)[i+1];
              SWALLOW_ARG (2);
              run_script (cc, file);
              ran_commands = TRUE;
              continue;
            }
        }

      i++;
    }

#undef SWALLOW_ARG
  return !ran_commands || user_specified_interactive;
}

/**
 * gsk_control_client_print_command_line_usage:
 * @flags: bitwise-OR'd flags telling which command-line arguments to parse.
 *
 * Prints the command-line usage that the control-client class defines.
 */
void
gsk_control_client_print_command_line_usage(GskControlClientOptionFlags flags)
{
  if (TEST_OPTION_FLAG (flags, RANDOM))
    {
      g_print ("  --exact-newlines      Don't add newlines to output.\n");
    }
  if (TEST_OPTION_FLAG (flags, INTERACTIVE))
    {
      g_print ("  -i, --interactive     Force interaction.\n");
    }
  if (TEST_OPTION_FLAG (flags, INLINE_COMMAND))
    {
      g_print ("  -e 'CMD'              Run the command CMD.\n");
    }
  if (TEST_OPTION_FLAG (flags, SCRIPTS))
    {
      g_print ("  -f 'SCRIPT'           Run commands from the file SCRIPT.\n");
    }
  if (TEST_OPTION_FLAG (flags, SOCKET))
    {
      g_print ("  --socket=SOCKET       Connect to the server on the given\n"
               "                        unix-domain socket.\n");
    }
}
#undef TEST_OPTION_FLAG

gboolean gsk_control_client_has_address (GskControlClient *client)
{
  return client->address != NULL;
}

void
gsk_control_client_run_command_line (GskControlClient *cc,
                                     const char       *line)
{
  int argc;
  char **argv;
  GError *error = NULL;
  char *infile = NULL, *outfile = NULL;
  if (!g_shell_parse_argv (line, &argc, &argv, &error))
    {
      g_warning ("error parsing command-line: %s", error->message);
      g_error_free (error);
      return;
    }
  while (argc >= 3)
    {
      if (strcmp (argv[argc - 2], "<") == 0)
        {
          g_free (argv[argc - 2]);
          g_free (infile);
          argv[argc - 2] = NULL;
          infile = argv[argc - 1];
          argc -= 2;
        }
      else if (strcmp (argv[argc - 2], ">") == 0)
        {
          g_free (argv[argc - 2]);
          g_free (outfile);
          argv[argc - 2] = NULL;
          outfile = argv[argc - 1];
          argc -= 2;
        }
      else
        break;
    }
  gsk_control_client_run_command (cc, argv, infile, outfile);
  g_free (infile);
  g_free (outfile);
  g_strfreev (argv);
  return;
}

typedef struct _RequestInfo RequestInfo;
struct _RequestInfo
{
  GskStream *output;
  gboolean output_finalized;
};

static void
handle_output_finalized (RequestInfo *ri)
{
  ri->output_finalized = TRUE;
}

static void
handle_response (GskHttpRequest  *request,
                 GskHttpResponse *response,
                 GskStream       *input,
                 gpointer         hook_data)
{
  RequestInfo *ri = hook_data;
  GskStream *out = GSK_STREAM (ri->output);
#if 0
  if (response->status_code != GSK_HTTP_STATUS_OK)
    g_warning ("ERROR response from server");
#endif

  /* TODO: this doesn't add a newline if needed... */
  gsk_stream_attach (input, out, NULL);

  g_object_weak_ref (G_OBJECT (out), (GWeakNotify) handle_output_finalized, ri);
}

static void
request_info_unref_output_stream (gpointer ri)
{
  RequestInfo *request_info = ri;
  g_object_unref (request_info->output);
}

static void
append_url_quoted (GString *str, const char *t)
{
  while (*t)
    {
      const char *end = t;
      while (('a' <= *end && *end <= 'z')
          || ('A' <= *end && *end <= 'Z')
          || ('0' <= *end && *end <= '9')
          || *end == '-' || *end == '_' || *end == '/')
        end++;
      if (end > t)
        g_string_append_len (str, t, end - t);
      t = end;
      if (*t == 0)
        break;
      g_string_append_printf (str, "%%%02X", (guint8)*t);
      t++;
    }
}


void
gsk_control_client_run_command (GskControlClient *client,
                                char **command_and_args,
                                const char *in_filename,
                                const char *out_filename)
{
  GskStream *client_stream;
  GError *error = NULL;
  GskStream *in_stream, *out_stream;
  GskHttpClient *http_client;
  GString *path;
  guint i;
  GskHttpRequest *request;
  RequestInfo request_info;

  client_stream = gsk_stream_new_connecting (client->address, &error);
  if (client_stream == NULL)
    {
      if (client->error_func)
        (*client->error_func) (error, client->error_func_data);
      g_error_free (error);
      return;
    }
  http_client = gsk_http_client_new ();
  if (!gsk_stream_attach_pair (GSK_STREAM (http_client), client_stream, &error))
    {
      if (client->error_func)
        (*client->error_func) (error, client->error_func_data);
      g_error_free (error);
      return;
    }

  path = g_string_new ("/run.txt?");
  g_string_append (path, "command=");
  append_url_quoted (path, command_and_args[0]);

  for (i = 1; command_and_args[i]; i++)
    {
      char buf[256];
      g_string_append_c (path, '&');
      g_snprintf (buf, sizeof (buf), "arg%u", i);
      g_string_append (path, buf);
      g_string_append_c (path, '=');
      append_url_quoted (path, command_and_args[i]);
    }
  client->cmd_no++;

  request = gsk_http_request_new (in_filename ? GSK_HTTP_VERB_POST : GSK_HTTP_VERB_GET, path->str);
  if (in_filename)
    GSK_HTTP_HEADER (request)->transfer_encoding_type = GSK_HTTP_TRANSFER_ENCODING_CHUNKED;
  else
    GSK_HTTP_HEADER (request)->connection_type = GSK_HTTP_CONNECTION_CLOSE; /* HACK */
  g_string_free (path, TRUE);
  if (in_filename)
    {
      in_stream = gsk_stream_fd_new_read_file (in_filename, &error);
      if (in_stream == NULL)
        {
          if (client->error_func)
            (*client->error_func) (error, client->error_func_data);
          g_error_free (error);
          return;
        }
    }
  else
    in_stream = NULL;
  if (out_filename)
    {
      out_stream = gsk_stream_fd_new_write_file (out_filename, TRUE, TRUE, &error);
      if (out_stream == NULL)
        {
          if (client->error_func)
            (*client->error_func) (error, client->error_func_data);
          g_error_free (error);
          return;
        }
    }
  else
    {
      int fd = dup (STDOUT_FILENO);
      if (fd < 0)
        {
          g_error ("error running dup(1)");
        }
      out_stream = gsk_stream_fd_new_auto (fd);
    }
  request_info.output = out_stream;
  request_info.output_finalized = FALSE;
  gsk_http_client_request (http_client, request, in_stream,
                           handle_response, &request_info,
                           request_info_unref_output_stream);
  gsk_http_client_shutdown_when_done (http_client);
  g_object_unref (http_client);
  g_object_unref (client_stream);
  if (in_stream)
    g_object_unref (in_stream);
  g_object_unref (request);

  while (!request_info.output_finalized)
    {
      gsk_main_loop_run (gsk_main_loop_default (), -1, NULL);
    }
}

void
gsk_control_client_increment_command_number (GskControlClient *cc)
{
  ++(cc->cmd_no);
}

void
gsk_control_client_set_command_number (GskControlClient *cc, guint no)
{
  cc->cmd_no = no;
}
