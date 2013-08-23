#include "gskurltransferfile.h"
#include "../gskstreamfd.h"

G_DEFINE_TYPE(GskUrlTransferFile, gsk_url_transfer_file, GSK_TYPE_URL_TRANSFER);

static gboolean 
gsk_url_transfer_file_test  (GskUrlTransferClass *transfer_class,
                             const GskUrl        *url)
{
  return url->scheme == GSK_URL_SCHEME_FILE
    && url->path != NULL
    && url->path[0] == '/';
}

static gboolean 
gsk_url_transfer_file_start (GskUrlTransfer      *transfer,
                             GError             **error)
{
  GskStream *content;
  content = gsk_stream_fd_new_read_file (transfer->url->path, error);
  if (content == NULL)
    return FALSE;
  gsk_url_transfer_set_download (transfer, content);
  g_object_unref (content);
  gsk_url_transfer_notify_done (transfer, GSK_URL_TRANSFER_SUCCESS);
  return TRUE;
}

static void
gsk_url_transfer_file_class_init (GskUrlTransferFileClass *class)
{
  GskUrlTransferClass *transfer_class = GSK_URL_TRANSFER_CLASS (class);
  transfer_class->test = gsk_url_transfer_file_test;
  transfer_class->start = gsk_url_transfer_file_start;
}
static void
gsk_url_transfer_file_init (GskUrlTransferFile *file)
{
}
