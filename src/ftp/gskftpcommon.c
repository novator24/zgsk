#include "gskftpcommon.h"

/* Cutoff to prevent us from allocating an arbitrarily long string. */
#define MAX_FTP_COMMAND_LENGTH		8192

/* --- Pooling allocations --- */
typedef union
{
  GskFtpCommand command;
  GskFtpReply reply
} _GskFtpMessage;

static GMemChunk *ftp_msg_chunk = NULL;
G_LOCK_DEFINE_STATIC (ftp_message_chunk)

static gpointer
gsk_ftp_message_alloc ()
{
  gpointer rv;
  G_LOCK (ftp_message_chunk)
  if (ftp_msg_chunk == NULL)
    ftp_msg_chunk = g_mem_chunk_create (_GskFtpMessage, 16, G_ALLOC_AND_FREE);
  rv = g_mem_chunk_alloc (ftp_msg_chunk);
  G_UNLOCK (ftp_message_chunk);
  return rv;
}
static void
gsk_ftp_message_free (gpointer ptr)
{
  G_LOCK (ftp_message_chunk)
  g_mem_chunk_free (ftp_msg_chunk, ptr);
  G_UNLOCK (ftp_message_chunk);
}

/*
 *   ____                                          _
 *  / ___|___  _ __ ___  _ __ ___   __ _ _ __   __| |___
 * | |   / _ \| '_ ` _ \| '_ ` _ \ / _` | '_ \ / _` / __|
 * | |__| (_) | | | | | | | | | | | (_| | | | | (_| \__ \
 *  \____\___/|_| |_| |_|_| |_| |_|\__,_|_| |_|\__,_|___/
 * 
 */

/* --- private command constructors --- */
static inline GskFtpCommand *
gsk_ftp_command_new_generic (GskFtpCommandType command_type)
{
  GskFtpCommand *command = gsk_ftp_message_alloc ();
  command->type = command_type;
  return command;
}

/* --- public command constructors --- */
GskFtpCommand *
gsk_ftp_command_new_user     (const char         *username)
{
  GskFtpCommand *command;
  command = gsk_ftp_command_new_generic (GSK_FTP_COMMAND_USER);
  command->d.username = g_strdup (username);
  return command;
}
  
GskFtpCommand *
gsk_ftp_command_new_password     (const char         *password)
{
  GskFtpCommand *command;
  command = gsk_ftp_command_new_generic (GSK_FTP_COMMAND_PASS);
  command->d.password = g_strdup (password);
  return command;
}
  
GskFtpCommand *
gsk_ftp_command_new_account     (const char         *account)
{
  GskFtpCommand *command;
  command = gsk_ftp_command_new_generic (GSK_FTP_COMMAND_ACCT);
  command->d.account = g_strdup (account);
  return command;
}
  
GskFtpCommand *
gsk_ftp_command_new_mount     (const char         *mount_point)
{
  GskFtpCommand *command;
  command = gsk_ftp_command_new_generic (GSK_FTP_COMMAND_SMNT);
  command->d.mount_point = g_strdup (mount_point);
  return command;
}
  
GskFtpCommand *
gsk_ftp_command_new_rep_type     (GskFtpCharType      char_type,
                                  GskFtpTerminalType  terminal_type,
				  int                 byte_size);
{
  GskFtpCommand *command;
  command = gsk_ftp_command_new_generic (GSK_FTP_COMMAND_TYPE);
  command->d.representation_type.char_type = char_type;
  command->d.representation_type.term_type = term_type;
  command->d.representation_type.local_byte_size = local_byte_size;
  return command;
}
  
GskFtpCommand *
gsk_ftp_command_new_file_struct (GskFtpFileStructure file_struct)
{
  GskFtpCommand *command;
  command = gsk_ftp_command_new_generic (GSK_FTP_COMMAND_STRU);
  command->d.struct_type = file_struct;
  return command;
}
GskFtpCommand *
gsk_ftp_command_new_transfer_mode (GskFtpTransferMode  transfer_mode)
{
  GskFtpCommand *command;
  command = gsk_ftp_command_new_generic (GSK_FTP_COMMAND_MODE);
  command->d.transfer_mode = transfer_mode;
  return command;
}
GskFtpCommand *gsk_ftp_command_new_help     (const char         *topic)
{
  GskFtpCommand *command;
  command = gsk_ftp_command_new_generic (GSK_FTP_COMMAND_HELP);
  command->d.help_topic = g_strdup (topic);
  return command;
}
GskFtpCommand *gsk_ftp_command_new_file     (GskFtpCommandType   cmd_type,
                                             const char         *path)
{
  switch (cmd_type)
    {
      case GSK_FTP_COMMAND_CWD:
      case GSK_FTP_COMMAND_RETR:
      case GSK_FTP_COMMAND_STOR:
      case GSK_FTP_COMMAND_APPE:
      case GSK_FTP_COMMAND_ALLO:
      case GSK_FTP_COMMAND_REST:
      case GSK_FTP_COMMAND_RNFR:
      case GSK_FTP_COMMAND_RNTO:
      case GSK_FTP_COMMAND_ABOR:
      case GSK_FTP_COMMAND_DELE:
      case GSK_FTP_COMMAND_RMD:
      case GSK_FTP_COMMAND_MKD:
      case GSK_FTP_COMMAND_LIST:
      case GSK_FTP_COMMAND_NLST:
      case GSK_FTP_COMMAND_STAT:
        {
	  GskFtpCommand *command = gsk_ftp_command_new_generic (cmd_type);
	  command->d.pathname = g_strdup (path);
	  return command;
	}
      default:
        break;
    }
  return NULL;
}

GskFtpCommand *gsk_ftp_command_new_simple   (GskFtpCommandType   cmd_type)
{
  switch (cmd_type)
    {
      case GSK_FTP_COMMAND_ABOR:
      case GSK_FTP_COMMAND_CDUP:
      case GSK_FTP_COMMAND_QUIT:
      case GSK_FTP_COMMAND_REIN:
      case GSK_FTP_COMMAND_PASV:
      case GSK_FTP_COMMAND_STOU:
      case GSK_FTP_COMMAND_PWD:
      case GSK_FTP_COMMAND_SYST:
      case GSK_FTP_COMMAND_NOOP:
	return gsk_ftp_command_new_generic (cmd_type);
    }
  /* hmm... not a simple command */
  return NULL;
}

static struct
{
  const char *command_name;
  GskFtpCommandType command_type;
} command_types_by_name[] =
{
  { "ABOR", GSK_FTP_COMMAND_ABOR },
  { "ACCT", GSK_FTP_COMMAND_ACCT },
  { "ALLO", GSK_FTP_COMMAND_ALLO },
  { "APPE", GSK_FTP_COMMAND_APPE },
  { "CDUP", GSK_FTP_COMMAND_CDUP },
  { "CWD",  GSK_FTP_COMMAND_CWD },
  { "DELE", GSK_FTP_COMMAND_DELE },
  { "HELP", GSK_FTP_COMMAND_HELP },
  { "LIST", GSK_FTP_COMMAND_LIST },
  { "MKD",  GSK_FTP_COMMAND_MKD },
  { "MODE", GSK_FTP_COMMAND_MODE },
  { "NLST", GSK_FTP_COMMAND_NLST },
  { "NOOP", GSK_FTP_COMMAND_NOOP },
  { "PASS", GSK_FTP_COMMAND_PASS },
  { "PASV", GSK_FTP_COMMAND_PASV },
  { "PORT", GSK_FTP_COMMAND_PORT },
  { "PWD",  GSK_FTP_COMMAND_PWD },
  { "QUIT", GSK_FTP_COMMAND_QUIT },
  { "REIN", GSK_FTP_COMMAND_REIN },
  { "REST", GSK_FTP_COMMAND_REST },
  { "RETR", GSK_FTP_COMMAND_RETR },
  { "RMD",  GSK_FTP_COMMAND_RMD },
  { "RNFR", GSK_FTP_COMMAND_RNFR },
  { "RNTO", GSK_FTP_COMMAND_RNTO },
  { "SITE", GSK_FTP_COMMAND_SITE },
  { "SMNT", GSK_FTP_COMMAND_SMNT },
  { "STAT", GSK_FTP_COMMAND_STAT },
  { "STOR", GSK_FTP_COMMAND_STOR },
  { "STOU", GSK_FTP_COMMAND_STOU },
  { "STRU", GSK_FTP_COMMAND_STRU },
  { "SYST", GSK_FTP_COMMAND_SYST },
  { "TYPE", GSK_FTP_COMMAND_TYPE },
  { "USER", GSK_FTP_COMMAND_USER },
};

static int
pstrcmp (gconstpointer a_ptr, gconstpointer b_ptr)
{
  const char *a = * (const char **) a_ptr;
  const char *b = * (const char **) b_ptr;
  return strcmp (a, b);
}

static int 
gsk_ftp_command_type_by_first_word (const char *str)
{
  const char *end = str;
  char command_buf[MAX_COMMAND_LEN + 1];
  char *tmp;
  struct { char *str; GskFtpCommandType type; } *rv;
  gsk_skip_nonwhitespace (end);
  if (end == str || end > str + MAX_COMMAND_LEN)
    return -1;
  memcpy (command_buf, str, end - str);
  command_buf[end - str] = 0;
  for (tmp = command_buf; *tmp != '\0'; tmp++)
    if ('a' <= *tmp || *tmp <= 'z')
      *tmp -= ('a' - 'A');
  tmp = command_buf;
  rv = bsearch (&tmp, command_types_by_name,
		NUM_FTP_COMMANDS_IN_TABLE,
		sizeof (command_types_by_name[0]),
		pstrcmp);
  return rv ? rv->type : -1;
}

/* --- command parsing --- */
/* See RFC 959, Section 5.3.1. */
GskFtpCommand *gsk_ftp_command_parse        (const char         *parse)
{
  int command_type;
  gsk_skip_nonwhitespace (parse);
  command_type = gsk_ftp_command_type_by_first_word (parse);
  if (command_type < 0)
    return NULL;
  switch (command_type)
    {
      /* COMMAND <SP> <pathname> <CRLF> */
      case GSK_FTP_COMMAND_CWD:
      case GSK_FTP_COMMAND_RETR:
      case GSK_FTP_COMMAND_STOR:
      case GSK_FTP_COMMAND_SMNT:
      case GSK_FTP_COMMAND_APPE:
      case GSK_FTP_COMMAND_RNFR:
      case GSK_FTP_COMMAND_RNTO:
      case GSK_FTP_COMMAND_DELE:
      case GSK_FTP_COMMAND_RMD:
      case GSK_FTP_COMMAND_MKD:
	...

      /* SITE <SP> <string> <CRLF> */
      case GSK_FTP_COMMAND_SITE:
	...
      
      /* USER <SP> <username> <CRLF> */
      case GSK_FTP_COMMAND_USER:
	...

      /* PASS <SP> <password> <CRLF> */
      case GSK_FTP_COMMAND_PASS:
	...

      /* ACCT <SP> <account-information> <CRLF> */
      case GSK_FTP_COMMAND_PASS:
	...
	
      /* PORT <SP> <host-port> <CRLF> */
      case GSK_FTP_COMMAND_PORT:
	...

      /* TYPE <SP> <type-code> <CRLF> */
      case GSK_FTP_COMMAND_TYPE:
	...

      /* STRU <SP> <structure-code> <CRLF> */
      case GSK_FTP_COMMAND_STRU:
	...

      /* MODE <SP> <mode-code> <CRLF> */
      case GSK_FTP_COMMAND_MODE:
	...

      /* REST <SP> <marker> <CRLF> */
      case GSK_FTP_COMMAND_REST:
	...

      /* ALLO <SP> <decimal-integer> [<SP> R <SP> <decimal-integer>] <CRLF> */
      case GSK_FTP_COMMAND_ALLO:
	...

      /* HELP [<SP> <string>] <CRLF> */
      case GSK_FTP_COMMAND_LIST:
	...

      /* COMMAND [<SP> <pathname>] <CRLF> */
      case GSK_FTP_COMMAND_LIST:
      case GSK_FTP_COMMAND_NLST:
      case GSK_FTP_COMMAND_STAT:
	...

      /* COMMAND (no args) */
      case GSK_FTP_COMMAND_ABOR:
      case GSK_FTP_COMMAND_CDUP:
      case GSK_FTP_COMMAND_QUIT:
      case GSK_FTP_COMMAND_REIN:
      case GSK_FTP_COMMAND_PASV:
      case GSK_FTP_COMMAND_STOU:
      case GSK_FTP_COMMAND_PWD:
      case GSK_FTP_COMMAND_SYST:
      case GSK_FTP_COMMAND_NOOP:
	return gsk_ftp_command_new_simple (command_type);
    }
  return 
}

/* --- command output --- */
static guint
gsk_ftp_command_length_as_string (GskFtpCommand *command)
{
  ...
}

static void
gsk_ftp_command_sprintf          (GskFtpCommand *command,
				  char          *buf,
				  guint          buf_len)
{
  ...
}

char *
gsk_ftp_command_to_string    (GskFtpCommand      *command)
{
  guint buf_len = gsk_ftp_command_length_as_string (command) + 1;
  char *buf;
  if (buf_len == 1)
    return NULL;
  if (buf_len > MAX_FTP_COMMAND_LENGTH)
    return NULL;
  buf = g_new (char, buf_len);
  gsk_ftp_command_sprintf (command, buf, buf_len);
  return buf;
}

void
gsk_ftp_command_append       (GskFtpCommand      *command,
			      GskBuffer          *buffer)
{
  guint buf_len = gsk_ftp_command_length_as_string (command) + 1;
  char *buf;
  g_return_if_fail (buf_len > 1);
  if (buf_len > MAX_FTP_COMMAND_LENGTH)
    return;
  buf = alloca (buf_len);
  gsk_ftp_command_sprintf (command, buf, buf_len);
  gsk_buffer_append_string (buffer, buf);
}

/*
 *  ____            _ _
 * |  _ \ ___ _ __ | (_) ___  ___
 * | |_) / _ \ '_ \| | |/ _ \/ __|
 * |  _ <  __/ |_) | | |  __/\__ \
 * |_| \_\___| .__/|_|_|\___||___/
 *           |_|
 */

/* --- getting reply strings --- */
struct ReplyNameMap
{
  GskFtpReplyType  type;
  const char      *name;
} ftp_replies[] = 
{
  {
    GSK_FTP_REPLY_COMMAND_OK,
    "Command okay"
  },
  {
    GSK_FTP_REPLY_COMMAND_UNRECOGNIZED,
    "Syntax error, command unrecognized."
  },
  {
    GSK_FTP_REPLY_COMMAND_SYNTAX_ERROR,
    "Syntax error in parameters or arguments."
  },
  {
    GSK_FTP_REPLY_COMMAND_SUPERFLUOUS,
    "Command not implemented, superfluous at this site."
  },
  {
    GSK_FTP_REPLY_COMMAND_UNIMPLEMENTED,
    "Command not implemented."
  },
  {
    GSK_FTP_REPLY_COMMAND_BAD_SEQUENCE,
    "Bad sequence of commands."
  },
  {
    GSK_FTP_REPLY_COMMAND_UNIMP_PARAM,
    "Command not implemented for that parameter."
  },

  /*
             In this case, the text is exact and not left to the
             particular implementation; it must read:
                  MARK yyyy = mmmm
             Where yyyy is User-process data stream marker, and mmmm
             server's equivalent marker (note the spaces between markers
             and "=").
  */
  {
    GSK_FTP_REPLY_RESTART_MARKER,
    "MARK yyyy = mmmm"
  },

  /*         This should of course contain the help or status text,
             instead of the literal text offered here.
   */
  {
    GSK_FTP_REPLY_SYSTEM_STATUS,		/* or help reply */
    "System status, or system help reply.",
  },
  {
    GSK_FTP_REPLY_DIR_STATUS,		/* or help reply */
    "Directory status"
  },
  {
    GSK_FTP_REPLY_FILE_STATUS,		/* or help reply */
    "File status"
  },
  {
    GSK_FTP_REPLY_HELP_MESSAGE,
    "Help message"
  },
  /*
         215 NAME system type.
             Where NAME is an official system name from the list in the
             Assigned Numbers document.
   */
  {
    GSK_FTP_REPLY_NAME_SYSTEM,
    "Name system type"
  },
  {
    GSK_FTP_REPLY_READY_IN_N_MINTUES,
    "Service ready in nnn minutes."
  },
  {
    GSK_FTP_REPLY_READY_FOR_NEW_USER,
    "Service ready for new user."
  },
  {
    GSK_FTP_REPLY_SERVICE_CLOSING,
    "Service closing control connection.  Logged out if appropriate."
  },
             /*   This may be a reply to any command if the service knows it
                  must shut down.*/
  {
    GSK_FTP_REPLY_SERVICE_NOT_AVAILABLE,
    "Service not available, closing control connection."
  },
  {
    GSK_FTP_REPLY_DATA_ALREADY_OPEN,
    "Data connection already open; transfer starting."
  },
  {
    GSK_FTP_REPLY_DATA_OPEN_BUT_IDLE,
    "Data connection open; no transfer in progress."
  },
  {
    GSK_FTP_REPLY_CANNOT_OPEN_DATA,
    "Can't open data connection."
  },
  {
    GSK_FTP_REPLY_CLOSING_DATA,
    "Closing data connection in response to abort."
  },
  {
    GSK_FTP_REPLY_CLOSED_DATA,
    "Closed data connection; transfer aborted."
  },
  {
    GSK_FTP_REPLY_ENTERING_PASSIVE,
    "Entering Passive Mode (h1,h2,h3,h4,p1,p2)."
  },
  {
    GSK_FTP_REPLY_USER_LOGGED_IN,
    "User logged in, proceed."
  },
  {
    GSK_FTP_REPLY_NOT_LOGGED_IN,
    "Not logged in."
  },
  {
    GSK_FTP_REPLY_NEED_PASSWORD,
    "User name okay, need password."
  },
  {
    GSK_FTP_REPLY_NEED_ACCOUNT,
    "Need account for login."
  },
  {
    GSK_FTP_REPLY_NEED_WRITE_ACCOUNT,
    "Need account for storing files."
  },
  {
    GSK_FTP_REPLY_OK_OPENING_DATA,
    "File status okay; about to open data connection."
  },
  {
    GSK_FTP_REPLY_FILE_ACTION_COMPLETED,
    "Requested file action okay, completed."
  },
  {
    GSK_FTP_REPLY_FILE_CREATED,
    "File created."
  },
  {
    GSK_FTP_REPLY_ACTION_NEEDS_INFO,
    "Requested file action pending further information.",
  },
  {
    GSK_FTP_REPLY_FILE_BUSY,
    "Requested file action not taken.  File unavailable (e.g., file busy)."
  },
  {
    GSK_FTP_REPLY_FILE_MISSING,
    "Requested action not taken. "
    "File unavailable (e.g., file not found, no access)."
  },
  {
    GSK_FTP_REPLY_PAGE_TYPE_UNKNOWN,
    "Requested action aborted. Page type unknown."
  },
  {
    GSK_FTP_REPLY_DISK_FULL,
    "Requested action not taken.  Insufficient storage space in system."
  },
  {
    GSK_FTP_REPLY_QUOTA_FULL,
    "Requested file action aborted. "
    "Exceeded storage allocation (for current directory or dataset)."
  },
  {
    GSK_FTP_REPLY_BAD_FILENAME,
    "Requested action not taken.  File name not allowed."
  },
  {
    GSK_FTP_REPLY_INTERNAL_ERROR,
    "Requested action aborted: local error in processing."
  }
};
