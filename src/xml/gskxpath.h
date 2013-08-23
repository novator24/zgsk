#ifndef __GSK_XPATH_H__
#define __GSK_XPATH_H__

typedef struct _GskXPathExpr GskXPathExpr;
typedef struct _GskXmlPointer GskXmlPointer;
typedef struct _GskXPath GskXPath;

struct _GskXmlPointer
{
  GskXmlNode *node;
  GskXmlString *attribute_ns_uri;
  GskXmlString *attribute;
};

typedef enum
{
  GSK_XPATH_EXPR_FUNCTION,
  GSK_XPATH_EXPR_LITERAL_STRING
} GskXPathExprType;

/* this is an expression in the XPath language.
 */
struct _GskXPathExpr
{
  GskXPathExprType type;

  union {
    struct {
      guint n_subexprs;
      GskXPathExpr **subexprs;
    } function;
    struct {
      GskXmlString *literal;
    } literal_string;
    struct {
      int value;
    } literal_int;
    struct {
      double value;
    } literal_double;
  } info;
};


struct _GskXPath
{
  GskXmlPathExpr *expr;
};

GskXPath *gsk_xpath_parse    (const char    *str,
                              GError       **error);
GskXPath *gsk_xpath_parse_len(const char    *str,
                              guint          len,
                              GError       **error);
gboolean  gsk_xpath_lookup   (GskXPath      *path,
                              GskXmlNode    *node,
			      GskXmlPointer *pointer,
			      GError       **error);
GskXPath *gsk_xpath_ref      (GskXPath      *path);
void      gsk_xpath_unref    (GskXPath      *path);


/* --- incremental xpath detection --- */
typedef void (*GskXPathCallback) (GskXPath *path,
                                  GskXmlString *attr,
                                  gpointer data);

typedef struct
{
  GskXPath *path;
  GskXPathCallback callback;
  gpointer data;
} GskXPathTest;


GskXmlCreator  *gsk_xpath_tester_new       (GskXPath *path,
                                            GskXPathCallback callback,
                                            gpointer data);
GskXmlCreator  *gsk_xpath_tester_new_multi (guint         n,
                                            GskXPathTest *tests);

#endif
