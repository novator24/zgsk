/*
   SSL Stream (used by Client and Server).

   To understand how to program SSL,
   I strongly recommend the O'Reilly book:

   Network Security with OpenSSL.
   John Viega, Matt Messier and Pravir Chandra.

   The book will be referenced as [OREILLY].
   Page numbers refer to the 1st Edition, published 2002. */



#include "gskstreamssl.h"
#include "gskopensslbiostream.h"
#include <openssl/rand.h>
#include <openssl/err.h>
#include "../gskbufferstream.h"
#include "../gskstreamconnection.h"
#include "../gskdebug.h"
#include "../debug.h"
#include "../gskmacros.h"
#include <string.h>

static GObjectClass *parent_class = NULL;

enum
{
  PROP_0,
  PROP_KEY_FILE, //???

  PROP_PASSWORD,

  /* only for clients. */
  PROP_CA_FILE,
  PROP_CA_DIR,

  /* for clients and servers. */
  PROP_CERT_FILE,
  PROP_IS_CLIENT
};

#define DEBUG(ssl, args)				\
  G_STMT_START{						\
    if (GSK_IS_DEBUGGING(SSL))				\
      {							\
	g_message args;					\
      }							\
  }G_STMT_END

/* --- stream and io methods --- */
static void
handle_transport_error (GskStream *transport,
                        GskStreamSsl *ssl)
{
  gsk_io_set_error (GSK_IO (ssl),
                    GSK_IO (transport)->error_cause,
                    GSK_IO (transport)->error->code,
                    "SSL: transport layer had error: %s",
                    GSK_IO (transport)->error->message);
}

static void
set_backend_flags_raw (GskStreamSsl *ssl,
		       gboolean want_read,
		       gboolean want_write)
{
  if (want_read && !ssl->backend_poll_read)
    {
      ssl->backend_poll_read = TRUE;
      if (ssl->backend)
	gsk_hook_unblock (gsk_buffer_stream_read_hook (ssl->backend));
    }
  else if (!want_read && ssl->backend_poll_read)
    {
      ssl->backend_poll_read = FALSE;
      if (ssl->backend)
	gsk_hook_block (gsk_buffer_stream_read_hook (ssl->backend));
    }

  if (want_write && !ssl->backend_poll_write)
    {
      ssl->backend_poll_write = TRUE;
      if (ssl->backend)
	gsk_hook_unblock (gsk_buffer_stream_write_hook (ssl->backend));
    }
  else if (!want_write && ssl->backend_poll_write)
    {
      ssl->backend_poll_write = FALSE;
      if (ssl->backend)
	gsk_hook_block (gsk_buffer_stream_write_hook (ssl->backend));
    }
}

static void
set_backend_flags_raw_to_underlying (GskStreamSsl *ssl)
{
  set_backend_flags_raw (ssl, ssl->this_readable, ssl->this_writable);
}

static gboolean
do_handshake (GskStreamSsl *stream_ssl, SSL* ssl, GError **error)
{
  int rv;
  DEBUG (stream_ssl, ("do_handshake[client=%u]: start", stream_ssl->is_client));
  rv = SSL_do_handshake (ssl);
  if (rv <= 0)
    {
      int error_code = SSL_get_error (ssl, rv);
      gulong l = ERR_peek_error();
      switch (error_code)
	{
	case SSL_ERROR_NONE:
	  stream_ssl->doing_handshake = 0;
	  set_backend_flags_raw_to_underlying (stream_ssl);
	  //g_message ("DONE HANDSHAKE (is-client=%u)", stream_ssl->is_client);
	  break;
	case SSL_ERROR_SYSCALL:
	case SSL_ERROR_WANT_READ:
	  set_backend_flags_raw (stream_ssl, FALSE, TRUE);
	  //g_message("do-handshake:want-read");
	  break;
	case SSL_ERROR_WANT_WRITE:
	  //g_message("do-handshake:want-write");
	  set_backend_flags_raw (stream_ssl, TRUE, FALSE);
	  break;
	default:
	  {
	    g_set_error (error,
			 GSK_G_ERROR_DOMAIN,
			 GSK_ERROR_BAD_FORMAT,
			 _("error doing-handshake on SSL socket: %s: %s [code=%08lx (%lu)] [rv=%d]"),
			 ERR_func_error_string(l),
			 ERR_reason_error_string(l),
			 l, l, error_code);
	    return FALSE;
	  }
	}
    }
  else
    {
      stream_ssl->doing_handshake = 0;
      set_backend_flags_raw_to_underlying (stream_ssl);
    }
  return TRUE;
}

static inline void
maybe_update_backend_poll_state (GskStreamSsl *ssl)
{
  gboolean backend_needs_write = ssl->this_readable;
  gboolean backend_needs_read = ssl->this_writable;

  if (ssl->read_needed_to_write)
    backend_needs_read = FALSE;
  else if (ssl->write_needed_to_read)
    backend_needs_write = FALSE;

  DEBUG (ssl, ("maybe_update_backend_poll_state: backend-needs: write=%d, read=%d",
	  backend_needs_write, backend_needs_read));


  set_backend_flags_raw (ssl, backend_needs_read, backend_needs_write);
}

static void
gsk_stream_ssl_set_poll_read  (GskIO      *io,
			       gboolean    do_poll)
{
  GskStreamSsl *ssl = GSK_STREAM_SSL (io);
  ssl->this_readable = do_poll ? 1 : 0;
  DEBUG (ssl, ("gsk_stream_ssl_set_poll_read: is_client=%d, do_poll=%d",
	   ssl->is_client, do_poll));
  maybe_update_backend_poll_state (ssl);
}

static void
gsk_stream_ssl_set_poll_write (GskIO      *io,
			       gboolean    do_poll)
{
  GskStreamSsl *ssl = GSK_STREAM_SSL (io);
  ssl->this_writable = do_poll ? 1 : 0;
  DEBUG (ssl, ("gsk_stream_ssl_set_poll_write: is_client=%d, do_poll=%d",
	  ssl->is_client, do_poll));
  maybe_update_backend_poll_state (ssl);
}

static gboolean
gsk_stream_ssl_shutdown_both (GskStreamSsl *ssl,
			      GError  **error)
{
  int rv;
  if (ssl->ssl == NULL)
    {
      gsk_io_notify_shutdown (GSK_IO (ssl));
      if (ssl->backend)
        gsk_io_shutdown (GSK_IO (ssl->backend), NULL);
      return TRUE;
    }
  rv = SSL_shutdown (ssl->ssl);
  if (rv == 0)
    {
      //g_message ("enterring shutting-down state [is-client=%u]", ssl->is_client);
      ssl->state = GSK_STREAM_SSL_STATE_SHUTTING_DOWN;
      gsk_io_write_shutdown (ssl->backend, NULL);
      gsk_buffer_stream_read_shutdown (GSK_BUFFER_STREAM (ssl->backend));
      gsk_io_notify_shutdown (GSK_IO (ssl));
      return TRUE;
    }
  else if (rv == 1)
    {
      DEBUG (ssl, ("shutdown! [is_client=%u]",ssl->is_client));
      ssl->state = GSK_STREAM_SSL_STATE_SHUT_DOWN;

      gsk_io_write_shutdown (ssl->backend, NULL);
      gsk_buffer_stream_read_shutdown (GSK_BUFFER_STREAM (ssl->backend));
      gsk_io_notify_shutdown (GSK_IO (ssl));

      return TRUE;
    }
  else
    {
      /* error handling */
      int error_code = SSL_get_error (ssl->ssl, rv);
      switch (error_code)
	{
	case SSL_ERROR_WANT_READ:
	  set_backend_flags_raw (ssl, FALSE, TRUE);
	  break;
	case SSL_ERROR_WANT_WRITE:
	  set_backend_flags_raw (ssl, TRUE, FALSE);
	  break;
	case SSL_ERROR_NONE:
	  break;
	default:
	  g_set_error (error,
		       GSK_G_ERROR_DOMAIN,
		       GSK_ERROR_IO,
		       "ssl error shutting down: code %d",
		       error_code);
	  //g_message("error with SSL_shutdown down: %s", (*error)->message);
	  return FALSE;
	}
      ssl->state = GSK_STREAM_SSL_STATE_SHUTTING_DOWN;
      return FALSE;
    }
}

static gboolean
gsk_stream_ssl_shutdown_read  (GskIO      *io,
			       GError    **error)
{
  GskStreamSsl *ssl = GSK_STREAM_SSL (io);
  if (!gsk_io_get_is_writable (io)
    || gsk_io_get_is_write_shutting_down (io))
    {
      //g_message("shutdown-both from read");
      return gsk_stream_ssl_shutdown_both (ssl, error);
    }

  /* NOTE: ssl does not support half-shutdowns... */
  return FALSE;
}

static gboolean
gsk_stream_ssl_shutdown_write (GskIO      *io,
			       GError    **error)
{
  GskStreamSsl *ssl = GSK_STREAM_SSL (io);
  if (!gsk_io_get_is_readable (io)
    || gsk_io_get_is_read_shutting_down (io)
    || ssl->got_remote_shutdown)
    {
      //g_message("shutdown-both from write");
      gsk_stream_ssl_shutdown_both (ssl, error);
      return TRUE;
    }

  /* NOTE: ssl does not support half-shutdowns... */
  return FALSE;
}

static guint
gsk_stream_ssl_raw_read       (GskStream     *stream,
			       gpointer       data,
			       guint          length,
			       GError       **error)
{
  GskStreamSsl *ssl = GSK_STREAM_SSL (stream);
  guint read_length;
  int ssl_read_rv;
  if (length == 0)
    return 0;
  if (ssl->doing_handshake)
    return 0;

process_has_read_buffer:
  if (ssl->read_buffer_length > 0)
    {
      guint rv = MIN (ssl->read_buffer_length, length);
      g_assert (rv > 0);
      memcpy (data, ssl->read_buffer, rv);
      ssl->read_buffer_length -= rv;
      if (ssl->read_buffer_length > 0)
	g_memmove (ssl->read_buffer, ssl->read_buffer + rv, ssl->read_buffer_length);
      return rv;
    }

  /* Make sure our buffer is long enough to accomodate the
     amount of data requested.  */
  if (!ssl->reread_length && ssl->read_buffer_alloc < length)
    {
      if (ssl->read_buffer_alloc == 0)
	ssl->read_buffer_alloc = 4096;
      while (ssl->read_buffer_alloc < length)
	ssl->read_buffer_alloc *= 2;
      ssl->read_buffer = g_realloc (ssl->read_buffer, ssl->read_buffer_alloc);
    }

  /* Do underlying read. */
  read_length = ssl->reread_length ? ssl->reread_length : length;
  ssl_read_rv = SSL_read (ssl->ssl, ssl->read_buffer, read_length);
  if (ssl_read_rv > 0)
    {
      ssl->reread_length = 0;
      ssl->read_buffer_length = ssl_read_rv;
      goto process_has_read_buffer;
    }
  if (ssl_read_rv == 0)
    {
      if (!gsk_io_get_is_readable (ssl->backend)
       || !gsk_io_get_is_writable (ssl->backend))
        {
          /* Either a clean shutdown occurred, or an error occurred in the
             underlying transport. */
          ssl->got_remote_shutdown = 1;
          gsk_io_notify_read_shutdown (ssl);
          gsk_stream_ssl_shutdown_write (GSK_IO(ssl), error);

          /* XXX: should call SSL_get_error(). */
#if 0
          g_message("ssl_read_rv returned %d: SSL_get_error returns %d",
                    ssl_read_rv, SSL_get_error (ssl->ssl, ssl_read_rv));
#endif
        }
    }
  else if (ssl_read_rv < 0)
    {
      int error_code = SSL_get_error (ssl->ssl, ssl_read_rv);
      switch (error_code)
	{
	case SSL_ERROR_WANT_READ:
	  ssl->write_needed_to_read = 0;
	  break;
	case SSL_ERROR_WANT_WRITE:
	  ssl->write_needed_to_read = 1;
	  break;
	case SSL_ERROR_NONE:
	  g_set_error (error,
		       GSK_G_ERROR_DOMAIN,
		       GSK_ERROR_IO,
		       "error reading from ssl stream, but error code set to none");
	  break;
	case SSL_ERROR_SYSCALL:
	  g_set_error (error,
		       GSK_G_ERROR_DOMAIN, GSK_ERROR_IO,
		       "Gsk-BIO interface had problems reading");
	  break;
	default:
	  {
	    gulong l;
	    l = ERR_peek_error();
	    g_set_error (error,
			 GSK_G_ERROR_DOMAIN,
			 GSK_ERROR_IO,
			 "error reading from ssl stream: %s: %s: %s",
			 ERR_lib_error_string(l),
			 ERR_func_error_string(l),
			 ERR_reason_error_string(l));
	    break;
	  }
	}
      maybe_update_backend_poll_state (ssl);
    }
  return 0;
}

static guint
try_writing_the_write_buffer (GskStreamSsl *ssl, GError **error)
{
  int ssl_write_rv;

  ssl_write_rv = SSL_write (ssl->ssl, ssl->write_buffer, ssl->write_buffer_length);
  if (ssl_write_rv > 0)
    {
      ssl->write_buffer_length -= ssl_write_rv;
      memmove (ssl->write_buffer, ssl->write_buffer + ssl_write_rv, ssl->write_buffer_length);
      return ssl_write_rv;
    }
  if (ssl_write_rv == 0)
    {
      /* TODO: is this right??? :
	 
	 Either a clean shutdown occurred, or an error occurred in the
	 underlying transport. */
      gsk_io_notify_write_shutdown (ssl);

      /* XXX: should call SSL_get_error(). */
    }
  else if (ssl_write_rv < 0)
    {
      int error_code = SSL_get_error (ssl->ssl, ssl_write_rv);
      switch (error_code)
	{
	case SSL_ERROR_WANT_READ:
	  ssl->read_needed_to_write = 1;
	  break;
	case SSL_ERROR_WANT_WRITE:
	  ssl->read_needed_to_write = 0;
	  break;
	case SSL_ERROR_SYSCALL:
	  g_set_error (error,
		       GSK_G_ERROR_DOMAIN, GSK_ERROR_IO,
		       "Gsk-BIO interface had problems writing");
	  break;
	case SSL_ERROR_NONE:
	  g_set_error (error,
		       GSK_G_ERROR_DOMAIN,
		       GSK_ERROR_IO,
		       "error writing to ssl stream, but error code set to none");
	  break;
	default:
	  {
	    gulong l;
	    l = ERR_peek_error();
	    g_set_error (error,
			 GSK_G_ERROR_DOMAIN,
			 GSK_ERROR_IO,
			 "error writing to ssl stream [in the '%s' library]: %s: %s [is-client=%d]",
			 ERR_lib_error_string(l),
			 ERR_func_error_string(l),
			 ERR_reason_error_string(l),
			 ssl->is_client);
	    break;
	  }
	}
      maybe_update_backend_poll_state (ssl);
    }
  return 0;
}

static guint
gsk_stream_ssl_raw_write      (GskStream     *stream,
			       gconstpointer  data,
			       guint          length,
			       GError       **error)
{
  GskStreamSsl *ssl = GSK_STREAM_SSL (stream);
  if (length == 0)
    return 0;
  if (ssl->doing_handshake)
    return 0;

  if (ssl->write_buffer_length > 0)
    {
      try_writing_the_write_buffer (ssl, error);
      return 0;
    }

  /* Make sure our buffer is long enough to accomodate the
     amount of data requested.  */
  if (ssl->write_buffer_alloc < length)
    {
      if (ssl->write_buffer_alloc == 0)
	ssl->write_buffer_alloc = 4096;
      while (ssl->write_buffer_alloc < length)
	ssl->write_buffer_alloc *= 2;
      ssl->write_buffer = g_realloc (ssl->write_buffer, ssl->write_buffer_alloc);
    }

  /* Do underlying write. */
  memcpy (ssl->write_buffer, data, length);
  ssl->write_buffer_length = length;
  length = try_writing_the_write_buffer (ssl, error);
  if (*error)
    {
      ssl->write_buffer_length = 0;
      return 0;
    }
  if (length > 0)
    {
      /* On partial success, just throw away the write buffer. */
      ssl->write_buffer_length = 0;
    }
  else if (length == 0)
    {
      /* We will automatically retry this write. */
      length = ssl->write_buffer_length;
    }
  return length;
}

static void
gsk_stream_ssl_finalize (GObject *object)
{
  GskStreamSsl *ssl = GSK_STREAM_SSL (object);
  if (ssl->backend != NULL)
    {
      gsk_hook_untrap (gsk_buffer_stream_read_hook (ssl->backend));
      gsk_hook_untrap (gsk_buffer_stream_write_hook (ssl->backend));
      g_object_unref (ssl->backend);
      ssl->backend = NULL;
    }
  if (ssl->transport != NULL)
    {
      g_signal_handlers_disconnect_by_func (ssl->transport, G_CALLBACK (handle_transport_error), ssl);
      g_object_unref (ssl->transport);
    }
  if (ssl->ssl)
    SSL_free (ssl->ssl);
  SSL_CTX_free (ssl->ctx);
  g_free (ssl->read_buffer);
  g_free (ssl->write_buffer);
  g_free (ssl->cert_file);
  g_free (ssl->key_file);
  g_free (ssl->password);
  (*parent_class->finalize) (object);
}

/* --- transport hooks --- */
/* TODO: these should probably handle write_buffer/read_buffer themselves... */
static gboolean
backend_buffered_write_hook (GskStream    *backend,
		     gpointer      data)
{
  GskStreamSsl *ssl = GSK_STREAM_SSL (data);
  g_return_val_if_fail (ssl->backend == backend, FALSE);
  if (ssl->doing_handshake)
    {
      GError *error = NULL;
      if (!do_handshake (ssl, ssl->ssl, &error))
	{
	  gsk_io_set_gerror (GSK_IO (ssl), GSK_IO_ERROR_INIT, error);
	  return FALSE;
	}
      return TRUE;
    }
  switch (ssl->state)
    {
    case GSK_STREAM_SSL_STATE_CONSTRUCTING:
      g_return_val_if_reached (FALSE);
    case GSK_STREAM_SSL_STATE_ERROR:
      return FALSE;
    case GSK_STREAM_SSL_STATE_NORMAL:
      if (ssl->backend_poll_write)
	{
	  if (ssl->read_needed_to_write && ssl->this_writable)
	    {
	      ssl->read_needed_to_write = 0;
	      gsk_io_notify_ready_to_write (ssl);
	    }
	  else if (ssl->this_readable)
	    gsk_io_notify_ready_to_read (ssl);
	}
      return TRUE;
    case GSK_STREAM_SSL_STATE_SHUTTING_DOWN:
      {
	GError *error = NULL;
	gsk_stream_ssl_shutdown_both (ssl, &error);
	if (error)
	  gsk_io_set_gerror (GSK_IO (ssl), GSK_IO_ERROR_SHUTDOWN_READ, error);
	return TRUE;
      }
    case GSK_STREAM_SSL_STATE_SHUT_DOWN:
      g_return_val_if_reached (FALSE);
    }
  g_return_val_if_reached (FALSE);
}

static gboolean
backend_buffered_write_shutdown (GskStream    *backend,
		         gpointer      data)
{
  GskStreamSsl *ssl = GSK_STREAM_SSL (data);
  g_return_val_if_fail (ssl->backend == backend, FALSE);
  if (ssl->read_buffer_length == 0)
    gsk_io_notify_read_shutdown (ssl);
  return FALSE;
}

static gboolean
backend_buffered_read_hook (GskStream    *backend,
		     gpointer      data)
{
  GskStreamSsl *ssl = GSK_STREAM_SSL (data);
  g_return_val_if_fail (ssl->backend == backend, FALSE);
  switch (ssl->state)
    {
    case GSK_STREAM_SSL_STATE_CONSTRUCTING:
      g_return_val_if_reached (FALSE);
    case GSK_STREAM_SSL_STATE_ERROR:
      return FALSE;
    case GSK_STREAM_SSL_STATE_NORMAL:
      if (ssl->doing_handshake)
	{
	  GError *error = NULL;
	  if (!do_handshake (ssl, ssl->ssl, &error))
	    {
	      gsk_io_set_gerror (GSK_IO (ssl), GSK_IO_ERROR_INIT,
				 error);
	      return FALSE;
	    }
	}
      else if (ssl->backend_poll_read)
	{
	  if (ssl->write_needed_to_read && ssl->this_readable)
	    {
	      ssl->write_needed_to_read = 0;
	      gsk_io_notify_ready_to_read (ssl);
	    }
	  else if (ssl->this_writable)
	    gsk_io_notify_ready_to_write (ssl);
	}
      return TRUE;
    case GSK_STREAM_SSL_STATE_SHUTTING_DOWN:
      {
	GError *error = NULL;
	gsk_stream_ssl_shutdown_both (ssl, &error);
	if (error)
	  gsk_io_set_gerror (GSK_IO (ssl), GSK_IO_ERROR_SHUTDOWN_READ, error);
	return ssl->state == GSK_STREAM_SSL_STATE_SHUTTING_DOWN
	    || ssl->state == GSK_STREAM_SSL_STATE_SHUT_DOWN;

      }
    case GSK_STREAM_SSL_STATE_SHUT_DOWN:
      g_return_val_if_reached (FALSE);
    }
  g_return_val_if_reached (FALSE);
}

static gboolean
backend_buffered_read_shutdown (GskStream    *backend,
		          gpointer      data)
{
  GskStreamSsl *ssl = GSK_STREAM_SSL (data);
  g_return_val_if_fail (ssl->backend == backend, FALSE);
  return FALSE;
}

static void
gsk_stream_ssl_init (GskStreamSsl *stream_ssl)
{
  SSL_CTX *ctx = SSL_CTX_new (SSLv23_method ());
  SSL_CTX_set_mode (ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
  stream_ssl->ctx = ctx;
  stream_ssl->state = GSK_STREAM_SSL_STATE_CONSTRUCTING;

  gsk_io_mark_is_writable (stream_ssl);
  gsk_io_mark_is_readable (stream_ssl);
  gsk_hook_mark_can_defer_shutdown (GSK_IO_READ_HOOK (stream_ssl));
  gsk_hook_mark_can_defer_shutdown (GSK_IO_WRITE_HOOK (stream_ssl));
}

static void
gsk_stream_ssl_alloc_backend (GskStreamSsl *ssl)
{
  BIO *bio;
  GskBufferStream *backend;
  if (!gsk_openssl_bio_stream_pair (&bio, &backend))
    {
      g_warning ("error making bio-stream pair");
      return;
    }
  ssl->backend = GSK_STREAM (backend);
  SSL_set_bio (ssl->ssl, bio, bio);
  gsk_hook_trap (gsk_buffer_stream_read_hook (backend),
		 (GskHookFunc) backend_buffered_read_hook,
		 (GskHookFunc) backend_buffered_read_shutdown,
		 ssl,
		 NULL);
  gsk_hook_trap (gsk_buffer_stream_write_hook (backend),
		 (GskHookFunc) backend_buffered_write_hook,
		 (GskHookFunc) backend_buffered_write_shutdown,
		 ssl,
		 NULL);
  ssl->backend_poll_read = ssl->backend_poll_write = 1;
  maybe_update_backend_poll_state (ssl);
}

static int
verify_callback (int ok, X509_STORE_CTX *store)
{
  return ok;
}

static void
set_error (GskStreamSsl *ssl,
	   GskIOErrorCause cause,
	   const char *format,
	   ...)
{
  char *msg;
  const char *ssl_error_message;
  const char *ssl_error_filename;
  int ssl_error_lineno;
  int ssl_error_flags;

  va_list args;
  va_start (args, format);
  msg = g_strdup_vprintf (format, args);
  va_end (args);
  if (ERR_get_error_line_data (&ssl_error_filename, &ssl_error_lineno,
			       &ssl_error_message, &ssl_error_flags) == 0)
    {
      ssl_error_filename = "[*unknown*]";
      ssl_error_lineno = -1;
      ssl_error_message = "No SSL error message";
    }
  gsk_io_set_error (GSK_IO (ssl), GSK_IO_ERROR_INIT,
		    GSK_ERROR_BAD_FORMAT,
		    _("error %s: %s [%s, %d]"),
		    msg, ssl_error_message, ssl_error_filename, ssl_error_lineno);
}

/* Initialize the client's SSL context,
   with or without CA support.
   
   Based on [OREILLY], Page 138.
   This corresponds to the function setup_client_ctx(). */
static gboolean
ssl_ctx_setup (GskStreamSsl *ssl)
{
  gboolean verify_ca = (ssl->ca_file != NULL);

  if (verify_ca)
    {
      if (SSL_CTX_load_verify_locations (ssl->ctx, ssl->ca_file, ssl->ca_dir) != 1)
	{
	  set_error (ssl, GSK_ERROR_BAD_FORMAT, "loading CA file or directory");
	  return FALSE;
	}
      if (SSL_CTX_set_default_verify_paths (ssl->ctx) != 1)
	{
	  set_error (ssl, GSK_ERROR_BAD_FORMAT, "loading default CA file and/or directory");
	  return FALSE;
	}
    }

  if (ssl->cert_file != NULL)
    if (SSL_CTX_use_certificate_chain_file (ssl->ctx, ssl->cert_file) != 1)
      {
        set_error (ssl, GSK_ERROR_BAD_FORMAT, "loading certificate from file '%s'",
                   ssl->cert_file);
        return FALSE;
      }
  if (ssl->key_file != NULL)
    if (SSL_CTX_use_PrivateKey_file (ssl->ctx, ssl->key_file, SSL_FILETYPE_PEM) != 1)
      {
        set_error (ssl, GSK_ERROR_BAD_FORMAT, "loading private key from file '%s'",
                   ssl->key_file);
        return FALSE;
      }

  if (verify_ca)
    {
      int verify_flags = SSL_VERIFY_PEER;
      if (!ssl->is_client)
	verify_flags |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
      SSL_CTX_set_verify (ssl->ctx, verify_flags, verify_callback);
      SSL_CTX_set_verify_depth (ssl->ctx, 4);
    }
  return TRUE;
}

static GObject*
gsk_stream_ssl_constructor    (GType                  type,
			       guint                  n_construct_properties,
			       GObjectConstructParam *construct_properties)
{
  GObject *rv = (*parent_class->constructor) (type, n_construct_properties, construct_properties);
  GskStreamSsl *ssl = GSK_STREAM_SSL (rv);

  if (ssl_ctx_setup (ssl))
    {
      ssl->ssl = SSL_new (ssl->ctx);
      gsk_stream_ssl_alloc_backend (ssl);
      ssl->state = GSK_STREAM_SSL_STATE_NORMAL;
    }
  else
    {
      ssl->state = GSK_STREAM_SSL_STATE_ERROR;
    }
  return rv;
}

static int
gsk_openssl_passwd_cb (char     *buf,
		       int       size,
		       int       is_for_encryption,
		       void     *userdata)
{
  GskStreamSsl *ssl = GSK_STREAM_SSL (userdata);
  if (ssl->password != NULL)
    {
      strncpy (buf, ssl->password, size);
      buf[size - 1] = '\0';
      return strlen (ssl->password);
    }
  return 0;
}

void
gsk_stream_ssl_set_property           (GObject        *object,
				       guint           property_id,
				       const GValue   *value,
				       GParamSpec     *pspec)
{
  GskStreamSsl *ssl = GSK_STREAM_SSL (object);
  char *arg;

  switch (property_id)
    {
      case PROP_PASSWORD:
	arg = g_value_dup_string (value);
	g_free (ssl->password);
	ssl->password = arg;
	if (ssl->password != NULL)
	  {
	    SSL_CTX_set_default_passwd_cb_userdata (ssl->ctx, ssl);
	    SSL_CTX_set_default_passwd_cb (ssl->ctx, gsk_openssl_passwd_cb);
	  }
	break;
      case PROP_CERT_FILE:
	arg = g_value_dup_string (value);
	g_free (ssl->cert_file);
	ssl->cert_file = arg;
        break;
      case PROP_KEY_FILE:
	arg = g_value_dup_string (value);
	g_free (ssl->key_file);
	ssl->key_file = arg;
        break;
      case PROP_IS_CLIENT:
	g_assert (ssl->ssl == NULL);
	ssl->is_client = g_value_get_boolean (value) ? 1 : 0;
	break;
      default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	break;
    }
}

void
gsk_stream_ssl_get_property	      (GObject        *object,
				       guint           property_id,
				       GValue         *value,
				       GParamSpec     *pspec)
{
  GskStreamSsl *ssl = GSK_STREAM_SSL (object);

  switch (property_id)
    {
      case PROP_CERT_FILE:
	g_value_set_string (value, ssl->cert_file);
        break;
      case PROP_KEY_FILE:
	g_value_set_string (value, ssl->key_file);
        break;
      case PROP_IS_CLIENT:
	g_value_set_boolean (value, ssl->is_client);
        break;
      default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	break;
    }
}

static void
actions_to_seed_PRNG    (void)
{
  while (RAND_status () == 0)
    {
      int i;
      int len;
      unsigned char *buf;

      /* TODO: if we are here, /dev/random doesn't exist ... warezed for now */

      len = 2048;
      buf = g_malloc (len);

      for (i = 0; i < len; i += 4)
        {
          long int h4x;

          h4x = lrand48 ();

          memcpy (buf + i, &h4x, 4);
        }

      RAND_seed (buf, len);

      g_free (buf);
    }
}

static void
gsk_stream_ssl_class_init (GskStreamSslClass *class)
{
  GskIOClass *io_class = GSK_IO_CLASS (class);
  GskStreamClass *stream_class = GSK_STREAM_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  static gboolean has_ssl_library_init = FALSE;
  GParamSpec *pspec;
  parent_class = g_type_class_peek_parent (class);
  io_class->set_poll_read = gsk_stream_ssl_set_poll_read;
  io_class->shutdown_read = gsk_stream_ssl_shutdown_read;
  io_class->set_poll_write = gsk_stream_ssl_set_poll_write;
  io_class->shutdown_write = gsk_stream_ssl_shutdown_write;
  stream_class->raw_read = gsk_stream_ssl_raw_read;
  stream_class->raw_write = gsk_stream_ssl_raw_write;
  object_class->constructor = gsk_stream_ssl_constructor;
  object_class->get_property = gsk_stream_ssl_get_property;
  object_class->set_property = gsk_stream_ssl_set_property;
  object_class->finalize = gsk_stream_ssl_finalize;

  if (!has_ssl_library_init)
    {
      //THREAD_setup();
      SSL_load_error_strings ();
      SSL_library_init ();
      actions_to_seed_PRNG ();
    }

  pspec = g_param_spec_string ("key-file",
			       _("Key File"),
			       _("the x.509 PEM Key"),
			       NULL,
			       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_KEY_FILE, pspec);
  pspec = g_param_spec_string ("cert-file",
			       _("Certificate File"),
			       _("the x.509 PEM Certificate"),
			       NULL,
			       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_CERT_FILE, pspec);
  pspec = g_param_spec_boolean ("is-client",
			       _("Is Client"),
			       _("is this a SSL client (versus a server)"),
			       FALSE,
			       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_IS_CLIENT, pspec);

  pspec = g_param_spec_string ("password",
			       _("Password"),
			       _("secret passphrase for the certificate"),
			       NULL,
			       G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_PASSWORD, pspec);
}

GType gsk_stream_ssl_get_type()
{
  static GType stream_ssl_type = 0;
  if (!stream_ssl_type)
    {
      static const GTypeInfo stream_ssl_info =
      {
	sizeof(GskStreamSslClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_stream_ssl_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskStreamSsl),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_stream_ssl_init,
	NULL		/* value_table */
      };
      stream_ssl_type = g_type_register_static (GSK_TYPE_STREAM,
                                                  "GskStreamSsl",
						  &stream_ssl_info, 0);
    }
  return stream_ssl_type;
}

static GskStreamSsl *
maybe_attach_transport (GskStreamSsl *ssl,
			GskStream    *transport,
			GError      **error)
{
  if (ssl->state == GSK_STREAM_SSL_STATE_ERROR)
    {
      g_propagate_error (error, GSK_IO (ssl)->error);
      g_object_unref (ssl);
      if (transport != NULL)
	g_object_unref (transport);
      return NULL;
    }
  if (transport != NULL)
    {
      GError *suberror = NULL;
      GskStreamConnection *connection;

      ssl->transport = g_object_ref (transport);
      g_signal_connect (transport, "on-error", G_CALLBACK (handle_transport_error), ssl);

      connection = gsk_stream_connection_new (ssl->backend, transport, &suberror);
      if (suberror)
	{
	  g_propagate_error (error, suberror);
	  g_object_unref (ssl);
	  return NULL;
	}
      gsk_stream_attach (transport, ssl->backend, &suberror);
      if (suberror)
	{
	  g_propagate_error (error, suberror);
	  gsk_stream_connection_detach (connection);
	  g_object_unref (connection);
	  g_object_unref (ssl);
	  return NULL;
	}
      g_object_unref (connection);
    }
  return ssl;
}

/**
 * gsk_stream_ssl_new_server:
 * @cert_file: the PEM x509 certificate file.
 * @key_file: key file???
 * @password: password required by the certificate, or NULL.
 * @transport: optional transport layer (which will be connected
 * to the backend stream by bidirectionally).
 * @error: optional location in which to store a #GError.
 *
 * Create a new SSL server.
 * It should be connected to a socket which was accepted from
 * a server (usually provided as the @transport argument).
 *
 * returns: the new SSL stream, or NULL if an error occurs.
 */
GskStream   *gsk_stream_ssl_new_server   (const char   *cert_file,
					  const char   *key_file,
					  const char   *password,
					  GskStream    *transport,
					  GError      **error)
{
  GskStreamSsl *stream_ssl = g_object_new (GSK_TYPE_STREAM_SSL,
				           "is-client", FALSE,
					   "password", password,
				           "cert-file", cert_file,
				           "key-file", key_file,
				           NULL);


  /* This is the OpenSSL library stream type. */
  SSL *ssl;

  stream_ssl = maybe_attach_transport (stream_ssl, transport, error);
  if (stream_ssl == NULL)
    return NULL;

  /* Cast from a gpointer to make the following code
     more typesafe */
  ssl = stream_ssl->ssl;

  SSL_set_accept_state (ssl);
#if 0
  rv = SSL_accept (ssl);
  if (rv <= 0)
    {
      int error_code = SSL_get_error (ssl, rv);
      gulong l = ERR_peek_error();
      switch (error_code)
	{
	case SSL_ERROR_NONE:
	  break;
	case SSL_ERROR_WANT_READ:
	  g_message("accept:want-read");
	  break;
	case SSL_ERROR_WANT_WRITE:
	  g_message("accept:want-write");
	  break;
	default:
	  {
	    g_set_error (error,
			 GSK_G_ERROR_DOMAIN,
			 GSK_ERROR_BAD_FORMAT,
			 _("error accepting on SSL socket: %s: %s [code=%08lx (%lu)]"),
			 ERR_func_error_string(l),
			 ERR_reason_error_string(l),
			 l, l);
	    g_object_unref (stream_ssl);
	    return NULL;
	  }
	}
    }
#endif
  stream_ssl->doing_handshake = 1;
  if (!do_handshake (stream_ssl, ssl, error))
    {
      g_object_unref (stream_ssl);
      return NULL;
    }

  return GSK_STREAM (stream_ssl);
}

/**
 * gsk_stream_ssl_new_client:
 * @cert_file: the PEM x509 certificate file.
 * @key_file: key file???
 * @password: password required by the certificate, or NULL.
 * @transport: optional transport layer (which will be connected
 * to the backend stream by bidirectionally).
 * @error: optional location in which to store a #GError.
 *
 * Create the client end of a SSL connection.
 * This should be attached to a connecting or connected stream,
 * usually provided as the @transport argument.
 *
 * returns: the new SSL stream, or NULL if an error occurs.
 */
GskStream   *gsk_stream_ssl_new_client   (const char   *cert_file,
					  const char   *key_file,
					  const char   *password,
					  GskStream    *transport,
					  GError      **error)
{
  GskStreamSsl *stream_ssl = g_object_new (GSK_TYPE_STREAM_SSL,
				           "is-client", TRUE,
					   "password", password,
				           "cert-file", cert_file,
				           "key-file", key_file,
				           NULL);
  SSL *ssl;
  //GError *suberror = NULL;
  stream_ssl = maybe_attach_transport (stream_ssl, transport, error);
  if (stream_ssl == NULL)
    return NULL;

  /* Cast from a gpointer to make the following code
     more typesafe */
  ssl = stream_ssl->ssl;

  SSL_set_connect_state (ssl);
#if 0
  /* Tell the SSL object to initiate the connection
     protocol.  (XXX: I Think it is correct to say
     that the client takes responsibility for beginning
     the connection, but a reference to a spec or [OREILLY]
     would be good.) */
  if (SSL_connect (ssl) <= 0)
    {
      gulong l = ERR_peek_error();
      g_set_error (error,
		   GSK_G_ERROR_DOMAIN,
		   GSK_ERROR_BAD_FORMAT,
		   _("error initiating SSL connect: %s: %s"),
		   ERR_func_error_string (l),
		   ERR_reason_error_string (l));
      g_object_unref (stream_ssl);
      return NULL;
    }

  g_message ("ran SSL_connect on %p", stream_ssl);
#endif
  stream_ssl->doing_handshake = 1;
  if (!do_handshake (stream_ssl, ssl, error))
    {
      g_object_unref (stream_ssl);
      return NULL;
    }

  return GSK_STREAM (stream_ssl);
}

/**
 * gsk_stream_ssl_peek_backend:
 * @ssl: the stream to query.
 *
 * Get a reference to the backend stream, which
 * should be connected to the underlying transport
 * layer.
 *
 * returns: the SSL backend (to be connected to the transport,
 * which is the stream which is typically insecure
 * without SSL protection).
 */
GskStream *
gsk_stream_ssl_peek_backend (GskStreamSsl *ssl)
{
  return ssl->backend;
}
