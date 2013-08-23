#include "gskcontrolserver.h"
#include "../http/gskhttpcontent.h"
#include "../gskmemory.h"
#include "../gsklog.h"
#include <string.h>


typedef struct _DirNode DirNode;
typedef struct _FileNode FileNode;
typedef struct _Command Command;



struct _FileNode
{
  char *name;
  gboolean is_virtual;
  union
  {
    struct {
      gsize size;
      gpointer data;
    } raw;
    struct {
      GskControlServerVFileContentsFunc vfile_func;
      gpointer          vfile_data;
      GDestroyNotify    vfile_data_destroy;
    } virtual;
  } info;
};

struct _DirNode
{
  char *name;
  GPtrArray *dirs, *files;
};

struct _Command
{
  char *name;
  GskControlServerCommandFunc func;
  gpointer func_data;
};

struct _GskControlServer
{
  GskHttpContent *content;
  DirNode *root;
  GHashTable *commands_by_name;
  Command *default_command;
};

static void
generic_failure (GskHttpServer *server,
                 GskHttpRequest *request,
                 GskHttpStatus status,
                 const char *status_name,
                 const char *error_message)
{
  GskHttpResponse *response;
  GskStream *content;
  response = gsk_http_response_from_request (request, status, -1);
  gsk_http_header_set_content_type (response, "text");
  gsk_http_header_set_content_subtype (response, "plain");
  content = gsk_memory_source_new_printf ("ERROR!!! (%s)\n\n"
                                          "%s\n",
                                          status_name, error_message);
  gsk_http_server_respond (server, request, response, content);
  g_object_unref (content);
  g_object_unref (response);
}

static void
bad_request_respond (GskHttpServer *server,
                     GskHttpRequest *request,
                     const char     *error_message)
{
  generic_failure (server, request, GSK_HTTP_STATUS_BAD_REQUEST, "Bad Request", error_message);
}

static void
error_processing_request (GskHttpServer *server,
                     GskHttpRequest *request,
                     const char     *error_message)
{
  generic_failure (server, request, GSK_HTTP_STATUS_BAD_REQUEST, "Error Processing Request", error_message);
}


static GskHttpContentResult
handle_run_txt (GskHttpContent   *content,
                GskHttpContentHandler *handler,
                GskHttpServer  *server,
                GskHttpRequest *request,
                GskStream      *post_data,
                gpointer        data)
{
  GError *error = NULL;
  Command *command;
  guint i;
  GskControlServer *cserver = data;
  GskStream *output;

  /* parse path down to an argument list */
  char **cmd = gsk_http_parse_cgi_query_string (request->path, &error);
  char **argv;
  if (cmd == NULL)
    {
      /* serve error page */
      bad_request_respond (server, request, error->message);
      g_error_free (error);
      g_strfreev (cmd);
      return GSK_HTTP_CONTENT_OK;
    }
  if (cmd[0] == NULL)
    {
      /* serve error page */
      bad_request_respond (server, request, "no command found");
      g_error_free (error);
      g_strfreev (cmd);
      return GSK_HTTP_CONTENT_OK;
    }
  if (strcmp (cmd[0], "command") != 0)
    {
      bad_request_respond (server, request, "first CGI variable should be command");
      g_error_free (error);
      g_strfreev (cmd);
      return GSK_HTTP_CONTENT_OK;
    }
  for (i = 1; cmd[2*i] != NULL; i++)
    {
      char buf[32];
      g_snprintf(buf,sizeof(buf),"arg%u", i);
      if (strcmp (cmd[2*i],buf) != 0)
        {
          bad_request_respond (server, request, "argument key was not argN (for N a natural number)");
          g_error_free (error);
          g_strfreev (cmd);
          return GSK_HTTP_CONTENT_OK;
        }
    }
  argv = g_new (char *, i + 1);
  for (i = 0; cmd[2*i] != NULL; i++)
    {
      g_free (cmd[2*i]);
      argv[i] = cmd[2*i+1];
    }
  g_free (cmd);
  argv[i] = NULL;

  /* Now handle argv[] */
  command = g_hash_table_lookup (cserver->commands_by_name, argv[0]);
  if (command == NULL)
    {
      if (cserver->default_command == NULL)
        {
          error_processing_request (server, request,
                               "no command handler for given commands nor a default handler");
          g_strfreev (argv);
          return GSK_HTTP_CONTENT_OK;
        }
    }
  output = NULL;
  if (!command->func (argv, post_data, &output, command->func_data, &error))
    {
      if (error)
        {
          error_processing_request (server, request, error->message);
          g_error_free (error);
        }
      else
          error_processing_request (server, request,
                               "command failed but got no specific error message");
      g_strfreev (argv);
      return GSK_HTTP_CONTENT_OK;
    }
  {
    GskHttpResponse *response;
    response = gsk_http_response_from_request (request, GSK_HTTP_STATUS_OK, -1);
    gsk_http_header_set_content_type (response, "text");
    gsk_http_header_set_content_subtype (response, "plain");
    if (output == NULL)
      output = gsk_memory_source_static_string ("");
    gsk_http_server_respond (server, request, response, output);
    g_object_unref (response);
    g_object_unref (output);
  }
  g_strfreev (argv);
  return GSK_HTTP_CONTENT_OK;
}

static void
add_command_internal (GskControlServer *server,
                      const char       *command_name,
                      GskControlServerCommandFunc func,
                      gpointer          data)
{
  Command *command;
  g_return_if_fail (g_hash_table_lookup (server->commands_by_name, command_name) == NULL);
  command = g_new (Command, 1);
  command->name = g_strdup (command_name);
  command->func = func;
  command->func_data = data;
  g_hash_table_insert (server->commands_by_name, command->name, command);
}

static char **
path_split (const char *path)
{
  char **split = g_strsplit (path, "/", 0);
  char **out = split;
  char **split_at = split;
  while (*split_at)
    {
      if (**split_at)
        *out++ = *split_at;
      else
        g_free (*split_at);
      split_at++;
    }
  *out = NULL;
  return split;
}

static DirNode *
maybe_get_dir_node (GskControlServer *server,
                    char            **path)
{
  char **at;
  DirNode *node = server->root;
  guint i;
  for (at = path; *at; at++)
    {
      DirNode *sub = NULL;
      if (node->dirs != NULL)
        for (i = 0; i < node->dirs->len; i++)
          {
            DirNode *dn = node->dirs->pdata[i];
            if (strcmp (dn->name, at[0]) == 0)
              {
                sub = dn;
                break;
              }
          }
      if (sub == NULL)
        return NULL;
      node = sub;
    }
  return node;
}

static void
append_command_star_to_str (gpointer key, gpointer value,
                            gpointer data)
{
  g_string_append_printf ((GString *) data, "%s*\n", (char*)key);
}

static gboolean
command_handler__ls (char **argv,
                     GskStream *input,
                     GskStream **output,
                     gpointer data,
                     GError **error)
{
  GskControlServer *cserver = data;
  char **path_pieces;
  DirNode *node;
  GString *rs;
  guint i;
  if (argv[1] == NULL)
    path_pieces = g_new0 (char *, 1);
  else if (argv[2] == NULL)
    path_pieces = path_split (argv[1]);
  else
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_INVALID_ARGUMENT,
                   "'ls' command takes just one argument");
      g_strfreev (path_pieces);
      return FALSE;
    }
  if (path_pieces[0] && path_pieces[1] == NULL
      && strcmp (path_pieces[0], "bin") == 0)
    {
      /* return list of commands */
      rs = g_string_new ("");
      g_hash_table_foreach (cserver->commands_by_name,
                            append_command_star_to_str,
                            rs);
      goto return_string;
    }

  node = maybe_get_dir_node (cserver, path_pieces);
  if (node == NULL)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_FILE_NOT_FOUND,
                   "directory %s not found",
                   argv[1] ? argv[1] : "/");
      g_strfreev (path_pieces);
      return FALSE;
    }

  /* return list of files and directories */
  rs = g_string_new ("");
  if (node->dirs)
    for (i = 0; i < node->dirs->len; i++)
      g_string_append_printf (rs, "%s/\n",
                              ((DirNode *)(node->dirs->pdata[i]))->name);
  if (node->files)
    for (i = 0; i < node->files->len; i++)
      g_string_append_printf (rs, "%s\n",
                              ((FileNode *)(node->files->pdata[i]))->name);

  if (path_pieces[0] == NULL)
    {
      /* add a few hack directories in */
      g_string_append_printf (rs, "bin/\n");
    }

return_string:
  {
    guint len = rs->len;
    char *mem = g_string_free (rs, FALSE);
    *output = gsk_memory_slab_source_new (mem, len, g_free, mem);
    g_strfreev (path_pieces);
    return TRUE;
  }
}

static gboolean
command_handler__cat(char **argv,
                     GskStream *input,
                     GskStream **output,
                     gpointer data,
                     GError **error)
{
  const guint8 *content;
  guint content_length;
  gpointer copy;
  GskControlServer *server = data;
  if (argv[1] == NULL || argv[2] != NULL)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_INVALID_ARGUMENT,
                   "'cat' command takes just one argument");
      return FALSE;
    }
  switch (gsk_control_server_stat (server, argv[1]))
    {
      case GSK_CONTROL_SERVER_FILE_RAW:
        if (!gsk_control_server_peek_raw_file (data, argv[1], &content, &content_length))
          {
            g_set_error (error, GSK_G_ERROR_DOMAIN,
                         GSK_ERROR_FILE_NOT_FOUND,
                         "cat: file %s not found", argv[1]);
            return FALSE;
          }
        copy = g_memdup (content, content_length);
        *output = gsk_memory_slab_source_new (copy, content_length, g_free, copy);
        return TRUE;
      case GSK_CONTROL_SERVER_FILE_VIRTUAL:
        {
          guint8 *content;
          guint length;
          GDestroyNotify release;
          gpointer release_data;
          if (!gsk_control_server_get_vfile_contents (server, argv[1],
                                                      &content, &length,
                                                      &release, &release_data))
            {
              g_set_error (error, GSK_G_ERROR_DOMAIN,
                           GSK_ERROR_FILE_NOT_FOUND,
                           "cat: error getting virtual file %s", argv[1]);
              return FALSE;
            }
          *output = gsk_memory_slab_source_new (content, length, release, release_data);
          return TRUE;
        }
      case GSK_CONTROL_SERVER_FILE_DIR:
        g_set_error (error, GSK_G_ERROR_DOMAIN,
                     GSK_ERROR_FILE_NOT_FOUND,
                     "cat: %s was a directory", argv[1]);
        return FALSE;
      case GSK_CONTROL_SERVER_FILE_NOT_EXIST:
        g_set_error (error, GSK_G_ERROR_DOMAIN,
                     GSK_ERROR_FILE_NOT_FOUND,
                     "cat: %s was not found", argv[1]);
        return FALSE;
      default:
        g_set_error (error, GSK_G_ERROR_DOMAIN,
                     GSK_ERROR_FILE_NOT_FOUND,
                     "cat: should not be reached (%s)", argv[1]);
        g_return_val_if_reached (FALSE);
    }
  return TRUE;
}

/**
 * gsk_control_server_new:
 * returns: a new GskControlServer.
 *
 * Allocate a new GskControlServer.
 *
 * It has a few builtin commands: 'ls', 'cat'.
 */
GskControlServer *
gsk_control_server_new (void)
{
  GskControlServer *server = g_new (GskControlServer, 1);
  GskHttpContentId id = GSK_HTTP_CONTENT_ID_INIT;
  GskHttpContentHandler *handler;
  server->content = gsk_http_content_new ();
  id.path_prefix = "/run.txt?";
  handler = gsk_http_content_handler_new (handle_run_txt, server, NULL);
  gsk_http_content_add_handler (server->content, &id, handler, GSK_HTTP_CONTENT_REPLACE);
  gsk_http_content_handler_unref (handler);
  server->root = g_new0 (DirNode, 1);
  server->default_command = NULL;
  server->commands_by_name = g_hash_table_new (g_str_hash, g_str_equal);

  add_command_internal (server, "ls", command_handler__ls, server);
  add_command_internal (server, "cat", command_handler__cat, server);
  return server;
}

/**
 * gsk_control_server_add_command:
 * @server: the server which should learn the command.
 * @command_name: the name of the new command.
 * @func: the function that implements the new command.
 * @data: opaque data to pass to 'func'.
 *
 * Add a command to the command server.
 * The command will show up when users 'ls /bin'
 * and they may also type it directly.
 *
 * The command must not conflict with any of the
 * reserved commands: ls, cat, set, get.
 */
static const char *reserved_commands[] =
{
  "cat",
  "get",
  "set",
  "ls",
  NULL
};

void
gsk_control_server_add_command (GskControlServer *server,
                                const char       *command_name,
                                GskControlServerCommandFunc func,
                                gpointer          data)
{
  guint i;
  for (i = 0; reserved_commands[i]; i++)
    if (strcmp (command_name, reserved_commands[i]) == 0)
      {
        g_warning ("command %s is reserved: you cannot add it",
                   command_name);
        return;
      }
  add_command_internal (server, command_name, func, data);
}

void
gsk_control_server_set_default_command
                               (GskControlServer *server,
                                GskControlServerCommandFunc func,
                                gpointer          data)
{
  Command *command;
  g_return_if_fail (server->default_command == NULL);
  command = g_new (Command, 1);
  command->name = NULL;
  command->func = func;
  command->func_data = data;
  server->default_command = command;
}

static DirNode *
server_mkdir (GskControlServer *server,
              char **split,
              GError    **error)
{
  DirNode *node = server->root;
  guint i, j;
  for (i = 0; split[i]; i++)
    {
      if (node->files)
        for (j = 0; j < node->files->len; j++)
          {
            FileNode *fn = node->files->pdata[j];
            if (strcmp (fn->name, split[i]) == 0)
              {
                g_set_error (error, GSK_G_ERROR_DOMAIN,
                             GSK_ERROR_IS_A_DIRECTORY,
                             "node %s already exists as a file",
                             split[i]);
                return NULL;
              }
          }
      if (node->dirs)
        {
          for (j = 0; j < node->dirs->len; j++)
            {
              DirNode *dn = node->dirs->pdata[j];
              if (strcmp (dn->name, split[i]) == 0)
                break;
            }
          if (j < node->dirs->len)
            {
              node = node->dirs->pdata[j];
              continue;
            }
        }
      else
        node->dirs = g_ptr_array_new ();
      {
        DirNode *new = g_new (DirNode, 1);
        new->name = g_strdup (split[i]);
        new->dirs = new->files = NULL;
        g_ptr_array_add (node->dirs, new);
        node = new;
      }
    }
  return node;
}

static void
destruct_file_node (FileNode *fn)
{
  g_free (fn->name);
  if (fn->is_virtual)
    fn->info.virtual.vfile_data_destroy (fn->info.virtual.vfile_data);
  else
    g_free (fn->info.raw.data);
}

static FileNode *
set_file_generic (GskControlServer *server,
                  const char       *path,
                  GError          **error)
{
  char **p = path_split (path);
  char **end;
  char *basename;
  DirNode *node;
  guint i;
  if (p[0] == NULL)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_INVALID_ARGUMENT,
                   "file name must have at least one component");
      g_strfreev (p);
      return NULL;
    }
  for (end = p; end[1]; end++)
    ;
  basename = *end;
  *end = NULL;
  node = server_mkdir (server, p, error);
  if (node == NULL)
    {
      g_strfreev (p);
      return NULL;
    }
  
  *end = basename;
  if (node->dirs)
    for (i = 0; i < node->dirs->len; i++)
      {
        DirNode *dn = node->dirs->pdata[i];
        if (strcmp (dn->name, basename) == 0)
          {
            g_set_error (error, GSK_G_ERROR_DOMAIN,
                         GSK_ERROR_IS_A_DIRECTORY,
                         "node %s already exists as a directory",
                         path);
            g_strfreev (p);
            return FALSE;
          }
      }
  if (node->files == NULL)
    node->files = g_ptr_array_new ();
  for (i = 0; i < node->files->len; i++)
    {
      FileNode *fn = node->files->pdata[i];
      if (strcmp (fn->name, basename) == 0)
        break;
    }
  if (i == node->files->len)
    {
      /* new file */
      FileNode *fn = g_new (FileNode, 1);
      fn->name = g_strdup (basename);
      g_ptr_array_add (node->files, fn);
      g_strfreev (p);
      return fn;
    }
  else
    {
      FileNode *fn = node->files->pdata[i];
      destruct_file_node (fn);
      g_strfreev (p);
      return fn;
    }
}

/**
 * gsk_control_server_set_file:
 * @server: the server which have the virtual file added.
 * @path: the virtual path for the file's location.
 * @content: the data for the file.
 * @content_length: the contents of the file.
 * @error: place to store the error if failure occurs.
 * 
 * Try to create a file in the virtual file-system
 * that clients will have access to.
 *
 * The clients can find the file using 'ls'
 * and read the file using 'cat'.
 *
 * This may replace an old copy of the file.
 *
 * returns: whether this command succeeded.
 * This can fail if it cannot make a virtual-directory.
 */
gboolean
gsk_control_server_set_file    (GskControlServer *server,
                                const char       *path,
                                const guint8     *content,
                                guint             content_length,
                                GError          **error)
{
  FileNode *fn;
  gpointer ccopy = g_memdup (content, content_length);
  fn = set_file_generic (server, path, error);
  if (fn == NULL)
    return FALSE;

  fn->is_virtual = FALSE;
  fn->info.raw.size = content_length;
  fn->info.raw.data = ccopy;
  return TRUE;
}

/**
 * gsk_control_server_set_vfile:
 * @server: the server which have the virtual file added.
 * @path: the virtual path for the file's location.
 * @content: the data for the file.
 * @content_length: the contents of the file.
 * @error: place to store the error if failure occurs.
 * 
 * Try to create a virtual-file in the virtual file-system
 * that clients will have access to.
 *
 * The clients can find the file using 'ls'
 * and read the file using 'cat'.
 *
 * This may replace an old copy of the file.
 */
gboolean
gsk_control_server_set_vfile   (GskControlServer *server,
                                const char       *path,
                                GskControlServerVFileContentsFunc vfile_func,
                                gpointer          vfile_data,
                                GDestroyNotify    vfile_data_destroy,
                                GError           **error)
{
  FileNode *fn;
  fn = set_file_generic (server, path, error);
  if (fn == NULL)
    return FALSE;

  fn->is_virtual = TRUE;
  fn->info.virtual.vfile_func = vfile_func;
  fn->info.virtual.vfile_data = vfile_data;
  fn->info.virtual.vfile_data_destroy = vfile_data_destroy;
  return TRUE;
}

static void
get_logfile_contents (gpointer  vfile_data,
                      guint    *len_out,
                      guint8   **contents_out,
                      GDestroyNotify *done_with_contents_out,
                      gpointer *done_with_contents_data_out)
{
  char *contents = gsk_log_ring_buffer_get (vfile_data);
  *len_out = strlen (contents);
  *contents_out = (guint8*) contents;
  *done_with_contents_data_out = contents;
  *done_with_contents_out = g_free;
}

/**
 * gsk_control_server_set_logfile_v:
 * @server: the server to which to add the virtual file.
 * @path: the virtual path for the file's location.
 * @ring_buffer_size: size in bytes of the log's ring-buffer.
 * @n_log_domains: number of log-domains to include in this ring-buffer.
 * @domains: domain/loglevel pairs.
 *
 * Make a virtual file in the control-server that will be a ring-buffer
 * of the most recent data all the log domains.
 */
void
gsk_control_server_set_logfile_v (GskControlServer *server,
                                  const char       *path,
                                  guint             ring_buffer_size,
                                  guint             n_log_domains,
                                  const GskControlServerLogDomain *domains)
{
  GskLogRingBuffer *ring_buffer = gsk_log_ring_buffer_new (ring_buffer_size);
  guint i;
  gsk_control_server_set_vfile (server, path,
                                get_logfile_contents, ring_buffer, NULL,
                                NULL);
  for (i = 0; i < n_log_domains; i++)
    gsk_log_trap_ring_buffer (domains[i].domain, domains[i].levels,
                              ring_buffer, NULL);
}

/**
 * gsk_control_server_set_logfile_v:
 * @server: the server to which to add the virtual file.
 * @path: the virtual path for the file's location.
 * @ring_buffer_size: size in bytes of the log's ring-buffer.
 * @first_log_domain: name of first log domain to put in ring-buffer.
 * @first_log_level_flags: first log domain's accepted log-levels.
 * @next_log_domain: name of second log domain to put in ring-buffer,
 * or NULL if there is only one log domain.
 * @...: continue listing domain/level-mask pairs.
 *
 * Make a virtual file in the control-server that will be a ring-buffer
 * of the most recent data from all the log domains.
 */
void
gsk_control_server_set_logfile   (GskControlServer *server,
                                  const char       *path,
                                  guint             ring_buffer_size,
                                  const char       *first_log_domain,
                                  GLogLevelFlags    first_log_level_flags,
                                  const char       *next_log_domain,
                                  ...)
{
  guint n_domains = 1;
  const char *d = next_log_domain;
  va_list args;
  GskControlServerLogDomain *domains;
  va_start (args, next_log_domain);
  while (d)
    {
      n_domains++;
      va_arg (args, GLogLevelFlags);
      d = va_arg (args, const char *);
    }
  va_end (args);

  domains = g_newa (GskControlServerLogDomain, n_domains);
  domains[0].domain = first_log_domain;
  domains[0].levels = first_log_level_flags;
  va_start (args, next_log_domain);
  d = next_log_domain;
  n_domains = 1;
  while (d)
    {
      domains[n_domains].domain = d;
      domains[n_domains].levels = va_arg (args, GLogLevelFlags);
      n_domains++;
      d = va_arg (args, const char *);
    }

  gsk_control_server_set_logfile_v (server, path, ring_buffer_size,
                                    n_domains, domains);
}



/**
 * gsk_control_server_delete_file:
 * @server: the server which have the virtual file removed from.
 * @path: the virtual path for the file's location.
 * @error: place to store the error if failure occurs.
 *
 * Remove a file from the virtual filesystem.
 *
 * Returns an error if the file does not exist or is a directory.
 */
gboolean
gsk_control_server_delete_file (GskControlServer *server,
                                const char       *path,
                                GError          **error)
{
  DirNode *node = server->root;
  FileNode *file_node;
  char **p = path_split (path);
  char **at;
  guint i;
  if (p[0] == NULL)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_INVALID_ARGUMENT,
                   "file name must have at least one component");
      g_strfreev (p);
      return FALSE;
    }
  for (at = p; at[1]; at++)
    {
      DirNode *sub = NULL;
      if (node->dirs != NULL)
        for (i = 0; i < node->dirs->len; i++)
          {
            DirNode *dn = node->dirs->pdata[i];
            if (strcmp (dn->name, at[0]) == 0)
              {
                sub = dn;
                break;
              }
          }
      if (sub == NULL)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN,
                       GSK_ERROR_FILE_NOT_FOUND,
                       "directory to %s did not exist", path);
          g_strfreev (p);
          return FALSE;
        }
      node = sub;
    }
  if (node->dirs)
    for (i = 0; i < node->dirs->len; i++)
      {
        DirNode *dn = node->dirs->pdata[i];
        if (strcmp (dn->name, *at) == 0)
          {
            g_set_error (error, GSK_G_ERROR_DOMAIN,
                         GSK_ERROR_IS_A_DIRECTORY,
                         "%s was a directory", path);
            g_strfreev (p);
            return FALSE;
          }
      }
  file_node = NULL;
  if (node->files != NULL)
    for (i = 0; i < node->files->len; i++)
      {
        FileNode *fn = node->files->pdata[i];
        if (strcmp (fn->name, *at) == 0)
          {
            file_node = fn;
            break;
          }
      }
  if (file_node == NULL)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_FILE_NOT_FOUND,
                   "%s was not found", path);
      g_strfreev (p);
      return FALSE;
    }
  g_ptr_array_remove_index_fast (node->files, i);
  destruct_file_node (file_node);
  g_free (file_node);
  g_strfreev (p);
  return TRUE;
}
/**
 * gsk_control_server_delete_directory:
 * @server: the server which have the virtual file removed from.
 * @path: the virtual path for the file's location.
 * @error: place to store the error if failure occurs.
 *
 * Remove a file from the virtual filesystem.
 *
 * Returns an error if the file does not exist or is a directory.
 */
static void delete_dirnode_recursively (DirNode *dir_node);
gboolean
gsk_control_server_delete_directory (GskControlServer *server,
                                     const char       *path,
                                     GError          **error)
{
  DirNode *node = server->root;
  char **p = path_split (path);
  char **at;
  guint i;
  if (p[0] == NULL)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_INVALID_ARGUMENT,
                   "file name must have at least one component");
      g_strfreev (p);
      return FALSE;
    }
  for (at = p; at[1]; at++)
    {
      DirNode *sub = NULL;
      if (node->dirs != NULL)
        for (i = 0; i < node->dirs->len; i++)
          {
            DirNode *dn = node->dirs->pdata[i];
            if (strcmp (dn->name, at[0]) == 0)
              {
                sub = dn;
                break;
              }
          }
      if (sub == NULL)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN,
                       GSK_ERROR_FILE_NOT_FOUND,
                       "directory to %s did not exist", path);
          g_strfreev (p);
          return FALSE;
        }
      node = sub;
    }
  if (node->dirs)
    for (i = 0; i < node->dirs->len; i++)
      {
        DirNode *dn = node->dirs->pdata[i];
        if (strcmp (dn->name, *at) == 0)
          break;
      }
  if (node->dirs == NULL
   || i == node->dirs->len)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_INVALID_ARGUMENT,
                   "%s was a not directory", path);
      g_strfreev (p);
      return FALSE;
    }

  {
    DirNode *to_kill = node->dirs->pdata[i];
    g_ptr_array_remove_index_fast (node->dirs, i);
    delete_dirnode_recursively (to_kill);
  }
  g_strfreev (p);

  return TRUE;
}

static void
delete_dirnode_recursively (DirNode *dir_node)
{
  guint i;
  if (dir_node->dirs)
    {
      for (i = 0; i < dir_node->dirs->len; i++)
        delete_dirnode_recursively (dir_node->dirs->pdata[i]);
      g_ptr_array_free (dir_node->dirs, TRUE);
    }
  if (dir_node->files)
    {
      for (i = 0; i < dir_node->files->len; i++)
        {
          FileNode *file_node;
          file_node = dir_node->files->pdata[i];
          destruct_file_node (file_node);
          g_free (file_node);
        }
      g_ptr_array_free (dir_node->files, TRUE);
    }
  g_free (dir_node->name);
  g_free (dir_node);
}

GskControlServerFileStat
gsk_control_server_stat        (GskControlServer *server,
                                const char       *path)
{
  char **p = path_split (path);
  char **at;
  DirNode *node = server->root;
  guint i;
  if (p[0] == NULL)
    {
      g_free (p);
      return GSK_CONTROL_SERVER_FILE_DIR;
    }
  /* terminate at the penultimate path component--
     the last path component is the base filename,
     which we handle separately */
  for (at = p; at[1]; at++)
    {
      DirNode *sub = NULL;
      if (node->dirs != NULL)
        for (i = 0; i < node->dirs->len; i++)
          {
            DirNode *dn = node->dirs->pdata[i];
            if (strcmp (dn->name, at[0]) == 0)
              {
                sub = dn;
                break;
              }
          }
      if (sub == NULL)
        {
          g_strfreev (p);
          return GSK_CONTROL_SERVER_FILE_NOT_EXIST;
        }
      node = sub;
    }
  if (node->files == NULL)
    {
      g_strfreev (p);
      return GSK_CONTROL_SERVER_FILE_NOT_EXIST;
    }
  if (node->files)
    for (i = 0; i < node->files->len; i++)
      {
        FileNode *fn = node->files->pdata[i];
        if (strcmp (fn->name, *at) == 0)
          {
            g_strfreev (p);
            return fn->is_virtual ? GSK_CONTROL_SERVER_FILE_VIRTUAL
                                  : GSK_CONTROL_SERVER_FILE_RAW;
          }
      }
  if (node->dirs)
    for (i = 0; i < node->dirs->len; i++)
      {
        DirNode *dn = node->dirs->pdata[i];
        if (strcmp (dn->name, *at) == 0)
          {
            g_strfreev (p);
            return GSK_CONTROL_SERVER_FILE_DIR;
          }
      }
  g_strfreev (p);
  return GSK_CONTROL_SERVER_FILE_NOT_EXIST;
}

static FileNode *
find_file_node (GskControlServer *server,
                const char       *path)
{
  char **p = path_split (path);
  char **at;
  DirNode *node = server->root;
  guint i;
  if (p[0] == NULL)
    {
      g_free (p);
      return FALSE;
    }
  /* terminate at the penultimate path component--
     the last path component is the base filename,
     which we handle separately */
  for (at = p; at[1]; at++)
    {
      DirNode *sub = NULL;
      if (node->dirs != NULL)
        for (i = 0; i < node->dirs->len; i++)
          {
            DirNode *dn = node->dirs->pdata[i];
            if (strcmp (dn->name, at[0]) == 0)
              {
                sub = dn;
                break;
              }
          }
      if (sub == NULL)
        {
          g_strfreev (p);
          return FALSE;
        }
      node = sub;
    }
  if (node->files == NULL)
    {
      g_strfreev (p);
      return FALSE;
    }
  for (i = 0; i < node->files->len; i++)
    {
      FileNode *fn = node->files->pdata[i];
      if (strcmp (fn->name, *at) == 0)
        {
          g_strfreev (p);
          return fn;
        }
    }
  g_strfreev (p);
  return NULL;
}

/**
 * gsk_control_server_peek_raw_file:
 * @server: the server to query.
 * @path: the path in the virtual file-system.
 * @content_out: where to put a pointer to the data.
 * @content_length_out: where to put the length of the data.
 * returns: whether we were able to find the file.
 *
 * Get the contents of a file that is in the virtual
 * file-system.  These data will go away if the file
 * is deleted or replaced: make a copy if you plan on holding
 * the data around.
 */
gboolean
gsk_control_server_peek_raw_file (GskControlServer *server,
                                  const char       *path,
                                  const guint8    **content_out,
                                  guint            *content_length_out)
{
  FileNode *fn = find_file_node (server, path);
  if (fn == NULL || fn->is_virtual)
    return FALSE;
  *content_out = fn->info.raw.data;
  *content_length_out = fn->info.raw.size;
  return TRUE;
}

/**
 * gsk_control_server_get_vfile_contents:
 * @server: the server to query.
 * @path: the file's path.
 * @content_out: the content that should be returned to the user.
 * @content_length_out: length of hte content that should be given to
 * the user.
 * @release_func_out:
 * @release_func_data_out:
 *
 * Obtain the data for a virtual function.
 * Callbacks to release the data are also provided.
 * 
 * This is rarely needed: it is mostly used by the control-server
 * itself.
 *
 * returns: whether we were able to find the file
 * and retrieve its contents.
 */
gboolean
gsk_control_server_get_vfile_contents (GskControlServer *server,
                                       const char       *path,
                                       guint8          **content_out,
                                       guint            *content_length_out,
                                       GDestroyNotify   *release_func_out,
                                       gpointer         *release_func_data_out)
{
  FileNode *fn = find_file_node (server, path);
  if (fn == NULL || !fn->is_virtual)
    return FALSE;
  fn->info.virtual.vfile_func (fn->info.virtual.vfile_data,
                               content_length_out, content_out,
                               release_func_out, release_func_data_out);
  return TRUE;
}

/**
 * gsk_control_server_listen:
 * @server: the server that should listen.
 * @address: the port to listen on.
 * @error: place to store the error if failure occurs.
 *
 * Bind to address and answer the requests using server.
 *
 * returns: whether the bind operation was successful.
 */
gboolean
gsk_control_server_listen (GskControlServer *server,
                           GskSocketAddress *address,
                           GError          **error)
{
  return gsk_http_content_listen (server->content, address, error);
}
