
struct _GskXmlCreatorClass
{
  GObjectClass base_class;

  void (*start) (GskXmlCreator *creator,
                 GskXmlString  *name,
                 guint          n_attrs,
                 GskXmlString**attrs);
  void (*end)   (GskXmlCreator *creator,
                 GskXmlString  *name);
  void (*add)   (GskXmlCreator *creator,
                 GskXmlNode    *subnode);
  void (*text)  (GskXmlCreator *creator,
                 GskXmlString  *content);
};
struct _GskXmlCreator
{
  GObject base_instance;
};


void           gsk_xml_creator_start    (GskXmlCreator *creator,
                                         GskXmlString  *ns_name,
                                         GskXmlString  *name,
                                         guint          n_attrs,
                                         GskXmlRawAttribute *attrs);
void           gsk_xml_creator_end      (GskXmlCreator *creator,
                                         GskXmlString  *name);
void           gsk_xml_creator_add      (GskXmlCreator *creator,
                                         GskXmlNode    *subnode);
void           gsk_xml_creator_text     (GskXmlCreator *creator,
                                         GskXmlString  *content);


