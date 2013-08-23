#include "gskstreamlistenerssl.h"
#include "../gskmacros.h"

static GObjectClass *parent_class = NULL;

/* --- GObject methods --- */
enum
{
  PROP_0,

  /* Construct-only */
  PROP_UNDERLYING_LISTENER,
  PROP_CERT_FILE,
  PROP_KEY_FILE,
  PROP_PASSWORD
};

static gboolean
handle_underlying_accept (GskStream    *stream,
			  gpointer      data,
			  GError      **error)
{
  GskStreamListenerSsl *listener_ssl = GSK_STREAM_LISTENER_SSL (data);
  GskStream *ssl = gsk_stream_ssl_new_server (listener_ssl->cert_file,
					      listener_ssl->key_file,
					      listener_ssl->password,
					      stream, error);
  if (ssl == NULL)
    {
      gsk_stream_listener_notify_error (GSK_STREAM_LISTENER (listener_ssl),
					*error);
      return TRUE;		/* ??? */
    }

  gsk_stream_listener_notify_accepted (GSK_STREAM_LISTENER (listener_ssl), ssl);
  g_object_unref (ssl);
  return TRUE;
}

static void
handle_underlying_error  (GError       *err,
			  gpointer      data)
{
  GskStreamListenerSsl *listener_ssl = GSK_STREAM_LISTENER_SSL (data);
  gsk_stream_listener_notify_error (GSK_STREAM_LISTENER (listener_ssl),
				    g_error_copy (err));
}

static void
handle_underlying_trap_destroyed (gpointer data)
{
  GskStreamListenerSsl *listener_ssl = GSK_STREAM_LISTENER_SSL (data);
  GskStreamListener *underlying = listener_ssl->underlying;
  listener_ssl->underlying = NULL;
  if (underlying)
    g_object_unref (underlying);
  g_object_unref (listener_ssl);
}

static void
gsk_stream_listener_ssl_set_property (GObject         *object,
				      guint            property_id,
				      const GValue    *value,
				      GParamSpec      *pspec)
{
  GskStreamListenerSsl *listener_ssl = GSK_STREAM_LISTENER_SSL (object);
  switch (property_id)
    {
    case PROP_UNDERLYING_LISTENER:
      g_return_if_fail (listener_ssl->underlying == NULL);
#if 0
      if (listener_ssl->underlying != NULL)
	{
	  listener_ssl->underlying = gsk_hook_untrap (listener_ssl);
	  g_assert (listener_ssl->underlying == NULL);
	}
#endif
      listener_ssl->underlying = GSK_STREAM_LISTENER (g_value_dup_object (value));
      gsk_stream_listener_handle_accept (listener_ssl->underlying,
		                         handle_underlying_accept,
		                         handle_underlying_error,
		                         g_object_ref (listener_ssl),
		                         handle_underlying_trap_destroyed);
      break;
    case PROP_CERT_FILE:
      g_return_if_fail (listener_ssl->cert_file == NULL);
      listener_ssl->cert_file = g_strdup (g_value_get_string (value));
      break;
    case PROP_KEY_FILE:
      g_return_if_fail (listener_ssl->key_file == NULL);
      listener_ssl->key_file = g_strdup (g_value_get_string (value));
      break;
    case PROP_PASSWORD:
      g_return_if_fail (listener_ssl->password == NULL);
      listener_ssl->password = g_strdup (g_value_get_string (value));
      break;
    }
}

static void
gsk_stream_listener_ssl_get_property (GObject         *object,
				      guint            property_id,
				      GValue          *value,
				      GParamSpec      *pspec)
{
  GskStreamListenerSsl *listener_ssl = GSK_STREAM_LISTENER_SSL (object);
  switch (property_id)
    {
    case PROP_UNDERLYING_LISTENER:
      g_value_set_object (value, listener_ssl->underlying);
      break;
    case PROP_CERT_FILE:
      g_value_set_string (value, listener_ssl->cert_file);
      break;
    case PROP_KEY_FILE:
      g_value_set_string (value, listener_ssl->key_file);
      break;
    }
}

static void
gsk_stream_listener_ssl_finalize (GObject        *object)
{
  GskStreamListenerSsl *listener_ssl = GSK_STREAM_LISTENER_SSL (object);
  g_assert (listener_ssl->underlying == NULL);
  g_assert (listener_ssl->key_file);
  g_assert (listener_ssl->cert_file);
  g_assert (listener_ssl->password);
  (*parent_class->finalize) (object);
}

/* --- functions --- */
static void
gsk_stream_listener_ssl_init (GskStreamListenerSsl *stream_listener_ssl)
{
}

static void
gsk_stream_listener_ssl_class_init (GskStreamListenerSslClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GParamSpec *pspec;
  parent_class = g_type_class_peek_parent (class);
  object_class->finalize = gsk_stream_listener_ssl_finalize;
  object_class->set_property = gsk_stream_listener_ssl_set_property;
  object_class->get_property = gsk_stream_listener_ssl_get_property;

  pspec = g_param_spec_object ("underlying",
			       _("Underlying Listener"),
			       _("the unencrypted underlying transport"),
			       GSK_TYPE_STREAM_LISTENER,
			       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_UNDERLYING_LISTENER, pspec);

  pspec = g_param_spec_string ("cert-file",
			       _("Certificate File"),
			       _("the x509 certificate file"),
			       NULL,
			       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_CERT_FILE, pspec);

  pspec = g_param_spec_string ("key-file",
			       _("Key File"),
			       _("the x509 key file"),
			       NULL,
			       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_KEY_FILE, pspec);

  pspec = g_param_spec_string ("password",
			       _("Password"),
			       _("secret passphrase for the certificate"),
			       NULL,
			       G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_PASSWORD, pspec);
}

GType gsk_stream_listener_ssl_get_type()
{
  static GType stream_listener_ssl_type = 0;
  if (!stream_listener_ssl_type)
    {
      static const GTypeInfo stream_listener_ssl_info =
      {
	sizeof(GskStreamListenerSslClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_stream_listener_ssl_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskStreamListenerSsl),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_stream_listener_ssl_init,
	NULL		/* value_table */
      };
      stream_listener_ssl_type = g_type_register_static (GSK_TYPE_STREAM_LISTENER,
                                                  "GskStreamListenerSsl",
						  &stream_listener_ssl_info, 0);
    }
  return stream_listener_ssl_type;
}

/**
 * gsk_stream_listener_ssl_new:
 * @underlying: the unencrypted raw stream-listener.
 * @cert_file: the certificate filename.
 * @key_file: the key filename.
 *
 * Create a SSL server-listener based on an unencrypted raw transport.
 *
 * returns: the new SSL-encrypted server.
 */
GskStreamListener *
gsk_stream_listener_ssl_new (GskStreamListener *underlying,
			     const char        *cert_file,
			     const char        *key_file)
{
  g_return_val_if_fail (underlying != NULL, NULL);
  g_return_val_if_fail (cert_file != NULL, NULL);
  g_return_val_if_fail (key_file != NULL, NULL);
  return g_object_new (GSK_TYPE_STREAM_LISTENER_SSL,
		       "underlying", underlying,
		       "cert-file", cert_file,
		       "key-file", key_file,
		       NULL);
}
