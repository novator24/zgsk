/*
    GSKB - a batch processing framework

    gskb-format: specify interpretation of packed bytes.

    Copyright (C) 2008 Dave Benson

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA

    Contact:
        daveb@ffem.org <Dave Benson>
*/


#ifndef __GSKB_FORMAT_H_
#define __GSKB_FORMAT_H_

#include <glib.h>
#include "../gskmempool.h"

/* description of data format */

typedef union _GskbFormat GskbFormat;

typedef struct _GskbFormatAny GskbFormatAny;
typedef struct _GskbFormatInt GskbFormatInt;
typedef struct _GskbFormatFloat GskbFormatFloat;
typedef struct _GskbFormatString GskbFormatString;
typedef struct _GskbFormatFixedArray GskbFormatFixedArray;
typedef struct _GskbFormatLengthPrefixedArray GskbFormatLengthPrefixedArray;
typedef struct _GskbFormatStructMember GskbFormatStructMember;
typedef struct _GskbFormatStruct GskbFormatStruct;
typedef struct _GskbFormatUnionCase GskbFormatUnionCase;
typedef struct _GskbFormatUnion GskbFormatUnion;
typedef struct _GskbFormatEnumValue GskbFormatEnumValue;
typedef struct _GskbFormatEnum GskbFormatEnum;
typedef struct _GskbFormatBitField GskbFormatBitField;
typedef struct _GskbFormatBitFields GskbFormatBitFields;
typedef struct _GskbFormatAlias GskbFormatAlias;
typedef struct _GskbUnknownValue GskbUnknownValue;
typedef struct _GskbUnknownValueArray GskbUnknownValueArray;
typedef struct _GskbFormatCMember GskbFormatCMember;
typedef struct _GskbNamespace GskbNamespace;
typedef struct _GskbContext GskbContext;


struct _GskbNamespace
{
  guint ref_count;

  gpointer name_to_format;

  char *name;
  char *c_func_prefix;
  char *c_type_prefix;

  guint n_formats;
  GskbFormat **formats;
  guint formats_alloced;

  guint is_global : 1;
  guint is_writable : 1;
};

GskbNamespace *gskb_namespace_new          (const char     *name);
GskbFormat    *gskb_namespace_lookup_format(GskbNamespace  *ns,
                                            const char     *name);
GskbNamespace *gskb_namespace_ref          (GskbNamespace  *ns);
void           gskb_namespace_unref        (GskbNamespace  *ns);
void           gskb_namespace_make_nonwritable (GskbNamespace *ns);

extern GskbNamespace gskb_namespace_gskb;

typedef enum
{
  GSKB_FORMAT_TYPE_INT,
  GSKB_FORMAT_TYPE_FLOAT,
  GSKB_FORMAT_TYPE_STRING,
  GSKB_FORMAT_TYPE_FIXED_ARRAY,
  GSKB_FORMAT_TYPE_LENGTH_PREFIXED_ARRAY,
  GSKB_FORMAT_TYPE_STRUCT,
  GSKB_FORMAT_TYPE_UNION,
  GSKB_FORMAT_TYPE_BIT_FIELDS,
  GSKB_FORMAT_TYPE_ENUM,
  GSKB_FORMAT_TYPE_ALIAS
} GskbFormatType;
#define GSKB_N_FORMAT_TYPES 10

const char *gskb_format_type_name (GskbFormatType type);

typedef enum
{
  GSKB_FORMAT_CTYPE_INT8,
  GSKB_FORMAT_CTYPE_INT16,
  GSKB_FORMAT_CTYPE_INT32,
  GSKB_FORMAT_CTYPE_INT64,
  GSKB_FORMAT_CTYPE_UINT8,
  GSKB_FORMAT_CTYPE_UINT16,
  GSKB_FORMAT_CTYPE_UINT32,
  GSKB_FORMAT_CTYPE_UINT64,
  GSKB_FORMAT_CTYPE_FLOAT,
  GSKB_FORMAT_CTYPE_DOUBLE,
  GSKB_FORMAT_CTYPE_STRING,
  GSKB_FORMAT_CTYPE_COMPOSITE
} GskbFormatCType;

struct _GskbFormatAny
{
  GskbFormatType type;		/* must be first */
  guint ref_count;

  GskbNamespace *ns;
  char *name;

  char *c_type_name, *c_func_prefix;

  /* how this maps to C structures */
  GskbFormatCType ctype;
  guint c_size_of, c_align_of;
  guint8 always_by_pointer : 1;
  guint8 requires_destruct : 1;

  guint8 is_global : 1;         /* private */

  /* general info about this things packed values */
  guint fixed_length;           /* or 0 */
};

typedef enum
{
  GSKB_FORMAT_INT_INT8,         /* signed byte                         */
  GSKB_FORMAT_INT_INT16,        /* int16, encoded little-endian        */
  GSKB_FORMAT_INT_INT32,        /* int32, encoded little-endian        */
  GSKB_FORMAT_INT_INT64,        /* int32, encoded little-endian        */
  GSKB_FORMAT_INT_UINT8,        /* unsigned byte                       */
  GSKB_FORMAT_INT_UINT16,       /* uint16, encoded little-endian       */
  GSKB_FORMAT_INT_UINT32,       /* uint32, encoded little-endian       */
  GSKB_FORMAT_INT_UINT64,       /* uint32, encoded little-endian       */
  GSKB_FORMAT_INT_INT,          /* var-len signed int, max 32 bits     */
  GSKB_FORMAT_INT_UINT,         /* var-len unsigned int, max 32 bits   */
  GSKB_FORMAT_INT_LONG,         /* var-len signed int, max 64 bits     */
  GSKB_FORMAT_INT_ULONG,        /* var-len unsigned int, max 64 bits   */
  GSKB_FORMAT_INT_BIT           /* byte that may only be set to 0 or 1 */
} GskbFormatIntType;
#define GSKB_N_FORMAT_INT_TYPES (GSKB_FORMAT_INT_BIT+1)
#define gskb_format_int_type_name(int_type) \
  ((const char *)(gskb_format_ints_array[(int_type)].base.name))

struct _GskbFormatInt
{
  GskbFormatAny     base;
  GskbFormatIntType int_type;
};
extern GskbFormatInt gskb_format_ints_array[GSKB_N_FORMAT_INT_TYPES];

typedef enum
{
  GSKB_FORMAT_FLOAT_FLOAT32,
  GSKB_FORMAT_FLOAT_FLOAT64,
} GskbFormatFloatType;
#define GSKB_N_FORMAT_FLOAT_TYPES   (GSKB_FORMAT_FLOAT_FLOAT64+1)

struct _GskbFormatFloat
{
  GskbFormatAny base;
  GskbFormatFloatType float_type;
};
#define gskb_format_float_type_name(float_type) \
  ((const char *)(gskb_format_floats_array[(float_type)].base.name))

struct _GskbFormatString
{
  GskbFormatAny base;
};

struct _GskbFormatFixedArray
{
  GskbFormatAny base;
  guint length;
  GskbFormat *element_format;
};

struct _GskbFormatLengthPrefixedArray
{
  GskbFormatAny base;
  GskbFormat *element_format;

  /* layout of the c structure */
  guint sys_length_offset, sys_data_offset;
};

struct _GskbFormatStructMember
{
  guint code;           /* 0 if not extensible */
  const char *name;
  GskbFormat *format;
};
struct _GskbFormatStruct
{
  GskbFormatAny base;
  gboolean is_extensible;
  guint n_members;
  GskbFormatStructMember *members;
  GskbFormat *contents_format;

  guint *sys_member_offsets;
  gpointer name_to_index;
  gpointer code_to_index;
};

struct _GskbFormatUnionCase
{
  const char *name;
  guint code;
  GskbFormat *format;
};
struct _GskbFormatUnion
{
  GskbFormatAny base;
  gboolean is_extensible;
  guint n_cases;
  GskbFormatUnionCase *cases;
  GskbFormatIntType int_type;
  GskbFormat *type_format;

  guint sys_type_offset, sys_info_offset;
  gpointer name_to_index;
  gpointer code_to_index;
};

struct _GskbFormatBitField
{
  const char *name;
  guint length;                 /* 1..8 */
};
struct _GskbFormatBitFields
{
  GskbFormatAny base;
  // XXX: do we want an is_extensible member?
  guint n_fields;
  GskbFormatBitField *fields;

  /* optimization: whether padding is needed to keep things aligned. */
  gboolean has_holes;
  guint8 *bits_per_unpacked_byte;

  guint total_bits;


  gpointer name_to_index;
};

struct _GskbFormatEnumValue
{
  const char *name;
  guint code;
};

struct _GskbFormatEnum
{
  GskbFormatAny base;
  GskbFormatIntType int_type;
  guint n_values;
  GskbFormatEnumValue *values;
  gboolean is_extensible;

  gpointer name_to_index;
  gpointer code_to_index;
};
#define GSKB_FORMAT_UNION_UNKNOWN_VALUE_CODE 0

struct _GskbFormatAlias
{
  GskbFormatAny base;
  GskbFormat *format;
};

struct _GskbUnknownValue
{
  guint32 code;
  gsize length;
  guint8 *data;
};
struct _GskbUnknownValueArray
{
  guint length;
  GskbUnknownValue *values;
};

union _GskbFormat
{
  GskbFormatType   type;
  GskbFormatAny    any;

  GskbFormatInt        v_int;
  GskbFormatFloat      v_float;
  GskbFormatString     v_string;
  GskbFormatFixedArray v_fixed_array;
  GskbFormatLengthPrefixedArray  v_length_prefixed_array;
  GskbFormatStruct     v_struct;
  GskbFormatUnion      v_union;
  GskbFormatBitFields  v_bit_fields;
  GskbFormatEnum       v_enum;
  GskbFormatAlias      v_alias;
};

GskbFormat *gskb_format_fixed_array_new (guint length,
                                         GskbFormat *element_format);
GskbFormat *gskb_format_length_prefixed_array_new (GskbFormat *element_format);
GskbFormat *gskb_format_struct_new (gboolean is_extensible,
                                    guint n_members,
                                    GskbFormatStructMember *members,
                                    GError       **error);
GskbFormat *gskb_format_union_new (gboolean is_extensible,
                                   GskbFormatIntType int_type,
                                   guint n_cases,
                                   GskbFormatUnionCase *cases,
                                   GError       **error);
GskbFormat *gskb_format_enum_new  (gboolean    is_extensible,
                                   GskbFormatIntType int_type,
                                   guint n_values,
                                   GskbFormatEnumValue *values,
                                   GError       **error);
GskbFormat *gskb_format_bit_fields_new
                                  (guint                  n_fields,
                                   GskbFormatBitField    *fields,
                                   GError               **error);
GskbFormat *gskb_format_alias_new (GskbFormat            *subformat);

/* ref-count handling */
GskbFormat *gskb_format_ref (GskbFormat *format);
void        gskb_format_unref (GskbFormat *format);

/* methods on certain formats */
GskbFormatStructMember *gskb_format_struct_find_member (GskbFormat *format,
                                                        const char *name);
GskbFormatStructMember *gskb_format_struct_find_member_code
                                                       (GskbFormat *format,
                                                        guint       code);
GskbFormatUnionCase    *gskb_format_union_find_case    (GskbFormat *format,
                                                        const char *name);
GskbFormatUnionCase    *gskb_format_union_find_case_code(GskbFormat *format,
                                                        guint       case_value);
GskbFormatEnumValue    *gskb_format_enum_find_value    (GskbFormat *format,
                                                        const char *name);
GskbFormatEnumValue    *gskb_format_enum_find_value_code(GskbFormat *format,
                                                        guint32      code);
GskbFormatBitField     *gskb_format_bit_fields_find_field(GskbFormat *format,
                                                        const char *name);


gboolean    gskb_format_has_name       (GskbFormat *format);
void        gskb_format_set_name       (GskbFormat *format,
                                        GskbNamespace *ns,
                                        const char *name);

typedef void (*GskbAppendFunc) (guint len,
                                const guint8 *data,
                                gpointer func_data);

void        gskb_format_pack           (GskbFormat    *format,
                                        gconstpointer  value,
                                        GskbAppendFunc append_func,
                                        gpointer       append_func_data);
guint       gskb_format_get_packed_size(GskbFormat    *format,
                                        gconstpointer  value);
guint       gskb_format_pack_slab      (GskbFormat    *format,
                                        gconstpointer  value,
                                        guint8        *slab);
guint       gskb_format_validate_partial(GskbFormat    *format,
                                        guint          len,
                                        const guint8  *data,
                                        GError       **error);
gboolean    gskb_format_validate_packed(GskbFormat    *format,
                                        guint          len,
                                        const guint8  *data,
                                        GError       **error);
guint       gskb_format_unpack         (GskbFormat    *format,
                                        const guint8  *data,
                                        gpointer       value);
void        gskb_format_destruct_value (GskbFormat    *format,
                                        gpointer       value);
gboolean    gskb_format_unpack_value_mempool
                                       (GskbFormat    *format,
                                        const guint8  *data,
                                        guint         *n_used_out,
                                        gpointer       value,
                                        GskMemPool    *mem_pool);

typedef enum
{
  GSKB_FORMAT_EQUAL_IGNORE_NAMES = (1<<0),
  GSKB_FORMAT_EQUAL_PERMIT_EXTENSIONS = (1<<1),
  GSKB_FORMAT_EQUAL_NO_ALIASES = (1<<2)
} GskbFormatEqualFlags;
gboolean    gskb_formats_equal         (GskbFormat    *a,
                                        GskbFormat    *b,
                                        GskbFormatEqualFlags flags,
                                        GError       **error);

/* useful for code generation */
const char *gskb_format_type_enum_name (GskbFormatType type);
const char *gskb_format_ctype_enum_name (GskbFormatCType type);
const char *gskb_format_int_type_enum_name (GskbFormatIntType type);
const char *gskb_format_float_type_enum_name (GskbFormatFloatType type);


struct _GskbContext
{
  guint ref_count;
  GHashTable *ns_by_name;
  GPtrArray *implemented_namespaces;
};

GskbContext   *gskb_context_new            (void);
GskbContext   *gskb_context_ref            (GskbContext   *context);
void           gskb_context_unref          (GskbContext   *context);

void           gskb_context_add_namespace  (GskbContext   *context,
                                            gboolean       is_implementing,
                                            GskbNamespace *ns);
GskbNamespace *gskb_context_find_namespace (GskbContext   *context,
                                            const char    *name);
gboolean       gskb_context_parse_string   (GskbContext   *context,
                                            const char    *pseudo_filename,
                                            const char    *str,
                                            GError       **error);
gboolean       gskb_context_parse_file     (GskbContext   *context,
                                            const char    *filename,
                                            GError       **error);
GskbFormat    *gskb_context_parse_format   (GskbContext   *context,
                                            const char    *str,
                                            GError       **error);
#endif
