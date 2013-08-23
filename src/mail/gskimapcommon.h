#ifndef __GSK_IMAP_COMMON_H_
#define __GSK_IMAP_COMMON_H_

G_BEGIN_DECLS

/* REFERENCES
 *
 * IMAP RFC Sections refer to RFC 2060.
 *
 * .1 means a subsection of the Section that was last referred to.
 *
 * For example, the first .1 tag in this document, a few
 * lines below, refers to RFC 2060, 6.1.1
 */

/* IMAP Commands: these are requests from the client to the server. */
typedef enum
{
  /* RFC Section 6.1: commands that can be used in any state */
  GSK_IMAP_COMMAND_CAPABILITY,  /* .1 XXX */
  GSK_IMAP_COMMAND_NOOP,        /* .2 XXX */
  GSK_IMAP_COMMAND_LOGOUT,      /* .3 XXX */

  /* RFC Section 6.2: commands that can be used in non-authenticated states.
     All the commands from 6.1 may be used as well. */
  GSK_IMAP_COMMAND_AUTHENTICATE,/* .1 provide credentials to the server */
  GSK_IMAP_COMMAND_LOGIN        /* .2 XXX */

  /* RFC Section 6.3: commands that can be used in authenticated states.
     All the commands from 6.1 and 6.2 may be used as well. */
  GSK_IMAP_COMMAND_SELECT,      /* .1 choose a mailbox to operate box */
  GSK_IMAP_COMMAND_EXAMINE,     /* .2 choose a mailbox to read (w/o changing) */
  GSK_IMAP_COMMAND_CREATE,      /* .3 create a new mailbox */
  GSK_IMAP_COMMAND_DELETE,      /* .4 delete a mailbox */
  GSK_IMAP_COMMAND_RENAME,      /* .5 rename a mailbox */
  GSK_IMAP_COMMAND_SUBSCRIBE,   /* .6 subscribe to a list */
  GSK_IMAP_COMMAND_UNSUBSCRIBE, /* .6 unsubscribe from a list */
  GSK_IMAP_COMMAND_LIST,        /* .7 list submailboxes of a mailbox */
  GSK_IMAP_COMMAND_LSUB,        /* .8 list subscriptions for a user */
  GSK_IMAP_COMMAND_STATUS,      /* .9 request status of a mailbox */
  GSK_IMAP_COMMAND_APPEND,      /* .10 add a message to a mailbox */

  /* RFC Section 6.4: commands that can 
     All the commands from 6.1, 6.2 and 6.3 may be used as well. */
  GSK_IMAP_COMMAND_CHECK,	/* .1 make mailbox checkpoint */
  GSK_IMAP_COMMAND_CLOSE,	/* .2 close mailbox; remove deleted messages */
  GSK_IMAP_COMMAND_EXPUNGE,	/* .3 remove deleted messages */
  GSK_IMAP_COMMAND_SEARCH,	/* .4 search mailbox */
  GSK_IMAP_COMMAND_FETCH,	/* .5 fetch items from a message */
  GSK_IMAP_COMMAND_STORE,	/* .6 alter a messages' contents */
  GSK_IMAP_COMMAND_COPY,	/* .7 copy message to another mailbox */
  GSK_IMAP_COMMAND_UID		/* .8 get unique-ids */

} GskImapCommandType;



/* ideas: a MAILBOX is specified by a name which MIGHT
 * be hierarchical.  for example, on unix a '/' is used
 * to denote subdirectory hierarchy.  
 * However, the hierarchy-delimited is unspecified,
 * except that it must be a single character.
 *
 * TODO: research actual usage.
 */

G_END_DECLS

#endif
