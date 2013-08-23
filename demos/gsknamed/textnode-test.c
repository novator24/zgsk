#include "textnode.h"

int main (int argc, char **argv)
{
  int i;
  for (i = 1 ; i < argc ; i ++)
    {
      TextNodeParser *parser = text_node_parser_new (argv[i]);
      TextNode *node;
      g_assert (parser != NULL);
      printf ("========= %s ========\n", argv[i]);
      while ((node = text_node_parser_parse (parser)) != NULL)
	{
	  text_node_dump (node, 1, stdout);
	}
      text_node_parser_destroy (parser);
    }
  return 0;
}
