#include "../http/gskhttpcontent.h"
#include "../http/gskhttpclient.h"
#include "../http/gskhttpserver.h"
#include "../gskmemory.h"
#include "../gskinit.h"
#include "../gskmainloop.h"
#include <string.h>

typedef struct _ResponseData ResponseData;
struct _ResponseData
{
  gboolean drained;
  GskHttpResponse *response;
  GskBuffer content;
};

static void
handle_buffer_done (GskBuffer *buffer, gpointer data)
{
  ResponseData *rd = data;
  gsk_buffer_drain (&rd->content, buffer);
  rd->drained = TRUE;
}

static void
handle_response (GskHttpRequest  *request,
                 GskHttpResponse *response,
                 GskStream       *input,
                 gpointer         hook_data)
{
  ResponseData *rd = hook_data;
  GskStream *sink = gsk_memory_buffer_sink_new (handle_buffer_done, rd, NULL);
  rd->response = g_object_ref (response);
  gsk_stream_attach (input, sink, NULL);
}

static void
test_get (GskHttpContent   *content,
          const char       *path,
          const char       *user_agent,
          GskHttpResponse **response_out,
          char            **text_out)
{
  GskHttpClient *client = gsk_http_client_new ();
  GskHttpServer *server = gsk_http_server_new ();
  GskHttpRequest *request = gsk_http_request_new (GSK_HTTP_VERB_GET, path);
  ResponseData rd = {FALSE, NULL, GSK_BUFFER_STATIC_INIT};
  gsk_http_content_manage_server (content, server);
  if (user_agent != NULL)
    g_object_set (request, "user-agent", user_agent, NULL);
  gsk_http_client_request (client, request, NULL,
                           handle_response, &rd, NULL);
  gsk_stream_attach_pair (GSK_STREAM (client), GSK_STREAM (server), NULL);

  while (!rd.drained)
    gsk_main_loop_run (gsk_main_loop_default (), 0, NULL);
  *response_out = rd.response;
  *text_out = g_malloc (rd.content.size + 1);
  (*text_out)[rd.content.size] = 0;
  gsk_buffer_read (&rd.content, *text_out, rd.content.size);
  g_assert (rd.response);
}

static void
add_static_text (GskHttpContent *content,
                 const GskHttpContentId *id,
                 const char *static_string)
{
  gsk_http_content_add_data (content, id, static_string, strlen (static_string), NULL, NULL);
}

int main(int argc, char **argv)
{
  GskHttpContent *content;
  GskHttpContentId id = GSK_HTTP_CONTENT_ID_INIT;
  GskHttpResponse *response;
  char *data;
  const char *type, *subtype;
  gsk_init_without_threads (&argc, &argv);
  content = gsk_http_content_new ();

  id.path = "/hi/my/name/is/q.html";
  add_static_text (content, &id, "this is q.html");
  id.path = NULL;

  id.path_prefix = "/hi/my/name/prefix";
  add_static_text (content, &id, "anything under ...my/name/prefix");

  id.path_suffix = ".a";
  add_static_text (content, &id, "anything under ...my/name/prefix ... .a");
  id.path_suffix = ".b";
  add_static_text (content, &id, "anything under ...my/name/prefix ... .b");
  id.path_prefix = NULL;
  id.path_suffix = NULL;

  id.path = "/data/foo.b";
  add_static_text (content, &id, "some b2 data");
  id.path = NULL;

  gsk_http_content_set_mime_type (content, NULL, ".html", "text", "html");
  gsk_http_content_set_mime_type (content, NULL, ".a", "x-application", "a");
  gsk_http_content_set_mime_type (content, "/hi", ".b", "x-application", "b");
  gsk_http_content_set_mime_type (content, "/data", ".b", "x-application", "b2");

  g_assert (gsk_http_content_get_mime_type (content, "whatever.html", &type, &subtype));
  g_assert (strcmp (type, "text") == 0 && strcmp (subtype, "html") == 0);
  g_assert (gsk_http_content_get_mime_type (content, "whatever.a", &type, &subtype));
  g_assert (strcmp (type, "x-application") == 0 && strcmp (subtype, "a") == 0);
  g_assert (gsk_http_content_get_mime_type (content, "/hi/whatever.b", &type, &subtype));
  g_assert (strcmp (type, "x-application") == 0 && strcmp (subtype, "b") == 0);
  g_assert (gsk_http_content_get_mime_type (content, "/data/whatever.b", &type, &subtype));
  g_assert (strcmp (type, "x-application") == 0 && strcmp (subtype, "b2") == 0);
  g_assert (!gsk_http_content_get_mime_type (content, "foo", &type, &subtype));

  gsk_http_content_set_default_mime_type (content, "x-application", "x-unknown");
  g_assert (gsk_http_content_get_mime_type (content, "foo", &type, &subtype));
  g_assert (strcmp (type, "x-application") == 0 && strcmp (subtype, "x-unknown") == 0);

  test_get (content, "/hi/my/name/is/q.html", NULL, &response, &data);
  g_assert (strcmp (data, "this is q.html") == 0);
  g_assert (strcmp (GSK_HTTP_HEADER (response)->content_type, "text") == 0);
  g_assert (strcmp (GSK_HTTP_HEADER (response)->content_subtype, "html") == 0);
  g_object_unref (response);
  g_free (data);

  test_get (content, "/hi/my/name/prefix/whatever/whatever", NULL, &response, &data);
  g_assert (strcmp (data, "anything under ...my/name/prefix") == 0);
  g_object_unref (response);
  g_free (data);

  test_get (content, "/hi/my/name/prefix/whatever/whatever.a", NULL, &response, &data);
  g_assert (strcmp (data, "anything under ...my/name/prefix ... .a") == 0);
  g_assert (strcmp (GSK_HTTP_HEADER (response)->content_type, "x-application") == 0);
  g_assert (strcmp (GSK_HTTP_HEADER (response)->content_subtype, "a") == 0);
  g_object_unref (response);
  g_free (data);

  test_get (content, "/hi/my/name/prefix/whatever/whatever2.b", NULL, &response, &data);
  g_assert (strcmp (data, "anything under ...my/name/prefix ... .b") == 0);
  g_assert (strcmp (GSK_HTTP_HEADER (response)->content_type, "x-application") == 0);
  g_assert (strcmp (GSK_HTTP_HEADER (response)->content_subtype, "b") == 0);
  g_object_unref (response);
  g_free (data);

  test_get (content, "/data/foo.b", NULL, &response, &data);
  g_assert (strcmp (data, "some b2 data") == 0);
  g_assert (strcmp (GSK_HTTP_HEADER (response)->content_type, "x-application") == 0);
  g_assert (strcmp (GSK_HTTP_HEADER (response)->content_subtype, "b2") == 0);
  g_object_unref (response);
  g_free (data);

  return 0;
}
