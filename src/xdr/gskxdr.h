#ifndef __GSK_XDR_H_
#define __GSK_XDR_H_

/* XDR: eXternal Data Representation.
 * See RFC 1832.
 *
 * A way of portably packing binary structures.
 *
 * Used by Sun RPC protocol.
 */

G_BEGIN_DECLS

typedef guint32    gsk_xdr_unsigned_int;
typedef gint32     gsk_xdr_int;
typedef guint64    gsk_xdr_unsigned_hyperint;
typedef gint64     gsk_xdr_hyperint;
typedef gfloat     gsk_xdr_single_float;
typedef gdouble    gsk_xdr_double_float;
typedef gldouble   gsk_xdr_quadruple_float;	/* not really correct */

typedef enum
{
  GSK_XDR_FUNDAMENTAL_INT,
  GSK_XDR_FUNDAMENTAL_UNSIGNED_INT,
  GSK_XDR_FUNDAMENTAL_HYPERINT,
  GSK_XDR_FUNDAMENTAL_UNSIGNED_HYPERINT,
  GSK_XDR_FUNDAMENTAL_SINGLE_FLOAT,
  GSK_XDR_FUNDAMENTAL_DOUBLE_FLOAT,
  GSK_XDR_FUNDAMENTAL_QUADRUPLE_FLOAT
} GskXdrFundamental;

#define GSK_XDR_NO_MAX_SIZE ((unsigned)-1)

typedef struct _GskXdrContext GskXdrContext;
typedef struct _GskXdrType GskXdrType;

typedef enum
{
  GSK_XDR_TYPE_CATEGORY_FUNDAMENTAL,
  GSK_XDR_TYPE_CATEGORY_ENUM,
  GSK_XDR_TYPE_CATEGORY_UNION,
  GSK_XDR_TYPE_CATEGORY_STRUCT,
  GSK_XDR_TYPE_CATEGORY_TYPEDEF,
  GSK_XDR_TYPE_CATEGORY_OPAQUE_FIXED,
  GSK_XDR_TYPE_CATEGORY_OPAQUE_VARIABLE,
  GSK_XDR_TYPE_CATEGORY_ARRAY_FIXED,
  GSK_XDR_TYPE_CATEGORY_ARRAY_VARIABLE,
  GSK_XDR_TYPE_CATEGORY_STRING
} GskXdrTypeCategory;

struct _GskXdrType
{
  GskXdrContext *context;
  GskXdrTypeCategory category;
  guint8 is_optional : 1;
  union
  {
    GskXdrFundamental fundamental;
    struct {
      unsigned n_entries;
      char **names;
      gsk_xdr_int *values;
    } enum_;
    struct {
      char *name;
      unsigned n_members;
      char **member_names;
      GskXdrType **member_types;
    } union_;
    struct {
      char *name;
      unsigned n_members;
      char **member_names;
      GskXdrType **member_types;

      /* from add_mapping */
      size_t c_sizeof;
      size_t *c_offsets;
    } struct_;
    struct {
      char *name;
      GskXdrType *def;
    } typedef_;
    struct {
      unsigned size;
    } opaque_fixed;
    struct {
      unsigned max_size;	/* or GSK_XDR_NO_MAX_SIZE */
    } opaque_variable;
    struct {
      GskXdrType *element_type;
      unsigned size;
    } array_fixed;
    struct {
      GskXdrType *element_type;
      unsigned max_size;	/* or GSK_XDR_NO_MAX_SIZE */
    } array_variable;
  } info;
};

GskXdrContext *gsk_xdr_context_new          (void);
void           gsk_xdr_context_destroy      (GskXdrContext    *context);

/* --- useful methods --- */
GskXdrType    *gsk_xdr_context_lookup       (GskXdrContext    *context,
                                             const char       *name);
gboolean       gsk_xdr_test_parse           (GskXdrType       *type,
                                             GskBuffer        *buffer);
gboolean       gsk_xdr_parse                (GskXdrType       *type,
                                             GskBuffer        *buffer,
					     gpointer          struct_ptr);
size_t         gsk_xdr_find_sizeof          (GskXdrType       *type);
size_t         gsk_xdr_find_wire_size       (GskXdrType       *type,
					     gconstpointer     struct_ptr);
gboolean       gsk_xdr_write                (GskXdrType       *type,
					     gconstpointer     struct_ptr,
                                             GskBuffer        *buffer);
gboolean       gsk_xdr_print_as_text        (GskXdrType       *type,
					     gconstpointer     struct_ptr,
                                             GskBuffer        *buffer);

/* --- incremental parsing --- */
GskXdrParser  *gsk_xdr_parser_new           (void);
gboolean       gsk_xdr_parser_parse         (GskXdrParser     *parser,
					     GskXdrType       *type_to_parse,
					     gpointer          struct_ptr,
					     GError          **error);
gboolean       gsk_xdr_parser_continue      (GskXdrParser     *parser,
					     GError          **error);
void           gsk_xdr_parser_abandon       (GskXdrParser     *parser,
					     GError          **error);
void           gsk_xdr_parser_destroy       (GskXdrParser     *parser);


/* --- mostly used by machine-generated code --- */
GskXdrType    *gsk_xdr_type_new_fundamental (GskXdrContext    *context,
                                             GskXdrFundamental fundamental);
GskXdrType    *gsk_xdr_type_new_enum        (GskXdrContext    *context,
                                             const char       *name,
                                             GskXdrType       *type);
GskXdrType    *gsk_xdr_type_new_union       (GskXdrContext    *context,
                                             const char       *name,
					     unsigned          n_members,
					     char            **member_names,
					     GskXdrType      **member_types);
GskXdrType    *gsk_xdr_type_new_struct      (GskXdrContext    *context,
                                             const char       *name,
					     unsigned          n_members,
					     char            **member_names,
					     GskXdrType      **member_types);
GskXdrType    *gsk_xdr_type_new_typedef     (GskXdrContext    *context,
                                             const char       *name,
                                             GskXdrType       *type);
GskXdrType    *gsk_xdr_type_new_opaque      (GskXdrContext    *context,
                                             gboolean          is_variable_len,
					     unsigned          size);
GskXdrType    *gsk_xdr_type_new_array       (GskXdrContext    *context,
                                             gboolean          is_variable_len,
					     GskXdrType       *element_type,
					     unsigned          size);
GskXdrType    *gsk_xdr_type_new_string      (GskXdrContext    *context,
					     unsigned          max_size);
void           gsk_xdr_context_add_struct_mapping
                                            (GskXdrType       *struct_type,
					     unsigned          sizeof_struct,
					     const unsigned   *offsets);

/* --- for use by gskxdr utility program --- */
gboolean       gsk_xdr_context_parse_xdr_buf(GskXdrContext    *context,
					     GdkBuffer        *to_parse,
					     GSList          **list_to_prepend_to,
					     GError          **error);
gboolean       gsk_xdr_context_write_gsk_code(GskXdrContext    *context,
					     GdkBuffer        *to_parse,
					     GError          **error);

G_END_DECLS

#endif
