#include "textnode.h"
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

TextNodeParser *text_node_parser_new     (const char     *filename)
{
  TextNodeParser *parser;
  int fd = open (filename, O_RDONLY);
  if (fd < 0)
    {
      g_warning ("file not found: %s", g_strerror (errno));
      return NULL;
    }
  parser = g_new (TextNodeParser, 1);
  parser->fp_stack = g_slist_prepend (NULL, fdopen (fd, "r"));
  return parser;
}

typedef struct _Nesting Nesting;
struct _Nesting
{
  TextNode *list;
  Nesting *up;
};

static void
g_string_chomp_one (GString *str)
{
  if (str->len > 0)
    if (isspace (str->str[str->len - 1]))
      str->str[str->len - 1] = 0;
}

/* XXX: these gslist_append's are n^2!!! */
TextNode *
text_node_parser_parse   (TextNodeParser *parser)
{
  Nesting *nesting = g_new (Nesting, 1);
  TextNode *node;
  GString *id_string = NULL;
  nesting->up = NULL;
  node = nesting->list = g_new (TextNode, 1);
  nesting->list->info.list = NULL;
  nesting->list->is_list = TRUE;

  while (parser->fp_stack != NULL)
    {
      int c = fgetc (parser->fp_stack->data);
      if (c < 0)
	{
	  fclose (parser->fp_stack->data);
	  g_slist_remove (parser->fp_stack, parser->fp_stack->data);
	  continue;
	}
      if ((c == '{' || c == '}' || c == ';') && id_string != NULL)
	{
	  TextNode *sub = g_new (TextNode, 1);
	  sub->is_list = FALSE;
	  g_string_chomp_one (id_string);
	  sub->info.name = id_string->str;
	  g_string_free (id_string, FALSE);
	  g_message ("got id=%s", sub->info.name);
	  nesting->list->info.list
	    = g_slist_append (nesting->list->info.list, sub);
	  id_string = NULL;
	}

      if (c == '{')
	{
	  Nesting *new_nesting = g_new (Nesting, 1);
	  new_nesting->up = nesting;
	  new_nesting->list = g_new (TextNode, 1);
	  new_nesting->list->info.list = NULL;
	  new_nesting->list->is_list = TRUE;
	  nesting->list->info.list = g_slist_append (nesting->list->info.list, new_nesting->list);
	  nesting = new_nesting;
	}
      else if (c == '}')
	{
	  if (nesting->up == NULL)
	    {
	      g_warning ("too many `}'s");
	      goto fail;
	    }
          nesting = nesting->up;
	}
      else if (isspace (c))
	{
	  if (id_string != NULL
	   && id_string->len > 0
	   && !isspace (id_string->str[id_string->len - 1]))
	    g_string_append_c (id_string, ' ');
	}
      else if (c == ';')
	{
	  if (nesting->up == NULL)
	    break;
	}
      else
	{
	  if (id_string == NULL)
	    {
	      char s[2] = {c,0};
	      id_string = g_string_new (s);
	    }
	  else
	    g_string_append_c (id_string, c);
	}
    }
  if (nesting->up != NULL)
    {
      g_warning ("too many `{'s");
      goto fail;
    }

  if (node->info.list == NULL)
    {
      g_free (node);
      return NULL;
    }
  g_free (nesting);
  return node;

fail:
  while (nesting != NULL)
    {
      Nesting *up = nesting->up;
      g_free (nesting);
      nesting = up;
    }
  text_node_destroy (node);
  return NULL;
}

gboolean
text_node_parser_include (TextNodeParser *parser,
			  const char     *filename)
{
  FILE *fp = fopen (filename, "r");
  if (fp == NULL)
    {
      g_warning ("include: file %s not found", filename);
      return FALSE;
    }
  parser->fp_stack = g_slist_prepend (parser->fp_stack, fp);
  return TRUE;
}

void
text_node_parser_destroy (TextNodeParser *parser)
{
  g_slist_foreach (parser->fp_stack, (GFunc) fclose, NULL);
  g_slist_free (parser->fp_stack);
  g_free (parser);
}

void
text_node_destroy        (TextNode       *node)
{
  if (node->is_list)
    {
      GSList *list = node->info.list;
      g_slist_foreach (list, (GFunc) text_node_destroy, NULL);
      g_slist_free (list);
    }
  else
    {
      g_free (node->info.name);
    }
  g_free (node);
}

void
text_node_dump           (TextNode       *node,
			  int             indent,
			  FILE           *fp)
{
  if (node->is_list)
    {
      GSList *at;
      for (at = node->info.list; at != NULL; at = at->next)
	text_node_dump (at->data, indent + 1, fp);
    }
  else
    {
      while (indent-- > 0)
	fputc (' ', fp);
      fprintf (fp, "%s\n", node->info.name);
    }
}
