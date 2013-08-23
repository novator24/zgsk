#ifndef __TEXT_NODE_H_
#define __TEXT_NODE_H_

#include <glib.h>
#include <stdio.h>

G_BEGIN_DECLS

/* lexer basics for bind-config-file parsing */
typedef struct _TextNode TextNode;
typedef struct _TextNodeParser TextNodeParser;

struct _TextNode
{
  gboolean is_list;
  union
  {
    GSList *list;
    char   *name;
  } info;
};

struct _TextNodeParser
{
  GSList *fp_stack;
};

TextNodeParser *text_node_parser_new     (const char     *filename);
gboolean        text_node_parser_include (TextNodeParser *parser,
					  const char     *filename);
TextNode       *text_node_parser_parse   (TextNodeParser *parser);
void            text_node_parser_destroy (TextNodeParser *parser);
void            text_node_destroy        (TextNode       *node);

void            text_node_dump           (TextNode       *node,
					  int             indent,
					  FILE           *fp);
G_END_DECLS

#endif
