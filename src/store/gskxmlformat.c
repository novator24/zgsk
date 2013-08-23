#include "gskxmlformat.h"
#include "gskxmlvaluereader.h"
#include "gskxmlvaluewriter.h"

#define ATOMIC_READ_SIZE 4096

/*
 *
 * GskXmlValueRequest
 *
 */

typedef GskValueRequestClass       GskXmlValueRequestClass;
typedef struct _GskXmlValueRequest GskXmlValueRequest;

GType gsk_xml_value_request_get_type (void) G_GNUC_CONST;

#define GSK_TYPE_XML_VALUE_REQUEST (gsk_xml_value_request_get_type ())
#define GSK_XML_VALUE_REQUEST(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
			       GSK_TYPE_XML_VALUE_REQUEST, \
			       GskXmlValueRequest))
#define GSK_IS_XML_VALUE_REQUEST(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_XML_VALUE_REQUEST))

struct _GskXmlValueRequest
{
  GskValueRequest value_request;

  GskStream *stream;
  GskXmlValueReader *xml_value_reader;
};

static GObjectClass *gsk_xml_value_request_parent_class = NULL;

/* GskXmlValueFunc */
static void
handle_value (const GValue *value, gpointer user_data)
{
  GskXmlValueRequest *request = GSK_XML_VALUE_REQUEST (user_data);
  GskStream *stream = request->stream;
  GError *error = NULL;

  g_return_if_fail (value);
  g_return_if_fail (stream);
  g_return_if_fail (G_VALUE_TYPE (value));

  g_value_init (&request->value_request.value, G_VALUE_TYPE (value));
  g_value_copy (value, &request->value_request.value);
  if (!gsk_io_read_shutdown (GSK_IO (stream), &error))
    {
      if (error)
	gsk_request_set_error (request, error);
    }
  gsk_request_done (request);
}

static gboolean
handle_stream_is_readable (GskIO *io, gpointer user_data)
{
  GskXmlValueRequest *request = GSK_XML_VALUE_REQUEST (user_data);
  GskStream *stream = request->stream;
  GError *error = NULL;
  char buf[ATOMIC_READ_SIZE];
  guint num_read;

  g_return_val_if_fail (stream == GSK_STREAM (io), FALSE);

  num_read = gsk_stream_read (stream, buf, ATOMIC_READ_SIZE, &error);
  if (error)
    goto ERROR;
  if (num_read == 0)
    return gsk_stream_get_is_readable (stream);

  if (!gsk_xml_value_reader_input (request->xml_value_reader,
				   buf,
				   num_read,
				   &error))
    goto ERROR;

  return TRUE;

ERROR:
  gsk_request_set_error (request, error);
  gsk_request_done (request);
  gsk_io_read_shutdown (GSK_IO (stream), NULL);
  return FALSE;
}

static gboolean
handle_stream_shutdown_read (GskIO *io, gpointer user_data)
{
  GskXmlValueRequest *request = GSK_XML_VALUE_REQUEST (user_data);
  GskStream *stream = request->stream;

  g_return_val_if_fail (stream == GSK_STREAM (io), FALSE);

  /* If we're not done or cancelled, a shutdown is an error as far as
   * whoever made the original request is concerned.
   */
  if (!gsk_request_get_is_done (request) &&
      !gsk_request_get_is_cancelled (request))
    {
      GError *error;
      g_return_val_if_fail (gsk_request_get_error (request) == NULL, FALSE);
      g_return_val_if_fail
	(G_VALUE_TYPE (&request->value_request.value) == G_TYPE_INVALID, FALSE);
      error = g_error_new (GSK_G_ERROR_DOMAIN,
			   0, /* TODO */
			   "premature shutdown of input XML stream");
      gsk_request_set_error (request, error);
    }
  return FALSE;
}

static void
handle_stream_is_readable_destroy (gpointer user_data)
{
  GskXmlValueRequest *request = GSK_XML_VALUE_REQUEST (user_data);
  GskStream *stream = request->stream;

  g_return_if_fail (stream);
  g_object_unref (stream);
  request->stream = NULL;

  /* Get rid of reference we added for gsk_io_trap_readable. */
  g_object_unref (request);
}

/* GskRequest methods. */

static void
gsk_xml_value_request_cancelled (GskRequest *request_parent)
{
  GskXmlValueRequest *request = GSK_XML_VALUE_REQUEST (request_parent);
  GskStream *stream = request->stream;

  g_return_if_fail (stream);
  gsk_io_read_shutdown (GSK_IO (stream), NULL);
  gsk_request_mark_is_cancelled (request);
}

static void
gsk_xml_value_request_start (GskRequest *request_parent)
{
  GskXmlValueRequest *request = GSK_XML_VALUE_REQUEST (request_parent);
  GskStream *stream = request->stream;

  g_return_if_fail (!gsk_request_get_is_running (request));
  g_return_if_fail (!gsk_request_get_is_cancelled (request));
  g_return_if_fail (!gsk_request_get_is_done (request));
  g_return_if_fail (request->xml_value_reader);
  g_return_if_fail (stream);

  /* This reference is on behalf of the stream's read hook. */
  g_object_ref (request);
  gsk_io_trap_readable (GSK_IO (stream),
			handle_stream_is_readable,
			handle_stream_shutdown_read,
			request,
			handle_stream_is_readable_destroy);
  gsk_request_mark_is_running (request);
}

/* GObject methods. */

static void
gsk_xml_value_request_finalize (GObject *object)
{
  GskXmlValueRequest *request = GSK_XML_VALUE_REQUEST (object);

  if (request->stream)
    g_object_unref (request->stream);
  if (request->xml_value_reader)
    gsk_xml_value_reader_free (request->xml_value_reader);

  (*gsk_xml_value_request_parent_class->finalize) (object);
}

static void
gsk_xml_value_request_class_init (GskRequestClass *request_class)
{
  gsk_xml_value_request_parent_class =
    g_type_class_peek_parent (request_class);
  G_OBJECT_CLASS (request_class)->finalize = gsk_xml_value_request_finalize;
  request_class->start = gsk_xml_value_request_start;
  request_class->cancelled = gsk_xml_value_request_cancelled;
}

GType
gsk_xml_value_request_get_type (void)
{
  static GType type = 0;
  if (G_UNLIKELY (type == 0))
    {
      static const GTypeInfo type_info =
	{
	  sizeof (GskXmlValueRequestClass),
	  (GBaseInitFunc) NULL,
	  (GBaseFinalizeFunc) NULL,
	  (GClassInitFunc) gsk_xml_value_request_class_init,
	  NULL,		/* class_finalize */
	  NULL,		/* class_data */
	  sizeof (GskXmlValueRequest),
	  0,		/* n_preallocs */
	  (GInstanceInitFunc) NULL,
	  NULL		/* value_table */
	};
      type = g_type_register_static (GSK_TYPE_VALUE_REQUEST,
				     "GskXmlValueRequest",
				     &type_info,
				     0);
    }
  return type;
}

/*
 *
 * GskXmlFormat
 *
 */

static GObjectClass *gsk_xml_format_parent_class = NULL;

/*
 * GskStorageFormat methods.
 */

static GskValueRequest *
gsk_xml_format_deserialize (GskStorageFormat  *storage_format,
			    GskStream         *stream,
			    GType              value_type,
			    GError           **error)
{
  GskXmlFormat *xml_format = GSK_XML_FORMAT (storage_format);
  GskGtypeLoader *type_loader = xml_format->type_loader;
  GskXmlValueRequest *request;

  (void) error;

  if (type_loader == NULL)
    type_loader = gsk_gtype_loader_default ();
  else
    gsk_gtype_loader_ref (type_loader);

  request = g_object_new (GSK_TYPE_XML_VALUE_REQUEST, NULL);
  g_return_val_if_fail (request, NULL);
  gsk_request_mark_is_cancellable (request);

  request->stream = stream;
  g_object_ref (stream);

  /* The GskXmlValueReader can only call us back when we feed data to
   * it, so no request reference needed here.
   */
  request->xml_value_reader =
    gsk_xml_value_reader_new (type_loader,
			      value_type,
			      handle_value,
			      request,
			      NULL);		/* value_func_destroy */
  gsk_gtype_loader_unref (type_loader);

  return GSK_VALUE_REQUEST (request);
}

static GskStream *
gsk_xml_format_serialize (GskStorageFormat  *format,
			  const GValue      *value,
			  GError           **error)
{
  (void) format;
  (void) error;
  return GSK_STREAM (gsk_xml_value_writer_new (value));
}

/*
 * GObject methods.
 */

static void
gsk_xml_format_finalize (GObject *object)
{
  GskXmlFormat *self = GSK_XML_FORMAT (object);

  if (self->type_loader)
    gsk_gtype_loader_unref (self->type_loader);

  (*gsk_xml_format_parent_class->finalize) (object);
}

static void
gsk_xml_format_class_init (GObjectClass *object_class)
{
  gsk_xml_format_parent_class = g_type_class_peek_parent (object_class);
  object_class->finalize = gsk_xml_format_finalize;
}

static void
gsk_xml_format_interface_init (GskStorageFormatIface *iface, gpointer unused)
{
  (void) unused;
  iface->serialize = gsk_xml_format_serialize;
  iface->deserialize = gsk_xml_format_deserialize;
}

GType
gsk_xml_format_get_type (void)
{
  static GType type = 0;
  if (G_UNLIKELY (type == 0))
    {
      static const GInterfaceInfo storage_format_info =
	{
	  (GInterfaceInitFunc) gsk_xml_format_interface_init,
	  NULL,			/* interface_finalize */
	  NULL			/* interface_data */
	};
      static const GTypeInfo info =
	{
	  sizeof (GskXmlFormatClass),
	  (GBaseInitFunc) NULL,
	  (GBaseFinalizeFunc) NULL,
	  (GClassInitFunc) gsk_xml_format_class_init,
	  NULL,		/* class_finalize */
	  NULL,		/* class_data */
	  sizeof (GskXmlFormat),
	  0,		/* n_preallocs */
	  (GInstanceInitFunc) NULL,
	  NULL		/* value_table */
	};
      type = g_type_register_static (G_TYPE_OBJECT,
				     "GskXmlFormat",
				     &info,
				     0);
      g_type_add_interface_static (type,
				   GSK_TYPE_STORAGE_FORMAT,
				   &storage_format_info);
    }
  return type;
}
