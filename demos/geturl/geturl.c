#include <gsk/protocols/gskurldownload.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <gsk/gskmain.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

typedef struct _DownloadInfo DownloadInfo;
struct _DownloadInfo
{
  GskUrl *url;
  GskMainLoop *main_loop;
  int fd;
  char *tmp_filename;
  char *final_filename;

  guint failed : 1;
  guint done : 1;
};

static int global_count = 0;

static gboolean
geturl_download_start    (GskUrlDownloadInfo   *info,
		          gpointer              func_data)
{
  (void) func_data;
  if (info->has_content_length)
    gsk_log_debug ("Download started %lld bytes...", info->content_length);
  else
    gsk_log_debug ("Download started (dunno how many bytes)");
  return TRUE;
}

static gboolean
geturl_download_process  (gconstpointer         input_data,
		          guint                 input_length,
		          gpointer              func_data)
{
  DownloadInfo *download_info = func_data;
  const guint8* in_at = input_data;
  int remaining = input_length;
  if (download_info->fd < 0)
    {
      do
	{
	  if (download_info->tmp_filename != NULL)
	    g_free (download_info->tmp_filename);
	  download_info->tmp_filename
	     = g_strdup_printf ("%s.tmp.%d", 
		                download_info->final_filename,
		                global_count++);
	  download_info->fd = open (download_info->tmp_filename,
				    O_WRONLY | O_CREAT | O_EXCL, 0644);
	}
      while (download_info->fd < 0);
    }
  while (remaining > 0)
    {
      int num_written = write (download_info->fd, in_at, remaining);
      if (num_written < 0)
	{
	  if (errno == EINTR || errno == EAGAIN)
	    continue;
	  download_info->failed = 1;
	  g_warning ("error writing to disk %s", g_strerror (errno));
	  return FALSE;
	}
      in_at += num_written;
      remaining -= num_written;
    }
  return TRUE;
}

static void
geturl_download_error    (GskUrlDownloadErr     err_code,
		          gpointer              func_data)
{
  DownloadInfo *download_info = func_data;
  g_warning ("error downloading %s: %s",
	     download_info->final_filename,
	     gsk_url_download_err_str (err_code));
  download_info->failed = 1;
}

static void
geturl_download_end      (gpointer              func_data)
{
  DownloadInfo *download_info = func_data;
  download_info->done = 1;
}

static void
geturl_download_destroy  (gpointer              func_data)
{
  DownloadInfo *download_info = func_data;
  if (download_info->fd > 0)
    close (download_info->fd);
  if (download_info->fd > 0)
    {
      if (download_info->failed || !download_info->done)
	{
	  if (unlink (download_info->tmp_filename) < 0)
	    g_error ("error deleting temporary file %s: %s",
		     download_info->tmp_filename,
		     g_strerror (errno));
	}
      else
	{
	  if (rename (download_info->tmp_filename,
		      download_info->final_filename) < 0)
	    g_error ("error renaming temporary file %s to %s: %s",
		     download_info->tmp_filename,
		 download_info->final_filename,
		 g_strerror (errno));
	}
    }
  download_info->done = 1;
}

static GskUrlDownloadFuncs handle_download_funcs =
{
  geturl_download_start,
  geturl_download_process,
  geturl_download_error,
  geturl_download_end,
  geturl_download_destroy
};

static void
usage ()
{
  printf ("usage: %s URL\n\n"
	  "Download the specified URL.\n",
	  g_get_prgname ());
  exit (1);
}

int main (int argc, char **argv)
{
  const char *url_string = NULL;
  DownloadInfo download_info;
  int i;
  GskMainLoop *main_loop;
  GskUrl *url;
  GskUrlDownload *download;

  g_set_prgname (argv[0]);
  gsk_init (&argc, &argv);
  gsk_url_download_init ();

  for (i = 1; i < argc; i++)
    {
      if (argv[i][0] == '-')
	usage ();
      else 
	{
	  if (url_string == NULL)
	    url_string = argv[i];
	  else
	    g_error ("too many arguments");
	}
    }
  if (url_string == NULL)
    usage ();
  main_loop = gsk_main_loop_new (0);

  url = gsk_url_new (url_string);
  if (url == NULL)
    g_error ("error parsing url %s", url_string);
  download_info.url = url;
  download_info.main_loop = main_loop;
  download_info.failed = 0;
  download_info.done = 0;
  download_info.fd = -1;

  gsk_log_debug ("url: host=%s, path=%s", url->host, url->path);

  /* compose the base filenames */
  download_info.tmp_filename = NULL;
  download_info.final_filename = g_strdup (g_basename (url->path));

  /* start the download */
  download = gsk_url_download_start (url, main_loop,
				     &handle_download_funcs,
				     &download_info);
  while (!download_info.done && !main_loop->quit)
    gsk_main_loop_run (main_loop, -1, NULL);
  return download_info.failed ? 1 : 0;
}
