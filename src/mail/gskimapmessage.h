#ifndef __GSK_IMAP_MESSAGE_H_
#define __GSK_IMAP_MESSAGE_H_

G_BEGIN_DECLS

/* IMAP Messages */
/* Comments mostly from RFC 2060, section 2.3.2. */
typedef enum
{
  /* Message has been read */
  GSK_IMAP_MESSAGE_SEEN		= (1<<0),

  /* Message has been answered */
  GSK_IMAP_MESSAGE_ANSWERED	= (1<<1),

  /* Message is flagged for urgent/special attention */
  GSK_IMAP_MESSAGE_FLAGGED	= (1<<2),

  /* Message is deleted for removal by later EXPUNGE */
  GSK_IMAP_MESSAGE_DELETED	= (1<<3),

  /* Message has not completed composition (marked as a draft). */
  GSK_IMAP_MESSAGE_DRAFT	= (1<<4),

  /* Message is recently arrived in this mailbox.  This
   * session is the first session to have been notified
   * about this message; subsequent sessions will not see
   * \Recent set for this message.  This flag can not be
   * altered by the client.
   */
  GSK_IMAP_MESSAGE_RECENTLY	= (1<<5)
} GskImapMessageFlags

/* --- a message --- */
/* All RFC references are to RFC 2060. */
struct _GskImapMessage
{

  /* From RFC 2060, Section 2.3.1.1.
   *
   * Unique Identifier (UID) Message Attribute
   *   A 32-bit value assigned to each message, which when used with the
   *   unique identifier validity value (see below) forms a 64-bit value
   *   that is permanently guaranteed not to refer to any other message in
   *   the mailbox.  Unique identifiers are assigned in a strictly ascending
   *   fashion in the mailbox; as each message is added to the mailbox it is
   *   assigned a higher UID than the message(s) which were added
   *   previously.
   *
   * [The actual RFC entry has a lot more.
   * See ??? command and UIDVALIDITY response
   * for the other half of the aforementioned
   * "unique identifiers validity value]
   */
  guint32             unique_id;

  /* From RFC 2060, Section 2.3.1.2:
   *
   *    A relative position from 1 to the number of messages in the mailbox.
   *    This position MUST be ordered by ascending unique identifier.  As
   *    each new message is added, it is assigned a message sequence number
   *    that is 1 higher than the number of messages in the mailbox before
   *    that new message was added.
   *
   * [The actual RFC entry has a lot more.
   * This is the message index in the mailbox.
   * Of course, for a given message it could change
   * at any time,]
   */
  guint32             mailbox_index;

  /* From RFC 2060, Section 2.3.2 */
  GskImapMessageFlags flags;

  GskDate 
};

G_END_DECLS

#endif
