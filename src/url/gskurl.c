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


#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "gskurl.h"
#include "../gskmacros.h"

static GObjectClass *parent_class = NULL;

#define IS_SCHEME_CHAR(c)			\
	(isalnum(c) || (c) == '+' || (c) == '-' || (c) == '.')

typedef enum
{
  GSK_URL_INTERPRETATION_RELATIVE, /* relative url */
  GSK_URL_INTERPRETATION_ABSOLUTE, /* same host, absolute url */
  GSK_URL_INTERPRETATION_REMOTE,   /* url on remote host */
  GSK_URL_INTERPRETATION_UNKNOWN
} GskUrlInterpretation;

static const char *
gsk_url_scheme_name (GskUrlScheme scheme)
{
  switch (scheme)
    {
      case GSK_URL_SCHEME_FILE: return "file";
      case GSK_URL_SCHEME_HTTP: return "http";
      case GSK_URL_SCHEME_HTTPS: return "https";
      case GSK_URL_SCHEME_FTP: return "ftp";
      case GSK_URL_SCHEME_OTHER: return "?other?";
      default: return NULL;
    }
}

/* general sanity check */
gboolean
gsk_url_is_valid_hostname (const char *hostname, char *bad_char_out)
{
  while (*hostname)
    {
      if (!isalnum (*hostname)
       && *hostname != '-'
       && *hostname != '-'
       && *hostname != '.')
        {
          *bad_char_out = *hostname;
          return FALSE;
        }
      hostname++;
    }
  return TRUE;
}

gboolean
gsk_url_is_valid_generic_component (const char *str, char *bad_char_out)
{
  while (33 <= *str && *str <= 126)
    str++;
  if (*str == 0)
    return TRUE;
  *bad_char_out = *str;
  return FALSE;
}

static inline gboolean
url_check_is_valid (GskUrl *url, GError **error)
{
  char bad_char;
  if (url->host && !gsk_url_is_valid_hostname (url->host, &bad_char))
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_INVALID_ARGUMENT,
	           "URL %s constructed uses unallowed character '%c' (0x%02x)",
                   "host", bad_char, bad_char);
      return FALSE;
    }
  if (url->path && !gsk_url_is_valid_path (url->path, &bad_char))
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_INVALID_ARGUMENT,
	           "URL %s constructed uses unallowed character '%c' (0x%02x)",
                   "path", bad_char, bad_char);
      return FALSE;
    }
  if (url->query && !gsk_url_is_valid_query (url->query, &bad_char))
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_INVALID_ARGUMENT,
	           "URL %s constructed uses unallowed character '%c' (0x%02x)",
                   "query", bad_char, bad_char);
      return FALSE;
    }
  if (url->fragment && !gsk_url_is_valid_fragment (url->fragment, &bad_char))
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_INVALID_ARGUMENT,
	           "URL %s constructed uses unallowed character '%c' (0x%02x)",
                   "fragment", bad_char, bad_char);
      return FALSE;
    }
  return TRUE;
}

static GskUrl *
gsk_url_new_from_scheme_specific  (GskUrlScheme       scheme,
				   const char        *spec,
				   GError           **error)
{
  int num_slashes;
  const char *start = spec;
  GskUrlInterpretation interpretation = GSK_URL_INTERPRETATION_UNKNOWN;
  GskUrl *url;

  char *host, *user_name, *password, *path, *query, *fragment;
  int port;

  num_slashes = 0;
  while (*spec == '/')
    {
      num_slashes++;
      spec++;
    }
  if (scheme == GSK_URL_SCHEME_FILE)
    interpretation = GSK_URL_INTERPRETATION_ABSOLUTE;
  else
    switch (num_slashes)
      {
	case 0:
	  interpretation = GSK_URL_INTERPRETATION_RELATIVE;
	  break;
	case 1:
	  interpretation = GSK_URL_INTERPRETATION_ABSOLUTE;
	  break;
	case 2:
	  /* ``schemes including a top hierarchical element for a naming
	   *   authority'' (Section 3.2)
	   */
	  interpretation = GSK_URL_INTERPRETATION_REMOTE;
	  break;
	case 3:
	  /* File urls (well those are now handled above so this
	   * is pretty dubious)
	   */
	  interpretation = GSK_URL_INTERPRETATION_ABSOLUTE;
	  break;
	default:
	  /* syntax error? */
	  break;
      }


  host = NULL;
  port = 0;
  user_name = NULL;
  path = NULL;
  query = NULL;
  fragment = NULL;
  password = NULL;

  switch (interpretation)
    {
      case GSK_URL_INTERPRETATION_REMOTE:
	/* rfc 2396, section 3.2.2. */
	{
	  const char *end_hostport;
	  const char *host_start;
	  const char *host_end;
	  const char *at_sign;
	  const char *colon;
	  /* basically the syntax is:
           *    USER@HOST:PORT/
           *        ^    |    ^
           *     at_sign ^  end_hostport
           *            colon
           */             
	  end_hostport = strchr (spec, '/');
	  if (end_hostport == NULL)
#if 1
            end_hostport = strchr (spec, 0);
#else           /* too strict for casual use ;) */
	    {
	      /* TODO: it's kinda hard to pinpoint where this
		 is specified.  See Section 3 in RFC 2396. */
	      g_set_error (error, GSK_G_ERROR_DOMAIN,
			   GSK_ERROR_INVALID_ARGUMENT,
			   _("missing / after host in URL"));
	      return NULL;
	    }
#endif
	  at_sign = memchr (spec, '@', end_hostport - spec);
	  host_start = at_sign != NULL ? (at_sign + 1) : spec;
	  colon = memchr (host_start, ':', end_hostport - host_start);
	  if (at_sign != NULL)
	    {
              const char *password_sep = memchr (spec, ':', at_sign - spec);
              if (password_sep)
                {
                  user_name = g_strndup (spec, password_sep - spec);
                  password = g_strndup (password_sep + 1,
                                        at_sign - (password_sep + 1));
                }
              else
                {
                  user_name = g_strndup (spec, at_sign - spec);
                }
	      /* XXX: should validate username against 
	       *         GSK_URL_USERNAME_CHARSET
	       */
	    }
	  host_end = colon != NULL ? colon : end_hostport;
	  host = g_strndup (host_start, host_end - host_start);

	  if (colon != NULL)
	    port = atoi (colon + 1);

	  spec = end_hostport;
          if (*spec == 0)
            {
              GskUrl *url;
              url = gsk_url_new_from_parts (scheme, host, port,
                                            NULL, NULL, "/", NULL, NULL);
              g_free (host);
              return url;
            }
	}

	/* fall through to parse the host-specific part of the url */
      case GSK_URL_INTERPRETATION_RELATIVE:
      case GSK_URL_INTERPRETATION_ABSOLUTE:
        {
	  const char *query_start;
	  const char *frag_start;
	  if (num_slashes > 0
           && interpretation == GSK_URL_INTERPRETATION_ABSOLUTE)
	    spec--;
	  query_start = strchr (spec, '?');
	  frag_start = strchr (query_start != NULL ? query_start : spec, '#');
	  if (query_start != NULL)
	    path = g_strndup (spec, query_start - spec);
	  else if (frag_start != NULL)
	    path = g_strndup (spec, frag_start - spec);
	  else
	    path = g_strdup (spec);
	  if (query_start != NULL)
	    {
	      if (frag_start != NULL)
		query = g_strndup ((query_start+1), frag_start - (query_start+1));
	      else
		query = g_strdup (query_start + 1);
	    }
	  if (frag_start != NULL)
	    fragment = g_strdup (frag_start + 1);
	  break;
	}
      case GSK_URL_INTERPRETATION_UNKNOWN:
        {
	  g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		       _("cannot guess how to interpret %s:%s"),
	  	       gsk_url_scheme_name (scheme), start);
	  goto error;
	}
    }

  if (interpretation == GSK_URL_INTERPRETATION_REMOTE
  && (host == NULL || host[0] == '\0' || !isalnum (host[0])))
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		   _("malformed host: should begin with a letter or number (%s)"),
		   host);
      goto error;
    }



  url = g_object_new (GSK_TYPE_URL, NULL);
  url->scheme = scheme;
  if (scheme == GSK_URL_SCHEME_OTHER)
    url->scheme_name = NULL;
  else
    url->scheme_name = (char *) gsk_url_scheme_name (scheme);
  url->host = host;
  url->user_name = user_name;
  url->password = password;
  url->query = query;
  url->fragment = fragment;
  url->port = port;
  url->path = path;

  if (!url_check_is_valid (url, error))
    {
      g_object_unref (url);
      return NULL;
    }
  return url;

error:
  g_free (host);
  g_free (user_name);
  g_free (password);
  g_free (query);
  g_free (fragment);
  g_free (path);
  return NULL;
}

/**
 * gsk_url_get_relative_path:
 * @url: the URL to get the host-relative path from.
 *
 * Obtain the path portion of a URL without
 * the initial slash (/) character.
 *
 * The query component and fragment are also returned.
 *
 * returns: the URL as a string.  This must be freed by the caller.
 */
char *
gsk_url_get_relative_path (GskUrl *url)
{
  GString *string = g_string_new ("");
  g_string_append (string, url->path);
  if (url->query != NULL)
    {
      g_string_append_c (string, '?');
      g_string_append (string, url->query);
    }
  if (url->fragment != NULL)
    {
      g_string_append_c (string, '#');
      g_string_append (string, url->fragment);
    }
  return g_string_free (string, FALSE);
}

/**
 * gsk_url_new_from_parts:
 * @scheme: the type of URL being created.
 * @host: the name (or numeric address as ASCII digits and dots) of the host.
 * This is called the Authority by RFC 2396, Section 3.2.
 * @port: the port number to use for the service, or 0 to use the default port
 * for this type of URL scheme.  For FTP, this is the control port and the data port
 * will default to the next integer.
 * @user_name: optional username identifier from the client.
 * @password: optional password to authenticate.
 * @path: the host-relative path for the URL
 * @query: optional query string for URL.
 * @fragment: optional information about a sublocation in the resource.
 *
 * Allocate a new URL from a bunch of pieces.
 *
 * returns: a reference to a new URL object.
 */
GskUrl *
gsk_url_new_from_parts      (GskUrlScheme     scheme,
			     const char      *host,
			     int              port,
			     const char      *user_name,
			     const char      *password,
			     const char      *path,
			     const char      *query,
			     const char      *fragment)
{
  GskUrl *url = g_object_new (GSK_TYPE_URL, NULL);
  url->scheme = scheme;
  url->scheme_name = (char *) gsk_url_scheme_name (scheme);
  url->host = g_strdup (host);
  url->port = port;
  url->user_name = g_strdup (user_name);
  url->password = g_strdup (password);
  url->path = g_strdup (path);
  url->query = g_strdup (query);
  url->fragment = g_strdup (fragment);
  return url;
}

static void
gsk_url_finalize(GObject *object)
{
  GskUrl *url = GSK_URL (object);
  if (url->scheme == GSK_URL_SCHEME_OTHER)
    g_free (url->scheme_name);
  g_free (url->host);
  g_free (url->user_name);
  g_free (url->path);
  g_free (url->query);
  g_free (url->fragment);
  (*parent_class->finalize) (object);
}

typedef struct _UrlSchemeTableEntry UrlSchemeTableEntry;
struct _UrlSchemeTableEntry
{
  char        *name;
  GskUrlScheme scheme;
};

static void
skip_scheme (const char **ptr)
{
  /* RFC 2396, Section 3.1 */
  if (isalpha (**ptr))
    (*ptr)++;
  else
    return;
  while (**ptr && (IS_SCHEME_CHAR (**ptr)))
    (*ptr)++;
}

static int pstrcmp (const void *a, const void *b)
{
  return strcmp (*(char**)a, *(char**)b);
}

static gboolean lookup_scheme_from_name (const char     *scheme_start,
                                         const char     *scheme_end,
					 GskUrlScheme   *scheme_out)
{
  static UrlSchemeTableEntry table[] = {
    /* MUST BE SORTED */
    { "file", GSK_URL_SCHEME_FILE },
    { "ftp", GSK_URL_SCHEME_FTP },
    { "http", GSK_URL_SCHEME_HTTP },
    { "https", GSK_URL_SCHEME_HTTPS },
  };
  int i;
  UrlSchemeTableEntry tmp;
  UrlSchemeTableEntry *entry;
  #define NUM_SCHEMES 		G_N_ELEMENTS (table)
  tmp.name = alloca (scheme_end - scheme_start + 1);
  for (i = 0; i < scheme_end - scheme_start; i++)
    tmp.name[i] = tolower (scheme_start[i]);
  tmp.name[i] = '\0';
  entry = bsearch (&tmp, table, G_N_ELEMENTS (table),
		   sizeof (UrlSchemeTableEntry), pstrcmp);
  if (entry == NULL)
    return FALSE;
  *scheme_out = entry->scheme;
  return TRUE;
}

/**
 * gsk_url_new:
 * @spec: standard string representation of the URL.
 * @error: place to store a #GError if an error occurs.
 *
 * Parse a URL object from a string.
 *
 * returns: a reference to a new URL object, or NULL if an error occurred.
 */
GskUrl       *gsk_url_new           (const char      *spec,
				     GError         **error)
{
  const char *scheme_start;
  const char *scheme_end;
  GskUrlScheme scheme;

  scheme_start = spec;
  skip_scheme (&spec);
  scheme_end = spec;

  if (*spec != ':')
    {
      /* Url scheme did not end in ':' */
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "URL did not begin scheme:");
      return NULL;
    }
  scheme_end = spec;
  /* skip the colon */
  spec++;
  if (!lookup_scheme_from_name (scheme_start, scheme_end, &scheme))
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "URL did not begin with known scheme");
      return NULL;
    }

  return gsk_url_new_from_scheme_specific (scheme, spec, error);
}

/**
 * gsk_url_new_in_context:
 * @spec: rough URL specification.  This may be a complete URL,
 * or it may have an implied scheme.
 * @context: default scheme for URL's in your context.
 * @error: place to store a #GError if something goes wrong.
 *
 * For places where you expect a certain type of URL,
 * soemtimes people get lazy and drop the scheme.
 * We support this here, by allowing a "backup scheme"
 * to be specified.
 *
 * To be fully paranoid in such a situation, you may wish to
 * if there appears to be a scheme, use gsk_url_new();
 * otherwise call gsk_url_new_from_scheme_specific() directly.
 * Alternately, it may be easier just to call
 * gsk_url_new_in_context() directly all the time.
 *
 * See also gsk_url_new_relative().
 *
 * returns: a newly allocated URL object.
 */
GskUrl       *gsk_url_new_in_context(const char      *spec,
                                     GskUrlScheme     context,
				     GError         **error)
{
  const char *scheme_start;
  const char *scheme_end;
  GskUrlScheme scheme;
  scheme_start = spec;
  skip_scheme (&spec);
  scheme_end = spec;
  if (scheme_start == scheme_end)
    scheme = context;
  else
    {
      if (!lookup_scheme_from_name (scheme_start, scheme_end, &scheme))
	{
	  g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_INVALID_ARGUMENT,
		       _("unknown url scheme (start of '%s')"), scheme_start);
	  return NULL;
	}
      /* skip the colon */
      spec++;
    }
  
  return gsk_url_new_from_scheme_specific (scheme, spec, error);
}

/**
 * gsk_url_new_relative:
 * @base_url: context of the @spec found.  This tells where @spec
 * may be relative to.
 * @location: the possibly relative spec.
 * @error: place to store a #GError if something goes wrong.
 *
 * Allocate a new URL, which will be taken
 * to be relative to @base_url if the @location
 * is not obviously an absolute URL.
 *
 * Note that there is some ambiguity in how relative urls are
 * interpreted.  Note especially that
 *    /foo + /bar = /bar.
 *    /foo +  bar = /bar.
 *    /foo/ + bar = /foo/bar.
 * That is, a symbol with a trailing slash is a directory,
 * otherwise the last piece of the url is assumed to be a file.
 *
 * returns: a newly allocated URL object.
 */
GskUrl *
gsk_url_new_relative  (GskUrl     *base_url,
		       const char *location,
		       GError    **error)
{
  /* XXX: what is the right way to determine if a string is
   * 	    a absolute v. relative url???
   * XXX: definitely NOT this, which doesn't have
   *      http:foo/bar.html
   */
/* TODO: See RFC 2396 section 5, "Relative URI References"? */

  /* if we have a ':' before a '/' character,
     then assume a full url. */
  const char *tmp;

  GSK_SKIP_WHITESPACE (location);

  if (*location == 0)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_INVALID_ARGUMENT,
                   "gsk_url_new_relative: location was empty");
      return NULL;
    }

  tmp = location;
  while (*tmp && *tmp != '/' && *tmp != ':')
    tmp++;
  if (*tmp == ':')
    {
      /* absolute redirect */
      return gsk_url_new (location, error);
    }
  else
    {
      const char *query_start = strchr (location, '?');
      const char *frag_start = strchr (query_start ? query_start : location, '#');
      const char *location_end = query_start ? query_start
                                : frag_start ? frag_start
                                : strchr (location, 0);
      char *query = NULL;
      char *fragment = NULL;
      char *path;
      guint path_len;
      char bad_char;
      GskUrl *rv;
      if (query_start)
	{
          query_start++;
	  query = g_alloca (strlen (query_start));
	  if (frag_start)
	    {
	      memcpy (query, query_start + 1, frag_start - query_start);
	      query[frag_start - query_start] = 0;
	    }
	  else
	    {
	      strcpy (query, query_start);
	    }
	}
      if (frag_start)
	fragment = strcpy (g_alloca (strlen (frag_start)), frag_start + 1);

      path_len = location_end - location;
      if (*location == '/')
	{
	  path = memcpy (g_alloca (path_len + 1), location, path_len);
	  path[path_len] = 0;
	}
      else
	{
	  const char *last_slash = strrchr (base_url->path, '/');
	  guint len, total_len;
          guint location_len = location_end - location;
	  if (!last_slash)
	    len = strlen (base_url->path);
	  else
	    len = last_slash - base_url->path;

	  /* TODO: deal with '.' and '..' */

	  total_len = len + 1 + location_len;
	  path = g_alloca (total_len + 1);
	  memcpy (path, base_url->path, len);
	  path[len] = '/';
	  memcpy (path + len + 1, location, location_len);
          path[len + 1 + location_len] = '\0';
	}
      if (path && !gsk_url_is_valid_path (path, &bad_char))
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_INVALID_ARGUMENT,
	           "URL %s constructed uses unallowed character '%c' (0x%02x)",
                   "relative path", bad_char, bad_char);
          return NULL;
        }
      if (query && !gsk_url_is_valid_query (query, &bad_char))
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_INVALID_ARGUMENT,
	           "URL %s constructed uses unallowed character '%c' (0x%02x)",
                   "query", bad_char, bad_char);
          return NULL;
        }
      if (fragment && !gsk_url_is_valid_fragment (fragment, &bad_char))
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_INVALID_ARGUMENT,
	           "URL %s constructed uses unallowed character '%c' (0x%02x)",
                   "fragment", bad_char, bad_char);
          return NULL;
        }
      rv = gsk_url_new_from_parts (base_url->scheme,
                                   base_url->host,
                                   base_url->port,
                                   base_url->user_name,
                                   base_url->password,
                                   path, query, fragment);
      if (!url_check_is_valid (rv, error))
        {
          g_object_unref (rv);
          return NULL;
        }
      return rv;
    }
}

/**
 * gsk_url_to_string:
 * @url: the URL to stringify.
 *
 * Convert the URL to a string.
 *
 * returns: the newly allocated string.
 */
char *
gsk_url_to_string (const GskUrl *url)
{
  guint len = strlen (url->scheme_name)
            + 4         /* :/// (max) */
            + (url->host ? strlen (url->host) : 0)
            + 3
            + (url->password ? strlen (url->password) : 0)
            + 10        /* port */
            + (url->user_name ? strlen (url->user_name) : 0)
            + 1
            + (url->path ? strlen (url->path) : 0)
            + 1
            + (url->query ? strlen (url->query) : 0)
            + 1
            + (url->fragment ? strlen (url->fragment) : 0)
            + 10        /* extra! */
            ;
  char *rv = g_malloc (len);
  char *at = rv;
#define ADD_STR(str)    \
  G_STMT_START{ strcpy(at,str); at=strchr(at,0); }G_STMT_END
#define ADD_CHAR(c)    \
  G_STMT_START{ *at++ = c; }G_STMT_END
  ADD_STR (url->scheme_name);
  if (url->scheme == GSK_URL_SCHEME_FILE)
    ADD_STR ("://");    /* note: the path typically includes one more '/' */
  else if (url->host != NULL)
    ADD_STR ("://");
  else
    ADD_STR (":");
  if (url->user_name)
    {
      ADD_STR (url->user_name);
      if (url->password)
        {
          ADD_CHAR (':');
          ADD_STR (url->password);
        }
      ADD_CHAR ('@');
    }
  if (url->host)
    {
      ADD_STR (url->host);
    }
  if (url->port)
    {
      char buf[64];
      g_snprintf(buf,sizeof(buf),":%u", url->port);
      ADD_STR (buf);
    }
  if (url->path)
    ADD_STR (url->path);
  if (url->query)
    {
      ADD_CHAR ('?');
      ADD_STR (url->query);
    }
  if (url->fragment)
    {
      ADD_CHAR ('#');
      ADD_STR (url->fragment);
    }
  *at = 0;

  return rv;
}

/**
 * gsk_url_get_port:
 * @url: the URL whose port is desired.
 *
 * Returns the port.  If the port is 0, the default port
 * for the type of scheme is returned (80 for HTTP, 21 for FTP
 * and 443 for HTTP/SSL).  If no default exists, 0 is returned.
 *
 * returns: the port as an integer, or 0 if no port could be computed.
 */
guint
gsk_url_get_port (const GskUrl *url)
{
  if (url->port == 0)
    {
      switch (url->scheme)
	{
	case GSK_URL_SCHEME_HTTP:
	  return 80;
	case GSK_URL_SCHEME_HTTPS:
	  return 443;
	case GSK_URL_SCHEME_FTP:
	  return 21;

	case GSK_URL_SCHEME_FILE:
	case GSK_URL_SCHEME_OTHER:
	  return 0;
	}
    }
  return url->port;
}

/* --- arguments --- */
enum
{
  PROP_0,
  PROP_HOST,
  PROP_PASSWORD,
  PROP_PORT,
  PROP_USER_NAME,
  PROP_PATH,
  PROP_QUERY,
  PROP_FRAGMENT,
};

static void
gsk_url_get_property (GObject        *object,
		      guint           property_id,
		      GValue         *value,
		      GParamSpec     *pspec)
{
  GskUrl *url = GSK_URL (object);
  switch (property_id)
    {
    case PROP_HOST:
      g_value_set_string (value, url->host);
      break;
    case PROP_PASSWORD:
      g_value_set_string (value, url->password);
      break;
    case PROP_PORT:
      g_value_set_uint (value, gsk_url_get_port (url));
      break;
    case PROP_USER_NAME:
      g_value_set_string (value, url->user_name);
      break;
    case PROP_PATH:
      g_value_set_string (value, url->path);
      break;
    case PROP_QUERY:
      g_value_set_string (value, url->query);
      break;
    case PROP_FRAGMENT:
      g_value_set_string (value, url->fragment);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}


static void
gsk_url_set_property (GObject        *object,
		      guint           property_id,
		      const GValue   *value,
		      GParamSpec     *pspec)
{
  GskUrl *url = GSK_URL (object);
  switch (property_id)
    {
    case PROP_HOST:
      g_free (url->host);
      url->host = g_strdup (g_value_get_string (value));
      break;
    case PROP_PASSWORD:
      g_free (url->password);
      url->password = g_strdup (g_value_get_string (value));
      break;
    case PROP_PORT:
      url->port = g_value_get_uint (value);
      break;
    case PROP_USER_NAME:
      g_free (url->user_name);
      url->user_name = g_strdup (g_value_get_string (value));
      break;
    case PROP_PATH:
      g_free (url->path);
      url->path = g_strdup (g_value_get_string (value));
      break;
    case PROP_QUERY:
      g_free (url->query);
      url->query = g_strdup (g_value_get_string (value));
      break;
    case PROP_FRAGMENT:
      g_free (url->fragment);
      url->fragment = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gsk_url_init (GskUrl *url)
{
  url->scheme = GSK_URL_SCHEME_OTHER;
}

static void
gsk_url_class_init (GskUrlClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GParamSpec *pspec;
  parent_class = g_type_class_peek_parent (class);
  object_class->set_property = gsk_url_set_property;
  object_class->get_property = gsk_url_get_property;
  object_class->finalize = gsk_url_finalize;
  pspec = g_param_spec_string ("host",
			       _("Host Name"),
			       _("name of host having resource"),
			       NULL,
                               G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_HOST, pspec);

  pspec = g_param_spec_string ("password",
			       _("Password"),
			       _("password protecting resource"),
			       NULL,
                               G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_PASSWORD, pspec);

  pspec = g_param_spec_uint ("port",
			     _("Port"),
			     _("port for resource (or 0 for default)"),
			     0, 65536, 0,
			     G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_PORT, pspec);

  pspec = g_param_spec_string ("user-name",
			       _("Username"),
			       _("username for resource"),
			       NULL,
                               G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_USER_NAME, pspec);

  pspec = g_param_spec_string ("path",
			       _("Path"),
			       _("Path on the server to the resource"),
			       NULL,
                               G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_PATH, pspec);

  pspec = g_param_spec_string ("query",
			       _("Query"),
			       _("Query (for HTTP resources)"),
			       NULL,
                               G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_QUERY, pspec);

  pspec = g_param_spec_string ("fragment",
			       _("Fragment"),
			       _("Fragment (for HTTP resources)"),
			       NULL,
                               G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_FRAGMENT, pspec);
}

GType
gsk_url_get_type()
{
  static GType url_type = 0;
  if (!url_type)
    {
      static const GTypeInfo url_info =
      {
	sizeof(GskUrlClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_url_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskUrl),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_url_init,
	NULL		/* value_table */
      };
      url_type = g_type_register_static (G_TYPE_OBJECT,
                                                  "GskUrl",
						  &url_info, 0);
    }
  return url_type;
}

/**
 * gsk_url_hash:
 * @url: a url.
 *
 * Compute a randomish hash code based on the URL.
 *
 * You can create a GHashTable that's keyed off of URLs with:
 *   g_hash_table_new((GHashFunc)gsk_url_hash,
 *                    (GEqualFunc)gsk_url_equal);
 *
 * returns: the hash code.
 */
guint           gsk_url_hash                (const GskUrl    *url)
{
  guint rv = 0;
  rv += g_str_hash (url->scheme_name);
  if (url->host)
    rv += 33 * g_str_hash (url->host);
  if (url->password)
    rv += 1001 * g_str_hash (url->password);
  rv += 11 * url->port;
  if (url->user_name)
    rv ^= g_str_hash (url->user_name);
  if (url->path)
    rv ^= 101 * g_str_hash (url->path);
  if (url->query)
    rv ^= 10009 * g_str_hash (url->query);
  if (url->fragment)
    rv += 100001 * g_str_hash (url->fragment);
  return rv;
}
static inline gboolean safe_strs_equal (const char *a, const char *b)
{
  if (a == NULL && b == NULL)
    return TRUE;
  if (a == NULL || b == NULL)
    return FALSE;
  return strcmp (a,b) == 0;
}

/**
 * gsk_url_equal:
 * @a: a url.
 * @b: another url.
 *
 * Test to see if two URLs are the same.
 *
 * returns: whether the URLs are the same.
 */
gboolean gsk_url_equal (const GskUrl *a,
                        const GskUrl *b)
{
  return safe_strs_equal (a->scheme_name, b->scheme_name)
      && safe_strs_equal (a->host, b->host)
      && safe_strs_equal (a->password, b->password)
      && a->port == b->port
      && safe_strs_equal (a->user_name, b->user_name)
      && safe_strs_equal (a->path, b->path)
      && safe_strs_equal (a->query, b->query)
      && safe_strs_equal (a->fragment, b->fragment);
}

/*
 * True if the ascii character c should be escaped within a URI.
 * See RFC 2396, section 2.
 *
 * According to section 2.4: "data must be escaped if it does not have a
 * representation using an unreserved character," where unreserved
 * characters are (section 2.3): "upper and lower case letters, decimal
 * digits, and a limited set of punctuation marks and symbols" [see below].
 */

static guint8 should_be_escaped_data[16] =
{
  0xff, 0xff, 0xff, 0xff, 0x7d, 0x98, 0x00, 0xfc,
  0x01, 0x00, 0x00, 0x78, 0x01, 0x00, 0x00, 0xb8,
};
static inline gboolean
should_be_escaped (char c)
{
  if (c & 0x80)
    return TRUE;
  return (should_be_escaped_data[c>>3] & (1<<(7&c))) != 0;
}

static const char *hex_characters = "0123456789abcdef";

/**
 * gsk_url_encode:
 * @decoded: decoded data to escape.
 *
 * Encode characters to be passed in a URL.
 * Basically, "unsafe" characters are converted
 * to %xx where 'x' is a hexidecimal digit.
 *
 * See RFC 2396 Section 2.
 * 
 * returns: a newly allocated string.
 */
char *
gsk_url_encode (const char      *raw)
{
  int length = 0;
  const char *at;
  char *out;
  char *rv;
  for (at = raw; *at != '\0'; at++)
    if (should_be_escaped (*at))
      length += 3;
    else
      length += 1;
  rv = g_new (char, length + 1);
  out = rv;
  for (at = raw; *at != '\0'; at++)
    if (should_be_escaped (*at))
      {
        *out++ = '%';
	*out++ = hex_characters [((guint8) *at) >> 4];
	*out++ = hex_characters [((guint8) *at) & 0xf];
      }
    else
      {
	*out = *at;
        out++;
      }
  *out = '\0';
  return rv;
}

/**
 * gsk_url_decode:
 * @encoded: encoded URL to convert to plaintext.
 *
 * Decode characters to be passed in a URL.
 * Basically, any %xx string is changed to the
 * character whose ASCII code is xx, treating xx as
 * a hexidecimal 2-digit number.
 *
 * See RFC ??, Section ??.
 * 
 * returns: a newly allocated string.
 */
char *
gsk_url_decode  (const char      *encoded)
{
  const char *at = encoded;
  int length = 0;
  char *rv;
  char *out;
  while (*at != '\0')
    {
      if (*at == '%')
        {
	  if (at[1] == '\0' || at[2] == '\0')
	    {
	      g_warning ("malformed URL encoded string");
	      return NULL;
	    }
	  at += 3;
	  length++;
	}
      else
	{
	  at++;
	  length++;
	}
    }
  rv = g_new (char, length + 1);
  out = rv;
  at = encoded;
  while (*at != '\0')
    {
      if (*at == '%')
        {
	  char hex[3];
	  hex[0] = at[1];
	  hex[1] = at[2];
	  hex[2] = '\0';
	  if (at[1] == '\0' || at[2] == '\0')
	    return NULL;
	  at += 3;
	  *out++ = (char) strtol (hex, NULL, 16);
	}
      else
	{
	  *out++ = *at++;
	  length++;
	}
    }
  *out = '\0';
  return rv;
}

/**
 * gsk_url_encode_http:
 * @decoded: the raw url text; this is treated as raw 8-bit data,
 * not UTF-8.
 *
 * Do what is typically thought of
 * as "url encoding" in http-land... namely SPACE maps to '+'
 * and funny characters are encoded
 * as %xx where 'x' denotes a single hex-digit.
 *
 * returns: a newly allocated encoded string that the caller
 * must free.
 */
char *
gsk_url_encode_http (const char *decoded)
{
  const char *at;
  guint len = 0;
  char *rv;
  char *rv_at;
  for (at = decoded; *at != '\0'; at++)
    {
      if (*at != ' ' && should_be_escaped (*at))
	len += 3;
      else
	len++;
    }

  rv = g_malloc (len + 1);
  rv_at = rv;
  for (at = decoded; *at != '\0'; at++)
    {
      if (*at == ' ')
	*rv_at++ = '+';
      else if (should_be_escaped (*at))
	{
	  *rv_at++ = '%';
	  *rv_at++ = hex_characters [((guint8) *at) >> 4];
	  *rv_at++ = hex_characters [((guint8) *at) & 0xf];
	}
      else
	*rv_at++ = *at;
    }
  *rv_at = '\0';
  return rv;
}

/**
 * gsk_url_encode_http_binary:
 * @decoded: the raw binary data: may contain NULs.
 * @length: length of the binary data, in bytes.
 *
 * Do what is typically thought of
 * as "url encoding" in http-land... namely SPACE maps to '+'
 * and funny characters are encoded
 * as %xx where 'x' denotes a single hex-digit.
 *
 * returns: a newly allocated encoded string that the caller
 * must free.
 */
char *
gsk_url_encode_http_binary (const guint8 *decoded,
                            guint         length)
{
  guint rv_len = length;
  char *rv;
  char *at;
  guint i;
  for (i = 0; i < length; i++)
    if (should_be_escaped (decoded[i]))
      rv_len += 2;
  rv = g_malloc (rv_len + 1);
  at = rv;
  for (i = 0; i < length; i++)
    if (should_be_escaped (decoded[i]))
      {
        *at++ = '%';
        *at++ = hex_characters[decoded[i] >> 4];
        *at++ = hex_characters[decoded[i] & 0xf];
      }
    else
      *at++ = decoded[i];
  *at = 0;
  return rv;
}

/**
 * gsk_url_decode_http:
 * @encoded: the encoded url text.
 *
 * Do what is typically thought of
 * as "url decoding" in http-land... namely '+' maps to SPACE
 * and %xx, where 'x' denotes a single hex-digit, maps to the character
 * given as hexidecimal.  (warning: the resulting string is not UTF-8)
 *
 * returns: a newly allocated encoded string that the caller
 * must free (the empty string "" when unable to decode hex).
 */
char *
gsk_url_decode_http (const char *encoded)
{
  const char *at;
  guint len = 0;
  char *rv;
  char *rv_at;
  for (at = encoded; *at != '\0'; at++)
    {
      if (*at == '%')
	{
	  at++;
	  if (!isxdigit(*at))
	    return g_strdup ("");
	  at++;
	  if (!isxdigit(*at))
	    return g_strdup ("");
	  len++;
	}
      else
	{
	  len++;
	}
    }
  rv = g_malloc (len + 1);
  rv_at = rv;
  for (at = encoded; *at != '\0'; at++)
    {
      if (*at == '%')
	{
	  char hex[3];
	  hex[0] = *(++at);
	  hex[1] = *(++at);
	  hex[2] = 0;
	  *rv_at++ = (char) strtol (hex, NULL, 16);
	}
      else if (*at == '+')
	*rv_at++ = ' ';
      else
	*rv_at++ = *at;
    }
  *rv_at = '\0';
  return rv;
}

/* gsk_url_split_form_urlencoded:
 * @encoded_query: the encoded form data
 *
 * Split an "application/x-www-form-urlencoded"
 * format query string into key-value pairs.
 *
 * See RFC 1866, section 8.2.1.
 *
 * returns: a null-terminated array of strings: key, value, ... NULL.
 * Caller must free result with g_strfreev.
 */
char **
gsk_url_split_form_urlencoded (const char *encoded_query)
{
  enum { START, GOT_OTHER, GOT_EQUALS, INVALID } state = START;
  guint num_pairs = 0;
  const char *query_at;
  char **rv, **rv_at;
  char *copy, *copy_at;
  const char *name = "", *value = "";

  g_return_val_if_fail (encoded_query, NULL);

  /* Scan for valid pairs:
   * one more more [^&=]; =; zero or more [^&=]; & or end.
   */
  for (query_at = encoded_query; ; ++query_at)
    switch (*query_at)
      {
	case '\0':
	  if (state == GOT_EQUALS)
	    ++num_pairs;
	  goto DONE_SCANNING;
	case '&':
	  if (state == GOT_EQUALS)
	    ++num_pairs;
	  state = START;
	  break;
	case '=':
	  state = GOT_OTHER ? GOT_EQUALS : INVALID;
	  break;
	default:
	  if (state == START)
	    state = GOT_OTHER;
	  break;
      }
DONE_SCANNING:
  /* num_pairs * (name, value) + terminating NULL */
  rv = g_new (gchar *, (num_pairs << 1) + 1);

  copy = g_strdup (encoded_query);
  for (state = START, rv_at = rv, copy_at = copy; ; ++copy_at)
    switch (*copy_at)
      {
	case '\0':
	  if (state == GOT_EQUALS)
	    {
	      *rv_at++ = gsk_url_decode_http (name);
	      *rv_at++ = gsk_url_decode_http (value);
	    }
	  goto DONE;
	case '&':
	  if (state == GOT_EQUALS)
	    {
	      *copy_at = 0;
	      *rv_at++ = gsk_url_decode_http (name);
	      *rv_at++ = gsk_url_decode_http (value);
	    }
	  state = START;
	  break;
	case '=':
	  if (state == GOT_OTHER)
	    {
	      state = GOT_EQUALS;
	      *copy_at = 0;
	      value = copy_at + 1;
	    }
	  else
	    state = INVALID;
	  break;
	default:
	  if (state == START)
	    {
	      state = GOT_OTHER;
	      name = copy_at;
	    }
	  break;
      }
DONE:
  g_free (copy);
  *rv_at = NULL;
  return rv;
}
