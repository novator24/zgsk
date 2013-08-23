#include <string.h>
#include "gskxmlformat.h"
#include "gskxmlvaluewriter.h"

#define MAX_SIMPLE_TYPE_ATOM_LEN 64

/*
 * Helpers.
 */

static GHashTable *serialized_properties_map = NULL;

static GPtrArray *
get_serialized_properties (GObjectClass *object_class)
{
  GPtrArray *serialized_properties;
  GType type;

  g_return_val_if_fail (object_class, NULL);
  type = G_OBJECT_CLASS_TYPE (object_class);
  g_return_val_if_fail (type, NULL);

  if (serialized_properties_map == NULL)
    {
      serialized_properties_map = g_hash_table_new (g_direct_hash,
						    g_direct_equal);
      serialized_properties = NULL;
    }
  else
    {
      serialized_properties = g_hash_table_lookup (serialized_properties_map,
						   GUINT_TO_POINTER (type));
    }
  if (serialized_properties == NULL)
    {
      GParamSpec **pspecs;
      guint n_pspecs;

      serialized_properties = g_ptr_array_new ();
      pspecs = g_object_class_list_properties (object_class, &n_pspecs);
      if (n_pspecs > 0)
	{
	  guint i;

	  g_return_val_if_fail (pspecs, NULL);

	  /* Collect only readable properties that aren't ignored. */
	  for (i = 0; i < n_pspecs; ++i)
	    {
	      if (pspecs[i]->flags & G_PARAM_READABLE &&
		  !(pspecs[i]->flags & GSK_XML_FORMAT_PARAM_IGNORE))
		g_ptr_array_add (serialized_properties, pspecs[i]);
	    }
	}
      if (pspecs)
	g_free (pspecs);

      g_hash_table_insert (serialized_properties_map,
                           GUINT_TO_POINTER (type),
                           serialized_properties);
    }
  return serialized_properties;
}

/*
 *
 * GskXmlValueWriter
 *
 */

typedef struct _XmlStackFrame XmlStackFrame;

/* XXX: need another pair of states to handle top-level values
 * of simple type, <TYPE>TEXT</TYPE>.
 */

struct _XmlStackFrame
{
  enum
    {
      STATE_VALUE_START,
      STATE_OBJECT_BODY,
      STATE_PROPERTY_VALUE,
      STATE_PROPERTY_END_TAG,
      STATE_VALUE_ARRAY_BODY,
      STATE_VALUE_ARRAY_VALUE,
      STATE_VALUE_ARRAY_END_TAG,
      STATE_START_TAG_OPEN,
      STATE_START_TAG_NAME,
      STATE_START_TAG_CLOSE,
      STATE_END_TAG_OPEN,
      STATE_END_TAG_NAME,
      STATE_END_TAG_CLOSE
    }
  state;

  union
    {
      struct
	{
	  GValue value;
	  GPtrArray *properties; /* of GParamSpec */
	  guint property_index;
	}
      value_info;
      struct
	{
	  GValueArray *value_array;
	  const char *property_name;
	  guint element_index;
	}
      value_array_info;
      struct
	{
	  const char *tag_name;
	}
      tag_info;
    }
  info;

  XmlStackFrame *parent;
};

static GObjectClass *parent_class = NULL;

static inline XmlStackFrame *
xml_stack_frame_alloc (void)
{
  /* TODO: better allocator */
  return g_new0 (XmlStackFrame, 1);
}

static inline void
xml_stack_frame_free (XmlStackFrame *frame)
{
  g_free (frame);
}

static XmlStackFrame *
push_value (const GValue *value, XmlStackFrame *parent)
{
  XmlStackFrame *frame;

  frame = xml_stack_frame_alloc ();
  frame->state = STATE_VALUE_START;
  g_value_init (&frame->info.value_info.value, G_VALUE_TYPE (value));
  g_value_copy (value, &frame->info.value_info.value);
  frame->parent = parent;
  return frame;
}

static XmlStackFrame *
push_value_property (GObject *object, GParamSpec *pspec, XmlStackFrame *parent)
{
  XmlStackFrame *frame;

  frame = xml_stack_frame_alloc ();
  frame->state = STATE_VALUE_START;
  g_value_init (&frame->info.value_info.value, pspec->value_type);
  g_object_get_property (object, pspec->name, &frame->info.value_info.value);
  frame->parent = parent;
  return frame;
}

static inline XmlStackFrame *
push_start_tag (const char *tag_name, XmlStackFrame *parent)
{
  XmlStackFrame *frame;

  frame = xml_stack_frame_alloc ();
  frame->state = STATE_START_TAG_OPEN;
  frame->info.tag_info.tag_name = tag_name;
  frame->parent = parent;
  return frame;
}

static inline XmlStackFrame *
push_end_tag (const char *tag_name, XmlStackFrame *parent)
{
  XmlStackFrame *frame;

  frame = xml_stack_frame_alloc ();
  frame->state = STATE_END_TAG_OPEN;
  frame->info.tag_info.tag_name = tag_name;
  frame->parent = parent;
  return frame;
}

static inline XmlStackFrame *
push_value_array (GValueArray   *value_array,
		  const char    *property_name,
		  XmlStackFrame *parent)
{
  XmlStackFrame *frame;

  frame = xml_stack_frame_alloc ();
  frame->state = STATE_VALUE_ARRAY_BODY;
  frame->info.value_array_info.value_array = value_array;
  frame->info.value_array_info.property_name = property_name;
  frame->parent = parent;
  return frame;
}

static inline XmlStackFrame *
xml_stack_frame_pop (XmlStackFrame *frame)
{
  XmlStackFrame *parent = frame->parent;
  switch (frame->state)
    {
      case STATE_VALUE_START:
      case STATE_OBJECT_BODY:
      case STATE_PROPERTY_VALUE:
      case STATE_PROPERTY_END_TAG:
	g_value_unset (&frame->info.value_info.value);
	break;
      case STATE_VALUE_ARRAY_BODY:
      case STATE_VALUE_ARRAY_VALUE:
      case STATE_VALUE_ARRAY_END_TAG:
	g_value_array_free (frame->info.value_array_info.value_array);
	break;
      case STATE_START_TAG_OPEN:
      case STATE_START_TAG_NAME:
      case STATE_START_TAG_CLOSE:
      case STATE_END_TAG_OPEN:
      case STATE_END_TAG_NAME:
      case STATE_END_TAG_CLOSE:
	break;
      default:
	g_return_val_if_reached (NULL);
	break;
    }
  xml_stack_frame_free (frame);
  return parent;
}

static inline guint
simple_type_atom (const GValue *value,
		  char          atom[MAX_SIMPLE_TYPE_ATOM_LEN])
{
  switch (G_VALUE_TYPE (value))
    {
      case G_TYPE_CHAR:
	*atom = g_value_get_char (value);
	return 1;
      case G_TYPE_UCHAR:
	*atom = g_value_get_uchar (value);
	return 1;
      case G_TYPE_BOOLEAN:
	*atom = g_value_get_boolean (value) ? '1' : '0';
	return 1;

#define TYPE_CASE_PRINTF(T,t,fmt) \
      case G_TYPE_##T: \
	g_snprintf (atom, \
		    MAX_SIMPLE_TYPE_ATOM_LEN, \
		    fmt, \
		    g_value_get_##t (value)); \
	return (strlen (atom));

      TYPE_CASE_PRINTF (INT,     int,    "%d")
      TYPE_CASE_PRINTF (UINT,    uint,   "%u")
      TYPE_CASE_PRINTF (LONG,    long,   "%ld")
      TYPE_CASE_PRINTF (ULONG,   ulong,  "%lu")
      TYPE_CASE_PRINTF (INT64,   int64,  "%" G_GINT64_FORMAT)
      TYPE_CASE_PRINTF (UINT64,  uint64, "%" G_GUINT64_FORMAT)
      TYPE_CASE_PRINTF (FLOAT,   float,  "%.18e")
      TYPE_CASE_PRINTF (DOUBLE,  double, "%.18e")

      default:
        break;
    }
  g_return_val_if_reached (0);
  return 0;
}

static inline guint
string_atom (GValue      *value,
	     const char **atom_out,
	     gboolean    *free_atom)
{
  static const guint8 must_escape[256] = {
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,

    /*  SP !  "  #  $  %  &  '  (  )  *  +  ,  -  .  /  */
	1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,

    /*  0  1  2  3  4  5  6  7  8  9  :  ;  <  =  >  ?  */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0,

    /*  @  A  B  C  D  E  F  G  H  I  J  K  L  M  N  O  */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    /*  P  Q  R  S  T  U  V  W  X  Y  Z  [  \  ]  ^  _  */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    /*  `  a  b  c  d  e  f  g  h  i  j  k  l  m  n  o  */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    /*  p  q  r  s  t  u  v  w  x  y  z  {  |  }  ~  DEL */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,

	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
  };
  char *atom;
  guint8 *p;

  /* YHWH */
  atom = value->data[0].v_pointer;
/* XXX: no representation for NULL */
  g_return_val_if_fail (atom, 0);
  if (*atom == 0)
    {
      *atom_out = "<![CDATA[]]>";
      return 12;
    }
  for (p = atom; *p; ++p)
    {
      if (must_escape[*p])
	{
	  *atom_out = g_strdup_printf ("<![CDATA[%s]]>", atom);
	  *free_atom = TRUE;
	  /* XXX: should really use &#nnn; entities */
	  g_return_val_if_fail (strstr (atom, "]]>") == NULL, 0);
	  return strlen (*atom_out);
	}
    }
  if (!(value->data[1].v_uint & G_VALUE_NOCOPY_CONTENTS))
    *free_atom = TRUE;
  value->data[0].v_pointer = NULL;
  *atom_out = atom;
  return strlen (atom);
}

static inline guint
next_atom (GskXmlValueWriter  *self,
	   char                atom_buf[MAX_SIMPLE_TYPE_ATOM_LEN],
	   const char        **atom_out,
	   gboolean           *free_atom)
{
  XmlStackFrame *top = (XmlStackFrame *) self->stack;
  const char *atom = NULL;
  guint atom_len = 0;

  g_return_val_if_fail (atom_out, 0);
  g_return_val_if_fail (free_atom, 0);
  *free_atom = FALSE;

  do
    {
      if (top == NULL)
	return 0;
      switch (top->state)
	{
	  case STATE_VALUE_START:
	    {
	      GValue *value = &top->info.value_info.value;
	      GType value_type = G_VALUE_TYPE (value);
	      if (g_type_is_a (value_type, G_TYPE_OBJECT))
		{
		  GObject *object;
		  object = g_value_get_object (value);
		  g_return_val_if_fail (object && G_IS_OBJECT (object), 0);
		  top->state = STATE_OBJECT_BODY;
		  top = push_start_tag (g_type_name (G_OBJECT_TYPE (object)),
					top);
		}
	      else if (value_type == G_TYPE_STRING)
		{
		  atom_len = string_atom (value, &atom, free_atom);
		  if (atom_len == 0)
		    {
		      if (atom && free_atom)
			g_free ((char *) atom);
		      atom = NULL;
		    }
		  top = xml_stack_frame_pop (top);
		}
	      else
		{
		  atom = atom_buf;
		  atom_len = simple_type_atom (value, atom_buf);
		  top = xml_stack_frame_pop (top);
		}
	    }
	    break;
	  case STATE_OBJECT_BODY:
	    {
	      GValue *value = &top->info.value_info.value;
	      GPtrArray *properties = top->info.value_info.properties;
	      guint index = top->info.value_info.property_index;
	      GObject *object;

	      object = g_value_get_object (value);
	      g_return_val_if_fail (object && G_IS_OBJECT (object), 0);
	      if (properties == NULL)
		{
		  g_return_val_if_fail (index == 0, 0);
		  properties = top->info.value_info.properties =
		    get_serialized_properties (G_OBJECT_GET_CLASS (object));
		  g_return_val_if_fail (properties, 0);
		}
	      /* Loop until we get a non-default property or last property. */
	      for (;;)
		{
		  if (index >= properties->len)
		    {
		      const char *type_name;
		      /* We assume the type name doesn't go away
		       * when the object is unreferenced!
		       */
		      type_name = g_type_name (G_OBJECT_TYPE (object));
		      top = xml_stack_frame_pop (top);
		      top = push_end_tag (type_name, top);
		      break;
		    }
		  else
		    {
		      GParamSpec *pspec = g_ptr_array_index (properties, index);
		      GValue value = { 0, { { 0 }, { 0 } } };
		      g_value_init (&value, pspec->value_type);
		      g_object_get_property (object, pspec->name, &value);
		      if (G_VALUE_TYPE (&value) == G_TYPE_VALUE_ARRAY)
			{
			  GValueArray *value_array;
			  value_array = g_value_get_boxed (&value);
			  if (value_array)
			    {
			      top->state = STATE_OBJECT_BODY;
			      top->info.value_info.property_index = index + 1;
			      top = push_value_array (value_array,
						      pspec->name,
						      top);
			      /* Don't unset value; we steal value_array. */
			      break;
			    }
			}
		      else if (!g_param_value_defaults (pspec, &value))
			{
			  /* TODO: could push an extra value frame now? */
			  top->state = STATE_PROPERTY_VALUE;
			  top->info.value_info.property_index = index;
			  top = push_start_tag (pspec->name, top);
			  g_value_unset (&value);
			  break;
			}
		      g_value_unset (&value);
		      ++index;
		    }
		}
	    }
	    break;
	  case STATE_PROPERTY_VALUE:
	    {
	      GPtrArray *properties = top->info.value_info.properties;
	      guint index = top->info.value_info.property_index;
	      GParamSpec *pspec;
	      GObject *object;

	      g_return_val_if_fail (properties, 0);
	      g_return_val_if_fail (index < properties->len, 0);
	      pspec = g_ptr_array_index (properties, index);
	      object = g_value_get_object (&top->info.value_info.value);
	      g_return_val_if_fail (object && G_IS_OBJECT (object), 0);

	      top->state = STATE_PROPERTY_END_TAG;
	      top = push_value_property (object, pspec, top);
	    }
	    break;
	  case STATE_PROPERTY_END_TAG:
	    {
	      GPtrArray *properties = top->info.value_info.properties;
	      guint index = top->info.value_info.property_index;
	      GParamSpec *pspec;

	      g_return_val_if_fail (properties, 0);
	      g_return_val_if_fail (index < properties->len, 0);
	      pspec = g_ptr_array_index (properties, index);

	      top->state = STATE_OBJECT_BODY;
	      ++top->info.value_info.property_index;
	      top = push_end_tag (pspec->name, top);
	    }
	    break;
	  case STATE_VALUE_ARRAY_BODY:
	    {
	      GValueArray *value_array = top->info.value_array_info.value_array;
	      guint index = top->info.value_array_info.element_index;

	      if (index >= value_array->n_values)
		top = xml_stack_frame_pop (top);
	      else
		{
		  top->state = STATE_VALUE_ARRAY_VALUE;
		  top =
		    push_start_tag (top->info.value_array_info.property_name,
				    top);
		}
	    }
	    break;
	  case STATE_VALUE_ARRAY_VALUE:
	    {
	      GValueArray *value_array = top->info.value_array_info.value_array;
	      guint index = top->info.value_array_info.element_index;
	      GValue *element;

	      g_return_val_if_fail (index < value_array->n_values, 0);
	      element = g_value_array_get_nth (value_array, index);
	      g_return_val_if_fail (element, 0);
	      top->state = STATE_VALUE_ARRAY_END_TAG;
	      top = push_value (element, top);
	    }
	    break;
	  case STATE_VALUE_ARRAY_END_TAG:
	    {
	      const char *property_name =
		top->info.value_array_info.property_name;
	      g_return_val_if_fail (property_name, 0);
	      top = xml_stack_frame_pop (top);
	      top = push_end_tag (property_name, top);
	    }
	    break;
	  case STATE_START_TAG_OPEN:
	    {
	      atom = "<";
	      atom_len = 1;
	      top->state = STATE_START_TAG_NAME;
	    }
	    break;
	  case STATE_START_TAG_NAME:
	    {
	      atom = top->info.tag_info.tag_name;
	      atom_len = strlen (atom);
	      top->state = STATE_START_TAG_CLOSE;
	    }
	    break;
	  case STATE_END_TAG_OPEN:
	    {
	      atom = "</";
	      atom_len = 2;
	      top->state = STATE_END_TAG_NAME;
	    }
	    break;
	  case STATE_END_TAG_NAME:
	    {
	      atom = top->info.tag_info.tag_name;
	      atom_len = strlen (atom);
	      top->state = STATE_END_TAG_CLOSE;
	    }
	    break;
	  case STATE_START_TAG_CLOSE:
	  case STATE_END_TAG_CLOSE:
	    {
	      atom = ">";
	      atom_len = 1;
	      top = xml_stack_frame_pop (top);
	    }
	    break;
	}
      }
  while (atom == NULL);

  self->stack = top;
  *atom_out = atom;
  return atom_len;
}

static guint
gsk_xml_value_writer_raw_read (GskStream  *stream,
			       gpointer    data,
			       guint       length,
			       GError    **error)
{
  char atom_buf[MAX_SIMPLE_TYPE_ATOM_LEN];
  GskXmlValueWriter *self = GSK_XML_VALUE_WRITER (stream);
  char *out = data;
  guint num_written = 0;

  (void) error;

  if (length == 0)
    return 0;

  /* First write out anything already buffered. */
  if (self->buf.size > 0)
    {
      num_written = gsk_buffer_read (&self->buf, out, length);
      out += num_written;
      length -= num_written;
    }

  /* Then run the state machine until we fill up length. */
  while (length > 0)
    {
      const char *atom;
      gboolean free_atom;
      guint atom_len;

      atom_len = next_atom (self, atom_buf, &atom, &free_atom);
      if (atom_len == 0)
	{
	  gsk_io_notify_read_shutdown (stream);
	  break;
	}
      if (atom_len < length)
	{
	  strncpy (out, atom, atom_len);
	  out += atom_len;
	  num_written += atom_len;
	  length -= atom_len;
	}
      else
	{
	  strncpy (out, atom, length);
	  num_written += length;
	  gsk_buffer_append (&self->buf, atom + length, atom_len - length);
	  length = 0;
	}
      if (free_atom)
	g_free ((char *) atom);
    }
  return num_written;
}

static void
gsk_xml_value_writer_finalize (GObject *object)
{
  GskXmlValueWriter *self = GSK_XML_VALUE_WRITER (object);

  while (self->stack)
    self->stack = xml_stack_frame_pop ((XmlStackFrame *) self->stack);

  (*parent_class->finalize) (object);
}

static void
gsk_xml_value_writer_init (GskXmlValueWriter *xml_value_writer)
{
  gsk_stream_mark_is_readable (xml_value_writer);
  gsk_stream_mark_never_blocks_read (xml_value_writer);
}

static void
gsk_xml_value_writer_class_init (GskStreamClass *stream_class)
{
  parent_class = g_type_class_peek_parent (stream_class);
  G_OBJECT_CLASS (stream_class)->finalize = gsk_xml_value_writer_finalize;
  stream_class->raw_read = gsk_xml_value_writer_raw_read;
}

GType
gsk_xml_value_writer_get_type (void)
{
  static GType type = 0;
  if (G_UNLIKELY (type == 0))
    {
      static const GTypeInfo type_info =
	{
	  sizeof(GskXmlValueWriterClass),
	  (GBaseInitFunc) NULL,
	  (GBaseFinalizeFunc) NULL,
	  (GClassInitFunc) gsk_xml_value_writer_class_init,
	  NULL,		/* class_finalize */
	  NULL,		/* class_data */
	  sizeof (GskXmlValueWriter),
	  0,		/* n_preallocs */
	  (GInstanceInitFunc) gsk_xml_value_writer_init,
	  NULL		/* value_table */
	};
      type = g_type_register_static (GSK_TYPE_STREAM,
				     "GskXmlValueWriter",
				     &type_info,
				     0);
    }
  return type;
}

GskXmlValueWriter *
gsk_xml_value_writer_new (const GValue *value)
{
  GskXmlValueWriter *stream;

  stream = g_object_new (GSK_TYPE_XML_VALUE_WRITER, NULL);
  stream->stack = push_value (value, NULL);
  return stream;
}
