#ifndef __GSK_XMLRPC_H_
#define __GSK_XMLRPC_H_

#include <glib.h>

G_BEGIN_DECLS
/* --- typedefs --- */
typedef struct _GskXmlrpcStream GskXmlrpcStream;
typedef struct _GskXmlrpcStreamClass GskXmlrpcStreamClass;
typedef struct _GskXmlrpcOutgoing GskXmlrpcOutgoing;
typedef struct _GskXmlrpcIncoming GskXmlrpcIncoming;

typedef struct _GskXmlrpcArray GskXmlrpcArray;
typedef struct _GskXmlrpcStruct GskXmlrpcStruct;
typedef struct _GskXmlrpcValue GskXmlrpcValue;
typedef struct _GskXmlrpcNamedValue GskXmlrpcNamedValue;
typedef struct _GskXmlrpcRequest GskXmlrpcRequest;
typedef struct _GskXmlrpcResponse GskXmlrpcResponse;
typedef struct _GskXmlrpcParser GskXmlrpcParser;

typedef enum
{
  GSK_XMLRPC_INT32,
  GSK_XMLRPC_BOOLEAN,
  GSK_XMLRPC_DOUBLE,
  GSK_XMLRPC_STRING,
  GSK_XMLRPC_DATE,
  GSK_XMLRPC_BINARY_DATA,
  GSK_XMLRPC_STRUCT,
  GSK_XMLRPC_ARRAY
} GskXmlrpcType;

struct _GskXmlrpcArray
{
  unsigned len;
  GskXmlrpcValue *values;

  /*< private >*/
  unsigned alloced;
};

struct _GskXmlrpcStruct
{
  unsigned n_members;
  GskXmlrpcNamedValue *members;

  /*< private >*/
  unsigned alloced;
};

struct _GskXmlrpcValue
{
  GskXmlrpcType type;
  union
  {
    int v_int32;
    gboolean v_boolean;
    double v_double;
    char *v_string;
    gulong v_date;
    GByteArray *v_binary_data;
    GskXmlrpcStruct *v_struct;
    GskXmlrpcArray *v_array;
  } data;
};

struct _GskXmlrpcNamedValue
{
  char *name;
  GskXmlrpcValue value;
};

struct _GskXmlrpcRequest
{
  unsigned magic;		/* private, must be first */
  char *method_name;
  GskXmlrpcArray *params;
  GskXmlrpcStream *xmlrpc_stream;
  /*< private >*/
  unsigned ref_count;
};

struct _GskXmlrpcResponse
{
  unsigned magic;		/* private, must be first */

  GskXmlrpcArray *params;
  gboolean has_fault;
  GskXmlrpcValue fault;

  /*< private >*/
  unsigned ref_count;
};


GskXmlrpcStruct *gsk_xmlrpc_struct_new         (void);
void             gsk_xmlrpc_struct_free        (GskXmlrpcStruct *structure);
void             gsk_xmlrpc_struct_add_int32   (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                gint32           value);
void             gsk_xmlrpc_struct_add_boolean(GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                gboolean         value);
void             gsk_xmlrpc_struct_add_double  (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                gdouble          value);
void             gsk_xmlrpc_struct_add_string  (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                const char      *value);
void             gsk_xmlrpc_struct_add_date    (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                gulong           value);

/* these take ownership of second argument */  
void             gsk_xmlrpc_struct_add_data    (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                GByteArray      *data);
void             gsk_xmlrpc_struct_add_struct  (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                GskXmlrpcStruct *substructure);
void             gsk_xmlrpc_struct_add_array   (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                GskXmlrpcArray  *array);

/* lookups */
gboolean         gsk_xmlrpc_struct_peek_int32  (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                gint32          *out);
gboolean         gsk_xmlrpc_struct_peek_boolean(GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                gboolean        *out);
gboolean         gsk_xmlrpc_struct_peek_double (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                double          *out);
const char *     gsk_xmlrpc_struct_peek_string (GskXmlrpcStruct *structure,
                                                const char      *member_name);
gboolean         gsk_xmlrpc_struct_peek_date   (GskXmlrpcStruct *structure,
                                                const char      *member_name,
						gulong          *out);
const GByteArray*gsk_xmlrpc_struct_peek_data   (GskXmlrpcStruct *structure,
                                                const char      *member_name);
GskXmlrpcStruct *gsk_xmlrpc_struct_peek_struct (GskXmlrpcStruct *structure,
                                                const char      *member_name);
GskXmlrpcArray  *gsk_xmlrpc_struct_peek_array  (GskXmlrpcStruct *structure,
                                                const char      *member_name);


GskXmlrpcArray  *gsk_xmlrpc_array_new          (void);
void             gsk_xmlrpc_array_free         (GskXmlrpcArray  *array);
void             gsk_xmlrpc_array_add_int32    (GskXmlrpcArray  *array,
                                                gint32           value);
void             gsk_xmlrpc_array_add_boolean  (GskXmlrpcArray  *array,
                                                gboolean         value);
void             gsk_xmlrpc_array_add_double   (GskXmlrpcArray  *array,
                                                gdouble          value);
void             gsk_xmlrpc_array_add_string   (GskXmlrpcArray  *array,
                                                const char      *value);
void             gsk_xmlrpc_array_add_date     (GskXmlrpcArray  *array,
                                                gulong           value);

/* these take ownership of second argument */  
void             gsk_xmlrpc_array_add_data     (GskXmlrpcArray  *array,
                                                GByteArray      *data);
void             gsk_xmlrpc_array_add_struct   (GskXmlrpcArray  *array,
                                                GskXmlrpcStruct *substructure);


GskXmlrpcArray  *gsk_xmlrpc_array_new          (void);
void             gsk_xmlrpc_array_free         (GskXmlrpcArray  *array);
void             gsk_xmlrpc_array_add_int32    (GskXmlrpcArray  *array,
                                                gint32           value);
void             gsk_xmlrpc_array_add_boolean  (GskXmlrpcArray  *array,
                                                gboolean         value);
void             gsk_xmlrpc_array_add_double   (GskXmlrpcArray  *array,
                                                gdouble          value);
void             gsk_xmlrpc_array_add_string   (GskXmlrpcArray  *array,
                                                const char      *value);
void             gsk_xmlrpc_array_add_date     (GskXmlrpcArray  *array,
                                                gulong           value);

/* these take ownership of second argument */  
void             gsk_xmlrpc_array_add_data     (GskXmlrpcArray  *array,
                                                GByteArray      *data);
void             gsk_xmlrpc_array_add_struct   (GskXmlrpcArray  *array,
                                                GskXmlrpcStruct *substructure);
void             gsk_xmlrpc_array_add_array    (GskXmlrpcArray  *array,
                                                GskXmlrpcArray  *subarray);

GskXmlrpcRequest*gsk_xmlrpc_request_new        (GskXmlrpcStream *xmlrpc_stream);
GskXmlrpcRequest*gsk_xmlrpc_request_ref        (GskXmlrpcRequest*request);
void             gsk_xmlrpc_request_unref      (GskXmlrpcRequest*request);
void             gsk_xmlrpc_request_set_name   (GskXmlrpcRequest*request,
                                                const char      *name);
void             gsk_xmlrpc_request_add_int32  (GskXmlrpcRequest *request,
                                                gint32           value);
void             gsk_xmlrpc_request_add_boolean(GskXmlrpcRequest *request,
                                                gboolean         value);
void             gsk_xmlrpc_request_add_double (GskXmlrpcRequest *request,
                                                gdouble          value);
void             gsk_xmlrpc_request_add_string (GskXmlrpcRequest *request,
                                                const char      *value);
void             gsk_xmlrpc_request_add_date   (GskXmlrpcRequest *request,
                                                gulong           value);

/* these take ownership of second argument */
void             gsk_xmlrpc_request_add_data (GskXmlrpcRequest *request,
                                              GByteArray      *data);
void             gsk_xmlrpc_request_add_struct(GskXmlrpcRequest *request,
                                              GskXmlrpcStruct *substructure);
void             gsk_xmlrpc_request_add_array(GskXmlrpcRequest *request,
                                              GskXmlrpcArray  *array);

GskXmlrpcResponse  *gsk_xmlrpc_response_new      (void);
GskXmlrpcResponse  *gsk_xmlrpc_response_ref      (GskXmlrpcResponse   *response);
void             gsk_xmlrpc_response_unref    (GskXmlrpcResponse   *response);
void             gsk_xmlrpc_response_add_int32(GskXmlrpcResponse *response,
                                              gint32           value);
void             gsk_xmlrpc_response_add_double(GskXmlrpcResponse *response,
                                              gdouble          value);
void             gsk_xmlrpc_response_add_boolean(GskXmlrpcResponse *response,
                                                gboolean         value);
void             gsk_xmlrpc_response_add_string(GskXmlrpcResponse *response,
                                              const char      *value);
void             gsk_xmlrpc_response_add_date (GskXmlrpcResponse *response,
                                              gulong           value);

/* these take ownership of second argument */
void             gsk_xmlrpc_response_add_data (GskXmlrpcResponse *response,
                                              GByteArray      *data);
void             gsk_xmlrpc_response_add_struct(GskXmlrpcResponse *response,
                                              GskXmlrpcStruct *substructure);
void             gsk_xmlrpc_response_add_array(GskXmlrpcResponse *response,
                                              GskXmlrpcArray  *array);


void             gsk_xmlrpc_response_fault   (GskXmlrpcResponse *response,
                                              GskXmlrpcStruct   *structure);

/* a parser */
GskXmlrpcParser  *gsk_xmlrpc_parser_new (GskXmlrpcStream* stream);
gboolean          gsk_xmlrpc_parser_feed (GskXmlrpcParser *parser,
				          const char              *text,
				          gssize                   len,
				          GError                 **error);
GskXmlrpcRequest *gsk_xmlrpc_parser_get_request (GskXmlrpcParser *parser);
GskXmlrpcResponse *gsk_xmlrpc_parser_get_response (GskXmlrpcParser *parser);
void              gsk_xmlrpc_parser_free (GskXmlrpcParser *parser);


/* printing */
#include "../gskbuffer.h"
void gsk_xmlrpc_response_to_buffer (GskXmlrpcResponse *response,
				    GskBuffer         *buffer);
void gsk_xmlrpc_request_to_buffer  (GskXmlrpcRequest  *request,
				    GskBuffer         *buffer);

G_END_DECLS

#endif
