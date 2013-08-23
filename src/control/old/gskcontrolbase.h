#ifndef __GSK_CONTROL_BASE_H_
#define __GSK_CONTROL_BASE_H_

/* base class for slinging around XML */

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskControlBase GskControlBase;
typedef struct _GskControlBaseClass GskControlBaseClass;
/* --- type macros --- */
GType gsk_control_base_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_CONTROL_BASE			(gsk_control_base_get_type ())
#define GSK_CONTROL_BASE(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_CONTROL_BASE, GskControlBase))
#define GSK_CONTROL_BASE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_CONTROL_BASE, GskControlBaseClass))
#define GSK_CONTROL_BASE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_CONTROL_BASE, GskControlBaseClass))
#define GSK_IS_CONTROL_BASE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_CONTROL_BASE))
#define GSK_IS_CONTROL_BASE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_CONTROL_BASE))

typedef struct _GskControlXmlNode GskControlXmlNode;
struct _GskControlXmlNode
{
  gboolean is_text;

  union
  {
    struct {
      /* for elements */
      char *name;
      char **attrs;
      guint n_children;
      GskControlXmlNode **children;
    } element;
    struct {
      /* for text nodes */
        char *contents;
    } text;
  } info;
};

typedef struct _GskControlXmlBuilder GskControlXmlBuilder;

GskControlXmlBuilder *gsk_control_xml_builder_new (void);
void  gsk_control_xml_builder_start_node (GskControlXmlBuilder *builder,
                                          const char           *name,
                                          char                **attrs);
void  gsk_control_xml_builder_end_node   (GskControlXmlBuilder *builder);
void  gsk_control_xml_builder_add_text   (GskControlXmlBuilder *builder,
                                          const char           *text,
                                          gssize                len);
GskControlXmlNode *
      gsk_control_xml_get_node           (GskControlXmlBuilder *);
void  gsk_control_xml_builder_free       (GskControlXmlBuilder *);



/* --- structures --- */
struct _GskControlBaseClass 
{
  GskStreamClass base_class;
};
struct _GskControlBase 
{
  GskStream      base_instance;

  /* triggerred when we have input xml that
     hasn't been retrieved. */
  GskHook        input_available;

  /* triggerred when we our output buffer is empty. */
  GskHook        need_output;

  /* parsed, unhandled xml nodes */
  GQueue        *parsed_xml_nodes;

  /* raw xml handling */
  GskControlXmlBuilder *xml_builder;
  GMarker *xml_parser;

  /* outgoing xml data */
  GskBuffer      outgoing;
};

/* --- prototypes --- */
G_END_DECLS

#endif
