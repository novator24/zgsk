#include "../zlib/gskzlibinflator.h"
#include "../zlib/gskzlibdeflator.h"
#include "../gskmemory.h"
#include "../gskinit.h"
#include <string.h>

typedef struct _Result Result;
struct _Result
{
  gboolean got_callback;
  guint size;
  guint8 *data;
};

static void
memory_buffer_callback (GskBuffer              *buffer,
		        gpointer                data)
{
  Result *res = data;
  g_assert (!res->got_callback);
  res->got_callback = TRUE;
  res->size = buffer->size;
  res->data = g_malloc (res->size);
  gsk_buffer_peek (buffer, res->data, res->size);
}

static void
test_with_string (const char *str)
{
  GskStream *inflator;
  GskStream *deflator;
  GskStream *memory_input;
  GskStream *memory_output;
  Result result = { FALSE, 0, NULL };
  GError *error = NULL;
  GskMainLoop *loop;
  loop = gsk_main_loop_default ();

  memory_input = gsk_memory_source_new_printf (str);
  deflator = gsk_zlib_deflator_new (-1, -1);
  inflator = gsk_zlib_inflator_new ();
  memory_output = gsk_memory_buffer_sink_new (memory_buffer_callback, &result, NULL);
  gsk_stream_attach (memory_input, deflator, &error);
  g_assert (error == NULL);
  gsk_stream_attach (deflator, inflator, &error);
  g_assert (error == NULL);
  gsk_stream_attach (inflator, memory_output, &error);
  g_assert (error == NULL);

  while (!result.got_callback)
    gsk_main_loop_run (loop, -1, NULL);

  g_assert (result.size == strlen (str));
  g_assert (memcmp (result.data, str, strlen (str)) == 0);
}

int main (int argc, char **argv)
{
  gsk_init (&argc, &argv, NULL);
  test_with_string ("hi mom");
  test_with_string ("<html><head><meta http-equiv=\"content-type\" content=\"text/html; charset=ISO-8859-1\"><title>Google</title><style><!--\nbody,td,a,p,.h{font-family:arial,sans-serif;}\n.h{font-size: 20px;}\n.q{color:#0000cc;}\n//-->\n</style>\n<script>\n<!--\nfunction sf(){document.f.q.focus();}\n// -->\n</script>\n</head><body bgcolor=#ffffff text=#000000 link=#0000cc vlink=#551a8b alink=#ff0000 onLoad=sf()><center><img src=\"/intl/en/images/logo.gif\" width=276 height=110 alt=\"Google\"><br><br>\n<form action=/search name=f><table border=0 cellspacing=0 cellpadding=4><tr><td nowrap><font size=-1><b>Web</b>&nbsp;&nbsp;&nbsp;&nbsp;<a id=1a class=q href=\"/imghp?hl=en&tab=wi&ie=UTF-8\">Images</a>&nbsp;&nbsp;&nbsp;&nbsp;<a id=2a class=q href=\"http://groups-beta.google.com/grphp?hl=en&tab=wg&ie=UTF-8\">Groups</a>&nbsp;&nbsp;&nbsp;&nbsp;<a id=4a class=q href=\"/nwshp?hl=en&tab=wn&ie=UTF-8\">News</a>&nbsp;&nbsp;&nbsp;&nbsp;<a id=5a class=q href=\"/frghp?hl=en&tab=wf&ie=UTF-8\">Froogle</a>&nbsp;&nbsp;&nbsp;&nbsp;<a id=7a class=q href=\"/lochp?hl=en&tab=wl&ie=UTF-8\">Local</a><sup><a href=\"/lochp?hl=en&tab=wl&ie=UTF-8\" style=\"text-decoration:none;\"><font color=red>New!</font></a></sup>&nbsp;&nbsp;&nbsp;&nbsp;<b><a href=\"/options/index.html\" class=q>more&nbsp;&raquo;</a></b></font></td></tr></table><table cellspacing=0 cellpadding=0><tr><td width=25%>&nbsp;</td><td align=center><input type=hidden name=hl value=en><input type=hidden name=ie value=\"ISO-8859-1\"><input maxLength=256 size=55 name=q value=\"\"><br><input type=submit value=\"Google Search\" name=btnG><input type=submit value=\"I'm Feeling Lucky\" name=btnI></td><td valign=top nowrap width=25%><font size=-2>&nbsp;&nbsp;<a href=/advanced_search?hl=en>Advanced Search</a><br>&nbsp;&nbsp;<a href=/preferences?hl=en>Preferences</a><br>&nbsp;&nbsp;<a href=/language_tools?hl=en>Language Tools</a></font></td></tr></table></form><br><br><font size=-1><a href=\"/ads/\">Advertising&nbsp;Programs</a> - <a href=/intl/en/about.html>About Google</a></font><p><font size=-2>&copy;2005 Google - Searching 8,058,044,651 web pages</font></p></center></body></html>");


  return 0;
}
