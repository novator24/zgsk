#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "gskxml.h"
#include "../gskerror.h"
#include "../gsksocketaddress.h"
#include "../gskghelpers.h"
#include "../config.h"

typedef struct _Parser Parser;
typedef struct _TypeInfo TypeInfo;
typedef struct _TypeTagInfo TypeTagInfo;

struct _TypeTagInfo
{
  GskXmlObjectHandler    handler;
  gpointer               data;
  GDestroyNotify         destroy;
};

struct _TypeInfo
{
  GType type;
  GHashTable *nickname_to_type; /* GskXmlString => type */
  GHashTable *tag_to_info;      /* GskXmlString => TypeTagInfo */

  GskXmlContextParserFunc func;
  GskXmlContextToXmlFunc  to_xml;
  gpointer                data;
  GDestroyNotify          destroy;

  GskXmlObjectWriter     add_misc_handler;
  gpointer               add_misc_data;
  GDestroyNotify         add_misc_destroy;

  GskXmlValidateFunc     validator;
  gpointer               validator_data;
  GDestroyNotify         validator_data_destroy;
};

struct _GskXmlContext
{
  GHashTable *type_info;         /* GType => TypeInfo */
};

static TypeInfo *
try_type_info (GskXmlContext *context,
               GType          type)
{
  return g_hash_table_lookup (context->type_info, (gpointer) type);
}
static TypeInfo *
force_type_info (GskXmlContext *context,
                 GType          type)
{
  TypeInfo *info = g_hash_table_lookup (context->type_info, (gpointer) type);
  if (info == NULL)
    {
      info = g_new0 (TypeInfo, 1);
      info->type = type;
      g_hash_table_insert (context->type_info, (gpointer) type, info);
    }
  return info;
}

static gboolean
find_best_child (GskXmlNode *node,
                 GskXmlNode **out,
                 GError     **error)
{
  guint i;
  if (node->type == GSK_XML_NODE_TYPE_TEXT)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "expected xml element, got text node");
      return FALSE;
    }
  g_assert (node->type == GSK_XML_NODE_TYPE_ELEMENT);
  if (node->v_element.n_children == 0)
    {
      *out = NULL;
      return TRUE;
    }
  *out = NULL;
  for (i = 0; i < node->v_element.n_children; i++)
    {
      if (node->v_element.children[i]->type == GSK_XML_NODE_TYPE_ELEMENT)
        {
          if (*out)
            {
              g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                           "two element nodes found: cannot tell which to use");
              return FALSE;
            }
          *out = node->v_element.children[i];
        }
    }

  /* use lone text node if nothing else is available. */
  if (*out == NULL)
    {
      g_return_val_if_fail (node->v_element.n_children == 1, FALSE);
      *out = node->v_element.children[0];
    }

  return TRUE;
}

void           gsk_xml_context_register_nickname (GskXmlContext *context,
                                                  GType          base_type,
						  const char    *nickname,
                                                  GType          type)
{
  TypeInfo *type_info = force_type_info (context, base_type);
  GskXmlString *nick = gsk_xml_string_new (nickname);
  if (type_info->nickname_to_type == NULL)
    type_info->nickname_to_type = g_hash_table_new_full (NULL, NULL,
                                                         (GDestroyNotify) gsk_xml_string_unref, NULL);
  g_return_if_fail (g_hash_table_lookup (type_info->nickname_to_type, nick) == NULL);
  g_hash_table_insert (type_info->nickname_to_type, nick, (gpointer) type);
}


void           gsk_xml_context_register_parser   (GskXmlContext         *context,
                                                  GType                  type,
						  GskXmlContextParserFunc func,
						  GskXmlContextToXmlFunc  to_xml,
						  gpointer               data,
						  GDestroyNotify         destroy)
{
  TypeInfo *type_info = force_type_info (context, type);
  g_return_if_fail (func != NULL);
  g_return_if_fail (to_xml != NULL);
  g_return_if_fail (type_info->func == NULL);

  type_info->func = func;
  type_info->to_xml = to_xml;
  type_info->data = data;
  type_info->destroy = destroy;
}


GskXmlNode    *gsk_xml_context_serialize_object  (GskXmlContext         *context,
                                                  gpointer               object,
                                                  GError               **error)
{
  GValue value;
  GskXmlNode *rv;
  memset (&value, 0, sizeof (value));
  g_value_init (&value, G_TYPE_OBJECT);
  g_value_set_object (&value, object);
  rv = gsk_xml_context_serialize_value (context, &value, error);
  g_value_unset (&value);
  return rv;
}

GskXmlNode    *gsk_xml_context_serialize_value   (GskXmlContext         *context,
                                                  GValue                *value,
                                                  GError               **error)
{
  GType type;
  for (type = G_VALUE_TYPE (value); type != 0; type = g_type_parent (type))
    {
      TypeInfo *type_info = try_type_info (context, type);
      if (type_info != NULL && type_info->to_xml != NULL)
        {
          GError *e = NULL;
          GskXmlNode *node = type_info->to_xml (context, value, type_info->data, &e);

          /* success */
          if (node != NULL)
            return node;


          /* real error */
          if (e)
            {
              g_propagate_error (error, e);
              return NULL;
            }

          /* continue trying */
        }
    }
  g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
               "no working serializer registered for type %s", G_VALUE_TYPE_NAME (value));
  return NULL;
}

gboolean       gsk_xml_context_deserialize_value (GskXmlContext         *context,
                                                  GskXmlNode            *node,
                                                  GValue                *out,
						  GError               **error)
{
  GType type;
  for (type = G_VALUE_TYPE (out); type != 0; type = g_type_parent (type))
    {
      TypeInfo *type_info = try_type_info (context, type);
      if (type_info != NULL && type_info->func != NULL)
        {
          GError *e = NULL;
          if (type_info->func (context, node, out, type_info->data, &e))
            return TRUE;

          /* real error */
          if (e)
            {
              g_propagate_error (error, e);
              return FALSE;
            }

          /* continue trying */
        }
    }
  g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
               "no working serializer registered for type %s",
               G_VALUE_TYPE_NAME (out));
  return FALSE;
}

gpointer       gsk_xml_context_deserialize_object(GskXmlContext         *context,
                                                  GType                  base_type,
                                                  GskXmlNode            *node,
						  GError               **error)
{
  GValue value;
  gpointer rv;
  g_return_val_if_fail (g_type_is_a (base_type, G_TYPE_OBJECT), NULL);
  memset (&value, 0, sizeof (value));
  g_value_init (&value, base_type);
  if (!gsk_xml_context_deserialize_value (context, node, &value, error))
    {
      g_value_unset (&value);
      return NULL;
    }
  rv = g_value_dup_object (&value);
  if (rv == NULL)
    g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                 "deserialization indicated success but returned a NULL object");
  return rv;
}


#define DECLARE_PARSER(lc_type)                 \
static gboolean                                 \
parser__##lc_type (GskXmlContext *context,      \
                   GskXmlNode    *node,         \
                   GValue        *value,        \
                   gpointer       data,         \
                   GError       **error)
#define DECLARE_TO_XML(lc_type)                 \
static GskXmlNode *                             \
to_xml__##lc_type (GskXmlContext *context,      \
                   const GValue  *value,        \
                   gpointer       data,         \
                   GError       **error)

#define PARSER_ENSURE_IS_TEXT(typename)                                 \
  if (node == NULL                                                      \
   || node->type != GSK_XML_NODE_TYPE_TEXT)                             \
    {                                                                   \
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,     \
                   "error parsing '%s': expected text node", typename); \
      return FALSE;                                                     \
    }

/* --- Builtin Types --- */
/* builtin type: char */
DECLARE_PARSER (char)
{
  if (node == NULL)
    {
      g_value_set_char (value, 0);
      return TRUE;
    }
  PARSER_ENSURE_IS_TEXT ("char");
  g_value_set_char (value, * (char *) node->v_text.content);
  return TRUE;
}

DECLARE_TO_XML (char)
{
  char c[2];
  c[0] = g_value_get_char (value);
  c[1] = 0;
  if ((guchar) c[0] >= 0x80)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "cannot serialize char with high-bit set");
      return NULL;
    }
  return gsk_xml_node_new_text_c (c);
}

/* builtin type: uchar */
DECLARE_PARSER (uchar)
{
  glong v;
  char *end;
  PARSER_ENSURE_IS_TEXT ("uchar");
  v = strtol ((char *) node->v_text.content, &end, 0);
  if (end == (char*)node->v_text.content || *end != '\0')
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "error parsing number for uchar");
      return FALSE;
    }
  if (v < 0 || v > 255)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "data out-of-range for uchar");
      return FALSE;
    }
  g_value_set_uchar (value, (guchar) v);
  return TRUE;
}

DECLARE_TO_XML (uchar)
{
  char slab[128];
  g_snprintf (slab, sizeof (slab), "%u", g_value_get_uchar (value));
  return gsk_xml_node_new_text_c (slab);
}

/* builtin type: boolean */
DECLARE_PARSER (boolean)
{
  const char *str;
  PARSER_ENSURE_IS_TEXT ("boolean");
  str = (char*)(node->v_text.content);
  if (str[0] == 0)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "cannot parse boolean from empty string");
      return FALSE;
    }
  switch (str[0])
    {
    case 'y':
    case 'Y':
    case 't':
    case 'T':
    case '1':
      g_value_set_boolean (value, TRUE);
      return TRUE;
    case 'n':
    case 'N':
    case 'f':
    case 'F':
    case '0':
      g_value_set_boolean (value, FALSE);
      return TRUE;
    default:
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "cannot parse boolean from the string '%s'", str);
      return FALSE;
    }
}
DECLARE_TO_XML (boolean)
{
  static GskXmlNode *true_node = NULL;
  static GskXmlNode *false_node = NULL;
  if (g_value_get_boolean (value))
    {
      if (true_node == NULL)
        true_node = gsk_xml_node_new_text_c ("1");
      return gsk_xml_node_ref (true_node);
    }
  else
    {
      if (false_node == NULL)
        false_node = gsk_xml_node_new_text_c ("0");
      return gsk_xml_node_ref (false_node);
    }
}

/* builtin type: int */
DECLARE_PARSER (int)
{
  glong v;
  char *end;
  PARSER_ENSURE_IS_TEXT ("int");
  v = strtol ((char *) node->v_text.content, &end, 0);
  if (end == (char*)node->v_text.content || *end != '\0')
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "error parsing number for int");
      return FALSE;
    }
  if (sizeof (long) != sizeof (int))
    {
      if (v < (glong) G_MININT || v > (glong) G_MAXINT)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                       "data out-of-range for int");
          return FALSE;
        }
    }
  g_value_set_int (value, (gint) v);
  return TRUE;
}
DECLARE_TO_XML (int)
{
  char buf[256];
  g_snprintf (buf, sizeof (buf), "%d", g_value_get_int (value));
  return gsk_xml_node_new_text_c (buf);
}

/* builtin type: uint */
DECLARE_PARSER (uint)
{
  gulong v;
  char *end;
  PARSER_ENSURE_IS_TEXT ("uint");
  v = strtoul ((char *) node->v_text.content, &end, 0);
  if (end == (char*)node->v_text.content || *end != '\0')
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "error parsing number for uint");
      return FALSE;
    }
  if (sizeof (long) != sizeof (int))
    {
      if (v > (gulong) G_MAXUINT)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                       "data out-of-range for uint");
          return FALSE;
        }
    }
  g_value_set_uint (value, (guint) v);
  return TRUE;
}
DECLARE_TO_XML (uint)
{
  char buf[256];
  g_snprintf (buf, sizeof (buf), "%u", g_value_get_uint (value));
  return gsk_xml_node_new_text_c (buf);
}

/* builtin type: long */
DECLARE_PARSER (long)
{
  glong v;
  char *end;
  PARSER_ENSURE_IS_TEXT ("long");
  v = strtol ((char *) node->v_text.content, &end, 0);
  if (end == (char*)node->v_text.content || *end != '\0')
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "error parsing number for long");
      return FALSE;
    }
  g_value_set_long (value, v);
  return TRUE;
}
DECLARE_TO_XML (long)
{
  char buf[256];
  g_snprintf (buf, sizeof (buf), "%ld", g_value_get_long (value));
  return gsk_xml_node_new_text_c (buf);
}

/* builtin type: ulong */
DECLARE_PARSER (ulong)
{
  gulong v;
  char *end;
  PARSER_ENSURE_IS_TEXT ("ulong");
  v = strtoul ((char *) node->v_text.content, &end, 0);
  if (end == (char*)node->v_text.content || *end != '\0')
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "error parsing number for ulong");
      return FALSE;
    }
  g_value_set_ulong (value, v);
  return TRUE;
}
DECLARE_TO_XML (ulong)
{
  char buf[256];
  g_snprintf (buf, sizeof (buf), "%lu", g_value_get_ulong (value));
  return gsk_xml_node_new_text_c (buf);
}

/* builtin type: int64 */
DECLARE_PARSER (int64)
{
  gint64 v;
  char *end;
  PARSER_ENSURE_IS_TEXT ("int64");
#if HAVE_STRTOLL
  v = strtoll ((char *) node->v_text.content, &end, 0);
#elif HAVE_STRTOQ
  v = strtoq ((char *) node->v_text.content, &end, 0);
#else
#error "no strtoll or strtoq"
#endif
  if (end == (char*)node->v_text.content || *end != '\0')
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "error parsing number for int64");
      return FALSE;
    }
  g_value_set_int64 (value, v);
  return TRUE;
}
DECLARE_TO_XML (int64)
{
  char buf[256];
  g_snprintf (buf, sizeof (buf), "%"G_GINT64_FORMAT, g_value_get_int64 (value));
  return gsk_xml_node_new_text_c (buf);
}

/* builtin type: uint64 */
DECLARE_PARSER (uint64)
{
  guint64 v;
  char *end;
  PARSER_ENSURE_IS_TEXT ("uint64");
  v = g_ascii_strtoull ((char *) node->v_text.content, &end, 0);
  if (end == (char*)node->v_text.content || *end != '\0')
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "error parsing number for uint64");
      return FALSE;
    }
  g_value_set_uint64 (value, v);
  return TRUE;
}
DECLARE_TO_XML (uint64)
{
  char buf[256];
  g_snprintf (buf, sizeof (buf), "%"G_GUINT64_FORMAT, g_value_get_uint64 (value));
  return gsk_xml_node_new_text_c (buf);
}

/* builtin type: enum */
DECLARE_PARSER (enum)
{
  GEnumClass *class;
  GEnumValue *enum_value;
  GskXmlString *content;
  PARSER_ENSURE_IS_TEXT ("enum");

  content = node->v_text.content;

  class = g_type_class_ref (G_VALUE_TYPE (value));
  g_return_val_if_fail (G_IS_ENUM_CLASS (class), FALSE);
  enum_value = g_enum_get_value_by_name (class, (char*) content);
  if (enum_value == NULL)
    enum_value = g_enum_get_value_by_nick (class, (char*) content);

  if (enum_value == NULL)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "no value '%s' for enum %s",
                   (char*) content, G_VALUE_TYPE_NAME (value));
      g_type_class_unref (class);
      return FALSE;
    }
  g_value_set_enum (value, enum_value->value);
  g_type_class_unref (class);
  return TRUE;
}
DECLARE_TO_XML (enum)
{
  GEnumClass *class;
  GEnumValue *enum_value;
  GskXmlNode *rv;
  class = g_type_class_ref (G_VALUE_TYPE (value));
  g_return_val_if_fail (G_IS_ENUM_CLASS (class), FALSE);
  enum_value = g_enum_get_value (class, g_value_get_enum (value));
  if (enum_value == NULL)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "invalid value %d for enum %s",
                   g_value_get_enum (value),
                   G_VALUE_TYPE_NAME (value));
      g_type_class_unref (class);
      return NULL;
    }
  rv = gsk_xml_node_new_text_c (enum_value->value_name);
  g_type_class_unref (class);
  return rv;
}

/* builtin type: flags */
static gboolean
add_flag (GType        type,
          GFlagsClass *class,
          const char  *start,
          guint        len,
          guint       *flags_inout,
          GError     **error)
{
  char *str = g_alloca (len + 1);
  memcpy (str, start, len);
  GFlagsValue *value;
  str[len] = '\0';

  value = g_flags_get_value_by_name (class, str);
  if (value == NULL)
    value = g_flags_get_value_by_nick (class, str);
  if (value == NULL)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "no value named %s in flag %s",
                   str, g_type_name (type));
      return FALSE;
    }

  *flags_inout |= value->value;
  return TRUE;
}

DECLARE_PARSER (flags)
{
  guint flags = 0;
  if (node != NULL)
    {
      GType type = G_VALUE_TYPE (value);
      GFlagsClass *class = g_type_class_ref (type);
      const char *str;
      const char *start = NULL;
      PARSER_ENSURE_IS_TEXT ("flags");
      str = (char*)(node->v_text.content);
      while (*str)
        {
          gunichar u = g_utf8_get_char (str);

          if (g_unichar_isspace (u) || *str == ',' || *str == '|')
            {
              if (start != NULL)
                {
                  if (!add_flag (type, class, start, str - start, &flags, error))
                    return FALSE;
                  start = NULL;
                }
            }
          else if (start == NULL)
            start = str;

          str = g_utf8_next_char (str);
        }
      if (start != NULL)
        if (!add_flag (type,class, start, str - start, &flags, error))
          return FALSE;
    }
  g_value_set_flags (value, flags);
  return TRUE;
}

DECLARE_TO_XML (flags)
{
  GFlagsClass *class;
  GskXmlNode *rv;
  char *flag_names[65];
  guint flags = g_value_get_flags (value);
  guint n_flags = 0;
  char *text;
  class = g_type_class_ref (G_VALUE_TYPE (value));
  g_return_val_if_fail (G_IS_FLAGS_CLASS (class), FALSE);
  while (flags != 0)
    {
      GFlagsValue *flag_value = g_flags_get_first_value (class, flags);
      if (flag_value == NULL)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                       "invalid value %d for flags %s",
                       flags,
                       G_VALUE_TYPE_NAME (value));
          g_type_class_unref (class);
          return NULL;
        }
      flag_names[n_flags++] = flag_value->value_nick;
    }
  flag_names[n_flags] = NULL;
  text = g_strjoinv (",", flag_names);
  rv = gsk_xml_node_new_text_c (text);
  g_type_class_unref (class);
  g_free (text);
  return rv;
}

/* builtin type: float */
DECLARE_PARSER (float)
{
  char *str;
  char *end;
  double d;
  PARSER_ENSURE_IS_TEXT ("float");
  str = (char*)(node->v_text.content);
  d = g_ascii_strtod (str, &end);
  if (str == end || *end != '\0')
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "error parsing float");
      return FALSE;
    }

  /* TODO: check in range for float */

  g_value_set_float (value, d);
  return TRUE;
}
DECLARE_TO_XML (float)
{
  char buf[G_ASCII_DTOSTR_BUF_SIZE];
  g_ascii_formatd (buf, sizeof (buf), "%.12g", g_value_get_float (value));
  return gsk_xml_node_new_text_c (buf);
}

/* builtin type: double */
DECLARE_PARSER (double)
{
  char *str;
  char *end;
  double d;
  PARSER_ENSURE_IS_TEXT ("double");
  str = (char*)(node->v_text.content);
  d = g_ascii_strtod (str, &end);
  if (str == end || *end != '\0')
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "error parsing double");
      return FALSE;
    }
  g_value_set_double (value, d);
  return TRUE;
}
DECLARE_TO_XML (double)
{
  char buf[G_ASCII_DTOSTR_BUF_SIZE];
  g_ascii_dtostr (buf, sizeof (buf), g_value_get_double (value));
  return gsk_xml_node_new_text_c (buf);
}

/* builtin type: string */
DECLARE_PARSER (string)
{
  if (node == NULL)
    {
      g_value_set_string (value, "");
      return TRUE;
    }
  PARSER_ENSURE_IS_TEXT ("string");
  g_value_set_string (value, (char*)(node->v_text.content));
  return TRUE;
}
DECLARE_TO_XML (string)
{
  const char *str = g_value_get_string (value);
  if (str == NULL)
    str = "";           /* XXX: what should we do here? */
  return gsk_xml_node_new_text_c (str);
}

/* builtin type: object */
DECLARE_PARSER (object)
{
  GType type;
  GObjectClass *class;
  GObject *rv = NULL;

  GArray *params;
  GPtrArray *extra_nodes;

  guint i;

  if (node == NULL)
    {
      g_value_set_object (value, NULL);
      return TRUE;
    }

  params = g_array_new (FALSE, FALSE, sizeof (GParameter));
  extra_nodes = g_ptr_array_new ();
  if (node->type != GSK_XML_NODE_TYPE_ELEMENT)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "only xml nodes are allowed for generic object parsing");
      return FALSE;
    }
  type = g_type_from_name ((char*) node->v_element.name);
  if (type == 0)
    {
      /* try nicknames */
      type = G_VALUE_TYPE (value);
      while (type != 0)
        {
          TypeInfo *type_info = try_type_info (context, type);
          if (type_info != NULL && type_info->nickname_to_type != NULL)
            {
              GType t = (GType) g_hash_table_lookup (type_info->nickname_to_type, node->v_element.name);
              if (t != 0)
                {
                  type = t;
                  break;
                }
            }
        }
      if (type == 0)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                       "no object type found for <%s>", (char*) node->v_element.name);
          return FALSE;
        }
    }

  /* XXX: real error-handling required here? (unsure) */
  g_return_val_if_fail (g_type_is_a (type, G_VALUE_TYPE (value)), FALSE);

  class = g_type_class_ref (type);

  for (i = 0; i < node->v_element.n_children; i++)
    if (node->v_element.children[i]->type == GSK_XML_NODE_TYPE_ELEMENT)
      {
        GskXmlNode *child = node->v_element.children[i];
        GParamSpec *pspec = g_object_class_find_property (class, (char*)(child->v_element.name));
        if (pspec != NULL)
          {
            GParameter p;
            GskXmlNode *value_child;
            p.name = pspec->name;
            memset (&p.value, 0, sizeof (GValue));
            g_value_init (&p.value, G_PARAM_SPEC_VALUE_TYPE (pspec));

            /* find the best child node of 'child' */
            if (!find_best_child (child, &value_child, error)
             || !gsk_xml_context_deserialize_value (context, value_child, &p.value, error))
              {
                gsk_g_error_add_prefix (error, "parsing %s::%s", g_type_name (type), pspec->name);
                goto cleanup_and_return;
              }
            g_array_append_val (params, p);
          }
        else
          {
            g_ptr_array_add (extra_nodes, child);
          }
      }

  /* Construct object */
  rv = g_object_newv (type, params->len, (GParameter *)(params->data));

  /* Handle 'extra' nodes */
  if (extra_nodes->len > 0)
    {
      GPtrArray *hashtables;
      TypeTagInfo *fallback = NULL;
      guint i;
      GType t;

      /* make a straightforward array of hashtables,
         and the best fallback */
      hashtables = g_ptr_array_new ();
      for (t = type; t != 0; t = g_type_parent (t))
        {
          TypeInfo *type_info = try_type_info (context, t);
          if (type_info->tag_to_info)
            g_ptr_array_add (hashtables, type_info->tag_to_info);
          if (fallback == NULL && type_info->tag_to_info)
            fallback = g_hash_table_lookup (type_info->tag_to_info, NULL);
        }

      for (i = 0; i < extra_nodes->len; i++)
        {
          GskXmlNode *n = extra_nodes->pdata[i];
          TypeTagInfo *handler = NULL;
          guint h;
          for (h = 0; h < hashtables->len && handler == NULL; h++)
            handler = g_hash_table_lookup (hashtables->pdata[h], n->v_element.name);
          if (handler == NULL)
            handler = fallback;
          if (!handler)
            {
              g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                           "cannot parse %s for %s",
                           (char*)(n->v_element.name), g_type_name (type));
              g_object_unref (rv);
              rv = NULL;

              goto cleanup_and_return;
            }
          if (!handler->handler (context, rv, n, handler->data, error))
            {
              gsk_g_error_add_prefix (error, "parsing %s", g_type_name (type));
              g_object_unref (rv);
              rv = NULL;
              goto cleanup_and_return;
            }
        }
      g_ptr_array_free (hashtables, TRUE);
    }

  /* Validate the object, if possible */
  {
    TypeInfo *type_info;
    GType t;
    for (t = G_OBJECT_TYPE (rv); t != 0; t = g_type_parent (t))
      {
        type_info = try_type_info (context, G_OBJECT_TYPE (rv));
        if (type_info && type_info->validator)
          if (!type_info->validator (context, rv, type_info->validator_data, error))
            {
              g_object_unref (rv);
              rv = NULL;
              goto cleanup_and_return;
            }
      }
  }

cleanup_and_return:
  for (i = 0; i < params->len; i++)
    g_value_unset (&g_array_index (params, GParameter, i).value);
  g_array_free (params, TRUE);
  g_ptr_array_free (extra_nodes, TRUE);
  g_type_class_unref (class);
  if (rv)
    {
      g_value_take_object (value, rv);
      return TRUE;
    }
  else
    return FALSE;
}

DECLARE_TO_XML (object)
{
  GskXmlBuilder *builder = gsk_xml_builder_new (0);
  GObject *object = g_value_get_object (value);
  GParamSpec **pspecs;
  guint n_pspecs;
  guint i;
  GType t;
  GskXmlNode *rv;
  if (object == NULL)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "cannot serialize a NULL object value");
      return NULL;
    }
  gsk_xml_builder_start_c (builder, G_OBJECT_TYPE_NAME (object), 0, NULL);

  /* properties */
  pspecs = g_object_class_list_properties (G_OBJECT_GET_CLASS (object), &n_pspecs);
  for (i = 0; i < n_pspecs; i++)
    {
      GValue value;
      GParamSpec *p = pspecs[i];
      GskXmlNode *child;
      memset (&value, 0, sizeof (GValue));
      g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (p));
      g_object_get_property (object, p->name, &value);
      child = gsk_xml_context_serialize_value (context, &value, error);
      gsk_xml_builder_start_c (builder, p->name, 0, NULL);
      if (child)
        {
          gsk_xml_builder_add_node (builder, child);
          gsk_xml_node_unref (child);
        }
      gsk_xml_builder_end (builder, NULL);
      g_value_unset (&value);
    }
  g_free (pspecs);

  /* misc serialization */
  for (t = G_OBJECT_TYPE (object); t != 0; t = g_type_parent (t))
    {
      TypeInfo *type_info = try_type_info (context, t);
      if (type_info && type_info->to_xml)
        {
          if (!type_info->add_misc_handler (context, object, builder, type_info->add_misc_data, error))
            {
              gsk_xml_builder_free (builder);
              return NULL;
            }
        }
    }

  rv = gsk_xml_builder_end (builder, NULL);
  gsk_xml_node_ref (rv);
  gsk_xml_builder_free (builder);
  return rv;
}

/* builtin type: GskSocketAddress */
DECLARE_PARSER (socket_address)
{
  if (node == NULL)
    {
      g_value_set_object (value, NULL);
      return TRUE;
    }
  if (node->type == GSK_XML_NODE_TYPE_ELEMENT)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                   "did not expect element node");
      return FALSE;
    }
  else
    {
      const char *text = (char *) (node->v_text.content);
      gint ip_int[4], ip_port;
      if (memcmp (text, "unix:", 5) == 0)
        {
          GskSocketAddress *addr = gsk_socket_address_local_new (text + 5);
          g_value_set_object (value, G_OBJECT (addr));
          g_object_unref (addr);
          return TRUE;
        }
      else if (sscanf (text, "%d.%d.%d.%d:%d",
                       &ip_int[0],
                       &ip_int[1],
                       &ip_int[2],
                       &ip_int[3],
                       &ip_port) == 5)
        {
          GskSocketAddress *addr;
          guint8 ip_addr[4] = { ip_int[0], ip_int[1], ip_int[2], ip_int[3] };
          addr = gsk_socket_address_ipv4_new (ip_addr, ip_port);
          g_value_set_object (value, G_OBJECT (addr));
          g_object_unref (addr);
          return TRUE;
        }
      else
        {
          /* TODO: other forms.  generally: what about symbolic hostnames? */
          g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
                       "could not parse socketaddress from %s", text);
          return FALSE;
        }
    }
}
DECLARE_TO_XML (socket_address)
{
  GskSocketAddress *addr = g_value_get_object (value);
  char *addr_str;
  GskXmlNode *rv;
  if (addr == NULL)
    return gsk_xml_node_new_text (gsk_xml_string__);
  addr_str = gsk_socket_address_to_string (addr);
  rv = gsk_xml_node_new_text_c (addr_str);
  g_free (addr_str);
  return rv;
}

GskXmlContext *gsk_xml_context_new               (void)
{
  GskXmlContext *context = g_new (GskXmlContext, 1);
  context->type_info = g_hash_table_new (NULL, NULL);
#define REGISTER(gtype, lc_suffix)                                   \
  gsk_xml_context_register_parser (context, gtype,                   \
                                   parser__##lc_suffix,              \
                                   to_xml__##lc_suffix,              \
                                   NULL, NULL)
  REGISTER (G_TYPE_CHAR, char);
  REGISTER (G_TYPE_UCHAR, uchar);
  REGISTER (G_TYPE_BOOLEAN, boolean);
  REGISTER (G_TYPE_INT, int);
  REGISTER (G_TYPE_UINT, uint);
  REGISTER (G_TYPE_LONG, long);
  REGISTER (G_TYPE_ULONG, ulong);
  REGISTER (G_TYPE_INT64, int64);
  REGISTER (G_TYPE_UINT64, uint64);
  REGISTER (G_TYPE_ENUM, enum);
  REGISTER (G_TYPE_FLAGS, flags);
  REGISTER (G_TYPE_FLOAT, float);
  REGISTER (G_TYPE_DOUBLE, double);
  REGISTER (G_TYPE_STRING, string);
  REGISTER (G_TYPE_OBJECT, object);
  REGISTER (GSK_TYPE_SOCKET_ADDRESS, socket_address);
#undef REGISTER
  return context;
};

GskXmlContext *gsk_xml_context_global            (void)
{
  static GskXmlContext *rv = NULL;
  if (rv == NULL)
    rv = gsk_xml_context_new ();
  return rv;
}
