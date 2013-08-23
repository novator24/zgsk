/*
    GSK - a library to write servers

    Copyright (C) 2006 Dave Benson

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

#include "gskstreamwatchdog.h"

G_DEFINE_TYPE(GskStreamWatchdog, gsk_stream_watchdog, GSK_TYPE_STREAM);

/* --- GskIO methods --- */
static gboolean
handle_underlying_readable (GskStream *underlying,
                            GskStreamWatchdog *watchdog)
{
  gsk_io_notify_ready_to_read (watchdog);
  return TRUE;
}

static gboolean
handle_underlying_read_shutdown (GskStream *underlying,
                                 GskStreamWatchdog *watchdog)
{
  gsk_io_notify_read_shutdown (watchdog);
  return FALSE;
}

static gboolean
notify_read_shutdown (gpointer data)
{
  GskStreamWatchdog *watchdog = GSK_STREAM_WATCHDOG (data);
  gsk_io_notify_read_shutdown (watchdog);
  g_object_unref (watchdog);
  return FALSE;
}

static void
gsk_stream_watchdog_set_poll_read   (GskIO      *io,
                                     gboolean    do_poll)
{
  GskStreamWatchdog *watchdog = GSK_STREAM_WATCHDOG (io);
  if (watchdog->underlying == NULL)
    return;
  if (do_poll)
    {
      if (gsk_io_get_is_readable (watchdog->underlying))
        {
          gsk_stream_trap_readable (watchdog->underlying,
                                    handle_underlying_readable,
                                    handle_underlying_read_shutdown,
                                    watchdog,
                                    NULL);
        }
      else
        {
          gsk_main_loop_add_idle (gsk_main_loop_default (),
                                  notify_read_shutdown,
                                  g_object_ref (watchdog),
                                  NULL);
        }
    }
  else
    {
      if (watchdog->underlying != NULL)
        gsk_stream_untrap_readable (watchdog->underlying);

    }
}

static gboolean
handle_underlying_writable (GskStream *underlying,
                            GskStreamWatchdog *watchdog)
{
  gsk_io_notify_ready_to_write (watchdog);
  return TRUE;
}

static gboolean
handle_underlying_write_shutdown (GskStream *underlying,
                                  GskStreamWatchdog *watchdog)
{
  gsk_io_notify_write_shutdown (watchdog);
  return FALSE;
}

static gboolean
notify_write_shutdown (gpointer data)
{
  GskStreamWatchdog *watchdog = GSK_STREAM_WATCHDOG (data);
  gsk_io_notify_write_shutdown (watchdog);
  g_object_unref (watchdog);
  return FALSE;
}

static void
gsk_stream_watchdog_set_poll_write   (GskIO      *io,
                                     gboolean    do_poll)
{
  GskStreamWatchdog *watchdog = GSK_STREAM_WATCHDOG (io);
  if (watchdog->underlying == NULL)
    return;
  if (do_poll)
    {
      if (gsk_io_get_is_writable (watchdog->underlying))
        {
          gsk_stream_trap_writable (watchdog->underlying,
                                    handle_underlying_writable,
                                    handle_underlying_write_shutdown,
                                    watchdog,
                                    NULL);
        }
      else
        {
          gsk_main_loop_add_idle (gsk_main_loop_default (),
                                  notify_write_shutdown,
                                  g_object_ref (watchdog),
                                  NULL);
        }
    }
  else
    {
      if (watchdog->underlying != NULL)
        gsk_stream_untrap_writable (watchdog->underlying);
    }
}

static gboolean
gsk_stream_watchdog_shutdown_read   (GskIO      *io,
                                     GError    **error)
{
  GskStreamWatchdog *watchdog = GSK_STREAM_WATCHDOG (io);
  if (watchdog->underlying == NULL)
    return TRUE;
  return gsk_io_read_shutdown (watchdog->underlying, error);
}

static gboolean
gsk_stream_watchdog_shutdown_write  (GskIO      *io,
                                     GError    **error)
{
  GskStreamWatchdog *watchdog = GSK_STREAM_WATCHDOG (io);
  if (watchdog->underlying == NULL)
    return TRUE;
  return gsk_io_write_shutdown (watchdog->underlying, error);
}

static gboolean
handle_inactivity_timeout (gpointer data)
{
  GskStreamWatchdog *watchdog = GSK_STREAM_WATCHDOG (data);
  if (watchdog->underlying)
    {
      gsk_io_untrap_readable (watchdog->underlying);
      gsk_io_untrap_writable (watchdog->underlying);
      gsk_io_shutdown (GSK_IO (watchdog->underlying), NULL);
    }
  watchdog->timeout = NULL;
  gsk_io_notify_shutdown (GSK_IO (watchdog));
  return FALSE;
}

/* --- GskStream methods --- */
static inline void
touch_timer (GskStreamWatchdog *watchdog)
{
  gsk_source_adjust_timer (watchdog->timeout,
                           watchdog->max_inactivity_millis,
                           watchdog->max_inactivity_millis);
}

static guint
gsk_stream_watchdog_raw_read  (GskStream     *stream,
                               gpointer       data,
                               guint          length,
                               GError       **error)
{
  GskStreamWatchdog *watchdog = GSK_STREAM_WATCHDOG (stream);
  guint rv;
  g_return_val_if_fail (watchdog->underlying != NULL, 0);
  rv = gsk_stream_read (watchdog->underlying, data, length, error);
  touch_timer (watchdog);
  return rv;
}

static guint
gsk_stream_watchdog_raw_write (GskStream     *stream,
                               gconstpointer  data,
                               guint          length,
                               GError       **error)
{
  GskStreamWatchdog *watchdog = GSK_STREAM_WATCHDOG (stream);
  guint rv;
  g_return_val_if_fail (watchdog->underlying != NULL, 0);
  rv = gsk_stream_write (watchdog->underlying, data, length, error);
  touch_timer (watchdog);
  return rv;
}

static guint
gsk_stream_watchdog_raw_read_buffer (GskStream     *stream,
                                     GskBuffer     *buffer,
                                     GError       **error)
{
  GskStreamWatchdog *watchdog = GSK_STREAM_WATCHDOG (stream);
  guint rv;
  g_return_val_if_fail (watchdog->underlying != NULL, 0);
  rv = gsk_stream_read_buffer (watchdog->underlying, buffer, error);
  touch_timer (watchdog);
  return rv;
}

static guint
gsk_stream_watchdog_raw_write_buffer(GskStream    *stream,
                                     GskBuffer     *buffer,
                                     GError       **error)
{
  GskStreamWatchdog *watchdog = GSK_STREAM_WATCHDOG (stream);
  guint rv;
  g_return_val_if_fail (watchdog->underlying != NULL, 0);
  rv = gsk_stream_write_buffer (watchdog->underlying, buffer, error);
  touch_timer (watchdog);
  return rv;
}

/* --- GObject methods --- */
static void
gsk_stream_watchdog_finalize (GObject *object)
{
  GskStreamWatchdog *watchdog = GSK_STREAM_WATCHDOG (object);
  if (watchdog->timeout)
    gsk_source_remove (watchdog->timeout);
  if (watchdog->underlying)
    {
      gsk_io_untrap_writable (watchdog->underlying);
      gsk_io_untrap_readable (watchdog->underlying);
      g_object_unref (watchdog->underlying);
    }
  G_OBJECT_CLASS (gsk_stream_watchdog_parent_class)->finalize (object);
}

static void
gsk_stream_watchdog_init (GskStreamWatchdog *watchdog)
{
}

static void
gsk_stream_watchdog_class_init (GskStreamWatchdogClass *class)
{
  GskIOClass *io_class = GSK_IO_CLASS (class);
  GskStreamClass *stream_class = GSK_STREAM_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  io_class->set_poll_read = gsk_stream_watchdog_set_poll_read;
  io_class->set_poll_write = gsk_stream_watchdog_set_poll_write;
  io_class->shutdown_read = gsk_stream_watchdog_shutdown_read;
  io_class->shutdown_write = gsk_stream_watchdog_shutdown_write;
  stream_class->raw_read = gsk_stream_watchdog_raw_read;
  stream_class->raw_write = gsk_stream_watchdog_raw_write;
  stream_class->raw_read_buffer = gsk_stream_watchdog_raw_read_buffer;
  stream_class->raw_write_buffer = gsk_stream_watchdog_raw_write_buffer;
  object_class->finalize = gsk_stream_watchdog_finalize;
}

/**
 * gsk_stream_watchdog_new:
 * @underlying_stream: the real underlying stream to proxy.
 * After calling this function, the underlying stream should not
 * be directly read from or written to, or trapped.
 * @max_inactivity_millis: maximum idle time permitted, in milliseconds.
 *
 * Create a proxy stream for the @underlying_stream which will
 * automatically shutdown if no reads or writes occur
 * for more that @max_inactivity_millis milliseconds.
 *
 * returns: the newly allocated stream.
 */
GskStream *gsk_stream_watchdog_new (GskStream       *underlying_stream,
                                    guint            max_inactivity_millis)
{
  GskStreamWatchdog *watchdog = g_object_new (GSK_TYPE_STREAM_WATCHDOG, NULL);
  watchdog->underlying = g_object_ref (underlying_stream);
  watchdog->max_inactivity_millis = max_inactivity_millis;
  watchdog->timeout = gsk_main_loop_add_timer (gsk_main_loop_default (),
                                               handle_inactivity_timeout,
                                               watchdog,
                                               NULL,
                                               max_inactivity_millis,
                                               max_inactivity_millis);
  if (gsk_io_get_is_readable (underlying_stream))
    gsk_io_mark_is_readable (watchdog);
  if (gsk_io_get_is_writable (underlying_stream))
    gsk_io_mark_is_writable (watchdog);
  return GSK_STREAM (watchdog);
}
