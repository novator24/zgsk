#include "../url/gskurl.h"
#include "../gskinit.h"
#include <string.h>

int
main (int argc, char **argv)
{
  GskUrl *url, *url0, *url1, *url2;
  char *str;
  GValue value;
  GError *error =NULL;

  gsk_init_without_threads (&argc, &argv);

  g_printerr ("Testing URL basics... ");
  url = gsk_url_new ("http://wigwam.whatever/path", &error);
  if (url == NULL)
    g_error ("error parsing url: %s", error->message);
  g_assert (url->scheme == GSK_URL_SCHEME_HTTP);
  g_assert (strcmp (url->host, "wigwam.whatever") == 0);
  g_assert (strcmp (url->path, "/path") == 0);
  memset(&value,0,sizeof(value));
  g_value_init(&value,G_TYPE_STRING);
  g_object_get_property (G_OBJECT (url), "host", &value);
  g_assert (strcmp (g_value_get_string (&value), "wigwam.whatever") == 0);
  g_object_get_property (G_OBJECT (url), "password", &value);
  g_assert (g_value_get_string (&value) == NULL);
  g_object_get_property (G_OBJECT (url), "path", &value);
  g_assert (strcmp (g_value_get_string (&value), "/path") == 0);
  g_object_get_property (G_OBJECT (url), "user_name", &value);
  g_assert (g_value_get_string (&value) == NULL);
  g_object_get_property (G_OBJECT (url), "query", &value);
  g_assert (g_value_get_string (&value) == NULL);
  g_value_unset(&value);
  g_value_init(&value,G_TYPE_UINT);
  g_object_get_property (G_OBJECT (url), "port", &value);
  g_assert (g_value_get_uint (&value) == 80);
  g_value_unset(&value);
  str = gsk_url_to_string (url);
  g_assert (strcmp (str, "http://wigwam.whatever/path") == 0);
  g_free (str);
  g_object_unref (url);

  url = gsk_url_new ("file:///yes/this/might/be/a/path", &error);
  if (url == NULL)
    g_error ("error parsing url: %s", error->message);
  g_assert (url->scheme == GSK_URL_SCHEME_FILE);
  g_assert (url->host == NULL);
  g_assert (strcmp (url->path, "/yes/this/might/be/a/path") == 0);
  g_object_unref (url);

  url = gsk_url_new ("http://daveb@host.com/whatever", &error);
  g_assert (url != NULL);
  g_assert (url->scheme == GSK_URL_SCHEME_HTTP);
  g_assert (strcmp (url->user_name, "daveb") == 0);
  g_assert (strcmp (url->host, "host.com") == 0);
  g_assert (strcmp (url->path, "/whatever") == 0);
  g_object_unref (url);

  url = gsk_url_new ("http://daveb:passwd@host.com/whatever2", &error);
  g_assert (url != NULL);
  g_assert (url->scheme == GSK_URL_SCHEME_HTTP);
  g_assert (strcmp (url->user_name, "daveb") == 0);
  g_assert (strcmp (url->password, "passwd") == 0);
  g_assert (strcmp (url->host, "host.com") == 0);
  g_assert (strcmp (url->path, "/whatever2") == 0);
  g_object_unref (url);

  g_printerr ("good.\n");

  g_printerr ("Testing URL new-relative error case... ");
  url0 = gsk_url_new ("http://www.time.com/time/time100/index.html", &error);
  g_assert (url0 != NULL);
  g_assert (strcmp (url0->host, "www.time.com") == 0);
  g_assert (strcmp (url0->path, "/time/time100/index.html") == 0);
  url1 = gsk_url_new_relative (url0, "https://secure.customersvc.com/servlet/Show?WESPAGE=td/home.html&MSRSM\nAG=TD", &error);
  if (url1 == NULL)
    {
      /* ok */
    }
  else
    {
      g_assert (strchr (url1->host, '\n') == NULL);
      g_assert (strchr (url1->path, '\n') == NULL);
      g_assert (strchr (url1->query, '\n') == NULL);
      g_object_unref (url1);
    }
  g_clear_error (&error);
  g_object_unref (url0);

  url0 = gsk_url_new ("http://www.ecookbooks.com/robots.txt", &error);
  g_assert (strcmp (url0->host, "www.ecookbooks.com") == 0);
  g_assert (strcmp (url0->path, "/robots.txt") == 0);
  url1 = gsk_url_new_relative (url0, "chefs.html?#urlparam#", &error);
  if (url1)
   {
     g_assert (url1->fragment != NULL);
     g_object_unref (url1);
   }
  else
   g_clear_error (&error);

  url0 = gsk_url_new ("http://censored.com/index.aspx", &error);
  g_assert (url0 != NULL);
  url1 = gsk_url_new_relative (url0, "Article.aspx?ArticleId=35", &error);
  g_assert (url1 != NULL);
  url2 = gsk_url_new_relative (url1, "Article.aspx?ArticleId=6", &error);
  g_assert (url2 != NULL);
  g_assert (strcmp (url0->host, "censored.com") == 0);
  g_assert (strcmp (url0->path, "/index.aspx") == 0);
  g_assert (url0->query == NULL);
  g_assert (url0->fragment == NULL);
  g_assert (strcmp (url1->host, "censored.com") == 0);
  g_assert (strcmp (url1->path, "/Article.aspx") == 0);
  g_assert (strcmp (url1->query, "ArticleId=35") == 0);
  g_assert (url1->fragment == NULL);
  g_assert (strcmp (url2->host, "censored.com") == 0);
  g_assert (strcmp (url2->path, "/Article.aspx") == 0);
  g_assert (strcmp (url2->query, "ArticleId=6") == 0);
  g_assert (url2->fragment == NULL);
  g_object_unref (url0);
  g_object_unref (url1);
  g_object_unref (url2);



  g_printerr ("done.\n");

#define _TEST_ENCDEC(test, test_encoded, suffix)					\
  G_STMT_START{								\
    const char *orig = test;						\
    char *encoded = gsk_url_encode##suffix (orig);			\
    char *decoded = gsk_url_decode##suffix (encoded);			\
    /*g_message ("orig='%s'; encoded='%s', re-decoded='%s'",orig,encoded,decoded);*/\
    g_assert (strcmp (orig, decoded) == 0);				\
    if (test_encoded) g_assert (strcmp (encoded, test_encoded) == 0);	\
    g_free(encoded); g_free(decoded);					\
  }G_STMT_END
#define TEST_ENCDEC_HTTP(test, test_encoded)				\
  _TEST_ENCDEC(test, test_encoded, _http)
#define TEST_ENCDEC(test, test_encoded)					\
  _TEST_ENCDEC(test, test_encoded, )


  g_printerr ("Testing encoding/decoding... ");
  TEST_ENCDEC_HTTP("a :b", "a+%3ab");
  TEST_ENCDEC_HTTP("hello, bob & mary j", NULL);
  g_printerr ("good.\n");

#undef _TEST_ENCDEC
#undef TEST_ENCDEC
#undef TEST_ENCDEC_HTTP

  return 0;
}


