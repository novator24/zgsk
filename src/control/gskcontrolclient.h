#ifndef __GSK_CONTROL_CLIENT_H_
#define __GSK_CONTROL_CLIENT_H_

/* talks to a special http server; reenters the main-loop */
typedef struct _GskControlClient GskControlClient;

#include "../gsksocketaddress.h"

G_BEGIN_DECLS

GskControlClient *
gsk_control_client_new (GskSocketAddress *server);

typedef enum
{
  GSK_CONTROL_CLIENT_ADD_NEWLINES_AS_NEEDED
} GskControlClientFlag;

typedef enum
{
  GSK_CONTROL_CLIENT_OPTIONS_INTERACTIVE = (1<<0),      /* -i, --interactive */
  GSK_CONTROL_CLIENT_OPTIONS_RANDOM = (1<<1),           /* many misc options */
  GSK_CONTROL_CLIENT_OPTIONS_INLINE_COMMAND = (1<<2),   /* -e 'COMMAND' */
  GSK_CONTROL_CLIENT_OPTIONS_SOCKET = (1<<3),           /* --socket=SOCKET */
  GSK_CONTROL_CLIENT_OPTIONS_SCRIPTS = (1<<4),          /* -f SCRIPTFILE */

  GSK_CONTROL_CLIENT_OPTIONS_DEFAULT = 0xffffffff       /* all options by default */
} GskControlClientOptionFlags;


typedef void (*GskControlClientErrorFunc) (GError *error,
                                           gpointer data);

void
gsk_control_client_set_flag (GskControlClient *cc,
                             GskControlClientFlag flag,
                             gboolean             value);

gboolean
gsk_control_client_get_flag (GskControlClient *cc,
                             GskControlClientFlag flag);

void
gsk_control_client_set_prompt(GskControlClient *cc,
                              const char       *prompt_format);

/* may invoke the main-loop */
char *
gsk_control_client_get_prompt_string(GskControlClient *cc);

gboolean
gsk_control_client_parse_command_line_args (GskControlClient *cc,
                                            int              *argc_inout,
                                            char           ***argv_inout,
                                            GskControlClientOptionFlags flags);
void
gsk_control_client_print_command_line_usage(GskControlClientOptionFlags flags);

gboolean gsk_control_client_has_address (GskControlClient *client);

void gsk_control_client_increment_command_number (GskControlClient *cc);
void gsk_control_client_set_command_number (GskControlClient *cc, guint no);

// invoke the mainloop
void
gsk_control_client_run_command_line (GskControlClient *client,
                                     const char       *line);
void
gsk_control_client_run_command (GskControlClient *,
                                char **command_and_args,
                                const char *input_file,
                                const char *output_file);

G_END_DECLS

#endif
