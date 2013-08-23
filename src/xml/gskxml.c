#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include "gskxmlparser.h"
#include "../gskerror.h"

/* valgrind hackery: to safely use optimized string functions
 * that read 32-bits at a time, we pad the allocations
 * for text buffers a bit */
#if 1
/* valgrind friendly */
#define TEXT_PAD(len)           (((len)+3) & (~3))
#else
/* correct, but falsely trips valgrind warnings */
#define TEXT_PAD(len)           (len) 
#endif

GType
gsk_xml_get_type (void)
{
  static GType rv = 0;
  if (rv == 0)
    rv = g_boxed_type_register_static ("GskXml",
                                       (GBoxedCopyFunc) gsk_xml_ref,
                                       (GBoxedFreeFunc) gsk_xml_unref);
  return rv;
}

static inline GskXml *
xml_text_new_raw (guint len)
{
  GskXml *rv = g_malloc (sizeof (GskXml) + TEXT_PAD (len + 1));
  rv->type = GSK_XML_TEXT;
  rv->ref_count = 1;
  return rv;
}

GskXml *
gsk_xml_text_new    (const char *text)
{
  guint len = strlen (text);
  GskXml *rv = xml_text_new_raw (len);
  memcpy (rv + 1, text, len + 1);
  return rv;
}

GskXml *
gsk_xml_text_new_len(const char *text,
                     guint       len)
{
  GskXml *rv = xml_text_new_raw (len);
  char *rv_str = (char*)(rv + 1);
  memcpy (rv_str, text, len);
  rv_str[len] = 0;
  return rv;
}

GskXml *
gsk_xml_text_new_vprintf (const char *format,
                          va_list     args)
{
  gsize bound = g_printf_string_upper_bound (format, args);
  if (bound < 32)
    {
      GskXml *rv = xml_text_new_raw (bound);
      g_vsnprintf (GSK_XML_PEEK_TEXT (rv), bound + 1, format, args);
      return rv;
    }
  else if (bound < 1024)
    {
      char *buf = g_alloca (bound + 1);
      g_vsnprintf (buf, bound + 1, format, args);
      return gsk_xml_text_new (buf);
    }
  else
    {
      char *buf = g_malloc (bound + 1);
      GskXml *rv;
      g_vsnprintf (buf, bound + 1, format, args);
      rv = gsk_xml_text_new (buf);
      g_free (buf);
      return rv;
    }
}

GskXml *
gsk_xml_text_new_printf (const char *format,
                         ...)
{
  va_list args;
  GskXml *rv;
  va_start (args, format);
  rv = gsk_xml_text_new_vprintf (format, args);
  va_end (args);
  return rv;
}

GskXml *
gsk_xml_text_child_printf(const char *name,
                          const char *format,
                          ...)
{
  GskXml *rv;
  va_list args;
  va_start (args, format);
  rv = gsk_xml_element_new_take_1 (name, gsk_xml_text_new_vprintf (format, args));
  va_end (args);
  return rv;
}

GskXml *
gsk_xml_element_new  (const char  *name,
                      guint        n_kv_pairs,
                      char       **attr_kv_pairs,
                      guint        n_children,
                      GskXml     **children)
{
  gsize base_size = sizeof (GskXmlElement)
                  + n_kv_pairs * sizeof (char*) * 2
                  + n_children * sizeof (GskXml *);
  gsize str_size = strlen (name) + 1;
  gpointer rv_ptr;
  GskXml *rv;
  GskXmlElement *rv_elt;
  char *rv_str;
  guint i, o;
  char **attrs;
  for (i = 0; i < n_kv_pairs * 2; i++)
    str_size += strlen (attr_kv_pairs[i]) + 1;

  rv_ptr = g_malloc (base_size + str_size);
  rv = rv_ptr;
  rv_elt = rv_ptr;
  rv_str = (char*)rv_ptr + base_size;

  rv->type = GSK_XML_ELEMENT;
  rv->ref_count = 1;
  rv_elt->name = rv_str;
  rv_str = g_stpcpy (rv_str, name) + 1;
  rv_elt->n_attrs = n_kv_pairs;
  rv_elt->n_children = n_children;
  attrs = (char **) (rv_elt + 1);
  rv_elt->attrs = n_kv_pairs ? attrs : NULL;
  rv_elt->children = (GskXml **) (attrs + n_kv_pairs * 2);

  for (i = 0; i < n_kv_pairs * 2; i++)
    {
      attrs[i] = rv_str;
      rv_str = g_stpcpy (rv_str, attr_kv_pairs[i]) + 1;
    }

  o = 0;
  for (i = 0; i < n_children; )
    {
      if (children[i]->type == GSK_XML_TEXT)
        {
          /* try to coalesce adjacent text nodes */
          guint n_text_children = 1;
          guint text_len = strlen (GSK_XML_PEEK_TEXT (children[i]));
          while (i + n_text_children < n_children
              && children[i + n_text_children]->type == GSK_XML_TEXT)
            {
              text_len += strlen (GSK_XML_PEEK_TEXT (children[i + n_text_children]));
              n_text_children++;
            }
          if (n_text_children == 1)
            rv_elt->children[o++] = gsk_xml_ref (children[i]);
          else
            {
              GskXml *sub = xml_text_new_raw (text_len);
              guint k;
              char *sub_str = (char*)(sub + 1);
              for (k = 0; k < n_text_children; k++)
                sub_str = g_stpcpy (sub_str, GSK_XML_PEEK_TEXT (children[i + k]));
              rv_elt->children[o++] = sub;
            }
          i += n_text_children;
        }
      else
        {
          rv_elt->children[o++] = gsk_xml_ref (children[i++]);
        }
    }
  rv_elt->n_children = o;

  return rv;
}

GskXml *
gsk_xml_element_new_take (const char  *name,
                          guint        n_kv_pairs,
                          char       **attr_kv_pairs,
                          guint        n_children,
                          GskXml     **children)
{
  gsize base_size = sizeof (GskXmlElement)
                  + n_kv_pairs * sizeof (char*) * 2
                  + n_children * sizeof (GskXml *);
  gsize str_size = strlen (name) + 1;
  gpointer rv_ptr;
  GskXml *rv;
  GskXmlElement *rv_elt;
  char *rv_str;
  guint i, o;
  char **attrs;
  for (i = 0; i < n_kv_pairs * 2; i++)
    str_size += strlen (attr_kv_pairs[i]) + 1;

  rv_ptr = g_malloc (base_size + str_size);
  rv = rv_ptr;
  rv_elt = rv_ptr;
  rv_str = (char*)rv_ptr + base_size;

  rv->type = GSK_XML_ELEMENT;
  rv->ref_count = 1;
  rv_elt->name = rv_str;
  rv_str = g_stpcpy (rv_str, name) + 1;
  rv_elt->n_attrs = n_kv_pairs;
  rv_elt->n_children = n_children;
  attrs = (char **) (rv_elt + 1);
  rv_elt->attrs = n_kv_pairs ? attrs : NULL;
  rv_elt->children = (GskXml **) (attrs + n_kv_pairs * 2);

  for (i = 0; i < n_kv_pairs * 2; i++)
    {
      attrs[i] = rv_str;
      rv_str = g_stpcpy (rv_str, attr_kv_pairs[i]) + 1;
    }

  o = 0;
  for (i = 0; i < n_children; )
    {
      if (children[i]->type == GSK_XML_TEXT)
        {
          /* try to coalesce adjacent text nodes */
          guint n_text_children = 1;
          guint text_len = strlen (GSK_XML_PEEK_TEXT (children[i]));
          while (i + n_text_children < n_children
              && children[i + n_text_children]->type == GSK_XML_TEXT)
            {
              text_len += strlen (GSK_XML_PEEK_TEXT (children[i + n_text_children]));
              n_text_children++;
            }
          if (n_text_children == 1)
            rv_elt->children[o++] = children[i];
          else
            {
              GskXml *sub = xml_text_new_raw (text_len);
              guint k;
              char *sub_str = (char*)(sub + 1);
              for (k = 0; k < n_text_children; k++)
                {
                  sub_str = g_stpcpy (sub_str, GSK_XML_PEEK_TEXT (children[i + k]));
                  gsk_xml_unref (children[i + k]);
                }
              rv_elt->children[o++] = sub;
            }
          i += n_text_children;
        }
      else
        {
          rv_elt->children[o++] = children[i++];
        }
    }
  rv_elt->n_children = o;

  return rv;
}

GskXml *
gsk_xml_ref (GskXml      *xml)
{
  g_assert (xml->ref_count > 0);
  ++(xml->ref_count);
  return xml;
}
void
gsk_xml_unref (GskXml      *xml)
{
  g_assert (xml->ref_count > 0);
  if (--(xml->ref_count) == 0)
    {
      if (xml->type == GSK_XML_ELEMENT)
        {
          guint i;
          GskXmlElement *elt = (GskXmlElement *) xml;
          for (i = 0; i < elt->n_children; i++)
            gsk_xml_unref (GSK_XML_PEEK_CHILDREN (xml)[i]);
        }
      g_free (xml);
    }
}

const char *
gsk_xml_find_attr  (GskXml      *xml,
                    const char  *attr_name)
{
  char **attrs;
  guint i;
  if (xml->type != GSK_XML_ELEMENT)
    return NULL;
  attrs = GSK_XML_PEEK_ATTRS (xml);
  for (i = 0; i < ((GskXmlElement *) xml)->n_attrs; i++)
    if (strcmp (attrs[2*i], attr_name) == 0)
      return attrs[2*i + 1];
  return NULL;
}

gboolean
gsk_xml_is_element (const GskXml *xml,
                    const char   *element_name)
{
  return xml->type == GSK_XML_ELEMENT
      && strcmp (GSK_XML_PEEK_NAME (xml), element_name) == 0;
}


gboolean
gsk_xml_is_whitespace (const GskXml *xml)
{
  const char *txt;
  if (xml->type != GSK_XML_TEXT)
    return FALSE;
  txt = GSK_XML_PEEK_TEXT (xml);
  /* optimize for ascii whitespace strings */
  while (*txt && g_ascii_isspace (*txt))
    txt++;
  if ((guint8)(*txt) & 0x80)
    {
      /* maybe have non-ascii whitespace. */
      while (*txt)
        {
          gunichar u = g_utf8_get_char (txt);
          /* TODO: later glib 2.14 will have iszerowidth(),
             which i guess is better than hardcoding 0xffef. */
          if (u != 0xfeff
           && !g_unichar_isspace (u))
            break;
          txt = g_utf8_next_char (txt);
        }
    }
  return *txt == '\0';
}

static GskXml *
convert_parser_to_doc (GskXmlParser *parser,
                       GError      **error)
{
  GskXml *rv = gsk_xml_parser_dequeue (parser, 0);
  GskXml *tmp;
  if (rv == NULL)
    goto empty;
  if (gsk_xml_is_whitespace (rv))
    {
      gsk_xml_unref (rv);
      rv = gsk_xml_parser_dequeue (parser, 0);
    }
  if (rv == NULL)
    goto empty;
  tmp = gsk_xml_parser_dequeue (parser, 0);
  if (tmp != NULL
   && gsk_xml_is_whitespace (tmp))
    {
      gsk_xml_unref (tmp);
      tmp = gsk_xml_parser_dequeue (parser, 0);
    }
  if (tmp != NULL)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_MULTIPLE_DOCUMENTS,
                   "multiple documents in file");
      gsk_xml_unref (tmp);
      gsk_xml_parser_free (parser);
      return NULL;
    }
  gsk_xml_parser_free (parser);
  return rv;

empty:
  g_set_error (error, GSK_G_ERROR_DOMAIN,
               GSK_ERROR_NO_DOCUMENT,
               "no node in parser");
  gsk_xml_parser_free (parser);
  return NULL;
}

GskXml *
gsk_xml_parse_file  (const char *filename,
                     GError    **error)
{
  GskXmlParser *parser = gsk_xml_parser_new (0);
  if (!gsk_xml_parser_feed_file (parser, filename, error))
    {
      gsk_xml_parser_free (parser);
      return NULL;
    }
  return convert_parser_to_doc (parser, error);
}

GskXml *
gsk_xml_parse_str   (const char *str,
                     GError    **error)
{
  GskXmlParser *parser = gsk_xml_parser_new (0);
  if (!gsk_xml_parser_feed (parser, (const guint8 *) str, -1, error))
    {
      gsk_xml_parser_free (parser);
      return NULL;
    }
  return convert_parser_to_doc (parser, error);
}

GskXml *
gsk_xml_parse_str_len   (const char *str,
                         gssize      len,
                         GError    **error)
{
  GskXmlParser *parser = gsk_xml_parser_new (0);
  if (!gsk_xml_parser_feed (parser, (const guint8 *) str, len, error))
    {
      gsk_xml_parser_free (parser);
      return NULL;
    }
  return convert_parser_to_doc (parser, error);
}

GskXml *
gsk_xml_find_child  (GskXml      *xml,
                     const char  *child_node_name,
                     guint        instance)
{
  guint n_children;
  GskXml **children;
  guint i;
  if (xml->type != GSK_XML_ELEMENT)
    return NULL;
  n_children = ((GskXmlElement*)xml)->n_children;
  children = GSK_XML_PEEK_CHILDREN (xml);
  for (i = 0; i < n_children; i++)
    if (children[i]->type == GSK_XML_ELEMENT
     && strcmp (GSK_XML_PEEK_NAME (children[i]), child_node_name) == 0)
      {
        if (instance == 0)
          return children[i];
        else
          instance--;
      }
  return NULL;
}
GskXml *
gsk_xml_find_child_len  (GskXml      *xml,
                         const char  *child_node_name,
                         guint        child_node_name_len,
                         guint        instance)
{
  guint n_children;
  GskXml **children;
  guint i;
  if (xml->type != GSK_XML_ELEMENT)
    return NULL;
  n_children = ((GskXmlElement*)xml)->n_children;
  children = GSK_XML_PEEK_CHILDREN (xml);
  for (i = 0; i < n_children; i++)
    if (children[i]->type == GSK_XML_ELEMENT
     && strncmp (GSK_XML_PEEK_NAME (children[i]), child_node_name, child_node_name_len) == 0
     && GSK_XML_PEEK_NAME (children[i])[child_node_name_len] == '\0')
      {
        if (instance == 0)
          return children[i];
        else
          instance--;
      }
  return NULL;
}

GskXml *
gsk_xml_lookup_path (GskXml     *start,
                     const char *path)
{
  const char *end = strchr (path, '/');
  if (end == NULL)
    end = strchr (path, 0);

  if (strncmp (GSK_XML_PEEK_NAME (start), path, end - path) != 0
   || GSK_XML_PEEK_NAME (start)[end - path] != '\0')
    return NULL;

  path = *end == 0 ? end : end + 1;

  while (*path)
    {
      GskXml *c;
      end = strchr (path, '/');
      if (end == NULL)
        end = strchr (path, 0);
      c = gsk_xml_find_child_len (start, path, end - path, 0);
      if (c == NULL)
        return NULL;
      start = c;
      path = *end == 0 ? end : end + 1;
    }
  return start;
}

/* gsk_xml_foreach_by_path:
 * 
 * Iterate all path matches, until foreach() returns FALSE.
 * Return FALSE iff a foreach() returns FALSE.
 */
typedef struct _IndexXml IndexXml;
struct _IndexXml
{
  guint next_index;
  GskXml *parent;
};
static inline gboolean
is_path_component_compliant (const char *elt_name,
                             const char *path_component_ptr)
{
  while (*elt_name)
    if (*elt_name++ != *path_component_ptr++)
      return FALSE;
  return *path_component_ptr == 0 || *path_component_ptr == '/';
}

static gboolean
matches_path (GskXml *xml,
              const char *path_component_ptr)
{
  if (*path_component_ptr == 0)
    return TRUE;
  if (xml->type != GSK_XML_ELEMENT)
    return FALSE;
  return is_path_component_compliant (GSK_XML_PEEK_NAME (xml), path_component_ptr);
}

gboolean
gsk_xml_foreach_by_path (GskXml             *start,
                         const char         *path,
                         GskXmlForeachFunc   foreach,
                         gpointer            foreach_data,
                         GskXmlForeachFlags  flags)
{
  guint n_slashes = 0;
  const char *at = 0;
  const char **path_components;
  IndexXml *pieces;
  guint i;
  guint stack_top;
  gboolean reverse = (flags & GSK_XML_FOREACH_REVERSED) != 0;
  if (!matches_path (start, path))
    return TRUE;
  if (*path == 0)
    return foreach (start, foreach_data);
  for (at = path; *at; at++)
    if (*at == '/')
      n_slashes++;
  if (n_slashes > 1024)
    return TRUE;                     /* right... */
  pieces = g_newa (IndexXml, n_slashes + 2);
  path_components = g_newa (const char *, n_slashes + 2);
  i = 0;
  path_components[i++] = path;
  for (at = path; *at; at++)
    if (*at == '/')
      path_components[i++] = at + 1;
  path_components[i++] = at;
  stack_top = 0;
  pieces[0].next_index = 0;
  pieces[0].parent = start;

  for (;;)
    {
      GskXml *top_parent = pieces[stack_top].parent;
      guint top_index = pieces[stack_top].next_index;
      g_assert (path_components[stack_top][0]==0||top_parent->type==GSK_XML_ELEMENT);
      if (path_components[stack_top+1][0] == 0)
        {
          if (!foreach (top_parent, foreach_data))
            return FALSE;
          if (stack_top == 0)
            break;
          stack_top--;
        }
      else if (top_index >= GSK_XML_PEEK_N_CHILDREN (top_parent))
        {
          if (stack_top == 0)
            break;
          stack_top--;
        }
      else
        {
          guint child_index;
          GskXml *child;
          if (reverse)
            child_index = GSK_XML_PEEK_N_CHILDREN (top_parent) - 1 - top_index;
          else
            child_index = top_index;
          child = GSK_XML_PEEK_CHILD (top_parent, child_index);
          if (matches_path (child, path_components[stack_top+1]))
            {
              pieces[stack_top+1].parent = child;
              pieces[stack_top+1].next_index = 0;
              pieces[stack_top].next_index += 1;
              ++stack_top;
            }
          else
            {
              pieces[stack_top].next_index += 1;
            }
        }
    }
  return TRUE;
}

GskXml *
gsk_xml_index       (GskXml       *xml,
                     guint         n_indices,
                     const guint  *indices)
{
  guint i;
  for (i = 0; i < n_indices; i++)
    {
      if (xml->type != GSK_XML_ELEMENT)
        return NULL;
      if (indices[i] >= GSK_XML_PEEK_N_CHILDREN (xml))
        return NULL;
      xml = GSK_XML_PEEK_CHILD (xml, indices[i]);
    }
  return xml;
}

gboolean   
gsk_xml_dump        (const GskXml  *xml,
                     int            fd,
                     GError       **error)
{
  GskBuffer buf = GSK_BUFFER_STATIC_INIT;
  gsk_xml_dump_buffer (xml, &buf);

  while (buf.size > 0)
    {
      if (gsk_buffer_writev (&buf, fd) < 0)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN,
                       gsk_error_code_from_errno (errno),
                       "error writing xml from buffer to fd: %s",
                       g_strerror (errno));
          gsk_buffer_destruct (&buf);
          return FALSE;
        }
    }
  gsk_buffer_destruct (&buf);
  return TRUE;
}

gboolean   
gsk_xml_dump_file   (const GskXml  *xml,
                     const char    *filename,
                     GError       **error)
{
  int fd = creat (filename, 0644);
  if (fd < 0)
    {
      g_set_error (error,
                   GSK_G_ERROR_DOMAIN,
                   gsk_error_code_from_errno (errno),
                   "error creating %s: %s", filename, g_strerror (errno));
      return FALSE;
    }
  if (!gsk_xml_dump (xml, fd, error))
    {
      close (fd);
      return FALSE;
    }
  close (fd);
  return TRUE;
}

static inline void
append_escaped (GskBuffer  *buffer,
                const char *text)
{
  guint n_nonescaped = 0;
  while (text[n_nonescaped] != '\0')
    {
      char c = text[n_nonescaped];
      const char *escaped;
      if (c == '<')
        escaped = "&lt;";
      else if (c == '>')
        escaped = "&gt;";
      else if (c == '&')
        escaped = "&amp;";
      else if (c == '"')
        escaped = "&quot;";
      else
        {
          n_nonescaped++;
          continue;
        }
      if (n_nonescaped)
        gsk_buffer_append (buffer, text, n_nonescaped);
      gsk_buffer_append_string (buffer, escaped);
      text += n_nonescaped + 1;
      n_nonescaped = 0;
    }
  if (n_nonescaped)
    gsk_buffer_append (buffer, text, n_nonescaped);
}

static inline void
append_escaped_len  (GskBuffer  *buffer,
                     const char *text,
                     guint       len)
{
  guint n_nonescaped = 0;
  guint rem = len;
  while (rem > 0)
    {
      char c = text[n_nonescaped];
      const char *escaped;
      if (c == '<')
        escaped = "&lt;";
      else if (c == '>')
        escaped = "&gt;";
      else if (c == '&')
        escaped = "&amp;";
      else if (c == '"')
        escaped = "&quot;";
      else
        {
          n_nonescaped++;
          rem--;        /* done with non-escaped char */
          continue;
        }
      if (n_nonescaped)
        gsk_buffer_append (buffer, text, n_nonescaped);
      gsk_buffer_append_string (buffer, escaped);
      text += n_nonescaped + 1;
      n_nonescaped = 0;
      rem--;            /* done with escaped char */
    }
  if (n_nonescaped)
    gsk_buffer_append (buffer, text, n_nonescaped);
}

void
gsk_xml_dump_buffer (const GskXml      *xml,
                     GskBuffer         *buffer)
{
  guint i;
  switch (xml->type)
    {
    case GSK_XML_ELEMENT:
      {
        GskXmlElement *element = (GskXmlElement *) xml;

        gsk_buffer_append_char (buffer, '<');
        append_escaped (buffer, element->name);
        for (i = 0; i < element->n_attrs; i++)
          {
            gsk_buffer_append_char (buffer, ' ');
            append_escaped (buffer, GSK_XML_PEEK_ATTRS (xml)[i*2+0]);
            gsk_buffer_append (buffer, "=\"", 2);
            append_escaped (buffer, GSK_XML_PEEK_ATTRS (xml)[i*2+1]);
            gsk_buffer_append_char (buffer, '"');
          }
        if (element->n_children == 0)
          {
            gsk_buffer_append (buffer, " />", 3);
          }
        else
          {
            gsk_buffer_append_char (buffer, '>');
            for (i = 0; i < element->n_children; i++)
              gsk_xml_dump_buffer (element->children[i], buffer);
            gsk_buffer_append (buffer, "</", 2);
            append_escaped (buffer, GSK_XML_PEEK_NAME (xml));
            gsk_buffer_append_char (buffer, '>');
          }
      }
      break;

    case GSK_XML_TEXT:
      append_escaped (buffer, GSK_XML_PEEK_TEXT (xml));
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
print_indent (GskBuffer *buffer,
	      guint      indent)
{
  static char *spaces = NULL;
  if (spaces == NULL)
    {
      spaces = g_malloc (1024);
      memset (spaces, ' ', 1024);
    }
  while (indent >= 1024)
    {
      gsk_buffer_append (buffer, spaces, 1024);
      indent -= 1024;
    }
  if (indent)
    gsk_buffer_append (buffer, spaces, indent);
}

static void
dump_formatted (const GskXml *xml,
                GskBuffer    *buffer,
                guint         element_indent)
{
  GskXmlElement *element = (GskXmlElement *) xml;
  guint i;

  g_return_if_fail (xml->type == GSK_XML_ELEMENT);

  gsk_buffer_append_char (buffer, '<');
  append_escaped (buffer, element->name);
  for (i = 0; i < element->n_attrs; i++)
    {
      gsk_buffer_append_char (buffer, ' ');
      append_escaped (buffer, GSK_XML_PEEK_ATTRS (xml)[i*2+0]);
      gsk_buffer_append (buffer, "=\"", 2);
      append_escaped (buffer, GSK_XML_PEEK_ATTRS (xml)[i*2+1]);
      gsk_buffer_append_char (buffer, '"');
    }
  if (element->n_children == 0)
    {
      gsk_buffer_append (buffer, " />", 3);
    }
  else
    {
      gboolean had_to_indent = FALSE;
      gsk_buffer_append_char (buffer, '>');
      for (i = 0; i < element->n_children; i++)
	{
	  GskXml *child = element->children[i];
	  switch (child->type)
	    {
	    case GSK_XML_ELEMENT:
	      had_to_indent = TRUE;
	      gsk_buffer_append_char (buffer, '\n');
	      print_indent (buffer, element_indent + 2);
	      dump_formatted (child, buffer, element_indent + 2);
	      break;
	    case GSK_XML_TEXT:
	      append_escaped (buffer, GSK_XML_PEEK_TEXT (child));
	      break;
	    default:
	      g_assert_not_reached ();
	    }
	}
      if (had_to_indent)
	{
	  gsk_buffer_append_char (buffer, '\n');
	  print_indent (buffer, element_indent);
	}
      gsk_buffer_append (buffer, "</", 2);
      append_escaped (buffer, GSK_XML_PEEK_NAME (xml));
      gsk_buffer_append_char (buffer, '>');
    }
}

void
gsk_xml_dump_buffer_formatted (const GskXml *xml,
                               GskBuffer    *buffer)
{
  switch (xml->type)
    {
    case GSK_XML_ELEMENT:
      dump_formatted (xml, buffer, 0);
      break;
    case GSK_XML_TEXT:
      append_escaped (buffer, GSK_XML_PEEK_TEXT (xml));
      break;
    default:
      g_assert_not_reached ();
    }
}

char *
gsk_xml_to_string (const GskXml *xml)
{
  GskBuffer buffer = GSK_BUFFER_STATIC_INIT;
  char *rv;
  gsk_xml_dump_buffer (xml, &buffer);
  rv = g_malloc (buffer.size + 1);
  rv[buffer.size] = 0;
  gsk_buffer_read (&buffer, rv, buffer.size);
  gsk_buffer_destruct (&buffer);
  return rv;
}

void
gsk_xml_append_text_to_string (const GskXml *node,
                               GString      *str)
{
  if (node->type == GSK_XML_TEXT)
    g_string_append (str, GSK_XML_PEEK_TEXT (node));
  else
    {
      guint i;
      GskXmlElement *element = (GskXmlElement *) node;
      for (i = 0; i < element->n_children; i++)
        gsk_xml_append_text_to_string (element->children[i], str);
    }
}

char *
gsk_xml_get_all_text (const GskXml *xml)
{
  GString *str = g_string_new ("");
  gsk_xml_append_text_to_string (xml, str);
  return g_string_free (str, FALSE);
}

const char *
gsk_xml_find_solo_text  (const GskXml     *xml,
                         GError          **error)
{
  const GskXmlElement *elt;
  if (xml->type != GSK_XML_ELEMENT)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_PARSE,
                   "gsk_xml_find_solo_text: non-element node");
      return NULL;
    }
  elt = (const GskXmlElement *) xml;
  if (elt->n_children == 0)
    return "";
  if (elt->n_children == 1)
    {
      if (elt->children[0]->type == GSK_XML_TEXT)
        return GSK_XML_PEEK_TEXT (elt->children[0]);
      else
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_PARSE,
                       "gsk_xml_find_solo_text: element <%s> encountered in <%s>",
                       GSK_XML_PEEK_NAME (elt->children[0]),
                       elt->name);
          return NULL;
        }
    }
  else
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_PARSE,
                   "%u children found in call to gsk_xml_find_solo_text",
                   elt->n_children);
      return NULL;
    }
}

GskXml *
gsk_xml_text_peek_empty (void)
{
  static GskXml *empty = NULL;
  if (empty == NULL)
    empty = gsk_xml_text_new ("");
  return empty;
}

GskXml *
gsk_xml_find_solo_child (GskXml     *xml,
                        GError   **error)
{
  GskXmlElement *elt;
  if (xml->type != GSK_XML_ELEMENT)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_PARSE,
                   "non-element child cannot have children");
      return NULL;
    }
  elt = (GskXmlElement *) xml;
  if (elt->n_children == 0)
    return gsk_xml_text_peek_empty ();
  else if (elt->n_children == 1)
    return elt->children[0];
  else
    {
      guint i;
      GskXml *rv;
      for (i = 0; i < elt->n_children; i++)
        if (elt->children[i]->type == GSK_XML_ELEMENT)
          break;
      if (i == elt->n_children)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN,
                       GSK_ERROR_PARSE,
                       "multiple non-element children???");
          return NULL;
        }
      rv = elt->children[i];
      for (i++; i < elt->n_children; i++)
        if (elt->children[i]->type == GSK_XML_ELEMENT)
          {
            g_set_error (error, GSK_G_ERROR_DOMAIN,
                         GSK_ERROR_PARSE,
                         "multiple element children not allowed in <%s>",
                         GSK_XML_PEEK_NAME (elt));
            return NULL;
          }
      return rv;
    }
}

gboolean
gsk_xml_peek_child_text (GskXml      *xml,
                        const char *name,
                        gboolean    required,
                        const char**p_out,
                        GError    **error)
{
  GskXml *child;
  if (xml->type != GSK_XML_ELEMENT)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_PARSE,
                   "peek_child_text failed: parent not a element node");
      return FALSE;
    }

  child = gsk_xml_find_child (xml, name, 0);
  if (child == NULL)
    {
      if (required)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_PARSE,
                       "<%s> missing required child %s",
                       GSK_XML_PEEK_NAME (xml),
                       name);
          return FALSE;
        }
      *p_out = NULL;
      return TRUE;
    }
  if (GSK_XML_PEEK_N_CHILDREN (child) == 0)
    {
      *p_out = "";
      return TRUE;
    }
  if (GSK_XML_PEEK_N_CHILDREN (child) != 1
   || GSK_XML_PEEK_CHILD (child, 0)->type != GSK_XML_TEXT)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_PARSE,
                   "peek_child_text(<%s><%s>): node contains something other than a single text node",
                   GSK_XML_PEEK_NAME (xml), name);
      return FALSE;
    }
  *p_out = GSK_XML_PEEK_TEXT (GSK_XML_PEEK_CHILD (child, 0));
  return TRUE;
}

gboolean
gsk_xml_peek_path_text (GskXml       *xml,
                       const char  *path,
                       gboolean     required,
                       const char **p_out,
                       GError     **error)
{
  GskXml *child;
  if (xml->type != GSK_XML_ELEMENT)
    {
      g_set_error (error,
		   GSK_G_ERROR_DOMAIN,
		   GSK_ERROR_PARSE,
                   "argument not a element node");
      return FALSE;
    }
  child = gsk_xml_lookup_path (xml, path);
  if (child == NULL)
    {
      if (required)
        {
          g_set_error (error,
		       GSK_G_ERROR_DOMAIN,
		       GSK_ERROR_PARSE,
                       "could not find path %s under <%s>",
                       path,
                       GSK_XML_PEEK_NAME (xml));
          return FALSE;
        }
      else
	{
	  *p_out = NULL;
	  return TRUE;
	}
    }
  if (GSK_XML_PEEK_N_CHILDREN (child) == 0)
    {
      *p_out = "";
      return TRUE;
    }
  if (GSK_XML_PEEK_N_CHILDREN (child) != 1
   || GSK_XML_PEEK_CHILD (child, 0)->type != GSK_XML_TEXT)
    {
      g_set_error (error,
		   GSK_G_ERROR_DOMAIN,
		   GSK_ERROR_PARSE,
                   "path %s under <%s> contains something other than a "
		     "single text node",
                   path,
		   GSK_XML_PEEK_NAME (xml));
      return FALSE;
    }
  *p_out = GSK_XML_PEEK_TEXT (GSK_XML_PEEK_CHILD (child, 0));
  return TRUE;
}

guint
gsk_xml_hash  (gconstpointer xml_node)
{
  const GskXml *xml = xml_node;
  switch (xml->type)
    {
    case GSK_XML_TEXT:
      return g_str_hash (GSK_XML_PEEK_TEXT (xml));
    case GSK_XML_ELEMENT:
      {
        const GskXmlElement *elt = xml_node;
        guint hash = g_str_hash (elt->name);
        guint i;
        for (i = 0; i < elt->n_attrs * 2; i++)
          {
            hash *= 33;
            hash += g_str_hash (GSK_XML_PEEK_ATTRS (elt)[i]);
          }
        for (i = 0; i < elt->n_children; i++)
          {
            hash *= 101;
            hash += gsk_xml_hash (GSK_XML_PEEK_CHILDREN (elt)[i]);
          }
        return hash;
      }
    default:
      g_assert_not_reached ();
    }
}

gboolean gsk_xml_equal (gconstpointer a_node,
                       gconstpointer b_node)
{
  if (((GskXml*)a_node)->type != ((GskXml*)b_node)->type)
    return FALSE;
  switch (((GskXml*)a_node)->type)
    {
    case GSK_XML_TEXT:
      return strcmp (GSK_XML_PEEK_TEXT (a_node), GSK_XML_PEEK_TEXT (b_node)) == 0;
    case GSK_XML_ELEMENT:
      {
        const GskXmlElement *a_elt = a_node;
        const GskXmlElement *b_elt = b_node;
        guint i;
        if (strcmp (a_elt->name, b_elt->name) != 0
         || a_elt->n_attrs != b_elt->n_attrs
         || a_elt->n_children != b_elt->n_children)
          return FALSE;
        for (i = 0; i < a_elt->n_attrs * 2; i++)
          if (strcmp (GSK_XML_PEEK_ATTRS (a_elt)[i], GSK_XML_PEEK_ATTRS (b_elt)[i]) != 0)
            return FALSE;
        for (i = 0; i < a_elt->n_children; i++)
          if (!gsk_xml_equal (GSK_XML_PEEK_CHILDREN (a_elt)[i], GSK_XML_PEEK_CHILDREN (b_elt)[i]))
            return FALSE;
        return TRUE;
      }
    default:
      g_return_val_if_reached (FALSE);
    }
}
GskXml *
gsk_xml_text_new_int       (int value)
{
  char buf[20];
  g_snprintf (buf, sizeof (buf), "%d", value);
  return gsk_xml_text_new (buf);
}

GskXml *
gsk_xml_element_new_take_1 (const char *name,
                            GskXml      *a)
{
  GskXml *arr[1] = { a };
  return gsk_xml_element_new_take (name, 0, NULL, 1, arr);
}
GskXml *
gsk_xml_element_new_take_2 (const char  *name,
                            GskXml      *a,
                            GskXml      *b)
{
  GskXml *arr[2] = { a, b };
  return gsk_xml_element_new_take (name, 0, NULL, 2, arr);
}
GskXml *
gsk_xml_element_new_take_3 (const char *name,
                            GskXml      *a,
                            GskXml      *b,
                            GskXml      *c)
{
  GskXml *arr[3] = { a, b, c };
  return gsk_xml_element_new_take (name, 0, NULL, 3, arr);
}

GskXml *
gsk_xml_element_new_take_list (const char *name,
                               GskXml *first_arg,
                               ...)
{
  va_list args;
  GskXml *arg;
  guint n_args = 0;
  GskXml **children;
  GskXml *rv;

  va_start (args, first_arg);
  for (arg = first_arg; arg != NULL; arg = va_arg (args, GskXml *))
    n_args++;
  va_end (args);

  children = g_newa (GskXml *, n_args);
  va_start (args, first_arg);
  n_args = 0;
  for (arg = first_arg; arg != NULL; arg = va_arg (args, GskXml *))
    children[n_args++] = arg;
  va_end (args);

  rv = gsk_xml_element_new_take (name, 0, NULL, n_args, children);
  return rv;
}
GskXml *
gsk_xml_element_new_contents (GskXml          *base,
                              guint            n_children,
                              GskXml         **children)
{
  GskXmlElement *elt = (GskXmlElement *) base;
  g_return_val_if_fail (base->type == GSK_XML_ELEMENT, NULL);
  return gsk_xml_element_new (elt->name,
                              elt->n_attrs, elt->attrs,
                              n_children, children);
}

GskXml *
gsk_xml_element_replace_name (GskXml          *base,
                              const char      *new_name)
{
  GskXmlElement *elt = (GskXmlElement *) base;
  g_return_val_if_fail (base->type == GSK_XML_ELEMENT, NULL);
  return gsk_xml_element_new (new_name,
                              elt->n_attrs, elt->attrs,
                              elt->n_children, elt->children);
}

GskXml *
gsk_xml_element_new_append   (GskXml     *base_xml,
                              guint       n_add_children,
                              GskXml    **add_children)
{
  GskXmlElement *base_elt = (GskXmlElement *) base_xml;
  if (base_elt->n_children == 0)
    return gsk_xml_element_new_contents (base_xml, n_add_children, add_children);
  else if (n_add_children == 0)
    return gsk_xml_ref (base_xml);
  else
    {
      guint n_total = base_elt->n_children + n_add_children;
      gboolean use_stack;
      GskXml **total;
      GskXml *rv;
      if (n_total > 64)
        {
          use_stack = FALSE;
          total = g_new (GskXml *, n_total);
        }
      else
        {
          use_stack = TRUE;
          total = g_newa (GskXml *, n_total);
        }
      memcpy (total, base_elt->children, base_elt->n_children * sizeof (GskXml*));
      memcpy (total + base_elt->n_children, add_children, n_add_children * sizeof (GskXml*));
      rv = gsk_xml_element_new_contents (base_xml, n_total, total);
      if (!use_stack)
        g_free (total);
      return rv;
    }
}

GskXml *
gsk_xml_element_new_prepend  (GskXml     *base_xml,
                              guint      n_add_children,
                              GskXml    **add_children)
{
  GskXmlElement *base_elt = (GskXmlElement *) base_xml;
  if (base_elt->n_children == 0)
    return gsk_xml_element_new_contents (base_xml, n_add_children, add_children);
  else if (n_add_children == 0)
    return gsk_xml_ref (base_xml);
  else
    {
      guint n_total = base_elt->n_children + n_add_children;
      gboolean use_stack;
      GskXml **total;
      GskXml *rv;
      if (n_total > 64)
        {
          use_stack = FALSE;
          total = g_new (GskXml *, n_total);
        }
      else
        {
          use_stack = TRUE;
          total = g_newa (GskXml *, n_total);
        }
      memcpy (total,
              add_children,
              n_add_children * sizeof (GskXml*));
      memcpy (total + n_add_children,
              GSK_XML_PEEK_CHILDREN (base_xml),
              base_elt->n_children * sizeof (GskXml*));
      rv = gsk_xml_element_new_contents (base_xml, n_total, total);
      if (!use_stack)
        g_free (total);
      return rv;
    }
}


GskXml *
gsk_xml_element_new_1      (const char  *name,
                            GskXml      *a)
{
  GskXml *arr[1] = { a };
  return gsk_xml_element_new (name, 0, NULL, 1, arr);
}
GskXml *
gsk_xml_element_new_2      (const char  *name,
                            GskXml      *a,
                            GskXml      *b)
{
  GskXml *arr[2] = { a, b };
  return gsk_xml_element_new (name, 0, NULL, 2, arr);
}
GskXml *
gsk_xml_element_new_3      (const char  *name,
                            GskXml      *a,
                            GskXml      *b,
                            GskXml      *c)
{
  GskXml *arr[3] = { a, b, c };
  return gsk_xml_element_new (name, 0, NULL, 3, arr);
}

/* --- Testing for equality --- */
static int
pstrcmp (gconstpointer a, gconstpointer b)
{
  return strcmp (*(char**) a, *(char**) b);
}

#define IS_TEXT_NODE_IGNORABLE(node, flags) \
  ((flags & GSK_XML_EQUAL_IGNORE_TRIVIAL_WHITESPACE) != 0 \
   && gsk_xml_is_whitespace (node))

static inline void
maybe_ignore_ws (const char **a_ptr, GskXmlEqualFlags flags)
{
  if (flags & GSK_XML_EQUAL_IGNORE_END_WHITESPACE)
    {
      const char *a = *a_ptr;
      while (*a && g_ascii_isspace (*a))
        a++;
      *a_ptr = a;
    }
}

gboolean
gsk_xml_equal_with_flags (const GskXml    *a,
                          const GskXml    *b,
                          GskXmlEqualFlags flags)
{
  if (a->type == GSK_XML_ELEMENT)
    {
      guint n_attrs;
      guint a_index, b_index;
      guint a_n_children, b_n_children;
      if (!gsk_xml_is_element (b, GSK_XML_PEEK_NAME (a)))
        return FALSE;
      if (GSK_XML_PEEK_N_ATTRS (a) != GSK_XML_PEEK_N_ATTRS (b))
        return FALSE;
      n_attrs = GSK_XML_PEEK_N_ATTRS (a);
      if (n_attrs)
        {
          if (flags & GSK_XML_EQUAL_SORT_ATTRIBUTES)
            {
              char **attrs = g_newa (char *, 4 * n_attrs);
              char **a_attrs = attrs;
              char **b_attrs = attrs + 2 * n_attrs;
              guint i;
              memcpy (a_attrs, GSK_XML_PEEK_ATTRS (a), n_attrs * sizeof (char*) * 2);
              memcpy (b_attrs, GSK_XML_PEEK_ATTRS (b), n_attrs * sizeof (char*) * 2);
              qsort (a_attrs, n_attrs, sizeof (char *) * 2, pstrcmp);
              qsort (b_attrs, n_attrs, sizeof (char *) * 2, pstrcmp);
              for (i = 0; i < 2 * n_attrs; i++)
                if (strcmp (GSK_XML_PEEK_ATTRS(a)[i],
                            GSK_XML_PEEK_ATTRS(b)[i]) != 0)
                  return FALSE;
            }
          else
            {
              guint i;
              for (i = 0; i < 2 * n_attrs; i++)
                if (strcmp (GSK_XML_PEEK_ATTRS(a)[i],
                            GSK_XML_PEEK_ATTRS(b)[i]) != 0)
                  return FALSE;
            }
        }

      a_index = 0;
      b_index = 0;
      a_n_children = GSK_XML_PEEK_N_CHILDREN (a);
      b_n_children = GSK_XML_PEEK_N_CHILDREN (b);
      while (a_index < a_n_children && b_index < b_n_children)
        {
          GskXml *achild = GSK_XML_PEEK_CHILD (a, a_index);
          GskXml *bchild = GSK_XML_PEEK_CHILD (b, b_index);
          if (achild->type == GSK_XML_ELEMENT
           && bchild->type == GSK_XML_ELEMENT)
            {
              if (!gsk_xml_equal_with_flags (achild, bchild, flags))
                return FALSE;
              a_index++;
              b_index++;
            }
          else if (achild->type == GSK_XML_ELEMENT
               &&  bchild->type == GSK_XML_TEXT)
            {
              /* is the text node ignorable? */
              if (IS_TEXT_NODE_IGNORABLE (bchild, flags))
                b_index++;
              else
                return FALSE;
            }
          else if (achild->type == GSK_XML_TEXT
               &&  bchild->type == GSK_XML_ELEMENT)
            {
              if (IS_TEXT_NODE_IGNORABLE (achild, flags))
                a_index++;
              else
                return FALSE;
            }
          else if (achild->type == GSK_XML_TEXT
                && bchild->type == GSK_XML_TEXT)
            {
              if (!gsk_xml_equal_with_flags (achild, bchild, flags))
                return FALSE;
              a_index++;
              b_index++;
            }
          else
            g_assert_not_reached ();
        }
      if (flags & GSK_XML_EQUAL_IGNORE_TRIVIAL_WHITESPACE)
        {
          if (a_index < a_n_children
           && gsk_xml_is_whitespace (GSK_XML_PEEK_CHILD (a, a_index)))
            a_index++;
          if (b_index < b_n_children
           && gsk_xml_is_whitespace (GSK_XML_PEEK_CHILD (b, b_index)))
            b_index++;
        }
      if (a_index != a_n_children
       || b_index != b_n_children)
        return FALSE;
      return TRUE;
    }
  else if (a->type == GSK_XML_TEXT)
    {
      const char *atxt = GSK_XML_PEEK_TEXT (a);
      const char *btxt = GSK_XML_PEEK_TEXT (b);
      maybe_ignore_ws (&atxt, flags);
      maybe_ignore_ws (&btxt, flags);
      while (*atxt != 0 && *atxt == *btxt)
        {
          atxt++;
          btxt++;
        }
      maybe_ignore_ws (&atxt, flags);
      maybe_ignore_ws (&btxt, flags);
      if (*atxt || *btxt)
        return FALSE;
      return TRUE;
    }
  else
    g_return_val_if_reached (FALSE);
}

gsize
gsk_xml_estimate_size (const GskXml  *xml)
{
  switch (xml->type)
    {
    case GSK_XML_TEXT:
      return strlen (GSK_XML_PEEK_TEXT (xml)) + 1 + sizeof (GskXml);
    case GSK_XML_ELEMENT:
      {
        guint n_attrs = GSK_XML_PEEK_N_ATTRS (xml);
        char **attrs = GSK_XML_PEEK_ATTRS (xml);
        gsize rv = sizeof (GskXmlElement);
        guint nc = GSK_XML_PEEK_N_CHILDREN (xml);
        guint i;
        for (i = 0; i < n_attrs * 2; i++)
          rv += sizeof (char *) + strlen (attrs[i]) + 1;
        rv += sizeof (char*);
        rv += nc * sizeof (GskXml *);
        for (i = 0; i < nc; i++)
          rv += gsk_xml_estimate_size (GSK_XML_PEEK_CHILD (xml, i));
        return rv;
      }
    default:
      g_return_val_if_reached (0);
    }
}
