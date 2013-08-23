#ifndef __GSK_CONTROL_SERVER_H__
#define __GSK_CONTROL_SERVER_H__

#include "../gskstream.h"
#include "../gsksocketaddress.h"

G_BEGIN_DECLS

typedef struct _GskControlServer GskControlServer;

GskControlServer *
gsk_control_server_new (void);


typedef gboolean (*GskControlServerCommandFunc) (char **argv,
                                                 GskStream *input,      /* for POST data */
                                                 GskStream **output,    /* content: must be plain text */
                                                 gpointer data,
                                                 GError **error);
void
gsk_control_server_add_command (GskControlServer *server,
                                const char       *command_name,
                                GskControlServerCommandFunc func,
                                gpointer          data);

void
gsk_control_server_set_default_command
                               (GskControlServer *server,
                                GskControlServerCommandFunc func,
                                gpointer          data);

gboolean
gsk_control_server_set_file    (GskControlServer *server,
                                const char       *path,
                                const guint8     *content,
                                guint             content_length,
                                GError          **error);

typedef void (*GskControlServerVFileContentsFunc) (gpointer  vfile_data,
                                                   guint    *len_out,
                                                   guint8   **contents_out,
                                                   GDestroyNotify *done_with_contents_out,
                                                   gpointer *done_with_contents_data_out);

gboolean
gsk_control_server_set_vfile   (GskControlServer *server,
                                const char       *path,
                                GskControlServerVFileContentsFunc vfile_func,
                                gpointer          vfile_data,
                                GDestroyNotify    vfile_data_destroy,
                                GError           **error);

typedef struct
{
  const char *domain;
  GLogLevelFlags levels;
} GskControlServerLogDomain;

void
gsk_control_server_set_logfile_v (GskControlServer *server,
                                  const char       *path,
                                  guint             ring_buffer_size,
                                  guint             n_log_domains,
                                  const GskControlServerLogDomain *domains);
void
gsk_control_server_set_logfile   (GskControlServer *server,
                                  const char       *path,
                                  guint             ring_buffer_size,
                                  const char       *first_log_domain,
                                  GLogLevelFlags    first_log_level_flags,
                                  const char       *next_log_domain,
                                  ...);

gboolean
gsk_control_server_delete_file (GskControlServer *server,
                                const char       *path,
                                GError          **error);

gboolean
gsk_control_server_delete_directory (GskControlServer *server,
                                     const char       *path,
                                     GError          **error);


typedef enum
{
  GSK_CONTROL_SERVER_FILE_RAW,
  GSK_CONTROL_SERVER_FILE_VIRTUAL,
  GSK_CONTROL_SERVER_FILE_DIR,
  GSK_CONTROL_SERVER_FILE_NOT_EXIST
} GskControlServerFileStat;

GskControlServerFileStat
gsk_control_server_stat        (GskControlServer *server,
                                const char       *path);

gboolean
gsk_control_server_peek_raw_file (GskControlServer *server,
                                  const char       *path,
                                  const guint8    **content_out,
                                  guint            *content_length_out);
gboolean
gsk_control_server_get_vfile_contents
                                 (GskControlServer *server,
                                  const char       *path,
                                  guint8          **content_out,
                                  guint            *content_length_out,
                                  GDestroyNotify   *release_func_out,
                                  gpointer         *release_func_data_out);




gboolean
gsk_control_server_listen (GskControlServer *server,
                           GskSocketAddress *address,
                           GError          **error);

G_END_DECLS

#endif
