#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "../gskstreamlistenersocket.h"
#include "../gsksimpleparserstream.h"
#include "../gskdebug.h"
#include "../gskinit.h"
#include "../gskmain.h"

static GskCommandTable *command_table;
static GskSimpleParser *simple_parser;

/* --- defining the default cli hook-type --- */

/* this code should probably be in command_table! */
static guint
cli_hash_key   (gconstpointer  key_to_hash,
		guint          key_len,
		gpointer       hook_type_data)
{
  return g_str_hash (key_to_hash);
}

static guint
cli_hash_input   (gconstpointer  ptr_to_hash,
		  guint          ptr_len,
		  gpointer       hook_type_data)
{
  const char *s = ptr_to_hash;
  guint remaining = ptr_len;
  const char *key_start, *key_end;
  char *key;
  while (*s && remaining > 0 && isspace (*s))
    {
      s++;
      remaining--;
    }
  key_start = s;
  while (*s && remaining > 0 && !isspace (*s))
    {
      s++;
      remaining--;
    }
  key_end = s;
  key = alloca (key_end - key_start + 1);
  memcpy (key, key_start, key_end - key_start);
  key[key_end - key_start] = 0;
  g_message ("cli_hash_input=`%s'", key);
  return g_str_hash (key);
}

static gboolean
cli_cmp_key_input (gconstpointer  ptr,
                   guint          ptr_len,
                   gconstpointer  key,
                   guint          key_len,
                   gpointer       hook_type_data)
{
  const char *s = ptr;
  guint remaining = ptr_len;
  const char *k = key;
  g_message ("testing key=`%s' versus user input `%s'", (char*)key, (char*)ptr);
  /* remember, key_len is bogus: the string is NUL terminated! */
  while (*s && remaining > 0 && isspace (*s))
    {
      s++;
      remaining--;
    }
  while (*s && *k && remaining > 0 && !isspace (*s))
    {
      if (*s != *k)
	return FALSE;
      s++;
      k++;
      remaining--;
    }
  if (*k)
    return FALSE;
  if (*s == 0 || isspace (*s))
    return TRUE;
  return FALSE;
}

/* --- hooks --- */
#define DEFINE_HOOK_START(function_name)				\
static void								\
function_name        (gconstpointer  data,				\
		      guint          len,				\
		      gpointer       hook_data,				\
		      GskBuffer     *output_buffer,			\
		      GValue        *args,				\
		      GskCommandTableDoneNotify *notify)		\
{									\
  GskSocketAddress *address = g_value_get_object (args + 0);		\
  {

#define DEFINE_HOOK_END()						\
  }									\
}

DEFINE_HOOK_START(handle_remote_address)
  char *str;
  if (address == NULL)
    str = g_strdup ("Remote address unknown.");
  else
    str = gsk_socket_address_to_string (address);
  gsk_buffer_append_string (output_buffer, str);
  gsk_buffer_append_string (output_buffer, "\r\n");
  gsk_command_table_done_notify (notify);
DEFINE_HOOK_END()

DEFINE_HOOK_START(handle_echo)
  gsk_buffer_append (output_buffer, data, len);
  gsk_command_table_done_notify (notify);
DEFINE_HOOK_END()

DEFINE_HOOK_START(handle_help)
  gsk_buffer_append_string (output_buffer,
			    "test-simple-parser-stream [help]\r\n"
			    "\r\n"
			    "well, this isn't useful or anything; here's the known commands:\r\n"
			    "  remote-address, help, ?, echo\r\n");
  gsk_command_table_done_notify (notify);
DEFINE_HOOK_END()

static gboolean
handle_on_accept (GskStream         *stream,
                  gpointer           data,
                  GError           **error)
{
  GError *e = NULL;
  GskStream *pstream;
  GskSocketAddress *remote_address;
  g_assert (data == NULL);
  remote_address = g_object_get_qdata (G_OBJECT (stream),
                                       GSK_SOCKET_ADDRESS_REMOTE_QUARK);
  pstream = gsk_simple_parser_stream_new (simple_parser,
					  command_table,
                                          remote_address);
  gsk_stream_attach (stream, pstream, &e);
  gsk_stream_attach (pstream, stream, &e);
  if (e != NULL)
    g_error ("gsk_stream_attach: %s", e->message);
  g_object_unref (stream);
  g_object_unref (pstream);

  g_message ("handle_on_accept: stream->ref_count=%d, pstream->ref_count=%d",
	     G_OBJECT (stream)->ref_count,
	     G_OBJECT (pstream)->ref_count);
  return TRUE;
}


static void
handle_errors (GError     *error,
               gpointer    data)
{
  g_error ("error accepting new socket: %s", error->message);
}

int main (int argc, char **argv)
{
  GskStreamListener *listener;
  GskSocketAddress *bind_addr;
  GError *error = NULL;
  guint cli_hook;
  gsk_init_without_threads (&argc, &argv);

  gsk_debug_add_flags (GSK_DEBUG_ALL);

  if (argc != 2)
    g_error ("%s requires exactly 1 argument, tcp port number", argv[0]);

  bind_addr = gsk_socket_address_ipv4_localhost (atoi (argv[1]));
  listener = gsk_stream_listener_socket_new_bind (bind_addr, &error);
  if (error != NULL)
    g_error ("gsk_stream_listener_tcp_unix failed: %s", error->message);
  g_assert (listener != NULL);

  command_table = gsk_command_table_new ((GskCommandTableFlags) 0,
					 GSK_TYPE_SOCKET_ADDRESS, 0);
  simple_parser = gsk_simple_parser_new_char_terminated ('\n', 16384, FALSE);

  cli_hook = gsk_command_table_add_hook_type (command_table,
                                              cli_hash_key,
                                              cli_hash_input,
                                              cli_cmp_key_input,
                                              NULL, NULL);
  gsk_command_table_add_hook (command_table, cli_hook,
                              "remote-address", 0, NULL,
                              handle_remote_address, NULL, NULL);
  gsk_command_table_add_hook (command_table, cli_hook,
                              "echo", 0, NULL,
                              handle_echo, NULL, NULL);
  gsk_command_table_add_hook (command_table, cli_hook,
                              "help", 0, NULL,
                              handle_help, NULL, NULL);
  gsk_command_table_add_hook (command_table, cli_hook,
                              "?", 0, NULL,
                              handle_help, NULL, NULL);
  gsk_stream_listener_handle_accept (listener,
                                     handle_on_accept,
                                     handle_errors,
                                     NULL,              /* data */
                                     NULL);             /* destroy */

  return gsk_main_run ();
}
