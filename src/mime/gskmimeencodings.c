#include "gskmimeencodings.h"
#include "../gskmacros.h"

/* See RFC 2045, Section 6. */

/**
 * gsk_mime_make_transfer_encoding_decoders:
 * @encoding: text representation of the encoding,
 * either "identity", "base64" or "quoted-printable".
 * If NULL, default to "identity".
 * @write_end_out: location to store a reference to the
 * write-end of the pipe.  This must be unreferenced by the caller.
 * @read_end_out: location to store a reference to the
 * read-end of the pipe.  This must be unreferenced by the caller.
 * @error: location to store a #GError if anything goes wrong.
 *
 * Allocate an attached pair: one for writing and one for reading
 * the decoded data.  On error, set *@error and return FALSE.
 *
 * returns: whether the construction succeeded.  If it returns
 * FALSE, should examine *@error to figure out why/inform the user.
 */
gboolean
gsk_mime_make_transfer_encoding_decoders (const char *encoding,
		                          GskStream **write_end_out,
		                          GskStream **read_end_out,
		                          GError    **error)
{
  if (encoding == NULL
   || g_ascii_strncasecmp (encoding, "identity", 8) == 0)
    {
      *write_end_out = gsk_mime_identity_filter_new ();
      *read_end_out = g_object_ref (*write_end_out);
    }
  else if (g_ascii_strncasecmp (encoding, "base64", 6) == 0)
    {
      *write_end_out = gsk_mime_base64_decoder_new ();
      *read_end_out = g_object_ref (*write_end_out);
    }
  else if (g_ascii_strncasecmp (encoding, "quoted-printable", 16) == 0)
    {
      *write_end_out = gsk_mime_quoted_printable_decoder_new ();
      *read_end_out = g_object_ref (*write_end_out);
    }
  else
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
		   GSK_ERROR_INVALID_ARGUMENT,
		   _("unknown transfer encoding '%s' making decoder stream"),
		   encoding);
      return FALSE;
    }
  return TRUE;
}

/**
 * gsk_mime_make_transfer_encoding_encoders:
 * @encoding: text representation of the encoding,
 * either "identity", "base64" or "quoted-printable".
 * If NULL, default to "identity".
 * @write_end_out: location to store a reference to the
 * write-end of the pipe.  This must be unreferenced by the caller.
 * @read_end_out: location to store a reference to the
 * read-end of the pipe.  This must be unreferenced by the caller.
 * @error: location to store a #GError if anything goes wrong.
 *
 * Allocate an attached pair: one for writing and one for reading
 * the encoded data.  On error, set *@error and return FALSE.
 *
 * returns: whether the construction succeeded.  If it returns
 * FALSE, should examine *@error to figure out why/inform the user.
 */
gboolean
gsk_mime_make_transfer_encoding_encoders (const char *encoding,
		                          GskStream **write_end_out,
		                          GskStream **read_end_out,
					  const char *bdy_str,
		                          GError    **error)
{
  if (encoding == NULL || g_ascii_strncasecmp (encoding, "identity", 8) == 0)
    {
      *write_end_out = gsk_mime_identity_filter_new ();
      *read_end_out = g_object_ref (*write_end_out);
      // I hope boundary_str is workable...
    }
  else if (g_ascii_strncasecmp (encoding, "base64", 6) == 0)
    {
      *write_end_out = gsk_mime_base64_encoder_new ();
      *read_end_out = g_object_ref (*write_end_out);
    }
  else if (g_ascii_strncasecmp (encoding, "quoted-printable", 16) == 0)
    {
      *write_end_out = gsk_mime_quoted_printable_encoder_new ();
      *read_end_out = g_object_ref (*write_end_out);
      // TODO: pay attention to boundary_str here!!!
    }
  else
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
		   GSK_ERROR_INVALID_ARGUMENT,
		   _("unknown transfer encoding '%s' making encoder stream"),
		   encoding);
      return FALSE;
    }
  return TRUE;
}
