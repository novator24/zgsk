/*
    GSK - a library to write servers
    Copyright (C) 1999-2000 Dave Benson

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA

    Contact:
        daveb@ffem.org <Dave Benson>
*/

#ifndef __GSK_FTP_COMMON_H_
#define __GSK_FTP_COMMON_H_

G_BEGIN_DECLS

/*
 * Definitions of FTP commands and replies.
 *
 * See RFC 959 for more information about FTP.
 */

/*
 *  _____ _            ____                                          _
 * |  ___| |_ _ __    / ___|___  _ __ ___  _ __ ___   __ _ _ __   __| |___
 * | |_  | __| '_ \  | |   / _ \| '_ ` _ \| '_ ` _ \ / _` | '_ \ / _` / __|
 * |  _| | |_| |_) | | |__| (_) | | | | | | | | | | | (_| | | | | (_| \__ \
 * |_|    \__| .__/   \____\___/|_| |_| |_|_| |_| |_|\__,_|_| |_|\__,_|___/
 *           |_|
 *
 * Commands sent from the client to the server.
 */
typedef struct _GskFtpCommand GskFtpCommand;

/* --- list of known ftp commands (Client => Server) */
typedef enum
{
  /* ---  Authorization commands.  See RFC 959, Section 4.1.1. --- */

  /* Login information */
  GSK_FTP_COMMAND_USER,
  GSK_FTP_COMMAND_PASS,
  GSK_FTP_COMMAND_ACCT,

  /* Directory-changing commands.  */
  GSK_FTP_COMMAND_CWD,
  GSK_FTP_COMMAND_CDUP,

  /* Mounting "structures" (eg filesystems) */
  GSK_FTP_COMMAND_SMNT,

  /* Clear current authorization. */
  GSK_FTP_COMMAND_REIN,		/* stay logged in */
  GSK_FTP_COMMAND_QUIT,		/* disconnect */

  /* --- Set data transport parameters.  See RFC 959, 4.1.2. --- */
  GSK_FTP_COMMAND_PORT,

  /* passive mode (client connects to remote side) */
  GSK_FTP_COMMAND_PASV,

  /* data format type */
  GSK_FTP_COMMAND_TYPE,
  GSK_FTP_COMMAND_STRU,
  GSK_FTP_COMMAND_MODE,

  /* --- Service commands --- */
  GSK_FTP_COMMAND_RETR,		/* read a file from the server */
  GSK_FTP_COMMAND_STOR,		/* write a file to the server */
  GSK_FTP_COMMAND_STOU,		/* create (don't overwrite) a file */
  GSK_FTP_COMMAND_APPE,		/* append data to the file */
  GSK_FTP_COMMAND_ALLO,		/* allocate space */
  GSK_FTP_COMMAND_REST,		/* restart transfer ???? */
  GSK_FTP_COMMAND_RNFR,		/* rename file from */
  GSK_FTP_COMMAND_RNTO,		/* rename from to */
  GSK_FTP_COMMAND_ABOR,		/* abort file transfer */
  GSK_FTP_COMMAND_DELE,		/* delet e file from the server */
  GSK_FTP_COMMAND_RMD,		/* remove a directory */
  GSK_FTP_COMMAND_MKD,		/* make a directory */
  GSK_FTP_COMMAND_PWD,		/* `print working directory' */
  GSK_FTP_COMMAND_LIST,		/* list files in dir (human-readable) */
  GSK_FTP_COMMAND_NLST,		/* list files in dir (machine-readable) */
  GSK_FTP_COMMAND_SITE,		/* site specific parameter */
  GSK_FTP_COMMAND_SYST,		/* report server system type */
  GSK_FTP_COMMAND_STAT,		/* stat a remote file */
  GSK_FTP_COMMAND_HELP,		/* request help */
  GSK_FTP_COMMAND_NOOP		/* no operation */
} GskFtpCommandType;

/* --- other helper enumerations --- */
typedef enum                            /* for ftp TYPE command */
{ 
  GSK_FTP_CHAR_TYPE_ASCII,
  GSK_FTP_CHAR_TYPE_EBCDIC,
  GSK_FTP_CHAR_TYPE_IMAGE
} GskFtpCharType;
typedef enum                            /* for ftp TYPE command */
{
  GSK_FTP_TERMINAL_NONPRINT,
  GSK_FTP_TERMINAL_TELNET,
  GSK_FTP_TERMINAL_CARRIAGE_CONTROL
} GskFtpTerminalType;
typedef enum                            /* for ftp STRU command */
{
  GSK_FTP_FILE_STRUCTURE_FILE,
  GSK_FTP_FILE_STRUCTURE_RECORD,
  GSK_FTP_FILE_STRUCTURE_PAGE
} GskFtpFileStructure;
typedef enum                            /* for ftp MODE command */
{
  GSK_FTP_TRANSFER_MODE_STREAM,
  GSK_FTP_TRANSFER_MODE_BLOCK,
  GSK_FTP_TRANSFER_MODE_COMPRESSED
} GskFtpTransferMode;



const char   *gsk_ftp_command_name (GskFtpCommandType   command_type);

struct _GskFtpCommand
{
  GskFtpCommandType      type;
  union
  {
    /* The following commands take no argument:
     *      CDUP      Change to parent directory.
     *      REIN      Reinitialize
     *      QUIT      Logout
     *      PASV      Set passive mode
     *      STOU      Store unique
     *      SYST      Find server system type.
     *      NOOP      No operation to perform.
     */
    char                *username;         /* for USER */
    char                *password;         /* for PASS */
    char                *account;          /* for ACCT */
    char                *mount_point;      /* for SMNT */
    struct                                 /* for PORT */
    {
      int                port;
      guint8             address[4];
    } data_port;
    struct 
    {                                      /* for TYPE */
      GskFtpCharType     char_type;
      GskFtpTerminalType term_type;
      int                local_byte_size;
    } representation_type;
    GskFtpFileStructure  struct_type;      /* for STRU */
    GskFtpTransferMode   transfer_mode;    /* for MODE */

    /*           The following commands use pathname:
     *                  CWD RETR STOR APPE ALLO REST RNFR RNTO
     *                  ABOR DELE RMD MKD LIST NLST STAT
     */
    char                *pathname;
    char                *help_topic;       /* for HELP (may be NULL) */
  } d;
};

/* --- constructors --- */
GskFtpCommand *gsk_ftp_command_new_user     (const char         *username);
GskFtpCommand *gsk_ftp_command_new_password (const char         *username);
GskFtpCommand *gsk_ftp_command_new_account  (const char         *account);
GskFtpCommand *gsk_ftp_command_new_mount    (const char         *mount_point);
GskFtpCommand *gsk_ftp_command_new_rep_type (GskFtpCharType      char_type,
                                             GskFtpTerminalType  terminal_type,
                                             int                 byte_size);
GskFtpCommand *gsk_ftp_command_new_file_struct 
                                            (GskFtpFileStructure file_struct);
GskFtpCommand *gsk_ftp_command_new_transfer_mode
                                            (GskFtpTransferMode  transfer_mode);
GskFtpCommand *gsk_ftp_command_new_help     (const char         *topic);
GskFtpCommand *gsk_ftp_command_new_file     (GskFtpCommandType   cmd_type,
                                             const char         *path);
GskFtpCommand *gsk_ftp_command_new_simple   (GskFtpCommandType   cmd_type);

/* --- command parsing --- */
GskFtpCommand *gsk_ftp_command_parse        (const char         *parse);

/* --- command output --- */
char          *gsk_ftp_command_to_string    (GskFtpCommand      *command);
void           gsk_ftp_command_append       (GskFtpCommand      *command,
                                             GskBuffer          *buffer);

void           gsk_ftp_command_destroy      (GskFtpCommand      *command);
/*
 *  _____ _           ____            _ _
 * |  ___| |_ _ __   |  _ \ ___ _ __ | (_) ___  ___
 * | |_  | __| '_ \  | |_) / _ \ '_ \| | |/ _ \/ __|
 * |  _| | |_| |_) | |  _ <  __/ |_) | | |  __/\__ \
 * |_|    \__| .__/  |_| \_\___| .__/|_|_|\___||___/
 *           |_|               |_|
 *
 * Replies sent back from the server to the client.
 */

/* --- Unused, but mildly interesting, reply meanings, by digit --- */
/* the `hundreds' of the reply code. */
enum _GskFtpReplyDigit0
{
  GSK_FTP_REPLY_DIGIT0_POSITIVE_PRELIM = 1,
  GSK_FTP_REPLY_DIGIT0_POSITIVE_COMPLETION = 2,
  GSK_FTP_REPLY_DIGIT0_POSITIVE_INTERMEDIATE = 3,
  GSK_FTP_REPLY_DIGIT0_NEGATIVE_TRANSIENT = 4,
  GSK_FTP_REPLY_DIGIT0_NEGATIVE_PERMANENT = 5
} GskFtpReplyDigit0;

/* the `tens' of the reply code. */
enum _GskFtpReplyDigit1
{
  GSK_FTP_REPLY_DIGIT1_SYNTAX = 0,
  GSK_FTP_REPLY_DIGIT1_INFO = 1,
  GSK_FTP_REPLY_DIGIT1_CONNECTIONS = 2,
  GSK_FTP_REPLY_DIGIT1_AUTH = 3,
  GSK_FTP_REPLY_DIGIT1_UNSPECIFIED = 4,
  GSK_FTP_REPLY_DIGIT1_FS = 5,
};

/* --- here are the specific replies --- */
enum _GskFtpReplyType
{
  GSK_FTP_REPLY_COMMAND_OK              =200,
  GSK_FTP_REPLY_COMMAND_UNRECOGNIZED    =500,
  GSK_FTP_REPLY_COMMAND_SYNTAX_ERROR    =501,
  GSK_FTP_REPLY_COMMAND_SUPERFLUOUS     =202,
  GSK_FTP_REPLY_COMMAND_UNIMPLEMENTED   =502,
  GSK_FTP_REPLY_COMMAND_BAD_SEQUENCE    =503,
  GSK_FTP_REPLY_COMMAND_UNIMP_PARAM     =504,
  GSK_FTP_REPLY_RESTART_MARKER          =110,
  GSK_FTP_REPLY_SYSTEM_STATUS           =211,
  GSK_FTP_REPLY_DIR_STATUS              =212,
  GSK_FTP_REPLY_FILE_STATUS             =213,
  GSK_FTP_REPLY_HELP_MESSAGE            =214,
  GSK_FTP_REPLY_NAME_SYSTEM             =215,
  GSK_FTP_REPLY_READY_IN_N_MINUTES      =120,
  GSK_FTP_REPLY_READY_FOR_NEW_USER      =220,
  GSK_FTP_REPLY_SERVICE_CLOSING         =221,
  GSK_FTP_REPLY_SERVICE_NOT_AVAILABLE   =421,
  GSK_FTP_REPLY_DATA_ALREADY_OPEN       =125,
  GSK_FTP_REPLY_DATA_OPEN_BUT_IDLE      =225,
  GSK_FTP_REPLY_CANNOT_OPEN_DATA        =425,
  GSK_FTP_REPLY_CLOSING_DATA            =226,
  GSK_FTP_REPLY_CLOSED_DATA             =426,
  GSK_FTP_REPLY_ENTERING_PASSIVE        =227,
  GSK_FTP_REPLY_USER_LOGGED_IN          =230,
  GSK_FTP_REPLY_NOT_LOGGED_IN           =530,
  GSK_FTP_REPLY_NEED_PASSWORD           =331,
  GSK_FTP_REPLY_NEED_ACCOUNT            =332,
  GSK_FTP_REPLY_NEED_WRITE_ACCOUNT      =532,
  GSK_FTP_REPLY_FILE_ACTION_COMPLETED   =250,
  GSK_FTP_REPLY_OK_OPENING_DATA         =150,
  GSK_FTP_REPLY_FILE_CREATED            =257,
  GSK_FTP_REPLY_ACTION_NEEDS_INFO       =350,
  GSK_FTP_REPLY_FILE_BUSY               =450,
  GSK_FTP_REPLY_FILE_MISSING            =550,
  GSK_FTP_REPLY_PAGE_TYPE_UNKNOWN       =551,
  GSK_FTP_REPLY_DISK_FULL               =452,
  GSK_FTP_REPLY_QUOTA_FULL              =552,
  GSK_FTP_REPLY_BAD_FILENAME            =553,
  GSK_FTP_REPLY_INTERNAL_ERROR          =451
} GskFtpReplyType;

struct _GskFtpReply
{
  GskFtpReplyType type;
  union
  {
    struct
    {
      char m[4];
      char d[4];
    } restart_marker;

    /* SYSTEM_STATUS, DIR_STATUS, FILE_STATUS, HELP_MESSAGE */
    char *literal;

    /* NAME_SYSTEM */
    struct
    {
      char *name;
      char *additional;
    } name_system;

    /* READY_IN_N_MINUTES */
    struct
    {
      int n_minutes;
    } n_minutes;
  } d;
};

/* --- constructors --- */
GskFtpReply *gsk_ftp_reply_new_simple      (GskFtpReplyType type);
GskFtpReply *gsk_ftp_reply_new_status      (GskFtpReplyType type,
                                            const char     *message);
GskFtpReply *gsk_ftp_reply_new_system      (const char     *system,
                                            const char     *additional);
GskFtpReply *gsk_ftp_reply_new_ready_in_n  (int             num_minutes);

/* --- reply parsing --- */
GskFtpReply *gsk_ftp_reply_parse           (const char     *raw_string);

/* --- reply output --- */
char        *gsk_ftp_reply_to_string       (GskFtpReply    *reply);
void         gsk_ftp_reply_append          (GskFtpReply    *reply,
                                            GskBuffer      *buffer);

G_END_DECLS

#endif
