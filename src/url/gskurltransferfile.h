#ifndef __GSK_URL_TRANSFER_FILE_H_
#define __GSK_URL_TRANSFER_FILE_H_

#include "gskurltransfer.h"

G_BEGIN_DECLS

typedef struct _GskUrlTransferFileClass GskUrlTransferFileClass;
typedef struct _GskUrlTransferFile GskUrlTransferFile;

GType gsk_url_transfer_file_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_URL_TRANSFER_FILE              (gsk_url_transfer_file_get_type ())
#define GSK_URL_TRANSFER_FILE(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_URL_TRANSFER_FILE, GskUrlTransferFile))
#define GSK_URL_TRANSFER_FILE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_URL_TRANSFER_FILE, GskUrlTransferFileClass))
#define GSK_URL_TRANSFER_FILE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_URL_TRANSFER_FILE, GskUrlTransferFileClass))
#define GSK_IS_URL_TRANSFER_FILE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_URL_TRANSFER_FILE))
#define GSK_IS_URL_TRANSFER_FILE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_URL_TRANSFER_FILE))

struct _GskUrlTransferFileClass
{
  GskUrlTransferClass base_class;
};

struct _GskUrlTransferFile
{
  GskUrlTransfer base_instance;
};

G_END_DECLS

#endif
