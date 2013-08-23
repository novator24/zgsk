#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <netinet/in.h>
#include "gskipv4.h"

/* Standard ipv4 addresses in guint8[4] format. */

const guint8 gsk_ipv4_ip_address_any[4] =
  {
    (INADDR_ANY & 0xff000000) >> 24,
    (INADDR_ANY & 0x00ff0000) >> 16,
    (INADDR_ANY & 0x0000ff00) >>  8,
    (INADDR_ANY & 0x000000ff)
  };
const guint8 gsk_ipv4_ip_address_loopback[4] =
  {
    (INADDR_LOOPBACK & 0xff000000) >> 24,
    (INADDR_LOOPBACK & 0x00ff0000) >> 16,
    (INADDR_LOOPBACK & 0x0000ff00) >>  8,
    (INADDR_LOOPBACK & 0x000000ff)
  };


/**
 * gsk_ipv4_parse:
 * @str: string containing dotted decimal IPv4 address.
 * @ip_addr_out: the 4-byte IPv4 address.
 *
 * Parse a numeric IP address, in the standard fashion (RFC 1034, 3.6.1).
 *
 * returns: whether the address was parsed successfully.
 */
gboolean
gsk_ipv4_parse (const char *str, guint8 *ip_addr_out)
{
  char *endp;
  gulong n;
  guint i;

  for (i = 0; i < 3; ++i)
    {
      const char *dot;

      dot = strchr (str, '.');
      if (!dot)
	return FALSE;

      n = strtoul (str, &endp, 10);
      if (endp != dot)
	return FALSE;
      if (n > 255)
	return FALSE;
      ip_addr_out[i] = n;

      str = dot + 1;
    }

  n = strtoul (str, &endp, 10);
  if (endp == str || *endp)
    return FALSE;
  if (n > 255)
    return FALSE;
  ip_addr_out[3] = n;

  return TRUE;
}
