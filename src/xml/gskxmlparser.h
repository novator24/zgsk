/* XML Parsing.
 *
 * This is streaming parsing, but implemented by returning a
 * sequence of XML node objects, rather than the conventional
 * SAX callback interface.
 *
 * You create a parser config, which acts as a factory for parser objects.
 *
 * Parser objects are fed xml data, and return (via the "dequeue")
 * method a sequence of xml nodes (the document need not
 * terminate to get nodes).  There may be several families of
 * nodes to dequeue.  They are allocated starting at 0.
 *
 * Name spaces are handled as follows:  ...
 *
 * So, in summary
 *  --- typically done at program startup ---
 *  config = gsk_xml_parser_config_new ();
 *  gsk_xml_parser_config_add_path (config, "foo/bar");
 *  gsk_xml_parser_config_add_path (config, "foo/baz");
 *  gsk_xml_parser_config_done (config);
 *
 *  --- for each document to parse ---
 *  text = "<foo><bar>a</bar><baz>b</baz></foo>";
 *  parser = gsk_xml_parser_new (config);
 *  if (!gsk_xml_parser_feed (parser, text, -1, &error))
 *    g_error ("error parsing xml: %s", error->message);
 *  bar = gsk_xml_parser_dequeue (parser, 0);
 *  baz = gsk_xml_parser_dequeue (parser, 1);
 *  gsk_xml_parser_free (parser);
 *  g_assert (bar && baz);
 *  gsk_xml_unref (bar);
 *  gsk_xml_unref (baz);
 *
 *  --- when done making parsers ---
 *  gsk_xml_parser_config_unref (config);
 */
#ifndef __GSK_XML_PARSER_H_
#define __GSK_XML_PARSER_H_

typedef struct _GskXmlParserConfig GskXmlParserConfig;
typedef struct _GskXmlParser GskXmlParser;

typedef enum
{
  GSK_XML_PARSER_IGNORE_NS_TAGS = (1<<0),
  GSK_XML_PARSER_PASSTHROUGH_UNKNOWN_NS = (1<<1),
} GskXmlParserFlags;

#include "gskxml.h"

/* lifetime:  you create a ParserConfig,
   then add_ns, add_path, set_flags.
   Then call gsk_xml_parser_config_done().
   Then you may use it to construct XmlParser objects. */
GskXmlParserConfig *gsk_xml_parser_config_new          (void);
gint                gsk_xml_parser_config_add_path (GskXmlParserConfig *,
                                                    const char         *path,
                                                    GError            **error);
void                gsk_xml_parser_config_add_ns   (GskXmlParserConfig *,
                                                    const char         *abbrev,
						    const char         *url);
void                gsk_xml_parser_config_set_flags(GskXmlParserConfig *config,
                                                    GskXmlParserFlags   flags);
void                gsk_xml_parser_config_done     (GskXmlParserConfig *config);

/* returns a "done" config */
GskXmlParserConfig *gsk_xml_parser_config_new_by_depth (guint depth);

/* config must be "done". */
GskXmlParserConfig *gsk_xml_parser_config_ref      (GskXmlParserConfig *);
void                gsk_xml_parser_config_unref    (GskXmlParserConfig *);

/* misc functions */
GskXmlParserFlags   gsk_xml_parser_config_get_flags(GskXmlParserConfig *config);

/* xml parsing */
GskXmlParser *gsk_xml_parser_new_take     (GskXmlParserConfig *config);
GskXmlParser *gsk_xml_parser_new          (GskXmlParserConfig *config);
GskXmlParser *gsk_xml_parser_new_by_depth (guint               depth);
GskXml       *gsk_xml_parser_dequeue      (GskXmlParser       *parser,
                                           guint               index);
gboolean      gsk_xml_parser_feed         (GskXmlParser       *parser,
                                           const guint8       *xml_data,
					   gssize              len,
					   GError            **error);
gboolean      gsk_xml_parser_feed_file    (GskXmlParser       *parser,
                                           const char         *filename,
					   GError            **error);
void          gsk_xml_parser_free         (GskXmlParser       *parser);


#endif
