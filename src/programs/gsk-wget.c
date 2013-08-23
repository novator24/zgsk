/* netcat-like program
 *    For testing, and for exposition.
 */
#include "../url/gskurltransfer.h"
#include "../url/gskurltransferhttp.h"
#include "../gskstreamfd.h"
#include "../gskmain.h"
#include "../gskmemory.h"
#include "../gskinit.h"
#include "../gskmacros.h"
#include "../gskghelpers.h"
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static void
usage (void)
{
  g_printerr ("gsk-wget [OPTIONS] URL\n"
              "\n"
              "Options allowed for any URL:\n"
              "  --upload-file=FILENAME    Upload the contents of FILENAME.\n"
              "  --upload-data=STRING      Upload the given string.\n"
              "  --timeout=MS              Specify the timeout in MS.\n"
              "\n"
              "Options allowed by HTTP and HTTPS:\n"
              "  --user-agent=STR    Set the HTTP User-Agent directive.\n"
              "  --add-http-header=L Add the given header line L.\n"
              "\n"
              "Options allowed by HTTPS:\n"
              "  --ssl[=CERT:KEY:PASSWORD]   Use SSL\n"
             );

  g_printerr ("\n"
              "Options for debugging only:\n"
              "  --dont-quit-when-done\n"
             );
  exit(1);
}

/* --- Options --- */

/* general options: SSL */
static gboolean use_ssl = FALSE;
static char *ssl_cert = NULL;
static char *ssl_key = NULL;
static char *ssl_password = NULL;
const char *upload_filename = NULL;
const char *upload_string = NULL;
const char *user_agent = NULL;
const char *output_filename;
FILE *output_fp;
int timeout = -1;
gboolean quit_when_done = TRUE;

/* http options */
GPtrArray *add_http_headers;

/* status */
gboolean done = FALSE;

#if 0
static void
connect_to_stdin (GskStream *str)
{
  GError *error = NULL;
  GskStream *in = gsk_stream_fd_new_auto (STDIN_FILENO);
  g_assert (in);
  if (!gsk_stream_attach (in, str, &error))
    g_error ("error connecting input: %s", error->message);
  g_object_unref (in);
}
#endif

#if 0
static void
connect_to_stdout (GskStream *str)
{
  GError *error = NULL;
  GskStream *out = gsk_stream_fd_new_auto (STDOUT_FILENO);
  g_assert (out);
  if (!gsk_stream_attach (str, out, &error))
    g_error ("error connecting output: %s", error->message);
  g_object_unref (out);
}
#endif

static void
handle_error (GskUrlTransfer *transfer)
{
  const char *res = gsk_url_transfer_result_name (transfer->result);
  const char *msg = transfer->error ? transfer->error->message
                                    : "(no GError available)";
  if (quit_when_done)
    g_error ("%s: %s", res, msg);
  else
    g_warning ("%s: %s", res, msg);
}

static GskStream *
make_upload_from_filename (gpointer data, gssize *size_out, GError **error)
{
  struct stat statbuf;
  if (stat (upload_filename, &statbuf) < 0)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   gsk_error_code_from_errno (errno),
                   "error stating upload data %s: %s", upload_filename, g_strerror(errno));
      return NULL;
    }
  *size_out = statbuf.st_size;
  return gsk_stream_fd_new_read_file (upload_filename, error);
}

static GskStream *
make_upload_from_string (gpointer data, gssize *size_out, GError **error)
{
  *size_out = strlen (upload_string);
  return gsk_memory_source_static_string (upload_string);
}

static gboolean
handle_content_readable (GskStream *stream)
{
  char buf[4096];
  GError *error = NULL;
  guint n_read = gsk_stream_read (stream, buf, sizeof(buf), &error);
  if (error)
    g_error ("error reading from HTTP content stream: %s", error->message);
  if (fwrite (buf, 1, n_read, output_fp) != n_read)
    g_error ("partial write to output file or stream");
  if (output_fp != stdout)
    g_printerr (".");
  return TRUE;
}
 
static gboolean
handle_content_read_shutdown (GskStream *stream)
{
  if (output_fp != stdout)
    fclose(output_fp);
  if (quit_when_done)
    gsk_main_quit ();
  g_object_unref(stream);
  return FALSE;
}

GskStream *the_content_stream;

static void
handle_transfer_done (GskUrlTransfer *transfer,
                      gpointer        data)
{
  done = TRUE;
  switch (transfer->result)
    {
    case GSK_URL_TRANSFER_ERROR_BAD_REQUEST:
    case GSK_URL_TRANSFER_ERROR_BAD_NAME:
    case GSK_URL_TRANSFER_ERROR_NO_SERVER:
    case GSK_URL_TRANSFER_ERROR_NOT_FOUND:
    case GSK_URL_TRANSFER_ERROR_SERVER_ERROR:
    case GSK_URL_TRANSFER_ERROR_UNSUPPORTED:
    case GSK_URL_TRANSFER_ERROR_TIMED_OUT:
    case GSK_URL_TRANSFER_ERROR_REDIRECT_LOOP:
      handle_error (transfer);
      break;

    case GSK_URL_TRANSFER_REDIRECT:
    {
      char *str = gsk_url_to_string (transfer->redirect_url);
      g_print ("REDIRECT -> %s\n", str);
      if (quit_when_done)
        gsk_main_quit ();
      break;
    }
    case GSK_URL_TRANSFER_CANCELLED:
      g_error ("CANCELLED...");
      break;
    case GSK_URL_TRANSFER_SUCCESS:
      {
        if (transfer->content)
          {
            if (output_filename == NULL
             || strcmp (output_filename, "-") == 0)
              output_fp = stdout;
            else
              {
                output_fp = fopen (output_filename, "wb");
                if (output_fp == NULL)
                  g_error ("error creating %s: %s", output_filename, g_strerror (errno));
              }
            gsk_stream_trap_readable (transfer->content,
                                      handle_content_readable,
                                      handle_content_read_shutdown,
                                      NULL,
                                      NULL);
            the_content_stream = g_object_ref (transfer->content);
          }
        break;
      }
    default:
      g_error ("unknown transfer result %d", transfer->result);
    }
}

static void
handle_transfer_destroyed (void *data)
{
  g_assert (done);
}

static void
add_http_header_line_to_transfer (gpointer data, gpointer func_data)
{
  const char *line = data;
  GskUrlTransferHttp *transfer = GSK_URL_TRANSFER_HTTP (func_data);
  const char *colon = strchr (line, ':');
  char *key;
  const char *value;
  if (colon == NULL)
    g_error ("malformed HTTP header line (missing ':'): '%s'", line);
  key = g_strndup (line, colon - line);
  value = colon + 1;
  GSK_SKIP_WHITESPACE (value);
  gsk_url_transfer_http_add_extra_header (transfer, key, value);
  g_free (key);
}

/* --- Main --- */
int main(int argc, char **argv)
{
  int i;
  GError *error = NULL;
  GskUrl *url = NULL;
  GskUrlTransfer *transfer;
  add_http_headers = g_ptr_array_new ();

  gsk_init_without_threads (&argc, &argv);

  /* parse arguments */
  for (i = 1; i < argc; i++)
    {
      const char *arg = argv[i];
      if (strcmp (arg, "--ssl") == 0)
        use_ssl = TRUE;
      else if (g_str_has_prefix (arg, "--ssl="))
        {
          char **pieces;
          use_ssl = TRUE;
          pieces = g_strsplit (strchr(arg,'=') + 1, ":", 3);
          if (pieces[0] && pieces[0][0])
            ssl_cert = pieces[0];
          if (pieces[0] && pieces[1] && pieces[1][0])
            ssl_key = pieces[1];
          if (pieces[0] && pieces[1] && pieces[2] && pieces[2][0])
            ssl_password = pieces[2];
          g_free (pieces);
        }
      else if (g_str_has_prefix (arg, "--upload-data="))
        upload_string = strchr (arg, '=') + 1;
      else if (g_str_has_prefix (arg, "--upload-file="))
        upload_filename = strchr (arg, '=') + 1;
      else if (g_str_has_prefix (arg, "--add-http-header="))
        g_ptr_array_add (add_http_headers, strchr (arg, '=') + 1);
      else if (g_str_has_prefix (arg, "--user-agent="))
	user_agent = strchr (arg, '=') + 1;
      else if (g_str_has_prefix (arg, "--timeout="))
        timeout = atoi (strchr (arg, '=') + 1);
      else if (strcmp (arg, "--dont-quit-when-done") == 0)
        quit_when_done = FALSE;
      else if (arg[0] == '-')
        usage ();
      else if (url == NULL)
        {
          url = gsk_url_new (arg, &error);
          if (url == NULL)
            g_error ("error parsing url %s: %s", arg, error->message);
        }
      else
        g_error ("too many arguments (at %s)", arg);
    }

  if (url == NULL)
    usage ();

  transfer = gsk_url_transfer_new (url);
  if (transfer == NULL)
    g_error ("cannot make transfer for %s", gsk_url_to_string (url));
  if (upload_string != NULL)
    gsk_url_transfer_set_upload (transfer, make_upload_from_string, NULL, NULL);
  else if (upload_filename != NULL)
    gsk_url_transfer_set_upload (transfer, make_upload_from_filename, NULL, NULL);

  if (timeout >= 0)
    gsk_url_transfer_set_timeout (transfer, timeout);

  if (GSK_IS_URL_TRANSFER_HTTP (transfer))
    {
      gsk_g_ptr_array_foreach (add_http_headers,
                               add_http_header_line_to_transfer,
                               transfer);
      if (user_agent)
	gsk_url_transfer_http_set_user_agent (GSK_URL_TRANSFER_HTTP (transfer),
					      user_agent);
    }
  else
    {
      if (add_http_headers->len > 0)
        g_error ("--add-http-header= specified for non-HTTP url");
    }

  gsk_url_transfer_set_handler (transfer,
                                handle_transfer_done,
                                NULL,
                                handle_transfer_destroyed);

  if (!gsk_url_transfer_start (transfer, &error))
    g_error ("error starting URL transfer: %s", error->message);
  g_object_unref (transfer);
  g_object_unref (url);

  return gsk_main_run();
}
