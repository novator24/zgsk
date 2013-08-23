#include "../http/gskhttprequest.h"
#include "../http/gskhttpresponse.h"
#include "../gskinit.h"
#include <string.h>

GskHttpHeader *
header_copy_through_buffers (GskHttpHeader *header)
{
  gboolean is_request = GSK_IS_HTTP_REQUEST (header);
  GskBuffer buffer1, buffer2;
  gpointer data1, data2;
  gsize len1, len2;
  GskHttpHeader *header1, *header2;
  GError *error = NULL;

  gsk_buffer_construct (&buffer1);
  gsk_buffer_construct (&buffer2);

  gsk_http_header_to_buffer (header, &buffer1);
  len1 = buffer1.size;
  data1 = g_malloc (len1);
  gsk_buffer_peek (&buffer1, data1, len1);

  header1 = gsk_http_header_from_buffer (&buffer1, is_request, GSK_HTTP_PARSE_STRICT, &error);
  g_assert (header1);
  g_assert (buffer1.size == 0);

  gsk_http_header_to_buffer (header1, &buffer2);
  len2 = buffer2.size;
  data2 = g_malloc (len2);
  gsk_buffer_peek (&buffer2, data2, len2);

  header2 = gsk_http_header_from_buffer (&buffer2, is_request, GSK_HTTP_PARSE_STRICT, &error);
  g_assert (header2);
  g_assert (buffer2.size == 0);

  g_assert (len1 == len2);
  g_assert (memcmp (data1, data2, len1) == 0);
  g_object_unref (header1);
  g_free (data1);
  g_free (data2);
  return header2;
}

static GskHttpHeader *
maybe_header_from_string(gboolean is_request, const char *str)
{
  GskBuffer buffer;
  GskHttpHeader *header;
  gsk_buffer_construct (&buffer);
  gsk_buffer_append_foreign (&buffer, str, strlen(str), NULL, NULL);
  header = gsk_http_header_from_buffer (&buffer, is_request, 0, NULL);
  gsk_buffer_destruct (&buffer);
  return header;
}

static GskHttpHeader *
header_from_string(gboolean is_request, const char *str)
{
  GskHttpHeader *header = maybe_header_from_string(is_request,str);
  g_assert (header != NULL);
  return header;
}

static gboolean
headers_equal (GskHttpHeader *h1, GskHttpHeader *h2)
{
  if ( ( (GSK_IS_HTTP_REQUEST (h1) ? 1 : 0)
       ^ (GSK_IS_HTTP_REQUEST (h2) ? 1 : 0) ) != 0)
    return FALSE;

  return TRUE;
}


#define EMPTY_OR_NULL_TOKEN     ((const char *)1)
#define NOT_FOUND_TOKEN         ((const char *)2)


static void
corruption_test (gboolean is_request,
                 const char *header_text)
{
  guint len = strlen (header_text);
  guint test;
  for (test = 0; test < 100; test++)
    {
      guint n_changes = g_random_int_range (0, 100);
      char *txt = g_malloc (len + 1);
      GskBuffer buffer = GSK_BUFFER_STATIC_INIT;
      guint i;
      GskHttpHeader *header;
      strcpy (txt, header_text);
      for (i = 0; i < n_changes; i++)
        txt[g_random_int_range (0, len)] = g_random_int_range (0,256);
      gsk_buffer_append_foreign (&buffer, txt, len, g_free, txt);
      header = gsk_http_header_from_buffer (&buffer, is_request, 0, NULL);
      if (header)
        g_object_unref (header);
      gsk_buffer_destruct (&buffer);
    }
}

static void
test_cgi_parsing (const char *query_string,
                  gboolean    may_be_null,
                  const char *first_key,
                  ...)
{
  va_list args;
  GHashTable *table;
  char **list;

#define CGI_ASSERT_NO_KEY(cond)                                 \
  G_STMT_START{                                                 \
    if (!(cond))                                                \
      g_error ("error in %s, line %d (%s) (query-string='%s')", \
               __FILE__, __LINE__, #cond, query_string);        \
  }G_STMT_END
#define CGI_ASSERT(cond)                                        \
  G_STMT_START{                                                 \
    if (!(cond))                                                \
      g_error ("error in %s, line %d (%s) (query-string='%s', key='%s')", \
               __FILE__, __LINE__, #cond, query_string, key);   \
  }G_STMT_END

  /* --- Hash-table test --- */
  va_start (args, first_key);
  table = gsk_http_request_parse_cgi_query_string (query_string);
  CGI_ASSERT_NO_KEY (may_be_null || table != NULL);

  if (table != NULL)
    {
      const char *key;
      for (key = first_key; key != NULL; key = va_arg (args, const char *))
        {
          const char *value = va_arg (args, const char *);
          if (value == NOT_FOUND_TOKEN)
            CGI_ASSERT (!g_hash_table_lookup_extended (table, key, NULL, NULL));
          else if (value == EMPTY_OR_NULL_TOKEN)
            {
              const char *ht_value = g_hash_table_lookup (table, key);
              CGI_ASSERT (ht_value == NULL || ht_value[0] == '\0');
            }
          else if (value == NULL)
            CGI_ASSERT (g_hash_table_lookup (table, key) == NULL);
          else
            CGI_ASSERT (strcmp (g_hash_table_lookup (table, key), value) == 0);
        }
      g_hash_table_destroy (table);
    }
  va_end (args);


  /* --- list --- */
  va_start (args, first_key);
  list = gsk_http_parse_cgi_query_string (query_string, NULL);
  CGI_ASSERT_NO_KEY (may_be_null || list != NULL);
  if (list != NULL)
    {
      guint n_kv = g_strv_length (list);
      const char *key;
      g_assert (n_kv % 2 == 0);
      n_kv /= 2;
      for (key = first_key; key != NULL; key = va_arg (args, const char *))
        {
          const char *value = va_arg (args, const char *);
          guint index;
          char **at = list;
          for (index = 0; *at; index++, at += 2)
            if (strcmp (*at, key) == 0)
              break;
          if (value == NOT_FOUND_TOKEN)
            CGI_ASSERT (index == n_kv);
          else if (value == EMPTY_OR_NULL_TOKEN)
            {
              CGI_ASSERT (index == n_kv
                       || at[1] == NULL
                       || strcmp (at[1], "") == 0);
            }
          else if (value == NULL)
            CGI_ASSERT (index == n_kv || at[1] == NULL);
          else
            CGI_ASSERT (index < n_kv && strcmp (at[1], value) == 0);
        }
      g_strfreev (list);
    }
  va_end (args);

#undef CGI_ASSERT_NO_KEY
#undef CGI_ASSERT
}


int main(int argc, char **argv)
{
  GskHttpHeader *header0, *header1;
  GValue value;

  gsk_init_without_threads (&argc, &argv);

  header0 = header_from_string (TRUE,
				"GET / HTTP/1.0\r\n"
				"User-Agent: foo\r\n"
				"\n");
  header1 = header_copy_through_buffers (header0);
  memset (&value, 0, sizeof(value));
  g_value_init (&value, G_TYPE_UINT);
  g_object_get_property (G_OBJECT (header1), "major-version", &value);
  g_assert (g_value_get_uint (&value) == 1);
  g_object_get_property (G_OBJECT (header1), "minor-version", &value);
  g_assert (g_value_get_uint (&value) == 0);
  g_value_unset (&value);
  g_value_init (&value, G_TYPE_STRING);
  g_object_get_property (G_OBJECT (header1), "user-agent", &value);
  g_assert (strcmp (g_value_get_string (&value), "foo") == 0);
  g_object_get_property (G_OBJECT (header1), "referrer", &value);
  g_assert (g_value_get_string (&value) == NULL);
  g_value_unset (&value);
  g_value_init (&value, GSK_TYPE_HTTP_CONNECTION);
  g_object_get_property (G_OBJECT (header1), "connection", &value);
  g_assert (g_value_get_enum (&value) == GSK_HTTP_CONNECTION_NONE);
  g_value_unset (&value);
  g_assert (headers_equal (header0, header1));
  g_assert (header1->http_major_version == 1
         && header1->http_minor_version == 0);
  g_assert (gsk_http_header_get_connection (header1) == GSK_HTTP_CONNECTION_CLOSE);
  g_assert (gsk_http_header_get_connection_type (header1) == GSK_HTTP_CONNECTION_NONE);
  g_object_unref (header0);
  g_object_unref (header1);

  header0 = header_from_string (TRUE,
				"GET / HTTP/1.1\r\n"
				"Referer: http://whatever.com\r\n"
				"User-Agent: foo\r\n"
				"Transfer-Encoding: chunked\r\n"
				"\n");
  header1 = header_copy_through_buffers (header0);
  g_assert (headers_equal (header0, header1));
  g_assert (header1->http_major_version == 1
         && header1->http_minor_version == 1);

  g_value_init (&value, G_TYPE_UINT);
  g_object_get_property (G_OBJECT (header1), "major-version", &value);
  g_assert (g_value_get_uint (&value) == 1);
  g_object_get_property (G_OBJECT (header1), "minor-version", &value);
  g_assert (g_value_get_uint (&value) == 1);
  g_value_unset (&value);
  g_value_init (&value, G_TYPE_STRING);
  g_object_get_property (G_OBJECT (header1), "user-agent", &value);
  g_assert (strcmp (g_value_get_string (&value), "foo") == 0);
  g_object_get_property (G_OBJECT (header1), "referrer", &value);
  g_assert (strcmp (g_value_get_string (&value), "http://whatever.com") == 0);
  g_value_unset (&value);
  g_value_init (&value, GSK_TYPE_HTTP_CONNECTION);
  g_object_get_property (G_OBJECT (header1), "connection", &value);
  g_assert (g_value_get_enum (&value) == GSK_HTTP_CONNECTION_NONE);
  g_value_unset (&value);
  g_value_init (&value, GSK_TYPE_HTTP_TRANSFER_ENCODING);
  g_object_get_property (G_OBJECT (header1), "transfer-encoding", &value);
  g_assert (g_value_get_enum (&value) == GSK_HTTP_TRANSFER_ENCODING_CHUNKED);
  g_value_unset (&value);
  g_assert (gsk_http_header_get_connection (header1) == GSK_HTTP_CONNECTION_KEEPALIVE);
  g_assert (gsk_http_header_get_connection_type (header1) == GSK_HTTP_CONNECTION_NONE);
  g_object_unref (header0);
  g_object_unref (header1);

  {
    GskHttpRequest *request;
    header0 = header_from_string (TRUE,
				  "GET / HTTP/1.1\r\n"
				  "Accept-Language: swahili;q=4, cherokee;q=2, estonian\r\n"
				  "\n");
    header1 = header_copy_through_buffers (header0);
    g_assert (GSK_IS_HTTP_REQUEST (header0));
    g_assert (headers_equal (header0, header1));
    request = GSK_HTTP_REQUEST (header1);
    g_assert (request->accept_languages != NULL);
    g_assert (request->accept_languages->next != NULL);
    g_assert (request->accept_languages->next->next != NULL);
    g_assert (request->accept_languages->next->next->next == NULL);
    g_assert (strcmp (request->accept_languages->language, "swahili") == 0);
    g_assert (request->accept_languages->quality == 4);
    g_assert (strcmp (request->accept_languages->next->language, "cherokee") == 0);
    g_assert (request->accept_languages->next->quality == 2);
    g_assert (strcmp (request->accept_languages->next->next->language, "estonian") == 0);
    g_assert (request->accept_languages->next->next->quality == -1);
    g_object_unref (header0);
    g_object_unref (header1);
  }
  {
    GskHttpRequest *request;
    header0 = header_from_string (TRUE,
				  "GET / HTTP/1.1\r\n"
				  "Accept-Language: en-us\r\n"
				  "\n");
    header1 = header_copy_through_buffers (header0);
    g_assert (GSK_IS_HTTP_REQUEST (header0));
    g_assert (headers_equal (header0, header1));
    request = GSK_HTTP_REQUEST (header1);
    g_assert (request->accept_languages->quality == -1.0);
    g_assert (request->accept_languages != NULL);
    g_assert (request->accept_languages->next == NULL);
    g_assert (strcmp (request->accept_languages->language, "en-us") == 0);
    g_object_unref (header0);
    g_object_unref (header1);
  }
  {
    GskHttpRequest *request;
    header0 = header_from_string (TRUE,
				  "GET / HTTP/1.1\r\n"
				  "Accept-Language: en-us,en;q=0.5\r\n"
				  "\n");
    header1 = header_copy_through_buffers (header0);
    g_assert (GSK_IS_HTTP_REQUEST (header0));
    g_assert (headers_equal (header0, header1));
    request = GSK_HTTP_REQUEST (header1);
    g_assert (request->accept_languages != NULL);
    g_assert (request->accept_languages->quality == -1.0);
    g_assert (request->accept_languages->next != NULL);
    g_assert (request->accept_languages->next->quality == 0.5);
    g_assert (request->accept_languages->next->next == NULL);
    g_assert (strcmp (request->accept_languages->language, "en-us") == 0);
    g_object_unref (header0);
    g_object_unref (header1);
  }
  /* test cache-control in request */
 {
    GskHttpRequest *request;
    header0 = header_from_string (TRUE,
				  "GET / HTTP/1.1\r\n"
				  "Accept-Language: en-us\r\n"
                                  "Cache-Control: no-cache, no-store, "
                                  "no-transform, max-age=120, max-stale=120, "
                                  "min-fresh=120, only-if-cached\r\n"
				  "\n");
    header1 = header_copy_through_buffers (header0);
    g_assert (GSK_IS_HTTP_REQUEST (header0));
    g_assert (headers_equal (header0, header1));
    request = GSK_HTTP_REQUEST (header1);
    g_assert (request->accept_languages->quality == -1.0);
    g_assert (request->accept_languages != NULL);
    g_assert (request->accept_languages->next == NULL);
    g_assert (strcmp (request->accept_languages->language, "en-us") == 0);
    g_object_unref (header0);
    g_object_unref (header1);
  }

  {
    guint iter;
    header0 = header_from_string (FALSE,
            "HTTP/1.0 200 OK\r\n"
            "Cache-Control: private=private-field, no-cache=no-cache-field, "
            "no-store, no-transform, must-revalidate, proxy-revalidate, "
            "max-age=120, s-maxage=120\r\n"
            "Content-Type: text/html\r\n"
            "Set-Cookie: PREF=ID=2c9b2e3669d1d5eb:TM=1110491972:"
            "LM=1110491972:S=JiXMvg60fPhnf8Ow; expires=Sun, 17-Jan-2038 "
            "19:14:07 GMT; path=/; domain=.google.com\r\n"
            "Server: GWS/2.1\r\n"
            "Date: Thu, 10 Mar 2005 21:59:32 GMT\r\n"
            "Connection: Close\r\n"
            "\n");
    header1 = header_copy_through_buffers (header0);
    for (iter = 0; iter < 2; iter++)
      {
        GskHttpHeader *header = iter ? header1 : header0;
        GskHttpResponse *response = GSK_HTTP_RESPONSE (header);
        GskHttpCookie *set_cookie;
        g_assert (GSK_IS_HTTP_RESPONSE (header));
        g_assert (header->http_major_version == 1);
        g_assert (header->http_minor_version == 0);
        g_assert (header->connection_type == GSK_HTTP_CONNECTION_CLOSE);
        g_assert (strcmp (header->content_type, "text") == 0);
        g_assert (strcmp (header->content_subtype, "html") == 0);
        //g_assert (header->has_content_body);
        g_assert (response->allowed_verbs == 0);

        g_assert (response->cache_control != NULL);
        g_assert (response->cache_control->is_private);
        g_assert (!response->cache_control->is_public);
        g_assert (NULL != response->cache_control->private_name);
        g_assert (strcmp (response->cache_control->private_name, "private-field") == 0);
        g_assert (NULL != response->cache_control->no_cache_name);
        g_assert (strcmp (response->cache_control->no_cache_name, "no-cache-field") == 0);
        g_assert (response->cache_control->no_cache);
        g_assert (response->cache_control->no_store);
        g_assert (response->cache_control->no_transform);
        g_assert (response->cache_control->must_revalidate);
        g_assert (response->cache_control->proxy_revalidate);
        g_assert (response->cache_control->max_age == 120);
        g_assert (response->cache_control->s_max_age == 120);

        g_assert (g_slist_length (response->set_cookies) == 1);
        set_cookie = response->set_cookies->data;
        g_assert (strcmp (set_cookie->key, "PREF") == 0);
        g_assert (strcmp (set_cookie->value, "ID=2c9b2e3669d1d5eb:TM=1110491972:LM=1110491972:S=JiXMvg60fPhnf8Ow") == 0);
        g_assert (strcmp (set_cookie->expire_date, "Sun, 17-Jan-2038 19:14:07 GMT") == 0);
        g_assert (strcmp (set_cookie->path, "/") == 0);
        g_assert (strcmp (set_cookie->domain, ".google.com") == 0);
        g_assert (set_cookie->comment == NULL);
        g_assert (set_cookie->max_age == -1);
      }
    g_object_unref (header0);
    g_object_unref (header1);
  }

  {
    guint iter;
    header0 = header_from_string (FALSE,
            "HTTP/1.0 200 OK\r\n"
            "Cache-Control: private=private-field, no-cache=no-cache-field, "
            "no-store, no-transform, must-revalidate, proxy-revalidate, "
            "max-age=120, s-maxage=120\r\n"
            "Content-Type: text/html\r\n"
            "Set-Cookie: PREF=ID=2c9b2e3669d1d5eb:TM=1110491972:"
            "LM=1110491972:S=JiXMvg60fPhnf8Ow; expires=Sun, 17-Jan-2038 "
            "19:14:07 GMT; path=/abcd/efgh/ijkl/mnop; domain=.google.com\r\n"
            "Server: GWS/2.1\r\n"
            "Date: Thu, 10 Mar 2005 21:59:32 GMT\r\n"
            "Connection: Close\r\n"
            "\n");
    header1 = header_copy_through_buffers (header0);
    for (iter = 0; iter < 2; iter++)
      {
        GskHttpHeader *header = iter ? header1 : header0;
        GskHttpResponse *response = GSK_HTTP_RESPONSE (header);
        GskHttpCookie *set_cookie;
        g_assert (GSK_IS_HTTP_RESPONSE (header));
        g_assert (header->http_major_version == 1);
        g_assert (header->http_minor_version == 0);
        g_assert (header->connection_type == GSK_HTTP_CONNECTION_CLOSE);
        g_assert (strcmp (header->content_type, "text") == 0);
        g_assert (strcmp (header->content_subtype, "html") == 0);
        //g_assert (header->has_content_body);
        g_assert (response->allowed_verbs == 0);

        g_assert (response->cache_control != NULL);
        g_assert (response->cache_control->is_private);
        g_assert (!response->cache_control->is_public);
        g_assert (NULL != response->cache_control->private_name);
        g_assert (strcmp (response->cache_control->private_name, "private-field") == 0);
        g_assert (NULL != response->cache_control->no_cache_name);
        g_assert (strcmp (response->cache_control->no_cache_name, "no-cache-field") == 0);
        g_assert (response->cache_control->no_cache);
        g_assert (response->cache_control->no_store);
        g_assert (response->cache_control->no_transform);
        g_assert (response->cache_control->must_revalidate);
        g_assert (response->cache_control->proxy_revalidate);
        g_assert (response->cache_control->max_age == 120);
        g_assert (response->cache_control->s_max_age == 120);

        g_assert (g_slist_length (response->set_cookies) == 1);
        set_cookie = response->set_cookies->data;
        g_assert (strcmp (set_cookie->key, "PREF") == 0);
        g_assert (strcmp (set_cookie->value, "ID=2c9b2e3669d1d5eb:TM=1110491972:LM=1110491972:S=JiXMvg60fPhnf8Ow") == 0);
        g_assert (strcmp (set_cookie->expire_date, "Sun, 17-Jan-2038 19:14:07 GMT") == 0);
        g_assert (strcmp (set_cookie->path, "/abcd/efgh/ijkl/mnop") == 0);
        g_assert (strcmp (set_cookie->domain, ".google.com") == 0);
        g_assert (set_cookie->comment == NULL);
        g_assert (set_cookie->max_age == -1);
      }
    g_object_unref (header0);
    g_object_unref (header1);
  }

  {
    guint iter;
    header0 = header_from_string (FALSE,
            "HTTP/1.0 200 OK\r\n"
            "Cache-Control: private=private-field, no-cache=no-cache-field, "
            "no-store, no-transform, must-revalidate, proxy-revalidate, "
            "max-age=120, s-maxage=120\r\n"
            "Content-Type: text/html\r\n"
            "Set-Cookie: PREF=ID=2c9b2e3669d1d5eb:TM=1110491972:"
            "LM=1110491972:S=JiXMvg60fPhnf8Ow; expires=Sun, 17-Jan-2038 "
            "19:14:07 GMT; domain=.google.com\r\n"
            "Server: GWS/2.1\r\n"
            "Date: Thu, 10 Mar 2005 21:59:32 GMT\r\n"
            "Connection: Close\r\n"
            "\n");
    header1 = header_copy_through_buffers (header0);
    for (iter = 0; iter < 2; iter++)
      {
        GskHttpHeader *header = iter ? header1 : header0;
        GskHttpResponse *response = GSK_HTTP_RESPONSE (header);
        GskHttpCookie *set_cookie;
        g_assert (GSK_IS_HTTP_RESPONSE (header));
        g_assert (header->http_major_version == 1);
        g_assert (header->http_minor_version == 0);
        g_assert (header->connection_type == GSK_HTTP_CONNECTION_CLOSE);
        g_assert (strcmp (header->content_type, "text") == 0);
        g_assert (strcmp (header->content_subtype, "html") == 0);
        //g_assert (header->has_content_body);
        g_assert (response->allowed_verbs == 0);

        g_assert (response->cache_control != NULL);
        g_assert (response->cache_control->is_private);
        g_assert (!response->cache_control->is_public);
        g_assert (NULL != response->cache_control->private_name);
        g_assert (strcmp (response->cache_control->private_name, "private-field") == 0);
        g_assert (NULL != response->cache_control->no_cache_name);
        g_assert (strcmp (response->cache_control->no_cache_name, "no-cache-field") == 0);
        g_assert (response->cache_control->no_cache);
        g_assert (response->cache_control->no_store);
        g_assert (response->cache_control->no_transform);
        g_assert (response->cache_control->must_revalidate);
        g_assert (response->cache_control->proxy_revalidate);
        g_assert (response->cache_control->max_age == 120);
        g_assert (response->cache_control->s_max_age == 120);

        g_assert (g_slist_length (response->set_cookies) == 1);
        set_cookie = response->set_cookies->data;
        g_assert (strcmp (set_cookie->key, "PREF") == 0);
        g_assert (strcmp (set_cookie->value, "ID=2c9b2e3669d1d5eb:TM=1110491972:LM=1110491972:S=JiXMvg60fPhnf8Ow") == 0);
        g_assert (strcmp (set_cookie->expire_date, "Sun, 17-Jan-2038 19:14:07 GMT") == 0);
        g_assert (set_cookie->path == NULL);
        g_assert (strcmp (set_cookie->domain, ".google.com") == 0);
        g_assert (set_cookie->comment == NULL);
        g_assert (set_cookie->max_age == -1);
      }
    g_object_unref (header0);
    g_object_unref (header1);
  }

  /* Check very long paths in a cookie */
  {
    guint iter;
    header0 = header_from_string (FALSE,
            "HTTP/1.0 200 OK\r\n"
            "Cache-Control: private=private-field, no-cache=no-cache-field, "
            "no-store, no-transform, must-revalidate, proxy-revalidate, "
            "max-age=120, s-maxage=120\r\n"
            "Content-Type: text/html\r\n"
            "Set-Cookie: PREF=ID=2c9b2e3669d1d5eb:TM=1110491972:"
            "LM=1110491972:S=JiXMvg60fPhnf8Ow; expires=Sun, 17-Jan-2038 "
            "19:14:07 GMT; path=/abcd/efgh/ijkl/mnop/qrst/uvwx/yz01/2345; domain=.google.com\r\n"
            "Server: GWS/2.1\r\n"
            "Date: Thu, 10 Mar 2005 21:59:32 GMT\r\n"
            "Connection: Close\r\n"
            "\n");
    header1 = header_copy_through_buffers (header0);
    for (iter = 0; iter < 2; iter++)
      {
        GskHttpHeader *header = iter ? header1 : header0;
        GskHttpResponse *response = GSK_HTTP_RESPONSE (header);
        GskHttpCookie *set_cookie;
        g_assert (GSK_IS_HTTP_RESPONSE (header));
        g_assert (header->http_major_version == 1);
        g_assert (header->http_minor_version == 0);
        g_assert (header->connection_type == GSK_HTTP_CONNECTION_CLOSE);
        g_assert (strcmp (header->content_type, "text") == 0);
        g_assert (strcmp (header->content_subtype, "html") == 0);
        //g_assert (header->has_content_body);
        g_assert (response->allowed_verbs == 0);

        g_assert (response->cache_control != NULL);
        g_assert (response->cache_control->is_private);
        g_assert (!response->cache_control->is_public);
        g_assert (NULL != response->cache_control->private_name);
        g_assert (strcmp (response->cache_control->private_name, "private-field") == 0);
        g_assert (NULL != response->cache_control->no_cache_name);
        g_assert (strcmp (response->cache_control->no_cache_name, "no-cache-field") == 0);
        g_assert (response->cache_control->no_cache);
        g_assert (response->cache_control->no_store);
        g_assert (response->cache_control->no_transform);
        g_assert (response->cache_control->must_revalidate);
        g_assert (response->cache_control->proxy_revalidate);
        g_assert (response->cache_control->max_age == 120);
        g_assert (response->cache_control->s_max_age == 120);

        g_assert (g_slist_length (response->set_cookies) == 1);
        set_cookie = response->set_cookies->data;
        g_assert (strcmp (set_cookie->key, "PREF") == 0);
        g_assert (strcmp (set_cookie->value, "ID=2c9b2e3669d1d5eb:TM=1110491972:LM=1110491972:S=JiXMvg60fPhnf8Ow") == 0);
        g_assert (strcmp (set_cookie->expire_date, "Sun, 17-Jan-2038 19:14:07 GMT") == 0);
        g_assert (strcmp (set_cookie->path,
                          "/abcd/efgh/ijkl/mnop/qrst/uvwx/yz01/2345")
                  == 0);
        g_assert (strcmp (set_cookie->domain, ".google.com") == 0);
        g_assert (set_cookie->comment == NULL);
        g_assert (set_cookie->max_age == -1);
      }
    g_object_unref (header0);
    g_object_unref (header1);
  }

  /* Set multiple cookies */
  {
    guint iter;
    gchar *buf;
    guint i;

    gchar *cookie_key[] =
      {
        "PREF1",
        "PREF2",
        "PREF3"
      };

    gchar *cookie_val[] =
      {
        "ID=2c9b2e3669d1d5eb:TM=1110491972:LM=1110491972:S=JiXMvg60fPhnf8Ow",
        "ID=1b8a2d2558c0c4da:TM=1110491972:LM=1110491972:S=JiXMvg60fPhnf8Ow",
        "ID=0a791c1447bfb3c9:TM=1110491972:LM=1110491972:S=JiXMvg60fPhnf8Ow"
      };

    gchar *cookie_expire[] =
      {
        "Sun, 17-Jan-2038 19:14:07 GMT",
        "Sun, 17-Jan-2038 19:14:08 GMT",
        "Sun, 17-Jan-2038 19:14:09 GMT"
      };

    gchar *cookie_path[] =
      {
        "/abcd/efgh/ijkl",
        "/efgh/ijkl/mnop",
        "/ijkl/mnop/qrst"
      };

    buf = g_strdup_printf
      ("HTTP/1.0 200 OK\r\n"
       "Cache-Control: private=private-field, no-cache=no-cache-field, "
       "no-store, no-transform, must-revalidate, proxy-revalidate, "
       "max-age=120, s-maxage=120\r\n"
       "Content-Type: text/html\r\n"
       "Set-Cookie: %s=%s; expires=%s; path=%s; domain=.google.com\r\n"
       "Set-Cookie: %s=%s; expires=%s; path=%s; domain=.google.com\r\n"
       "Set-Cookie: %s=%s; expires=%s; path=%s; domain=.google.com\r\n"
       "Server: GWS/2.1\r\n"
       "Date: Thu, 10 Mar 2005 21:59:32 GMT\r\n"
       "Connection: Close\r\n"
       "\n",
       cookie_key[0], cookie_val[0], cookie_expire[0], cookie_path[0],
       cookie_key[1], cookie_val[1], cookie_expire[1], cookie_path[1],
       cookie_key[2], cookie_val[2], cookie_expire[2], cookie_path[2]);
    
    header0 = header_from_string (FALSE, buf);
    header1 = header_copy_through_buffers (header0);
    for (iter = 0; iter < 2; iter++)
      {
        GskHttpHeader *header = iter ? header1 : header0;
        GskHttpResponse *response = GSK_HTTP_RESPONSE (header);
        GskHttpCookie *set_cookie;
        g_assert (GSK_IS_HTTP_RESPONSE (header));
        g_assert (header->http_major_version == 1);
        g_assert (header->http_minor_version == 0);
        g_assert (header->connection_type == GSK_HTTP_CONNECTION_CLOSE);
        g_assert (strcmp (header->content_type, "text") == 0);
        g_assert (strcmp (header->content_subtype, "html") == 0);
        //g_assert (header->has_content_body);
        g_assert (response->allowed_verbs == 0);

        g_assert (response->cache_control != NULL);
        g_assert (response->cache_control->is_private);
        g_assert (!response->cache_control->is_public);
        g_assert (NULL != response->cache_control->private_name);
        g_assert (strcmp (response->cache_control->private_name, "private-field") == 0);
        g_assert (NULL != response->cache_control->no_cache_name);
        g_assert (strcmp (response->cache_control->no_cache_name, "no-cache-field") == 0);
        g_assert (response->cache_control->no_cache);
        g_assert (response->cache_control->no_store);
        g_assert (response->cache_control->no_transform);
        g_assert (response->cache_control->must_revalidate);
        g_assert (response->cache_control->proxy_revalidate);
        g_assert (response->cache_control->max_age == 120);
        g_assert (response->cache_control->s_max_age == 120);

        g_assert (g_slist_length (response->set_cookies) == 3);
        for (i = 0; i < g_slist_length (response->set_cookies); ++i)
          {
            set_cookie = g_slist_nth(response->set_cookies, i)->data;
            g_assert (strcmp (set_cookie->key, cookie_key[i]) == 0);
            g_assert (strcmp (set_cookie->value, cookie_val[i]) == 0);
            g_assert (strcmp (set_cookie->expire_date, cookie_expire[i]) == 0);
            g_assert (strcmp (set_cookie->path, cookie_path[i]) == 0);
            g_assert (strcmp (set_cookie->domain, ".google.com") == 0);
            g_assert (set_cookie->comment == NULL);
            g_assert (set_cookie->max_age == -1);
          }
      }
    g_object_unref (header0);
    g_object_unref (header1);
  }

  /* random parsing bug */
  {
    header0 = maybe_header_from_string (FALSE,
            "HTTP/1.0 200 OK\r\n"
            "Accept-Ranges: bytes\r\n"
            /*                               v- malformed response.. doesn't matter if we handle it */
            "Content-Type: text/html; charset!UTF-8\r\n");
    if (header0)
      g_object_unref (header0);
  }

  {
    guint iter;
    header0 = header_from_string (FALSE,
            "HTTP/1.0 200 OK\r\n"
            "Accept-Ranges: bytes\r\n"
            "Vary: *\r\n"
            "P3P: policyref=\"http://www.lycos.com/w3c/p3p.xml\", CP=\"IDC DSP COR CURa ADMa DEVa CUSa PSAa IVAa CONo OUR IND UNI STA\"\r\n"
            "Cache-Expires: Thu, 24 Mar 2005 13:11:58 GMT\r\n"
            "Connection: Close\r\n"
            "PICS-Label: (PICS-1.0 \"http://www.rsac.org/ratingsv01.html\" l on \"2003.09.02T11:37-0700\" exp \"2004.09.02T12:00-0700\" r (v 0 s 0 n 0 l 0))\r\n"
            "X-Powered-By: ASP.NET\r\n"
            "Pragma: no-cache\r\n"
            "X-Cache: HIT from eserver.org\r\n"
            "\n");
    header1 = header_copy_through_buffers (header0);
    for (iter = 0; iter < 2; iter++)
      {
        GskHttpHeader *header = iter ? header1 : header0;
        GskHttpResponse *response = GSK_HTTP_RESPONSE (header);
        g_assert (GSK_IS_HTTP_RESPONSE (header));
        g_assert (header->http_major_version == 1);
        g_assert (header->http_minor_version == 0);
        g_assert (header->connection_type == GSK_HTTP_CONNECTION_CLOSE);
        //g_assert (header->has_content_body);
        g_assert (response->status_code == 200);
        g_assert (g_slist_length (header->pragmas) == 1);
        g_assert (strcmp ((char *)(header->pragmas->data), "no-cache") == 0);
        g_assert (gsk_http_header_lookup_misc (header, "X-Cache") != NULL);
        g_assert (gsk_http_header_lookup_misc (header, "X-Powered-By") != NULL);
        g_assert (strcmp (gsk_http_header_lookup_misc (header, "X-Cache"), "HIT from eserver.org") == 0);
        g_assert (strcmp (gsk_http_header_lookup_misc (header, "X-Powered-By"), "ASP.NET") == 0);
        g_assert (gsk_http_header_lookup_misc (header, "PICS-Label") != NULL);
      }
    g_object_unref (header0);
    g_object_unref (header1);
  }

  test_cgi_parsing ("/whaever?a=1&b=42", FALSE,
                    "a", "1",
                    "b", "42",
                    "c", NOT_FOUND_TOKEN,
                    NULL);
  test_cgi_parsing ("/whaever?a=1&b=42&c", TRUE,
                    "a", "1",
                    "b", "42",
                    "c", EMPTY_OR_NULL_TOKEN,
                    "d", NOT_FOUND_TOKEN,
                    NULL);
  test_cgi_parsing ("/whaever?a=1&&b=42", TRUE,
                    "a", "1",
                    //"b", "42",
                    "d", NOT_FOUND_TOKEN,
                    NULL);
  test_cgi_parsing ("/whaever?a=1&2&b=42", TRUE,
                    "a", "1",
                    NULL);
  test_cgi_parsing ("/account-query.php?sk=6666666&&ref=http%3A%2F%2Fwww.cardiowave.net%2F&proxy_hash=sk|ref", TRUE,
                    "sk", "6666666",
                    "ref", "http://www.cardiowave.net/",
                    "proxy_hash", "sk|ref",
                    NULL);

  /* example from rfc 2617, page 5 */
  {
    GskHttpResponse *res = GSK_HTTP_RESPONSE
      (header_from_string (FALSE,
                           "HTTP/1.0 401 Unauthorized\r\n"
                           "WWW-Authenticate: Basic realm=\"WallyWorld\"\r\n"
                           "\n"));
    g_assert (res);
    g_assert (res->authenticate != NULL);
    g_assert (res->authenticate->mode == GSK_HTTP_AUTH_MODE_BASIC);
    g_assert (strcmp (res->authenticate->realm, "WallyWorld") == 0);
    g_object_unref (res);
  }
  /* example from rfc 2617, page 6 */
  {
    GskHttpRequest *req = GSK_HTTP_REQUEST
      (header_from_string (TRUE,
                           "GET / HTTP/1.1\r\n"
                           "Authorization: Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==\r\n"
                           "\n"));
    g_assert (req);
    g_assert (req->authorization != NULL);
    g_assert (req->authorization->mode == GSK_HTTP_AUTH_MODE_BASIC);
    g_assert (strcmp (req->authorization->info.basic.user, "Aladdin") == 0);
    g_assert (strcmp (req->authorization->info.basic.password, "open sesame") == 0);
    g_object_unref (req);
  }



  corruption_test (TRUE,
                   "GET /foo.txt HTTP/1.0\r\n"
                   "Host: foo.com\r\n"
                   "\n");
  corruption_test (TRUE,
                   "GET /foo.txt HTTP/1.0\r\n"
                   "Host: foo.com\r\n"
                   "Accept-Ranges: bytes\r\n"
                   "Content-length: 1231\r\n"
                   "Transfer-Encoding: chunked\r\n"
                   "Connection: close\r\n"
                   "Pragma: dfasdfasdsadfasd\r\n"
                   "Max-forwards: 10\r\n"
                   "Keep-alive: 3\r\n"
  //DATE_LINE_PARSER ("if-modified-since", GskHttpRequest, if_modified_since),
  //DATE_LINE_PARSER ("date", GskHttpRequest, date),
                   "From: hi@mom.com\r\n"
                   "UA-Pixels: 320x200\r\n"
                   "Cookie: Cookie: $Version=\"1\"; Customer=\"WILE_E_COYOTE\"; $Path=\"/acme\"\r\n"
                   "Accept-Charset: iso-8859-5, unicode-1-1;q=0.8\r\n"
                   "Accept-Encoding: compress;q=0.5, gzip;q=1.0\r\n"
                   "TE: \r\n"
                   "Cache-Control: private, community=\"UCI\"\r\n"
                   "\n");
  corruption_test (FALSE,
                   "HTTP/1.0 200 OK\r\n"
                   "Last-Modified: Thu, 11 Mar 2004 23:31:11 GMT\r\n"
                   "ETag: \"220062-35b-4050f6bf\"\r\n"
                   "Accept-Ranges: bytes\r\n"
                   "Content-length: 1231\r\n"
                   "Content-Type:  text/html; charset=iso-8859-1\r\n"
                   "Connection: close\r\n"
                   "\n");

  return 0;
}
