#include <stdlib.h>
#include <string.h>
#include "../gsk.h"
#include "../http/gskhttpcontent.h"

static void
usage (void)
{
  g_printerr ("gsk-webserver OPTIONS\n\n"
              "OPTIONS include:\n");
  g_printerr ("  --bind-tcp=PORT\n");
  g_printerr ("  --mime PREFIX*SUFFIX TYPE/SUBTYPE\n");
  g_printerr ("  --default-mime TYPE/SUBTYPE\n");
  g_printerr ("  --location FS_PATH URI_PATH\n");
  exit (1);
}

int main(int argc, char **argv)
{
  guint i;
  GskHttpContent *content;
  GError *error = NULL;
  gsk_init_without_threads (&argc, &argv);
  content = gsk_http_content_new ();
  for (i = 1; i < (guint)argc; i++)
    {
      if (g_str_has_prefix (argv[i], "--bind-tcp="))
        {
	  guint port = atoi (strchr (argv[i], '=') + 1);
          GskSocketAddress *addr;
          addr = gsk_socket_address_ipv4_new (gsk_ipv4_ip_address_any, port);
          if (!gsk_http_content_listen (content, addr, &error))
            g_error ("error binding: %s", error->message);
          g_object_unref (addr);
        }
      else if (strcmp (argv[i], "--mime") == 0)
        {
          const char *pattern = argv[++i];
          const char *type_subtype = argv[++i];
          char *prefix, *suffix, *type, *subtype;
          if (pattern == NULL || type_subtype == NULL)
            g_error ("--mime takes two argument: pattern type/subtype");
          if (strchr (pattern, '*') == NULL)
            g_error ("mime pattern needs a '*'");
          if (strchr (strchr (pattern, '*') + 1, '*') != NULL)
            g_error ("mime pattern may not contain more than one '*'");
          if (strchr (type_subtype, '/') == NULL)
            g_error ("missing '/' in type/subtype");
          prefix = g_strndup (pattern, strchr (pattern, '*') - pattern);
          suffix = g_strdup (strchr (pattern, '*') + 1);
          type = g_strndup (type_subtype, strchr (type_subtype, '/') - type_subtype);
          subtype = g_strdup (strchr (type_subtype, '/') + 1);
          gsk_http_content_set_mime_type (content, prefix, suffix, type, subtype);
          g_free (prefix);
          g_free (suffix);
          g_free (type);
          g_free (subtype);
        }
      else if (strcmp (argv[i], "--default-mime") == 0)
        {
          const char *type_subtype = argv[++i];
          char *type, *subtype;
          if (type_subtype == NULL)
            g_error ("--default-mime takes two argument: pattern type/subtype");
          if (strchr (type_subtype, '/') == NULL)
            g_error ("missing '/' in type/subtype");
          type = g_strndup (type_subtype, strchr (type_subtype, '/') - type_subtype);
          subtype = g_strdup (strchr (type_subtype, '/') + 1);
          gsk_http_content_set_default_mime_type (content, type, subtype);
          g_free (type);
          g_free (subtype);
        }
      else if (strcmp (argv[i], "--location") == 0)
        {
          const char *fs_path = argv[++i];
          const char *url_path = argv[++i];
          if (fs_path == NULL || url_path == NULL)
            g_error ("--location take two arguments: fs_path url_path");
          gsk_http_content_add_file (content, url_path, fs_path,
                                     GSK_HTTP_CONTENT_FILE_DIR_TREE);
        }
      else
        {
          usage ();
        }
    }

  return gsk_main_run ();
}
