#include <stdlib.h>
#include <string.h>
#include "../gskerror.h"
#include "gskxmlvaluereader.h"

/*
 * XXX: GMarkup assumes UTF-8, so this isn't 8-bit clean (not to mention
 * string-escaping issues in GskXmlValueWriter).
 */

/*
 *
 * Helpers.
 *
 */

static __inline__ void
g_value_set_string_len (GValue *value, const char *str, guint len)
{
  char *tmp;
  tmp = g_alloca (len + 1);
  memcpy (tmp, str, len);
  tmp[len] = 0;
  g_value_set_string (value, tmp);
}

static __inline__ void
g_value_move (GValue *dst, GValue *src)
{
  memcpy (dst, src, sizeof (*dst));
  memset (src, 0, sizeof (*src));
}

/* Compare canonical versions of property names. */

static gboolean
property_names_equal (const char *pa, const char *pb)
{
  g_return_val_if_fail (pa && pb, FALSE);
  for ( ; ; ++pa, ++pb)
    {
      char a = *pa;
      char b = *pb;

      if (a == '\0')
	return b ? FALSE : TRUE;
      else if (b == '\0')
	return FALSE;
      else if (a != b)
	{
	  /* Only if both a and b are equivalent to '-', are they still
	   * equal, so if either one is in the non-equivalent class,
	   * they are not equal.
	   */
	  if (g_ascii_isalnum (a) || g_ascii_isalnum (b))
	    return FALSE;
	}
    }
}

static gboolean
parse_text_value (GValue *value, const char *str, int len, GError **error)
{
  GType type = G_VALUE_TYPE (value);
  switch (type)
    {
      case G_TYPE_CHAR:
	if (len == 0) return FALSE;
	g_value_set_char (value, *str);
	return TRUE;
      case G_TYPE_UCHAR:
	if (len == 0) return FALSE;
	g_value_set_uchar (value, *str);
	return TRUE;
      case G_TYPE_BOOLEAN:
	{
	  if (len == 0) return FALSE;
          switch (*str)
	    {
	      case 'y': case 'Y': case 't': case 'T': case '1':
		g_value_set_boolean (value, TRUE);
		return TRUE;
	      case 'n': case 'N': case 'f': case 'F': case '0':
		g_value_set_boolean (value, FALSE);
		return TRUE;
	    }
	  return FALSE;
	}

#define STRTOX_TYPE_CASE(T,t,fct) \
      case G_TYPE_##T: \
	{ \
	  char *tmp, *end; \
	  int base; \
	  g##t parsed_val; \
	  if (len > 1023) \
	    len = 1023; \
	  tmp = g_alloca (len + 1); \
	  memcpy (tmp, str, len); \
	  tmp[len] = 0; \
	  base = (tmp[0] == '0' && tmp[1] == 'x') ? 16 : 10; \
	  parsed_val = (g##t) fct (tmp, &end, base); \
	  if (tmp == end) \
	    return FALSE; \
	  g_value_set_##t (value, parsed_val); \
	} \
	break;

      STRTOX_TYPE_CASE (INT,    int,    strtol)
      STRTOX_TYPE_CASE (UINT,   uint,   strtoul)
      STRTOX_TYPE_CASE (LONG,   long,   strtol)
      STRTOX_TYPE_CASE (ULONG,  ulong,  strtoul)
      STRTOX_TYPE_CASE (INT64,  int64,  strtoll)
      STRTOX_TYPE_CASE (UINT64, uint64, strtoull)

#undef STRTOX_TYPE_CASE

      case G_TYPE_FLOAT:
      case G_TYPE_DOUBLE:
	{
	  char *tmp;
	  char *end;
	  if (len > 1023)
	    len = 1023;
	  tmp = g_alloca (len + 1);
	  memcpy (tmp, str, len);
	  tmp[len] = 0;
	  switch (type)
	    {
	      case G_TYPE_FLOAT:
		g_value_set_float (value, (gfloat) strtod (tmp, &end));
		break;
	      case G_TYPE_DOUBLE:
		g_value_set_double (value, (gdouble) strtod (tmp, &end));
		break;
	    }
	  if (tmp == end)
	    return FALSE;
	}
	break;

      case G_TYPE_STRING:
	g_value_set_string_len (value, str, len);
	break;

      /* TODO: FLAGS, ENUMS */

      default:
	{
	  /* See if we can use g_value_transform to parse type
	   * from a string.
	   */
	  if (g_value_type_transformable (G_TYPE_STRING, type))
	    {
	      GValue tmp_value = { 0, { { 0 }, { 0 } } };
	      gboolean result;

	      g_value_init (&tmp_value, G_TYPE_STRING);
	      g_value_set_string_len (&tmp_value, str, len);
	      result = g_value_transform (&tmp_value, value);
	      if (!result)
		{
		  g_set_error (error,
			       GSK_G_ERROR_DOMAIN,
			       0, /* TODO: error code */
			       "error transforming string '%s' to a %s",
			       g_value_get_string (&tmp_value),
			       g_type_name (type));
		}
	      g_value_unset (&tmp_value);
	      return result;
	    }
	  else
	    {
	      g_set_error (error,
			   GSK_G_ERROR_DOMAIN,
			   0, /* TODO: error code */
			   "cannot parse value of type %s",
			   g_type_name (type));
	      return FALSE;
	    }
	}
    }
  return TRUE;
}

/*
 *
 * XmlStackFrame
 *
 *
 */

/* Our little grammar/state diagram:
 *
 *   VALUE:         <CLASS> OBJECT-BODY </CLASS>
 *                | <TYPE> VALUE-TEXT </TYPE>
 *                | TEXT
 *
 *   OBJECT-BODY:   <PROPERTY> [push] VALUE [pop] </PROPERTY> OBJECT-BODY
 *                | empty
 *
 *   VALUE-TEXT:    TEXT
 */
typedef enum _XmlState
  {
    STATE_VALUE,           /* expecting VALUE       */
    STATE_OBJECT_BODY,     /* expecting OBJECT-BODY */
    STATE_PROPERTY_CLOSE,  /* expecting </PROPERTY> */
    STATE_VALUE_TEXT,      /* expecting TEXT */
    STATE_VALUE_CLOSE      /* expecting </TYPE> */
  }
XmlState;

typedef struct _XmlStackFrame XmlStackFrame;

struct _XmlStackFrame
{
  /* Current scanning state. */
  XmlState state;

  /* What type we're supposed to instantiate in this frame
   * (or G_TYPE_INVALID for a top-level value of unspecified type).
   */
  GType type;

  /* The value being constructed. */
  GValue value;

  /* If value is an object, properties to be set at construction: */
  GArray *properties; /* of GParameter */

  /* If value is an object and we've pushed a new stack frame for
   * one of its properties, this is the corresponding GParamSpec.
   * (We assume this cannot go away, so we don't reference it.)
   */
  GParamSpec *param_spec;

  /* Parent stack frame. */
  XmlStackFrame *parent;
};

static GMemChunk *xml_stack_frame_chunk = NULL;
G_LOCK_DEFINE_STATIC (xml_stack_frame_chunk);

static __inline__ XmlStackFrame *
xml_stack_frame_alloc0 (void)
{
  XmlStackFrame *frame;

  G_LOCK (xml_stack_frame_chunk);
  if (xml_stack_frame_chunk == NULL)
    {
      xml_stack_frame_chunk =
	g_mem_chunk_create (XmlStackFrame, 64, G_ALLOC_AND_FREE);
    }
  frame = g_mem_chunk_alloc0 (xml_stack_frame_chunk);
  G_UNLOCK (xml_stack_frame_chunk);
  return frame;
}

static __inline__ void
xml_stack_frame_free (XmlStackFrame *frame)
{
  G_LOCK (xml_stack_frame_chunk);
  g_mem_chunk_free (xml_stack_frame_chunk, frame);
  G_UNLOCK (xml_stack_frame_chunk);
}

/* Push a stack frame for a value of the given type. */
static XmlStackFrame *
xml_stack_push (GType type, XmlStackFrame *parent)
{
  XmlStackFrame *frame;

  frame = xml_stack_frame_alloc0 ();
  frame->state = STATE_VALUE;
  frame->type = type;
  frame->parent = parent;
  if (type)
    g_value_init (&frame->value, type);
  return frame;
}

/* Push a stack frame for an object property. */
static XmlStackFrame *
xml_stack_push_property (GParamSpec *param_spec, XmlStackFrame *parent)
{
  GType type;

  parent->param_spec = param_spec;

  /* For GValueArray properties, we instantiate the element type. */
  if (G_PARAM_SPEC_VALUE_TYPE (param_spec) == G_TYPE_VALUE_ARRAY)
    {
      GParamSpecValueArray *param_spec_value_array;
      g_return_val_if_fail (G_IS_PARAM_SPEC_VALUE_ARRAY (param_spec), NULL);
      param_spec_value_array = G_PARAM_SPEC_VALUE_ARRAY (param_spec);
      type = G_PARAM_SPEC_VALUE_TYPE (param_spec_value_array->element_spec);
    }
  else
    type = G_PARAM_SPEC_VALUE_TYPE (param_spec);

  return xml_stack_push (type, parent);
}

static void
xml_stack_frame_destroy_one (XmlStackFrame *frame)
{
  if (G_VALUE_TYPE (&frame->value))
    g_value_unset (&frame->value);

  /* (param_spec is static) */

  if (frame->properties)
    {
      GArray *properties = frame->properties;
      guint i;
      for (i = 0; i < properties->len; ++i)
	{
	  /* (param->name is copy of static param_spec->name) */
	  GValue *value = & g_array_index (properties, GParameter, i).value;
	  if (G_VALUE_TYPE (value))
	    g_value_unset (value);
	}
      g_array_free (properties, TRUE);
    }

  xml_stack_frame_free (frame);
}

static void
xml_stack_frame_destroy_stack (XmlStackFrame *top)
{
  while (top)
    {
      XmlStackFrame *parent = top->parent;
      xml_stack_frame_destroy_one (top);
      top = parent;
    }
}

/*
 *
 * GskXmlValueReader
 *
 */

struct _GskXmlValueReader
{
  GMarkupParseContext *parse_context;

  GskGtypeLoader *type_loader;
  XmlStackFrame *stack;

  /* Position data for error reporting. */
  char *filename;
  gint line_start, line_offset, char_offset;

  /* What type we must output. */
  GType output_type;

  /* Callback to report output to client. */
  GskXmlValueFunc value_callback;
  gpointer value_callback_data;
  GDestroyNotify value_callback_destroy;

  guint had_error : 1;
};

#define GSK_XML_VALUE_READER(obj) ((GskXmlValueReader *) (obj))

/*
 *
 * Error handling
 *
 */

void
gsk_xml_value_reader_set_error (GskXmlValueReader  *reader,
				GError            **error,
				gint                error_code,
				const char         *format,
				...)
{
  va_list args;
  char *message;
  gint line_no;
  gint char_no;

  reader->had_error = 1;

  va_start (args, format);
  message = g_strdup_vprintf (format, args);
  va_end (args);

  g_markup_parse_context_get_position (reader->parse_context,
				       &line_no,
				       &char_no);
  if (line_no == reader->line_start)
    char_no += reader->char_offset;
  line_no += reader->line_offset;

  if (reader->filename)
    g_set_error (error,
		 GSK_G_ERROR_DOMAIN,
		 error_code,
		 "%s, line %d, character %d: %s",
		 reader->filename, line_no, char_no, message);
  else
    g_set_error (error,
		 GSK_G_ERROR_DOMAIN,
		 error_code,
		 "line %d, character %d: %s",
		 line_no, char_no, message);

  g_free (message);
}

/* format describes what we got; add a description of what we expected. */

static void
gsk_xml_value_reader_set_error_mismatch (GskXmlValueReader  *reader,
					 GError            **error,
					 gint                error_code,
					 const char         *format,
					 ...)
{
  va_list args;
  XmlStackFrame *top = reader->stack;
  gchar *got, *expected;

  va_start (args, format);
  got = g_strdup_vprintf (format, args);
  va_end (args);

  g_return_if_fail (top);
  switch (top->state)
    {
      case STATE_VALUE:
	expected = g_strdup ("<CLASS> or text");
	break;
      case STATE_OBJECT_BODY:
	expected = g_strdup_printf ("<PROPERTY>, or </%s>",
				    g_type_name (top->type));
	break;
      case STATE_PROPERTY_CLOSE:
	g_return_if_fail (top->param_spec);
	g_return_if_fail (top->param_spec->name);
	expected = g_strdup_printf ("</%s>", top->param_spec->name);
	break;
      case STATE_VALUE_TEXT:
	expected = g_strdup ("text");
	break;
      case STATE_VALUE_CLOSE:
	expected = g_strdup_printf ("</%s>", g_type_name (top->type));
	break;
      default:
	g_return_if_reached ();
    }
  gsk_xml_value_reader_set_error (reader,
				  error,
				  error_code,
				  "got %s; expected %s",
				  got, expected);
  g_free (expected);
  g_free (got);
}

/* A value has been instantiated and stored in stack->value,
 * by <CLASS> ... </CLASS> or text.
 * Deal with this value according to the parent stack frame.
 */
static void
gsk_xml_value_reader_pop_value (GskXmlValueReader *reader)
{
  XmlStackFrame *top = reader->stack;
  XmlStackFrame *parent = top->parent;
  GValue *value = &top->value;

  if (parent)
    {
      /* value is an object property. */
      GArray *properties = parent->properties;
      GParamSpec *param_spec = parent->param_spec;
      GParameter *param;
      guint index;

      g_return_if_fail (parent->state == STATE_PROPERTY_CLOSE);
      g_return_if_fail (param_spec);

      if (properties == NULL)
	{
	  properties = parent->properties =
	    g_array_new (FALSE, FALSE, sizeof (GParameter));
	}

      /* Special case for GValueArray properties. */
      if (G_PARAM_SPEC_VALUE_TYPE (param_spec) == G_TYPE_VALUE_ARRAY)
	{
	  GValueArray *value_array;
	  /* Check whether the array has already been created. */
	  for (index = 0; index < properties->len; ++index)
	    {
	      param = & g_array_index (properties, GParameter, index);
	      if (property_names_equal (param->name, param_spec->name))
		{
		  value_array = g_value_get_boxed (&param->value);
		  goto ADD;
		}
	    }
	  /* If not, create the array. */
	  value_array = g_value_array_new (1);
	  g_array_set_size (properties, index + 1);
	  param = & g_array_index (properties, GParameter, index);
	  param->name = param_spec->name;
	  memset (&param->value, 0, sizeof (param->value));
	  g_value_init (&param->value, G_TYPE_VALUE_ARRAY);
	  g_value_set_boxed_take_ownership (&param->value, value_array);

	ADD:
	  g_value_array_append (value_array, value);
	}
      else
	{
	  index = properties->len;
	  g_array_set_size (properties, index + 1);
	  param = & g_array_index (properties, GParameter, index);
	  param->name = param_spec->name;
	  g_value_move (&param->value, value);
	}

      reader->stack = parent;
    }
  else
    {
      /* No parent stack frame; done with a top-level value.  Invoke user
       * callback.
       */
      if (reader->value_callback)
	(*reader->value_callback) (value, reader->value_callback_data);

      /* Replace top stack frame for next value. */
      reader->stack = xml_stack_push (reader->output_type, NULL);
    }
  xml_stack_frame_destroy_one (top);
}

/* Text is either a simple type to be deserialized, or a
 * representation for g_value_transform.
 */
static gboolean
instantiate_value_from_text (GskXmlValueReader *reader,
			     const char        *text,
			     gsize              text_len,
			     GError           **error)
{
  XmlStackFrame *top = reader->stack;
  GValue *value = &top->value;
  GError *tmp_error = NULL;

  if (G_VALUE_TYPE (value) == G_TYPE_INVALID)
    {
      gsk_xml_value_reader_set_error (reader,
				      error,
				      0, /* TODO: error_code */
				      "can't parse a value of "
					"unspecified type from text");
      return FALSE;
    }
  if (!parse_text_value (value, text, text_len, &tmp_error))
    {
      const char *msg = tmp_error ? tmp_error->message : "unknown error";
      gsk_xml_value_reader_set_error (reader,
				      error,
				      0, /* TODO: error_code */
				      "error parsing %s from text: %s",
				      g_type_name (G_VALUE_TYPE (value)),
				      msg);
      if (tmp_error)
	g_error_free (tmp_error);
      return FALSE;
    }
  return TRUE;
}

static void
handle_start_element (GMarkupParseContext  *context,
		      const gchar          *element_name,
		      const gchar         **attribute_names,
		      const gchar         **attribute_values,
		      gpointer              user_data,
		      GError              **error)
{
  GskXmlValueReader *reader = GSK_XML_VALUE_READER (user_data);
  XmlStackFrame *top = reader->stack;

  (void) context;
  (void) attribute_names;
  (void) attribute_values;

  if (reader->had_error)
    return;

  g_return_if_fail (top);
  switch (top->state)
    {
      case STATE_VALUE:
	{
	  /* Load the type and make sure it's allowed by the
	   * current configuration.
	   */
	  GError *tmp_error = NULL;
	  GType type;

	  type = gsk_gtype_loader_load_type (reader->type_loader,
					     element_name,
					     &tmp_error);
	  if (type == G_TYPE_INVALID)
	    {
	      const char *msg =
		tmp_error ? tmp_error->message : "unknown error";
	      gsk_xml_value_reader_set_error (reader,
					      error,
					      0, /* TODO: error_code */
					      "couldn't load type %s: %s",
					      element_name,
					      msg);
	      g_error_free (tmp_error);
	      return;
	    }
	  if (!gsk_gtype_loader_test_type (reader->type_loader, type))
	    {
	      gsk_xml_value_reader_set_error (reader,
					      error,
					      0, /* TODO: error_code */
					      "%s is not an allowed type",
					      g_type_name (type));
	      return;
	    }
	  if (top->type)
	    {
/* XXX: do we really handle g_value_type_transformable? */
	      if (!(g_type_is_a (type, top->type) ||
		    g_value_type_transformable (type, top->type)))
		{
		  gsk_xml_value_reader_set_error (reader,
						  error,
						  0, /* TODO: error_code */
						  "%s is not a %s",
						  g_type_name (type),
						  g_type_name (top->type));
		  return;
		}
	    }
	  else
	    {
	      /* Only the top-level output type can be unspecified. */
	      g_return_if_fail (top->parent == NULL);
	      g_value_init (&top->value, type);
	    }
	  /* Now scan for properties, if this is an object type,
	   * or parse text if this is a fundamental type.
	   */
	  top->type = type;
	  if (g_type_is_a (type, G_TYPE_OBJECT))
	    top->state = STATE_OBJECT_BODY;
	  else
	    top->state = STATE_VALUE_TEXT;
	  return;
	}

      case STATE_OBJECT_BODY:
	{
	  /* Expecting <PROPERTY>. Is element_name a property? */
	  GObjectClass *klass;
	  GParamSpec *param_spec;

	  klass = G_OBJECT_CLASS (g_type_class_ref (top->type));
	  g_return_if_fail (klass);
	  param_spec = g_object_class_find_property (klass, element_name);
	  g_type_class_unref (klass);
	  if (param_spec)
	    {
	      /* When we pop, we'll scan for </PROPERTY>. */
	      top->state = STATE_PROPERTY_CLOSE;
	      reader->stack = xml_stack_push_property (param_spec, top);
	      return;
	    }
	  /* element_name is not a property, error. */
	  gsk_xml_value_reader_set_error (reader,
					  error,
					  0, /* TODO: error_code */
					  "%s is not a property of %s",
					  element_name,
					  g_type_name (top->type));
	  return;
	}

      default:
	break;
    }
  gsk_xml_value_reader_set_error_mismatch (reader,
					   error,
					   0, /* TODO: error_code */
					   "tag <%s>",
					   element_name);
}

static void
handle_end_element (GMarkupParseContext  *context,
		    const gchar          *element_name,
		    gpointer              user_data,
		    GError              **error)
{
  GskXmlValueReader *reader = GSK_XML_VALUE_READER (user_data);
  XmlStackFrame *top = reader->stack;

  (void) context;
  (void) error;

  if (reader->had_error)
    return;

REDO:
  switch (top->state)
    {
      case STATE_OBJECT_BODY:
	{
	  /* Expecting </CLASS>. */
	  const char *class_name;
	  class_name = g_type_name (top->type);
	  if (strcmp (element_name, class_name) == 0)
	    {
	      /* Try to construct the object. */
	      GParameter *parameters = NULL;
	      int n_parameters = 0;
	      GObject *object;
	      if (top->properties)
		{
		  parameters = (GParameter *) top->properties->data;
		  n_parameters = top->properties->len;
		}
	      object = g_object_newv (top->type, n_parameters, parameters);
	      if (object == NULL)
		{
		  gsk_xml_value_reader_set_error (reader,
						  error,
						  0, /* TODO: error_code */
						  "error constructing a %s",
						  class_name);
		  return;
		}
	      g_value_set_object (&top->value, object);
	      /* value has referenced object */
	      g_object_unref (object);
	      gsk_xml_value_reader_pop_value (reader);
	      return;
	    }
	}
	break;

      case STATE_PROPERTY_CLOSE:
	{
	  /* Expecting </PROPERTY>. */
	  GArray *properties = top->properties;
	  const char *property_name;

	  g_return_if_fail (properties);
	  property_name =
	    g_array_index (properties, GParameter, properties->len - 1).name;
	  g_return_if_fail (property_name);

	  if (property_names_equal (element_name, property_name))
	    {
	      /* Scan for next property. */
	      top->state = STATE_OBJECT_BODY;
	      return;
	    }
	}
	break;

      case STATE_VALUE:
      case STATE_VALUE_TEXT:
	{
	  /* Might get a close tag when we're expecting a length 0 string. */
	  if (!instantiate_value_from_text (reader, "", 0, error))
	    return;
	  if (top->state == STATE_VALUE)
	    {
	      gsk_xml_value_reader_pop_value (reader);
	      top = reader->stack;
	    }
	  else
	    top->state = STATE_VALUE_CLOSE;
	  goto REDO;
	}
	break;

      case STATE_VALUE_CLOSE:
	{
	  /* Expecting </TYPE>. */
	  const char *type_name;
	  type_name = g_type_name (top->type);
	  g_return_if_fail (type_name);
	  if (strcmp (element_name, type_name) == 0)
	    {
	      gsk_xml_value_reader_pop_value (reader);
	      return;
	    }
	}
	break;

      default:
	break;
    }
  gsk_xml_value_reader_set_error_mismatch (reader,
					   error,
					   0, /* TODO: error_code */
					   "tag </%s>",
					   element_name);
}

static void
handle_text (GMarkupParseContext  *context,
	     const gchar          *text,
	     gsize                 text_len,  
	     gpointer              user_data,
	     GError              **error)
{
  GskXmlValueReader *reader = GSK_XML_VALUE_READER (user_data);
  XmlStackFrame *top = reader->stack;

  (void) context;

  g_return_if_fail (top);
  if (reader->had_error)
    return;

  /* Eat whitespace. */
  while (text_len > 0)
    {
      char c = *text;
      if (!g_ascii_isspace (c))
        break;
      --text_len;
      ++text;
    }
  if (text_len <= 0)
    return;

  if (top->state == STATE_VALUE || top->state == STATE_VALUE_TEXT)
    {
      if (instantiate_value_from_text (reader, text, text_len, error))
	{
	  if (top->state == STATE_VALUE)
	    gsk_xml_value_reader_pop_value (reader);
	  else
	    top->state = STATE_VALUE_CLOSE;
	}
      return;
    }

  {
    char *terminated_text;
    terminated_text = g_strndup (text, text_len);
    gsk_xml_value_reader_set_error_mismatch (reader,
					     error,
					     0, /* TODO: error_code */
					     "text '%s'",
					     terminated_text);
    g_free (terminated_text);
  }
}

/* text is not nul-terminated. */
static void
handle_passthrough (GMarkupParseContext *context,
		    const gchar         *passthrough_text,
		    gsize                text_len,
		    gpointer             user_data,
		    GError             **error)
{
  GskXmlValueReader *reader = GSK_XML_VALUE_READER (user_data);
  XmlStackFrame *top = reader->stack;

  (void) context;

  if (reader->had_error)
    return;
  g_return_if_fail (top);

  /* Simply ignore any passthrough except CDATA. */
  if (text_len < 12 ||
      (strncmp (passthrough_text, "<![CDATA[", 9) != 0) ||
      (strncmp (passthrough_text + text_len - 3, "]]>", 3) != 0))
    return;

  passthrough_text += 9;
  text_len -= 12;

/* TODO: perhaps CDATA should be merged with plain text blocks? */
  if (top->state == STATE_VALUE || top->state == STATE_VALUE_TEXT)
    {
      if (instantiate_value_from_text (reader,
				       passthrough_text,
				       text_len,
				       error))
	{
	  if (top->state == STATE_VALUE)
	    gsk_xml_value_reader_pop_value (reader);
	  else
	    top->state = STATE_VALUE_CLOSE;
	}
      return;
    }
  {
    char *terminated_text;
    terminated_text = g_strndup (passthrough_text, text_len);
    gsk_xml_value_reader_set_error_mismatch (reader,
					     error,
					     0, /* TODO: error_code */
					     "text '%s'",
					     terminated_text);
    g_free (terminated_text);
  }
}

/* Create the GMarkupParseContext and the stack. */
static void
gsk_xml_value_reader_create_parser (GskXmlValueReader *reader)
{
  static const GMarkupParser g_markup_parser =
    {
      handle_start_element,
      handle_end_element,
      handle_text,
      handle_passthrough,
      NULL, /* error */
    };

  g_return_if_fail (reader->parse_context == NULL);
  reader->parse_context =
    g_markup_parse_context_new (&g_markup_parser,
				0,          /* flags */
				reader,  /* user_data */
				NULL);      /* user_data_dnotify */

  g_return_if_fail (reader->stack == NULL);
  reader->stack = xml_stack_push (reader->output_type, NULL);
}

/*
 *
 * Public interface.
 *
 */

GskXmlValueReader *
gsk_xml_value_reader_new (GskGtypeLoader  *type_loader,
			  GType            output_type,
			  GskXmlValueFunc  value_func,
			  gpointer         value_func_data,
			  GDestroyNotify   value_func_destroy)
{
  GskXmlValueReader *reader;

  g_return_val_if_fail (type_loader, NULL);
  reader = g_new0 (GskXmlValueReader, 1);
  reader->type_loader = type_loader;
  gsk_gtype_loader_ref (type_loader);
  reader->output_type = output_type;
  g_return_val_if_fail (output_type == G_TYPE_INVALID ||
			  g_type_name (output_type),
			NULL);
  reader->value_callback = value_func;
  reader->value_callback_data = value_func_data;
  reader->value_callback_destroy = value_func_destroy;
  return reader;
}

void
gsk_xml_value_reader_free (GskXmlValueReader *reader)
{
  if (reader->value_callback_destroy)
    (*reader->value_callback_destroy) (reader->value_callback_data);

  if (reader->parse_context)
    {
/* XXX: this isn't reentrant... */
      g_markup_parse_context_free (reader->parse_context);
      reader->parse_context = NULL;
    }

  xml_stack_frame_destroy_stack (reader->stack);

  if (reader->type_loader)
    {
      gsk_gtype_loader_unref (reader->type_loader);
      reader->type_loader = NULL;
    }

  g_free (reader);
}

gboolean
gsk_xml_value_reader_input (GskXmlValueReader *reader,
			    const char        *input,
			    guint              len,
			    GError           **error)
{
  const char *start = input;

  if (reader->had_error)
    return FALSE;

  if (reader->parse_context == NULL)
    {
      /* Only create parser if we get non-whitespace. */
      for (;;)
	{
	  if (len == 0)
	    return TRUE;
	  if (!g_ascii_isspace (*start))
	    break;
	  if (*start == '\n')
	    {
	      ++reader->line_offset;
	      reader->char_offset = 0;
	    }
	  else
	    ++reader->char_offset;
	  ++start;
	  --len;
	}
      gsk_xml_value_reader_create_parser (reader);
    }

  /* Feed the data to the parser. */
  return g_markup_parse_context_parse (reader->parse_context,
				       start,
				       len,
				       error);
}

void
gsk_xml_value_reader_set_pos (GskXmlValueReader *reader,
			      const char        *filename,
			      gint               line_no,
			      gint               char_no)
{
  gint parser_line_no, parser_char_no;

  if (reader->filename)
    g_free (reader->filename);
  reader->filename = filename ? g_strdup (filename) : NULL;

  if (reader->parse_context == NULL)
    gsk_xml_value_reader_create_parser (reader);
  g_markup_parse_context_get_position (reader->parse_context,
				       &parser_line_no,
				       &parser_char_no);
  reader->line_start  = parser_line_no;
  reader->line_offset = line_no - parser_line_no;
  reader->char_offset = char_no - parser_char_no;
}

gboolean
gsk_xml_value_reader_had_error (GskXmlValueReader *reader)
{
  return reader->had_error;
}

/*
 *
 * gsk_load_object_from_xml_file
 *
 */

/* GskXmlValueFunc */
static void
set_object_ptr (const GValue *value, gpointer user_data)
{
  *((GObject **) user_data) = g_value_dup_object (value);
}

GObject *
gsk_load_object_from_xml_file (const char      *path,
			       GskGtypeLoader  *type_loader,
			       GType            object_type,
			       GError         **error)
{
  GskXmlValueReader *xml_value_reader = NULL;
  char *file_contents = NULL;
  gsize file_contents_len;
  GObject *object = NULL;

  if (!g_file_get_contents (path, &file_contents, &file_contents_len, error))
    goto FAIL;
  g_return_val_if_fail (file_contents, NULL);

  xml_value_reader = gsk_xml_value_reader_new (type_loader,
					       object_type,
					       set_object_ptr,
					       &object,
					       NULL);
  g_return_val_if_fail (xml_value_reader, NULL);

  if (!gsk_xml_value_reader_input (xml_value_reader,
				   file_contents,
				   file_contents_len,
				   error))
    goto FAIL;

  if (object == NULL || !g_type_is_a (G_OBJECT_TYPE (object), object_type))
    goto FAIL;

  gsk_xml_value_reader_free (xml_value_reader);
  g_free (file_contents);
  return object;

FAIL:
  if (object)
    g_object_unref (object);
  if (file_contents)
    g_free (file_contents);
  if (xml_value_reader)
    gsk_xml_value_reader_free (xml_value_reader);
  return NULL;
}
