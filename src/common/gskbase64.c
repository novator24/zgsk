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


#include "gskbase64.h"
#include <string.h>

static guint8 *to_base64 = (guint8 *) "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                      "abcdefghijklmnopqrstuvwxyz"
			              "0123456789"
			              "+/";
static guint  end_marker = '=';

static gboolean      inited_from_base64_table = 0;
static guint8        from_base64_table[256]   = { 0 };
static void init_from_base64_table()
{
  guint8 *at;
  guint8 val = 0;
  memset (from_base64_table, 255, 256);
  for (at = to_base64; *at != '\0'; at++)
    from_base64_table[*at] = val++;
}

static void gsk_base64_decode_internal   (char            *dst,
				          guint           *dst_len,
				          int              dst_len_max,
                                          const char      *src,
					  gsize            src_len)
{
  char *start_dst = dst;
  const char *end_src = src + src_len;
  int num_bits = 0;
  guint8 partial = 0;
  if (!inited_from_base64_table)
    init_from_base64_table ();

  while (dst_len_max > 0 && *src != '=' && src < end_src)
    {
      guint8 src_b64_encoded = *src++;
      guint8 src_6bits = from_base64_table[src_b64_encoded];
      if (src_6bits == 255)
	continue;
      if (num_bits == 0)
        {
	  /* Dst len isn't changed -- merely bits are shifted into 
	   * partial.
	   */
	  partial = src_6bits << 2;
	  num_bits = 6;
	}
      else
        {
	  /* The dst will have one byte shifted into it. */
	  partial |= (src_6bits >> (num_bits - 2));
	  *dst++ = partial;
	  dst_len_max--;
	  num_bits += (6 - 8);
	  if (num_bits == 0)
	    partial = 0;
	  else
	    partial = src_6bits << (8 - num_bits);
	}
    }
  *dst_len = dst - start_dst;
}

/**
 * gsk_base64_decode:
 * @dst: output area for binary data.
 * Should be GSK_BASE64_GET_MAX_DECODED_LEN(@src) long at least.
 * @dst_len: length of buffer allocated for @dst.
 * @src: base64 encoded data.
 * @src_len: length of the binary data, or -1 to assume that @src is
 * NUL-terminated.
 *
 * Decode a base64-encoded string into binary.
 * returns: number of bytes decoded.
 */
guint
gsk_base64_decode       (char            *dst,
			 guint            dst_len,
			 const char      *src,
			 gssize           src_len)
{
  guint rv;
  if (src_len < 0)
    src_len = strlen (src);
  gsk_base64_decode_internal (dst, &rv, dst_len, src, src_len);
  return rv;
}

/**
 * gsk_base64_decode_alloc:
 * @src: base64 encoded data, NUL terminated.
 *
 * Decode a base64-encoded string into binary.
 *
 * GSK_BASE64_GET_MAX_DECODED_LEN might not be the return value,
 * because it doesn't take a terminate '=' sign into account.
 * The return value should be exactly that if @src
 * is not = terminated, or GSK_BASE64_GET_MAX_DECODED_LEN()
 * is only called on the length which precedes the = sign.
 *
 * returns: the byte-array with the binary data.
 */
GByteArray *
gsk_base64_decode_alloc (const char      *src)
{
  const char *end;
  guint rv_size;
  int len;
  int decoded_len;
  GByteArray *rv;
  end = strchr (src, '=');
  if (end == NULL)
    len = strlen (src);
  else
    len = end - src;
  rv = g_byte_array_new ();
  decoded_len = (((len * 6) + 7) / 8);
  g_byte_array_set_size (rv, decoded_len);
  gsk_base64_decode_internal ((char*)rv->data, &rv_size, rv->len, src, len);
  if (rv->len != rv_size)
    {
      g_assert (rv->len > rv_size);
      g_byte_array_set_size (rv, rv_size);
    }
  return rv;
}

/**
 * gsk_base64_encode:
 * @dst: output base64 encoded string.  The result is NOT nul-terminated,
 * but is terminated with an = sign.  @dst should be exactly
 * GSK_BASE64_GET_ENCODED_LEN(@src_len) bytes long.
 * @src: input binary data.
 * @src_len: length of @src.
 *
 * base64 encodes binary data.
 */
void
gsk_base64_encode       (char            *dst,
			 const char      *src,
			 guint            src_len)
{
  /* constraint: num_bits is either 0, 2, 4 */
  int num_bits = 0;

  /* carry has only its most-significant num_bits set */
  guint8 carry = 0;

  while (src_len-- > 0)
    {
      guint cur = (guint8) *src++;

      int use;	/* # of bits to use from `cur' for the next byte of output */

      /* shovel as many bits as will fit into carry. */
      use = 6 - num_bits;

      carry |= (cur >> (8 - use));
      *dst++ = to_base64[carry];

      if (use == 2)
        {
	  cur &= 63;
	  *dst++ = to_base64[cur];
	  num_bits = 0;
	  carry = 0;
	}
      else
        {
	  num_bits = (8 - use);
	  cur <<= (6 - num_bits);
	  cur &= 63;
	  carry = cur;
	}
    }
  if (num_bits != 0)
    *dst++ = to_base64[carry];
  *dst = end_marker;
}


/**
 * gsk_base64_encode_alloc:
 * @src: data to base64 encode.
 * @src_len: length of binary data to encode, or -1 to take @src as a NUL-terminated string.
 *
 * base64 encodes binary data (that does not contain a NULL).
 *
 * returns: an newly allocated base64 encoded NUL-terminated ASCII string.
 */
char *
gsk_base64_encode_alloc (const char      *src,
			 gssize           src_len)
{
  unsigned raw_len = (src_len >= 0) ? (gsize)src_len : strlen (src);
  unsigned enc_len = GSK_BASE64_GET_ENCODED_LEN (raw_len);
  char *buf = g_malloc (enc_len + 1);
  gsk_base64_encode (buf, src, raw_len);
  buf[enc_len] = '\0';
  return buf;
}
