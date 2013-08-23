#include "gskxmlrpc.h"
#include "../gskerror.h"
#include "../gskmacros.h"
#include "../common/gskbase64.h"
#include "../common/gskdate.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define DEBUG_XMLRPC_PARSER	0

#define RESPONSE_MAGIC	0x3524de1a
#define REQUEST_MAGIC	0x3524de2b

/**
 * gsk_xmlrpc_struct_new:
 *
 * Allocate a new structure, with no members.
 *
 * returns: the newly allocated structure.
 */
GskXmlrpcStruct *gsk_xmlrpc_struct_new         (void)
{
  GskXmlrpcStruct *structure = g_new0 (GskXmlrpcStruct, 1);
  return structure;
}

static void
gsk_xmlrpc_value_destruct (GskXmlrpcValue *value)
{
  switch (value->type)
    {
    case GSK_XMLRPC_STRING:
      g_free (value->data.v_string);
      break;
    case GSK_XMLRPC_BINARY_DATA:
      g_byte_array_free (value->data.v_binary_data, TRUE);
      break;
    case GSK_XMLRPC_STRUCT:
      gsk_xmlrpc_struct_free (value->data.v_struct);
      break;
    case GSK_XMLRPC_ARRAY:
      gsk_xmlrpc_array_free (value->data.v_array);
      break;
    default:
      break;
    }
}

/**
 * gsk_xmlrpc_struct_free:
 * @structure: the structure to free.
 *
 * Free memory associated with an XMLRPC struct.
 */
void             gsk_xmlrpc_struct_free        (GskXmlrpcStruct *structure)
{
  unsigned i;
  for (i = 0; i < structure->n_members; i++)
    {
      g_free (structure->members[i].name);
      gsk_xmlrpc_value_destruct (&structure->members[i].value);
    }
}
static void
gsk_xmlrpc_struct_add_value_steal_name (GskXmlrpcStruct *structure,
                             char      *member_name,
			     GskXmlrpcValue  *value)
{
  if (structure->n_members == structure->alloced)
    {
      unsigned new_alloced = structure->alloced;
      if (new_alloced == 0)
	new_alloced = 16;
      else
	new_alloced += new_alloced;
      structure->members = g_renew (GskXmlrpcNamedValue, structure->members, new_alloced);
      structure->alloced = new_alloced;
    }
  structure->members[structure->n_members].name = member_name;
  structure->members[structure->n_members].value = *value;
  ++(structure->n_members);
}


static inline void
gsk_xmlrpc_struct_add_value (GskXmlrpcStruct *structure,
                             const char      *member_name,
			     GskXmlrpcValue  *value)
{
  gsk_xmlrpc_struct_add_value_steal_name (structure, g_strdup (member_name), value);
}

/**
 * gsk_xmlrpc_struct_add_int32:
 * @structure: the structure to append to.
 * @member_name: name of the new int32 member.
 * @value: value of the new int32 member.
 *
 * Add a single int32 member to the given struct.
 */
void             gsk_xmlrpc_struct_add_int32   (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                gint32           value)
{
  GskXmlrpcValue v;
  v.type = GSK_XMLRPC_INT32;
  v.data.v_int32 = value;
  gsk_xmlrpc_struct_add_value (structure, member_name, &v);
}

/**
 * gsk_xmlrpc_struct_add_boolean:
 * @structure: the structure to append to.
 * @member_name: name of the new boolean member.
 * @value: value of the new boolean member.
 *
 * Add a single boolean member to the given struct.
 */
void             gsk_xmlrpc_struct_add_boolean (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                gboolean         value)
{
  GskXmlrpcValue v;
  v.type = GSK_XMLRPC_BOOLEAN;
  v.data.v_boolean = value;
  gsk_xmlrpc_struct_add_value (structure, member_name, &v);
}

/**
 * gsk_xmlrpc_struct_add_double:
 * @structure: the structure to append to.
 * @member_name: name of the new double member.
 * @value: value of the new double member.
 *
 * Add a single double member to the given struct.
 */
void             gsk_xmlrpc_struct_add_double  (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                gdouble          value)
{
  GskXmlrpcValue v;
  v.type = GSK_XMLRPC_DOUBLE;
  v.data.v_double = value;
  gsk_xmlrpc_struct_add_value (structure, member_name, &v);
}

/**
 * gsk_xmlrpc_struct_add_string:
 * @structure: the structure to append to.
 * @member_name: name of the new string member.
 * @value: value of the new string member.
 *
 * Add a single string member to the given struct.
 */
void             gsk_xmlrpc_struct_add_string  (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                const char      *value)
{
  GskXmlrpcValue v;
  v.type = GSK_XMLRPC_STRING;
  v.data.v_string = g_strdup (value);
  gsk_xmlrpc_struct_add_value (structure, member_name, &v);
}

/**
 * gsk_xmlrpc_struct_add_date:
 * @structure: the structure to append to.
 * @member_name: name of the new date member.
 * @value: value of the new date member.
 *
 * Add a single date member to the given struct.
 */
void             gsk_xmlrpc_struct_add_date    (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                gulong           value)
{
  GskXmlrpcValue v;
  v.type = GSK_XMLRPC_DATE;
  v.data.v_date = value;
  gsk_xmlrpc_struct_add_value (structure, member_name, &v);
}

/**
 * gsk_xmlrpc_struct_add_data:
 * @structure: the structure to append to.
 * @member_name: name of the new date member.
 * @data: binary data to add, which will be freed by the structure when
 * it is freed.
 *
 * Add a single data member to the given struct.
 */
void             gsk_xmlrpc_struct_add_data    (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                GByteArray      *data)
{
  GskXmlrpcValue v;
  v.type = GSK_XMLRPC_BINARY_DATA;
  v.data.v_binary_data = data;
  gsk_xmlrpc_struct_add_value (structure, member_name, &v);
}

/**
 * gsk_xmlrpc_struct_add_struct:
 * @structure: the structure to append to.
 * @member_name: name of the new struct member.
 * @substructure: substructure to add, which will be freed by @structure when
 * it is freed.
 *
 * Add a structure as a member of another structure.
 */
void             gsk_xmlrpc_struct_add_struct  (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                GskXmlrpcStruct *substructure)
{
  GskXmlrpcValue v;
  v.type = GSK_XMLRPC_STRUCT;
  v.data.v_struct = substructure;
  gsk_xmlrpc_struct_add_value (structure, member_name, &v);
}

/**
 * gsk_xmlrpc_struct_add_array:
 * @structure: the structure to append to.
 * @member_name: name of the new struct member.
 * @array: subarray to add, which will be freed by @structure when
 * it is freed.
 *
 * Add an array as a member of a structure.
 */
void             gsk_xmlrpc_struct_add_array   (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                GskXmlrpcArray  *array)
{
  GskXmlrpcValue v;
  v.type = GSK_XMLRPC_ARRAY;
  v.data.v_array = array;
  gsk_xmlrpc_struct_add_value (structure, member_name, &v);
}

static GskXmlrpcValue *
gsk_xmlrpc_struct_peek_any (GskXmlrpcStruct *structure,
			    const char      *member_name,
			    GskXmlrpcType    type)
{
  guint i;
  for (i = 0; i < structure->n_members; i++)
    if (strcmp (member_name, structure->members[i].name) == 0
     && structure->members[i].value.type == type)
      return &structure->members[i].value;
  return NULL;
}

/**
 * gsk_xmlrpc_struct_peek_int32:
 * @structure: the structure to lookup the member of.
 * @member_name: the value to retrieve.
 * @out: place to store the result.
 *
 * Lookup a named int32 member of a structure,
 * storing the result, if any, in @out.
 *
 * returns: whether there was an int32 member named @member_name.
 */
gboolean         gsk_xmlrpc_struct_peek_int32  (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                gint32          *out)
{
  GskXmlrpcValue *value = gsk_xmlrpc_struct_peek_any (structure, member_name, GSK_XMLRPC_INT32);
  if (value)
    *out = value->data.v_int32;
  return value != NULL;
}

/**
 * gsk_xmlrpc_struct_peek_boolean:
 * @structure: the structure to lookup the member of.
 * @member_name: the value to retrieve.
 * @out: place to store the result.
 *
 * Lookup a named boolean member of a structure,
 * storing the result, if any, in @out.
 *
 * returns: whether there was an boolean member named @member_name.
 */
gboolean         gsk_xmlrpc_struct_peek_boolean(GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                gboolean        *out)
{
  GskXmlrpcValue *value = gsk_xmlrpc_struct_peek_any (structure, member_name, GSK_XMLRPC_BOOLEAN);
  if (value)
    *out = value->data.v_boolean;
  return value != NULL;
}

/**
 * gsk_xmlrpc_struct_peek_double:
 * @structure: the structure to lookup the member of.
 * @member_name: the value to retrieve.
 * @out: place to store the result.
 *
 * Lookup a named double member of a structure,
 * storing the result, if any, in @out.
 *
 * returns: whether there was an double member named @member_name.
 */
gboolean         gsk_xmlrpc_struct_peek_double (GskXmlrpcStruct *structure,
                                                const char      *member_name,
                                                double          *out)
{
  GskXmlrpcValue *value = gsk_xmlrpc_struct_peek_any (structure, member_name, GSK_XMLRPC_DOUBLE);
  if (value)
    *out = value->data.v_double;
  return value != NULL;
}


/**
 * gsk_xmlrpc_struct_peek_string:
 * @structure: the structure to lookup the member of.
 * @member_name: the value to retrieve.
 *
 * Lookup a named string member of a structure,
 * returning the result, or #NULL.
 * 
 * returns: the string value, or NULL.
 */
const char *     gsk_xmlrpc_struct_peek_string (GskXmlrpcStruct *structure,
                                                const char      *member_name)
{
  GskXmlrpcValue *value = gsk_xmlrpc_struct_peek_any (structure, member_name, GSK_XMLRPC_STRING);
  return value ? value->data.v_string : NULL;
}

/**
 * gsk_xmlrpc_struct_peek_date:
 * @structure: the structure to lookup the member of.
 * @member_name: the value to retrieve.
 * @out: place to store the result.
 *
 * Lookup a named date member of a structure,
 * storing the result, if any, in @out.
 *
 * returns: whether there was an date member named @member_name.
 */
gboolean         gsk_xmlrpc_struct_peek_date   (GskXmlrpcStruct *structure,
                                                const char      *member_name,
						gulong          *out)
{
  GskXmlrpcValue *value = gsk_xmlrpc_struct_peek_any (structure, member_name, GSK_XMLRPC_DATE);
  if (value)
    *out = value->data.v_date;
  return value != NULL;
}
/**
 * gsk_xmlrpc_struct_peek_data:
 * @structure: the structure to lookup the member of.
 * @member_name: the value to retrieve.
 *
 * Lookup a named binary-data member of a structure,
 * returning a reference to the #GByteArray result, or #NULL.
 * 
 * returns: a reference (not a copy!) to the binary data, or #NULL.
 */
const GByteArray*gsk_xmlrpc_struct_peek_data   (GskXmlrpcStruct *structure,
                                                const char      *member_name)
{
  GskXmlrpcValue *value = gsk_xmlrpc_struct_peek_any (structure, member_name, GSK_XMLRPC_BINARY_DATA);
  return value ? value->data.v_binary_data : NULL;
}
/**
 * gsk_xmlrpc_struct_peek_struct:
 * @structure: the structure to lookup the member of.
 * @member_name: the value to retrieve.
 *
 * Lookup a named substructure member of a structure,
 * returning a reference to the #GskXmlrpcStruct result, or #NULL.
 * 
 * returns: a reference (not a copy!) to the struct, or #NULL.
 */
GskXmlrpcStruct *gsk_xmlrpc_struct_peek_struct (GskXmlrpcStruct *structure,
                                                const char      *member_name)
{
  GskXmlrpcValue *value = gsk_xmlrpc_struct_peek_any (structure, member_name, GSK_XMLRPC_STRUCT);
  return value ? value->data.v_struct : NULL;
}
/**
 * gsk_xmlrpc_struct_peek_array:
 * @structure: the structure to lookup the member of.
 * @member_name: the value to retrieve.
 *
 * Lookup a named subarray member of a structure,
 * returning a reference to the #GskXmlrpcArray result, or #NULL.
 * 
 * returns: a reference (not a copy!) to the array, or #NULL.
 */
GskXmlrpcArray  *gsk_xmlrpc_struct_peek_array  (GskXmlrpcStruct *structure,
                                                const char      *member_name)
{
  GskXmlrpcValue *value = gsk_xmlrpc_struct_peek_any (structure, member_name, GSK_XMLRPC_ARRAY);
  return value ? value->data.v_array : NULL;
}


/**
 * gsk_xmlrpc_array_new:
 *
 * Allocate a new, empty array.*
 *
 * returns: the newly allocated arrays.
 */
GskXmlrpcArray  *gsk_xmlrpc_array_new          (void)
{
  return g_new0 (GskXmlrpcArray, 1);
}

/**
 * gsk_xmlrpc_array_free:
 * @array: the array to free.
 *
 * Free the array and all its values.
 */
void             gsk_xmlrpc_array_free         (GskXmlrpcArray  *array)
{
  unsigned i;
  for (i = 0; i < array->len; i++)
    gsk_xmlrpc_value_destruct (array->values + i);
  g_free (array->values);
  g_free (array);
}

static void
gsk_xmlrpc_array_add_value (GskXmlrpcArray *array,
			    GskXmlrpcValue  *value)
{
  if (array->len == array->alloced)
    {
      unsigned new_alloced = array->alloced;
      if (new_alloced == 0)
	new_alloced = 16;
      else
	new_alloced += new_alloced;
      array->values = g_renew (GskXmlrpcValue, array->values, new_alloced);
      array->alloced = new_alloced;
    }
  array->values[array->len] = *value;
  ++(array->len);
}

/**
 * gsk_xmlrpc_array_add_int32:
 * @array: array to which to append a value.
 * @value: integer value to append.
 *
 * Append an integer to an XMLRPC array.
 */
void             gsk_xmlrpc_array_add_int32    (GskXmlrpcArray  *array,
                                                gint32           value)
{
  GskXmlrpcValue v;
  v.type = GSK_XMLRPC_INT32;
  v.data.v_int32 = value;
  gsk_xmlrpc_array_add_value (array, &v);
}

/**
 * gsk_xmlrpc_array_add_boolean:
 * @array: array to which to append a value.
 * @value: boolean value to append.
 *
 * Append a boolean to an XMLRPC array.
 */
void             gsk_xmlrpc_array_add_boolean  (GskXmlrpcArray  *array,
                                                gboolean         value)
{
  GskXmlrpcValue v;
  v.type = GSK_XMLRPC_BOOLEAN;
  v.data.v_boolean = value;
  gsk_xmlrpc_array_add_value (array, &v);
}

/**
 * gsk_xmlrpc_array_add_double:
 * @array: array to which to append a value.
 * @value: double value to append.
 *
 * Append a double-precision floating-pointer value to an XMLRPC array.
 */
void             gsk_xmlrpc_array_add_double   (GskXmlrpcArray  *array,
                                                gdouble          value)
{
  GskXmlrpcValue v;
  v.type = GSK_XMLRPC_DOUBLE;
  v.data.v_double = value;
  gsk_xmlrpc_array_add_value (array, &v);
}

/**
 * gsk_xmlrpc_array_add_string:
 * @array: array to which to append a value.
 * @value: string value to append.
 * This value is copied: we do not take ownership.
 *
 * Append a string to an XMLRPC array.
 */
void             gsk_xmlrpc_array_add_string   (GskXmlrpcArray  *array,
                                                const char      *value)
{
  GskXmlrpcValue v;
  v.type = GSK_XMLRPC_STRING;
  v.data.v_string = g_strdup (value);
  gsk_xmlrpc_array_add_value (array, &v);
}

/**
 * gsk_xmlrpc_array_add_date:
 * @array: array to which to append a value.
 * @value: date/time value to append.
 *
 * Append a string to an XMLRPC array.
 */
void             gsk_xmlrpc_array_add_date     (GskXmlrpcArray  *array,
                                                gulong           value)
{
  GskXmlrpcValue v;
  v.type = GSK_XMLRPC_DATE;
  v.data.v_date = value;
  gsk_xmlrpc_array_add_value (array, &v);
}


/**
 * gsk_xmlrpc_array_add_data:
 * @array: array to which to append a value.
 * @data: a byte array containing arbitrary binary-data.
 * The XMLRPC array takes ownership of the data.
 *
 * Append a binary-data element to an XMLRPC array.
 * This take ownership of @data.
 */
void             gsk_xmlrpc_array_add_data     (GskXmlrpcArray  *array,
                                                GByteArray      *data)
{
  GskXmlrpcValue v;
  v.type = GSK_XMLRPC_BINARY_DATA;
  v.data.v_binary_data = data;
  gsk_xmlrpc_array_add_value (array, &v);
}

/**
 * gsk_xmlrpc_array_add_struct:
 * @array: array to which to append a value.
 * @substructure: structure to append to @array.
 * It will be freed by the @array.
 *
 * Append a binary-data element to an XMLRPC array.
 * This take ownership of @substructure.
 */
void             gsk_xmlrpc_array_add_struct   (GskXmlrpcArray  *array,
                                                GskXmlrpcStruct *substructure)
{
  GskXmlrpcValue v;
  v.type = GSK_XMLRPC_STRUCT;
  v.data.v_struct = substructure;
  gsk_xmlrpc_array_add_value (array, &v);
}

/**
 * gsk_xmlrpc_array_add_array:
 * @array: array to which to append a value.
 * @subarray: array to append to @array.
 * It will be freed by the @array.
 *
 * Append a binary-data element to an XMLRPC array.
 * This take ownership of @subarray.
 */
void             gsk_xmlrpc_array_add_array    (GskXmlrpcArray  *array,
                                                GskXmlrpcArray  *subarray)
{
  GskXmlrpcValue v;
  v.type = GSK_XMLRPC_ARRAY;
  v.data.v_array = subarray;
  gsk_xmlrpc_array_add_value (array, &v);
}


/**
 * gsk_xmlrpc_request_new:
 *
 * Allocate a new request.
 * At a very minimum, it should have a method name set with
 * gsk_xmlrpc_request_set_name().
 *
 * returns: a newly allocated request.
 */
GskXmlrpcRequest *
gsk_xmlrpc_request_new(GskXmlrpcStream *xmlrpc_stream)
{
  GskXmlrpcRequest *request = g_new (GskXmlrpcRequest, 1);
  request->magic = REQUEST_MAGIC;
  request->ref_count = 1;
  request->method_name = NULL;
  request->params = gsk_xmlrpc_array_new ();
  if (xmlrpc_stream != NULL)
    request->xmlrpc_stream = g_object_ref (xmlrpc_stream);
  else
    request->xmlrpc_stream = NULL;
  return request;
}

/**
 * gsk_xmlrpc_request_ref:
 * @request: the request to reference.
 *
 * Increase the reference count on @request.
 *
 * returns: the @request, for convenience.
 */
GskXmlrpcRequest   *gsk_xmlrpc_request_ref        (GskXmlrpcRequest   *request)
{
  g_assert (request->ref_count > 0);
  g_assert (request->magic == REQUEST_MAGIC);
  ++(request->ref_count);
  return request;
}

/**
 * gsk_xmlrpc_request_unref:
 * @request: the request to stop referencing.
 *
 * Decrease the reference count on @request,
 * and free it if the count reached 0.
 */
void             gsk_xmlrpc_request_unref      (GskXmlrpcRequest   *request)
{
  g_assert (request->ref_count > 0);
  g_assert (request->magic == REQUEST_MAGIC);
  --(request->ref_count);
  if (request->ref_count == 0)
    {
      if (request->xmlrpc_stream != NULL)
	g_object_unref (request->xmlrpc_stream);
      gsk_xmlrpc_array_free (request->params);
      g_free (request->method_name);
      request->magic = 0;
      g_free (request);
    }
}

/**
 * gsk_xmlrpc_request_set_name:
 * @request: the request whose method-name should be set.
 * @name: the name of the method to invoke.
 *
 * Set the method name for this request.
 */
void             gsk_xmlrpc_request_set_name   (GskXmlrpcRequest   *request,
                                                const char      *name)
{
  char *nname = g_strdup (name);
  g_free (request->method_name);
  request->method_name = nname;
}

/**
 * gsk_xmlrpc_request_add_int32:
 * @request: request to whose parameters a value shall be appended.
 * @value: integer value to append.
 *
 * Append an integer to an XMLRPC request.
 */
void             gsk_xmlrpc_request_add_int32  (GskXmlrpcRequest *request,
                                                gint32           value)
{
  gsk_xmlrpc_array_add_int32 (request->params, value);
}

/**
 * gsk_xmlrpc_request_add_boolean:
 * @request: request to whose parameters a value shall be appended.
 * @value: integer value to append.
 *
 * Append a boolean to an XMLRPC request.
 */
void             gsk_xmlrpc_request_add_boolean  (GskXmlrpcRequest *request,
                                                gboolean         value)
{
  gsk_xmlrpc_array_add_boolean (request->params, value);
}
/**
 * gsk_xmlrpc_request_add_double:
 * @request: request to whose parameters a value shall be appended.
 * @value: double value to append.
 *
 * Append a double to an XMLRPC request.
 */
void             gsk_xmlrpc_request_add_double (GskXmlrpcRequest *request,
                                                gdouble          value)
{
  gsk_xmlrpc_array_add_double (request->params, value);
}
/**
 * gsk_xmlrpc_request_add_string:
 * @request: request to whose parameters a value shall be appended.
 * @value: string value to append.
 *
 * Append a string to an XMLRPC request.
 */
void             gsk_xmlrpc_request_add_string (GskXmlrpcRequest *request,
                                                const char      *value)
{
  gsk_xmlrpc_array_add_string (request->params, value);
}
/**
 * gsk_xmlrpc_request_add_date:
 * @request: request to whose parameters a value shall be appended.
 * @value: date value to append.
 *
 * Append a date to an XMLRPC request.
 */
void             gsk_xmlrpc_request_add_date   (GskXmlrpcRequest *request,
                                                gulong           value)
{
  gsk_xmlrpc_array_add_date (request->params, value);
}
/**
 * gsk_xmlrpc_request_add_data:
 * @request: request to whose parameters a value shall be appended.
 * @data: GByteArray to append: it shall be freed by the request,
 * that is, there is a transfer of ownership.
 *
 * Append binary data to an XMLRPC request (it will be base64 encoded transparently).
 */
void             gsk_xmlrpc_request_add_data (GskXmlrpcRequest *request,
                                              GByteArray      *data)
{
  gsk_xmlrpc_array_add_data (request->params, data);
}
/**
 * gsk_xmlrpc_request_add_struct:
 * @request: request to whose parameters a value shall be appended.
 * @substructure: structure to append to @array.
 * This will be freed by the @request automatically: a transfer of ownership
 * occurs in this function.
 *
 * Add a structure as a parameter to the request.
 */
void             gsk_xmlrpc_request_add_struct(GskXmlrpcRequest *request,
                                              GskXmlrpcStruct *substructure)
{
  gsk_xmlrpc_array_add_struct (request->params, substructure);
}
/**
 * gsk_xmlrpc_request_add_array:
 * @request: request to whose parameters a value shall be appended.
 * @array: array to append to @request's parmeter list.
 * This will be freed by the @request automatically: a transfer of ownership
 * occurs in this function.
 *
 * Add a structure as a parameter to the request.
 */
void             gsk_xmlrpc_request_add_array(GskXmlrpcRequest *request,
                                              GskXmlrpcArray  *array)
{
  gsk_xmlrpc_array_add_array (request->params, array);
}

/**
 * gsk_xmlrpc_response_new:
 * 
 * Allocate a new response.
 *
 * returns: the newly allocated response which has no parameters
 * and no fault.
 */
GskXmlrpcResponse   *gsk_xmlrpc_response_new        (void)
{
  GskXmlrpcResponse *response = g_new (GskXmlrpcResponse, 1);
  response->magic = RESPONSE_MAGIC;
  response->ref_count = 1;
  response->params = gsk_xmlrpc_array_new ();
  response->has_fault = FALSE;
  return response;
}

/**
 * gsk_xmlrpc_response_ref:
 * @response: the response to reference.
 *
 * Increase the reference count on @response.
 *
 * returns: the @response, for convenience.
 */
GskXmlrpcResponse   *gsk_xmlrpc_response_ref        (GskXmlrpcResponse   *response)
{
  g_assert (response->ref_count > 0);
  g_assert (response->magic == RESPONSE_MAGIC);
  ++(response->ref_count);
  return response;
}

/**
 * gsk_xmlrpc_response_unref:
 * @response: the response to stop referencing.
 *
 * Decrease the reference count on @response,
 * and free it if the count reached 0.
 */
void             gsk_xmlrpc_response_unref      (GskXmlrpcResponse   *response)
{
  g_assert (response->ref_count > 0);
  g_assert (response->magic == RESPONSE_MAGIC);
  --(response->ref_count);
  if (response->ref_count == 0)
    {
      gsk_xmlrpc_array_free (response->params);
      if (response->has_fault)
	gsk_xmlrpc_value_destruct (&response->fault);
      response->magic = 0;
      g_free (response);
    }
}

/**
 * gsk_xmlrpc_response_add_int32:
 * @response: response to whose parameters a value shall be appended.
 * @value: integer value to append.
 *
 * Append an integer to an XMLRPC response.
 */
void             gsk_xmlrpc_response_add_int32  (GskXmlrpcResponse *response,
                                                gint32           value)
{
  gsk_xmlrpc_array_add_int32 (response->params, value);
}
/**
 * gsk_xmlrpc_response_add_boolean:
 * @response: response to whose parameters a value shall be appended.
 * @value: integer value to append.
 *
 * Append a boolean to an XMLRPC response.
 */
void             gsk_xmlrpc_response_add_boolean  (GskXmlrpcResponse *response,
                                                gboolean           value)
{
  gsk_xmlrpc_array_add_boolean (response->params, value);
}
/**
 * gsk_xmlrpc_response_add_double:
 * @response: response to whose parameters a value shall be appended.
 * @value: double value to append.
 *
 * Append a double to an XMLRPC response.
 */
void             gsk_xmlrpc_response_add_double (GskXmlrpcResponse *response,
                                                gdouble          value)
{
  gsk_xmlrpc_array_add_double (response->params, value);
}
/**
 * gsk_xmlrpc_response_add_string:
 * @response: response to whose parameters a value shall be appended.
 * @value: string value to append.
 *
 * Append a string to an XMLRPC response.
 */
void             gsk_xmlrpc_response_add_string (GskXmlrpcResponse *response,
                                                const char      *value)
{
  gsk_xmlrpc_array_add_string (response->params, value);
}
/**
 * gsk_xmlrpc_response_add_date:
 * @response: response to whose parameters a value shall be appended.
 * @value: date value to append.
 *
 * Append a date to an XMLRPC response.
 */
void             gsk_xmlrpc_response_add_date   (GskXmlrpcResponse *response,
                                                gulong           value)
{
  gsk_xmlrpc_array_add_date (response->params, value);
}

/**
 * gsk_xmlrpc_response_add_data:
 * @response: response to whose parameters a value shall be appended.
 * @data: GByteArray to append: it shall be freed by the response,
 * that is, there is a transfer of ownership.
 *
 * Append binary data to an XMLRPC response (it will be base64 encoded transparently).
 */
void             gsk_xmlrpc_response_add_data (GskXmlrpcResponse *response,
                                              GByteArray      *data)
{
  gsk_xmlrpc_array_add_data (response->params, data);
}
/**
 * gsk_xmlrpc_response_add_struct:
 * @response: response to whose parameters a value shall be appended.
 * @substructure: structure to append to @array.
 * This will be freed by the @response automatically: a transfer of ownership
 * occurs in this function.
 *
 * Add a structure as a parameter to the response.
 */
void             gsk_xmlrpc_response_add_struct(GskXmlrpcResponse *response,
                                              GskXmlrpcStruct *substructure)
{
  gsk_xmlrpc_array_add_struct (response->params, substructure);
}
/**
 * gsk_xmlrpc_response_add_array:
 * @response: response to whose parameters a value shall be appended.
 * @array: array to append to @response's parmeter list.
 * This will be freed by the @response automatically: a transfer of ownership
 * occurs in this function.
 *
 * Add a structure as a parameter to the response.
 */
void             gsk_xmlrpc_response_add_array(GskXmlrpcResponse *response,
                                              GskXmlrpcArray  *array)
{
  gsk_xmlrpc_array_add_array (response->params, array);
}


#if 0
void             gsk_xmlrpc_response_fault_int32(GskXmlrpcResponse *response,
                                              gint32           value)
{
  if (response->has_fault)
    gsk_xmlrpc_value_destruct (&response->fault);
  response->has_fault = TRUE;
  response->fault.type = GSK_XMLRPC_INT32;
  response->fault.data.v_int32 = value;
}

void             gsk_xmlrpc_response_fault_double(GskXmlrpcResponse *response,
                                              gdouble          value)
{
  if (response->has_fault)
    gsk_xmlrpc_value_destruct (&response->fault);
  response->has_fault = TRUE;
  response->fault.type = GSK_XMLRPC_DOUBLE;
  response->fault.data.v_int32 = value;
}
void             gsk_xmlrpc_response_fault_string(GskXmlrpcResponse *response,
                                              const char      *value)
{
  if (response->has_fault)
    gsk_xmlrpc_value_destruct (&response->fault);
  response->has_fault = TRUE;
  response->fault.type = GSK_XMLRPC_STRING;
  response->fault.data.v_string = g_strdup (value);
}
void             gsk_xmlrpc_response_fault_date (GskXmlrpcResponse *response,
                                              gulong           value)
{
  if (response->has_fault)
    gsk_xmlrpc_value_destruct (&response->fault);
  response->has_fault = TRUE;
  response->fault.type = GSK_XMLRPC_DATE;
  response->fault.data.v_date = value;
}

/* these take ownership of second argument */
void             gsk_xmlrpc_response_fault_data (GskXmlrpcResponse *response,
                                              GByteArray      *data)
{
  if (response->has_fault)
    gsk_xmlrpc_value_destruct (&response->fault);
  response->has_fault = TRUE;
  response->fault.type = GSK_XMLRPC_BINARY_DATA;
  response->fault.data.v_binary_data = data;
}
void             gsk_xmlrpc_response_fault_array(GskXmlrpcResponse *response,
                                              GskXmlrpcArray  *array)
{
  if (response->has_fault)
    gsk_xmlrpc_value_destruct (&response->fault);
  response->has_fault = TRUE;
  response->fault.type = GSK_XMLRPC_ARRAY;
  response->fault.data.v_array = array;
}
#endif
/**
 * gsk_xmlrpc_response_fault:
 * @response: the response to affect.
 * @structure: the fault information.
 * This should be a struct with
 * at most an integer 'faultCode'
 * and a string 'faultString'.
 *
 * Indicate that an error occurred trying to
 * process the XMLRPC request.
 */
void             gsk_xmlrpc_response_fault   (GskXmlrpcResponse *response,
                                              GskXmlrpcStruct *structure)
{
  if (response->has_fault)
    gsk_xmlrpc_value_destruct (&response->fault);
  response->has_fault = TRUE;
  response->fault.type = GSK_XMLRPC_STRUCT;
  response->fault.data.v_struct = structure;
}

/* --- parsing --- */
/* States for a nesting level in the ValueStack.
 * These states apply to 'is_structure' ValueStacks. */
enum
{
  STRUCT_STATE_OUTER,
  STRUCT_STATE_IN_MEMBER,
  STRUCT_STATE_IN_MEMBERNAME,
  STRUCT_STATE_IN_VALUE,
  STRUCT_STATE_IN_TYPED_VALUE
};

/* States for a nesting level in the ValueStack.
 * These states apply to '!is_structure' ValueStacks. */
enum
{
  ARRAY_STATE_OUTER,
  ARRAY_STATE_IN_DATA,
  ARRAY_STATE_IN_VALUE,
  ARRAY_STATE_IN_TYPED_VALUE
};

/* ValueStacks: handle one nesting level of either
   an array or a structure. */
typedef struct _ValueStack ValueStack;
struct _ValueStack
{
  gboolean is_structure;	/* if not, it's an array */
  gpointer cur;		/* either GskXmlrpcArray or GskXmlrpcStruct */

  guint state;

  /* for structures only: the current member-name */
  char *name;

  gboolean got_value;
  GskXmlrpcValue value;

  ValueStack *up;
};

enum
{
  OUTER,

  REQUEST_IN_METHODCALL,
  REQUEST_IN_METHODNAME,
  REQUEST_IN_PARAMS,
  REQUEST_IN_PARAM,
  REQUEST_IN_PARAM_VALUE,
  REQUEST_IN_PARAM_TYPED_VALUE,

  RESPONSE_IN_METHODRESPONSE,
  RESPONSE_IN_PARAMS,
  RESPONSE_IN_PARAM,
  RESPONSE_IN_PARAM_VALUE,
  RESPONSE_IN_PARAM_TYPED_VALUE,
  RESPONSE_IN_FAULT,
  RESPONSE_IN_FAULT_VALUE,
  RESPONSE_IN_FAULT_TYPED_VALUE,
};

struct _GskXmlrpcParser
{
  ValueStack *stack;
  /* This is so the request & response 
   * can ref the stream so it does not
   * get shut down
   */
  GskXmlrpcStream *xmlrpc_stream;
  guint state;
  gboolean got_cur_param;
  GskXmlrpcValue cur_param;
  gpointer cur_message;

  GMarkupParseContext *context;
  GQueue *messages;
};

static gboolean
deal_with_stack_and_type (GskXmlrpcParser *parser,
			  const char *element_name,
			  GskXmlrpcValue *value_init_out,
			  GError **error)
{
  ValueStack *old = parser->stack;

  memset (value_init_out, 0, sizeof (GskXmlrpcValue));

  if (strcmp (element_name, "i4") == 0
   || strcmp (element_name, "int") == 0)
    value_init_out->type = GSK_XMLRPC_INT32;
  else if (strcmp (element_name, "boolean") == 0)
    value_init_out->type = GSK_XMLRPC_BOOLEAN;
  else if (strcmp (element_name, "double") == 0)
    value_init_out->type = GSK_XMLRPC_DOUBLE;
  else if (strcmp (element_name, "dateTime.iso8601") == 0)
    value_init_out->type = GSK_XMLRPC_DATE;
  else if (strcmp (element_name, "base64") == 0)
    value_init_out->type = GSK_XMLRPC_BINARY_DATA;
  else if (strcmp (element_name, "string") == 0)
    value_init_out->type = GSK_XMLRPC_STRING;
  else if (strcmp (element_name, "struct") == 0)
    value_init_out->type = GSK_XMLRPC_STRUCT;
  else if (strcmp (element_name, "array") == 0)
    value_init_out->type = GSK_XMLRPC_ARRAY;
  else
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
		   GSK_ERROR_BAD_FORMAT,
		   _("in XML-RPC, expected type tag in <value> tag, got <%s>"),
		   element_name);
    }

  if (value_init_out->type == GSK_XMLRPC_STRUCT
   || value_init_out->type == GSK_XMLRPC_ARRAY)
    {
      parser->stack = g_new (ValueStack, 1);
      parser->stack->up = old;
      parser->stack->name = NULL;
      parser->stack->got_value = FALSE;
      if (value_init_out->type == GSK_XMLRPC_STRUCT)
	{
	  parser->stack->is_structure = TRUE;
	  parser->stack->cur = gsk_xmlrpc_struct_new ();
	  parser->stack->state = STRUCT_STATE_OUTER;
	  value_init_out->data.v_struct = parser->stack->cur;
	}
      else /*if (*type_out == GSK_XMLRPC_ARRAY)*/
	{
	  parser->stack->is_structure = FALSE;
	  parser->stack->cur = gsk_xmlrpc_array_new ();
	  parser->stack->state = ARRAY_STATE_OUTER;
	  value_init_out->data.v_array = parser->stack->cur;
	}
    }
  return TRUE;
}

#if DEBUG_XMLRPC_PARSER
static char *
get_state_string (GskXmlrpcParser *parser)
{
  GString *str = g_string_new ("");
  switch (parser->state)
    {
#define ST(st) case st: g_string_append(str, #st); break;
  ST(OUTER)
  ST(REQUEST_IN_METHODCALL)
  ST(REQUEST_IN_METHODNAME)
  ST(REQUEST_IN_PARAMS)
  ST(REQUEST_IN_PARAM)
  ST(REQUEST_IN_PARAM_VALUE)
  ST(REQUEST_IN_PARAM_TYPED_VALUE)
  ST(RESPONSE_IN_METHODRESPONSE)
  ST(RESPONSE_IN_PARAMS)
  ST(RESPONSE_IN_PARAM)
  ST(RESPONSE_IN_PARAM_VALUE)
  ST(RESPONSE_IN_PARAM_TYPED_VALUE)
  ST(RESPONSE_IN_FAULT)
  ST(RESPONSE_IN_FAULT_VALUE)
  ST(RESPONSE_IN_FAULT_TYPED_VALUE)
    default: g_assert_not_reached();
    }

  ValueStack *stack = parser->stack;
  while (stack)
    {
      g_string_append(str, " << ");
      if (stack->is_structure)
	{
	  switch (stack->state)
	    {
	    ST(STRUCT_STATE_OUTER)
	    ST(STRUCT_STATE_IN_MEMBER)
	    ST(STRUCT_STATE_IN_MEMBERNAME)
	    ST(STRUCT_STATE_IN_VALUE)
	    ST(STRUCT_STATE_IN_TYPED_VALUE)
	    default: g_assert_not_reached();
	    }
	}
      else
	{
	  switch (stack->state)
	    {
            ST(ARRAY_STATE_OUTER)
            ST(ARRAY_STATE_IN_DATA)
            ST(ARRAY_STATE_IN_VALUE)
            ST(ARRAY_STATE_IN_TYPED_VALUE)
	    default: g_assert_not_reached();
	    }
	}
      stack = stack->up;
    }
#undef ST
  return g_string_free (str, FALSE);
}
#endif	 /* DEBUG_XMLRPC_PARSER */

static void 
parser_start_element   (GMarkupParseContext *context,
			const gchar         *element_name,
			const gchar        **attribute_names,
			const gchar        **attribute_values,
			gpointer             user_data,
                        GError             **error)
{
  GskXmlrpcParser *parser = user_data;
#if DEBUG_XMLRPC_PARSER
    {
      char *state = get_state_string(parser);
      g_message ("<%s>: %s",element_name,state);
      g_free (state);
    }
#endif
  switch (parser->state)
    {
    case OUTER:
      if (strcmp (element_name, "methodCall") == 0)
	{
	  parser->state = REQUEST_IN_METHODCALL;
	  /* The GskXmlrpcStream is needed so we can ref it
	   * and prevent a stream shutdown 
	   */
	  parser->cur_message = gsk_xmlrpc_request_new (parser->xmlrpc_stream);
	}
      else if (strcmp (element_name, "methodResponse") == 0)
	{
	  parser->state = RESPONSE_IN_METHODRESPONSE;
	  parser->cur_message = gsk_xmlrpc_response_new ();
	}
      else
	g_set_error (error, GSK_G_ERROR_DOMAIN,
		     GSK_ERROR_BAD_FORMAT,
		     _("root element in XML-RPC must be methodCall, got %s"),
		     element_name);
      return;

    case REQUEST_IN_METHODCALL:
      if (strcmp (element_name, "methodName") == 0)
	parser->state = REQUEST_IN_METHODNAME;
      else if (strcmp (element_name, "params") == 0)
	parser->state = REQUEST_IN_PARAMS;
      else
	g_set_error (error, GSK_G_ERROR_DOMAIN,
		     GSK_ERROR_BAD_FORMAT,
		     _("in XML-RPC, <methodCall> only contains <methodName> and <params>, got %s"),
		     element_name);
      return;
    case REQUEST_IN_METHODNAME:
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		   _("in XML-RPC, <methodName> cannot contain subtags, got <%s>"),
		   element_name);
      return;
    case RESPONSE_IN_METHODRESPONSE:
      if (strcmp (element_name, "params") == 0)
	parser->state = RESPONSE_IN_PARAMS;
      else if (strcmp (element_name, "fault") == 0)
	parser->state = RESPONSE_IN_FAULT;
      else
	{
	  g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		       _("expected <fault> or <params>, got <%s>"),
		       element_name);
	}
      return;
    case RESPONSE_IN_PARAMS:
    case REQUEST_IN_PARAMS:
      if (strcmp (element_name, "param") == 0)
	parser->state = parser->state == REQUEST_IN_PARAMS ? REQUEST_IN_PARAM
	                                                   : RESPONSE_IN_PARAM;
      else
	g_set_error (error, GSK_G_ERROR_DOMAIN,
		     GSK_ERROR_BAD_FORMAT,
		     _("in XML-RPC, <params> only contains <param> tags, got <%s>"),
		     element_name);
      return;
    case RESPONSE_IN_PARAM:
    case REQUEST_IN_PARAM:
      if (strcmp (element_name, "value") == 0)
	parser->state
	  = (parser->state == REQUEST_IN_PARAM) ? REQUEST_IN_PARAM_VALUE
					        : RESPONSE_IN_PARAM_VALUE;
      else
	g_set_error (error, GSK_G_ERROR_DOMAIN,
		     GSK_ERROR_BAD_FORMAT,
		     _("in XML-RPC, <param> only contains <value> tags, got <%s>"),
		     element_name);
      return;
    case RESPONSE_IN_PARAM_VALUE:
    case REQUEST_IN_PARAM_VALUE:
    case RESPONSE_IN_FAULT_VALUE:
      {
	GskXmlrpcValue *value = (parser->state == RESPONSE_IN_FAULT_VALUE)
	                      ? &((GskXmlrpcResponse*)parser->cur_message)->fault
			      : &parser->cur_param;
	if (deal_with_stack_and_type (parser, element_name, value, error))
	  {
	    if (parser->state == REQUEST_IN_PARAM_VALUE)
	      parser->state = REQUEST_IN_PARAM_TYPED_VALUE;
	    else if (parser->state == RESPONSE_IN_PARAM_VALUE)
	      parser->state = RESPONSE_IN_PARAM_TYPED_VALUE;
	    else
	      parser->state = RESPONSE_IN_FAULT_TYPED_VALUE;
	  }
	return;
      }
    case REQUEST_IN_PARAM_TYPED_VALUE:
    case RESPONSE_IN_PARAM_TYPED_VALUE:
    case RESPONSE_IN_FAULT_TYPED_VALUE:
      if (parser->stack == NULL)
	{
	  g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		       _("in XMLRPC, subtags not allowed in unstructured type, got <%s>"),
		       element_name);
	  return;
	}
      if (parser->stack->is_structure)
	{
	  switch (parser->stack->state)
	    {
	    case STRUCT_STATE_OUTER:
	      if (strcmp (element_name, "member") == 0)
		parser->stack->state = STRUCT_STATE_IN_MEMBER;
	      else
		g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
			     _("in XMLRPC, got <%s> where <member> expected"),
			     element_name);
	      return;
	    case STRUCT_STATE_IN_MEMBER:
	      if (strcmp (element_name, "value") == 0)
		parser->stack->state = STRUCT_STATE_IN_VALUE;
	      else if (strcmp (element_name, "name") == 0)
		parser->stack->state = STRUCT_STATE_IN_MEMBERNAME;
	      else
		g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
			     _("in XMLRPC, got <%s> where <value> or <name> expected"),
			     element_name);
	      return;
	    case STRUCT_STATE_IN_VALUE:
	      {
		ValueStack *cur_stack = parser->stack;

		if (!deal_with_stack_and_type (parser, element_name, 
					       &cur_stack->value, error))
		  return;
		cur_stack->state = STRUCT_STATE_IN_TYPED_VALUE;
		return;
	      }

	    case STRUCT_STATE_IN_TYPED_VALUE:
	    case STRUCT_STATE_IN_MEMBERNAME:
	      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
			   _("got <%s> where only text expected"),
			   element_name);
	      return;
	    }
	}
      else /* it's an array */
	{
	  switch (parser->stack->state)
	    {
	    case ARRAY_STATE_OUTER:
	      if (strcmp (element_name, "data") == 0)
		parser->stack->state = ARRAY_STATE_IN_DATA;
	      else
		g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
			     _("in XMLRPC, got <%s> where <data> expected"),
			     element_name);
	      return;
	    case ARRAY_STATE_IN_DATA:
	      if (strcmp (element_name, "value") == 0)
		parser->stack->state = ARRAY_STATE_IN_VALUE;
	      else
		g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
			     _("in XMLRPC, got <%s> where <value> expected"),
			     element_name);
	      return;
	    case ARRAY_STATE_IN_VALUE:
	      {
		ValueStack *cur_stack = parser->stack;
		if (!deal_with_stack_and_type (parser, element_name, 
					       &cur_stack->value, error))
		  return;
		cur_stack->state = ARRAY_STATE_IN_TYPED_VALUE;
		return;
	      }

		/* no open tags allowed in these states */
	    case ARRAY_STATE_IN_TYPED_VALUE:
	      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
			   _("got <%s> where only text expected"),
			   element_name);
	      return;
	    }
	}
      break;
    case RESPONSE_IN_FAULT:
      if (strcmp (element_name, "value") == 0)
	parser->state = RESPONSE_IN_FAULT_VALUE;
      else
	g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		     _("only <value> allowed under <fault>, got <%s>"),
		     element_name);
      return;
    default:
      g_assert_not_reached ();
    }
}

#define ASSERT_ELEMENT_NAME(name) g_assert(strcmp(element_name,name)==0)

static void 
parser_end_element     (GMarkupParseContext *context,
			const gchar         *element_name,
			gpointer             user_data,
			GError             **error)
{
  GskXmlrpcParser *parser = user_data;
#if DEBUG_XMLRPC_PARSER
    {
      char *state = get_state_string(parser);
      g_message ("</%s>: %s",element_name,state);
      g_free (state);
    }
#endif
  switch (parser->state)
    {
    case OUTER:
      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		   _("got unexpected closing tag at outer scope (</%s>)"),
		   element_name);
      return;
    case REQUEST_IN_METHODCALL:
    case RESPONSE_IN_METHODRESPONSE:
      g_queue_push_tail (parser->messages, parser->cur_message);
      parser->cur_message = NULL;
      parser->state = OUTER;
      return;
    case REQUEST_IN_METHODNAME:
      parser->state = REQUEST_IN_METHODCALL;
      ASSERT_ELEMENT_NAME("methodName");
      break;
    case REQUEST_IN_PARAMS:
      parser->state = REQUEST_IN_METHODCALL;
      ASSERT_ELEMENT_NAME("params");
      break;
    case RESPONSE_IN_PARAMS:
      parser->state = RESPONSE_IN_METHODRESPONSE;
      ASSERT_ELEMENT_NAME("params");
      break;
    case REQUEST_IN_PARAM:
    case RESPONSE_IN_PARAM:
      if (!parser->got_cur_param)
	{
	  g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		       _("missing <value> tag in <param> tag"));
	  return;
	}

      /* Append param to set */
      {
	GskXmlrpcArray *array = (parser->state == REQUEST_IN_PARAM)
	                         ? ((GskXmlrpcRequest*)(parser->cur_message))->params
	                         : ((GskXmlrpcResponse*)(parser->cur_message))->params;
	gsk_xmlrpc_array_add_value (array, &parser->cur_param);
      }
      parser->got_cur_param = FALSE;

      parser->state = (parser->state == REQUEST_IN_PARAM)
	              ? REQUEST_IN_PARAMS : RESPONSE_IN_PARAMS;
      ASSERT_ELEMENT_NAME("param");
      break;
    case REQUEST_IN_PARAM_VALUE:
    case RESPONSE_IN_PARAM_VALUE:
      if (!parser->got_cur_param)
	{
	  // XXX: default to string????
	  g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		       _("missing 'type' tag in 'value'"));
	  return;
	}
      ASSERT_ELEMENT_NAME("value");
      parser->state = (parser->state == REQUEST_IN_PARAM_VALUE)
	               ? REQUEST_IN_PARAM : RESPONSE_IN_PARAM;
      break;
    case REQUEST_IN_PARAM_TYPED_VALUE:
    case RESPONSE_IN_PARAM_TYPED_VALUE:
    case RESPONSE_IN_FAULT_TYPED_VALUE:
      {
	ValueStack *orig_stack = parser->stack;
	if (parser->stack != NULL)
	  {
	    if (parser->stack->is_structure)
	      {
		switch (parser->stack->state)
		  {
		  case STRUCT_STATE_OUTER:
		    /* Ok, pop stack. */
		    {
		      ValueStack *to_kill = parser->stack;
		      parser->stack = to_kill->up;
		      g_assert (to_kill->got_value == FALSE);
		      g_assert (to_kill->name == NULL);
		      g_free (to_kill);
		      ASSERT_ELEMENT_NAME("struct");
		    }
		    break;
		  case STRUCT_STATE_IN_MEMBER:
		    {
		      if (parser->stack->name == NULL
		       || parser->stack->got_value == FALSE)
			{
			  g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
				       _("required field (<name> or <value>) missing in struct's member"));
			  return;
			}
		      gsk_xmlrpc_struct_add_value_steal_name (parser->stack->cur, parser->stack->name, &parser->stack->value);
		      parser->stack->got_value = FALSE;
		      parser->stack->name = NULL;
		      parser->stack->state = STRUCT_STATE_OUTER;
		      ASSERT_ELEMENT_NAME("member");
		    }
		    return;
		  case STRUCT_STATE_IN_MEMBERNAME:
		    if (parser->stack->name == NULL)
		      {
			g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
				     _("missing text in <name> tag"));
			return;
		      }
		    parser->stack->state = STRUCT_STATE_IN_MEMBER;
		    ASSERT_ELEMENT_NAME("name");
		    return;
		  case STRUCT_STATE_IN_VALUE:
		    if (parser->stack->got_value == FALSE)
		      {
			parser->stack->got_value = TRUE;
			parser->stack->value.type = GSK_XMLRPC_STRING;
			parser->stack->value.data.v_string = g_strdup ("");
		      }
		    ASSERT_ELEMENT_NAME("value");
		    parser->stack->state = STRUCT_STATE_IN_MEMBER;
		    return;
		  case STRUCT_STATE_IN_TYPED_VALUE:
		    parser->stack->got_value = TRUE;
		    parser->stack->state = STRUCT_STATE_IN_VALUE;
		    return;
		  default:
		      g_assert_not_reached ();
		  }
	      }
	    else
	      {
		switch (parser->stack->state)
		  {
		  case ARRAY_STATE_OUTER:
		    {
		      ValueStack *to_kill = parser->stack;
		      parser->stack = to_kill->up;
		      g_assert (to_kill->got_value == FALSE);
		      g_free (to_kill);
		    }
		    ASSERT_ELEMENT_NAME("array");
		    break;
		  case ARRAY_STATE_IN_DATA:
		    parser->stack->state = ARRAY_STATE_OUTER;
		    ASSERT_ELEMENT_NAME("data");
		    break;
		  case ARRAY_STATE_IN_VALUE:
		    if (parser->stack->got_value == FALSE)
		      {
			parser->stack->got_value = TRUE;
			parser->stack->value.type = GSK_XMLRPC_STRING;
			parser->stack->value.data.v_string = g_strdup ("");
			return;
		      }
		    gsk_xmlrpc_array_add_value (parser->stack->cur, &parser->stack->value);
		    parser->stack->got_value = FALSE;
		    parser->stack->state = ARRAY_STATE_IN_DATA;
		    ASSERT_ELEMENT_NAME("value");
		    return;
		  case ARRAY_STATE_IN_TYPED_VALUE:
		    parser->stack->got_value = TRUE;
		    parser->stack->state = ARRAY_STATE_IN_VALUE;
		    return;
		  default:
		      g_assert_not_reached ();
		  }
	      }
	  }
	if (parser->stack == NULL)
	  {
	    parser->got_cur_param = TRUE;
	    parser->state = (parser->state == REQUEST_IN_PARAM_TYPED_VALUE) ? REQUEST_IN_PARAM_VALUE :
			    (parser->state == RESPONSE_IN_PARAM_TYPED_VALUE) ?  RESPONSE_IN_PARAM_VALUE :
			    RESPONSE_IN_FAULT_VALUE;


	    // XXX: NULL string => "" conversion?  or next case?
	  }
	else if (parser->stack != orig_stack)
	  {
	    if (parser->stack->is_structure)
	      {
		g_assert (parser->stack->state == STRUCT_STATE_IN_TYPED_VALUE);
		parser->stack->state = STRUCT_STATE_IN_VALUE;
	      }
	    else
	      {
		g_assert (parser->stack->state == ARRAY_STATE_IN_TYPED_VALUE);
		parser->stack->state = ARRAY_STATE_IN_VALUE;
	      }
	    parser->stack->got_value = TRUE;
	  }
      }
      return;
    case RESPONSE_IN_FAULT:
      parser->state = RESPONSE_IN_METHODRESPONSE;
      ASSERT_ELEMENT_NAME("fault");
      return;
    case RESPONSE_IN_FAULT_VALUE:
      ((GskXmlrpcResponse*)(parser->cur_message))->has_fault = TRUE;
      parser->state = RESPONSE_IN_FAULT;
      ASSERT_ELEMENT_NAME("value");
      return;
    default: g_assert_not_reached ();
    }
}
static gboolean
is_whitespace (const char *txt, unsigned len)
{
  while (len--)
    {
      if (!isspace (*txt++))
	return FALSE;
    }
  return TRUE;
}

static gboolean
parse_value_from_string (const char *text, gsize text_len,
			 GskXmlrpcValue *val, GError **error)
{
  switch (val->type)
    {
    case GSK_XMLRPC_INT32:
      {
	char *tmp;
	char *end;
	if (text_len > 1000)
	  {
	    g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
			 _("integer value way too long"));
	    return FALSE;
	  }
	tmp = g_alloca (text_len + 1);
	memcpy (tmp, text, text_len);
	tmp[text_len] = 0;
	val->data.v_int32 = strtol (tmp, &end, 10);
	if (tmp == end)
	  {
	    g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
			 _("error parsing int from '%.*s'"), text_len, text);
	    return FALSE;
	  }
	break;
      }
    case GSK_XMLRPC_BOOLEAN:
      if (text_len != 1
       || (text[0] != '0' && text[0] != '1'))
	{
	  g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		       _("unexpected boolean value '%.*s'"), text_len, text);
	  return FALSE;
	}
      val->data.v_boolean = (text[0] == '1');
      break;
    case GSK_XMLRPC_DOUBLE:
      {
	char *tmp;
	char *end;
	if (text_len > 1000)
	  {
	    g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
			 _("double value way too long"));
	    return FALSE;
	  }
	tmp = g_alloca (text_len + 1);
	memcpy (tmp, text, text_len);
	tmp[text_len] = 0;
	val->data.v_double = strtod (tmp, &end);
	if (tmp == end)
	  {
	    g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
			 _("error parsing double from '%.*s'"), text_len, text);
	    return FALSE;
	  }
	break;
      }
      break;
    case GSK_XMLRPC_STRING:
      val->data.v_string = g_strndup (text, text_len);
      break;
    case GSK_XMLRPC_DATE:
      {
	time_t t;
	char *tmp = g_alloca (text_len + 1);
	memcpy (tmp, text, text_len);
	tmp[text_len] = 0;
	if (!gsk_date_parse_timet (tmp, &t, GSK_DATE_FORMAT_ISO8601))
	  {
	    g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
			 _("error parsing ISO8601 date"));
	    return FALSE;
	  }
	val->data.v_date = t;
      }
      break;
    case GSK_XMLRPC_BINARY_DATA:
      {
	GByteArray *array = g_byte_array_new ();
	g_byte_array_set_size (array, GSK_BASE64_GET_MAX_DECODED_LEN (text_len));
	g_byte_array_set_size (array, gsk_base64_decode ((char*)array->data, array->len, text, text_len));
	val->data.v_binary_data = array;
	break;
      }
    case GSK_XMLRPC_STRUCT:
    case GSK_XMLRPC_ARRAY:
      if (!is_whitespace (text, text_len))
	g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		     _("struct/array had raw text inside it"));
      return FALSE;
    }
  return TRUE;
}

/* Called for character data */
/* text is not nul-terminated */
static void 
parser_text            (GMarkupParseContext *context,
			const gchar         *text,
			gsize                text_len,  
			gpointer             user_data,
			GError             **error)
{
  GskXmlrpcParser *parser = user_data;
  gboolean got_implicit_string = FALSE;
#if DEBUG_XMLRPC_PARSER
    {
      char *state = get_state_string(parser);
      g_message ("%s: '%.*s'", state, text_len, text);
      g_free (state);
    }
#endif
  switch (parser->state)
    {
    case OUTER:
      if (!is_whitespace (text, text_len))
	g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		     _("did not expect non-whitespace text at outer scope"));
      return;
    case REQUEST_IN_METHODCALL:
      if (!is_whitespace (text, text_len))
	g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		     _("did not expect non-whitespace text in <methodCall>"));
      return;
    case RESPONSE_IN_METHODRESPONSE:
      if (!is_whitespace (text, text_len))
	g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		     _("did not expect non-whitespace text in <methodResponse>"));
      return;
    case REQUEST_IN_METHODNAME:
      {
	GskXmlrpcRequest *req = parser->cur_message;
	g_free (req->method_name);
	req->method_name = g_strndup (text, text_len);
	return;
      }
    case RESPONSE_IN_PARAMS:
    case REQUEST_IN_PARAMS:
      if (!is_whitespace (text, text_len))
	g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		     _("did not expect non-whitespace text in <params>"));
      return;
    case REQUEST_IN_PARAM:
    case RESPONSE_IN_PARAM:
      if (!is_whitespace (text, text_len))
	g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		     _("did not expect non-whitespace text in <param>"));
      return;
    case RESPONSE_IN_FAULT:
    case RESPONSE_IN_FAULT_VALUE:
      if (!is_whitespace (text, text_len))
	g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
		     _("did not expect non-whitespace text in <fault>"));
      return;
    case RESPONSE_IN_PARAM_VALUE:
    case REQUEST_IN_PARAM_VALUE:
      if (is_whitespace (text, text_len))
	return;
      {
	GskXmlrpcValue *value = (parser->state == RESPONSE_IN_FAULT_VALUE)
			      ? &((GskXmlrpcResponse*)parser->cur_message)->fault
			      : &parser->cur_param;
	if (parser->state == REQUEST_IN_PARAM_VALUE)
	  parser->state = REQUEST_IN_PARAM_TYPED_VALUE;
	else if (parser->state == RESPONSE_IN_PARAM_VALUE)
	  parser->state = RESPONSE_IN_PARAM_TYPED_VALUE;

	value->type = GSK_XMLRPC_STRING;
	/* fall-through */
	got_implicit_string = TRUE;
      }
    case REQUEST_IN_PARAM_TYPED_VALUE:
    case RESPONSE_IN_PARAM_TYPED_VALUE:
    case RESPONSE_IN_FAULT_TYPED_VALUE:
      {
	GskXmlrpcValue *value = NULL;
	gboolean *got_value;
	if (parser->stack)
	  {
	    ValueStack *st = parser->stack;
	    if (st->is_structure && st->state == STRUCT_STATE_IN_VALUE)
	      {
		if (is_whitespace (text, text_len))
		  return;
		got_implicit_string = TRUE;
		st->state = STRUCT_STATE_IN_TYPED_VALUE;
	      }
	    else if (!st->is_structure && st->state == ARRAY_STATE_IN_VALUE)
	      {
		if (is_whitespace (text, text_len))
		  return;
		got_implicit_string = TRUE;
		st->state = ARRAY_STATE_IN_TYPED_VALUE;
	      }
	    if ((st->is_structure && st->state == STRUCT_STATE_IN_TYPED_VALUE)
             || (!st->is_structure && st->state == ARRAY_STATE_IN_TYPED_VALUE))
	      {
		value = &parser->stack->value;
		got_value = &parser->stack->got_value;
		if (got_implicit_string)
		  value->type = GSK_XMLRPC_STRING;
	      }
	    else if (st->is_structure && st->state == STRUCT_STATE_IN_MEMBERNAME)
	      {
		g_free (st->name);
		st->name = g_strndup (text, text_len);
		return;
	      }
	  }
	else if (parser->state == REQUEST_IN_PARAM_TYPED_VALUE
              || parser->state == RESPONSE_IN_PARAM_TYPED_VALUE)
	  {
	    value = &parser->cur_param;
	    got_value = &parser->got_cur_param;
	  }
	else if (parser->state == RESPONSE_IN_FAULT_TYPED_VALUE)
	  {
	    GskXmlrpcResponse *res = parser->cur_message;
	    value = &res->fault;
	    got_value = &res->has_fault;
	  }
	else
	  g_assert_not_reached ();

	if (value == NULL)
	  {
	    if (!is_whitespace (text, text_len))
	      g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
			   _("got text where none expected (text='%.*s'"),
			   text_len, text);
	    return;
	  }

	if (*got_value)
	  {
	    g_set_error (error, GSK_G_ERROR_DOMAIN, GSK_ERROR_BAD_FORMAT,
			 _("already got value (text='%.*s'"),
			 text_len, text);
	    return;
	  }
	if (!parse_value_from_string (text, text_len, value, error))
	  return;
	*got_value = TRUE;
	if (got_implicit_string)
	  {
	    if (parser->stack)
	      {
		ValueStack *st = parser->stack;
		if (st->is_structure && st->state == STRUCT_STATE_IN_TYPED_VALUE)
		  st->state = STRUCT_STATE_IN_VALUE;
		else if (!st->is_structure && st->state == ARRAY_STATE_IN_TYPED_VALUE)
		  st->state = ARRAY_STATE_IN_VALUE;
		else
		  g_warning ("unexpected state");
	      }
	    else if (parser->state == RESPONSE_IN_PARAM_TYPED_VALUE)
	      parser->state = RESPONSE_IN_PARAM_VALUE;
	    else if (parser->state == REQUEST_IN_PARAM_TYPED_VALUE)
	      parser->state = REQUEST_IN_PARAM_VALUE;
	    else
	      g_assert_not_reached ();
	  }
	return;
      }
    default: g_assert_not_reached ();
    }
}

static void 
parser_passthrough     (GMarkupParseContext *context,
			const gchar         *passthrough_text,
			gsize                text_len,  
			gpointer             user_data,
                        GError             **error)
{
  /* do nothing */
}

static void
parser_error           (GMarkupParseContext *context,
			GError              *error,
			gpointer             user_data)
{
  /* do nothing */
  //g_message ("got parser error '%s'", error->message);
}

static GMarkupParser parser_funcs =
{
  parser_start_element,
  parser_end_element,
  parser_text,
  parser_passthrough,
  parser_error
};

/**
 * gsk_xmlrpc_parser_new:
 *
 * Allocate a new XMLRPC parser. A parser can parse both requests and responses.
 *
 * returns: the newly allocated parser.
 */
GskXmlrpcParser *gsk_xmlrpc_parser_new (GskXmlrpcStream *stream)
{
  GskXmlrpcParser *parser = g_new0 (GskXmlrpcParser, 1);
  parser->xmlrpc_stream = stream;
  parser->state = OUTER;
  parser->messages = g_queue_new ();
  parser->context = g_markup_parse_context_new (&parser_funcs, 0, parser, NULL);
  return parser;
}

/**
 * gsk_xmlrpc_parser_feed:
 * @parser: the parser to give data.
 * @text: the data to parse (it may just be a partial request).
 * @len: the length of @text, or -1 to use NUL-termination.
 * @error: place to put the error object if the parsing has a problem.
 *
 * Pass data into the XMLRPC parser.
 *
 * If this works, gsk_xmlrpc_parser_get_request() and/or
 * gsk_xmlrpc_parser_get_response() should be called as appropriate
 * until there are no more messages to dequeue.
 *
 * returns: TRUE if processing did not encounter an error.
 */
gboolean   gsk_xmlrpc_parser_feed   (GskXmlrpcParser *parser,
				     const char              *text,
				     gssize                   len,
				     GError                 **error)
{
  gboolean rv = g_markup_parse_context_parse(parser->context, text, len, error);
  return rv;
}

static gpointer
gsk_xmlrpc_parser_get_either (GskXmlrpcParser *parser,
			      guint            magic)
{
  GskXmlrpcRequest *rv;
  if (parser->messages->head == NULL)
    return NULL;
  rv = parser->messages->head->data;
  if (* (guint*) parser->messages->head->data == magic)
    return g_queue_pop_head (parser->messages);
  return NULL;
}

/**
 * gsk_xmlrpc_parser_get_request:
 * @parser: parser to try and get a parsed request from.
 *
 * Get a parsed request from the list maintained by
 * the parser.
 *
 * returns: a reference to the request; the caller
 * must call gsk_xmlrpc_request_unref() eventually.
 */
GskXmlrpcRequest *
gsk_xmlrpc_parser_get_request (GskXmlrpcParser *parser)
{
  return gsk_xmlrpc_parser_get_either (parser, REQUEST_MAGIC);
}

/**
 * gsk_xmlrpc_parser_get_response:
 * @parser: parser to try and get a parsed response from.
 *
 * Get a parsed response from the list maintained by
 * the parser.
 *
 * returns: a reference to the response; the caller
 * must call gsk_xmlrpc_response_unref() eventually.
 */
GskXmlrpcResponse *
gsk_xmlrpc_parser_get_response (GskXmlrpcParser *parser)
{
  return gsk_xmlrpc_parser_get_either (parser, RESPONSE_MAGIC);
}

static void
gsk_xmlrpc_either_unref (gpointer data)
{
  const guint *magic = data;
  if (*magic == REQUEST_MAGIC)
    gsk_xmlrpc_request_unref (data);
  else if (*magic == RESPONSE_MAGIC)
    gsk_xmlrpc_request_unref (data);
  else
    g_assert_not_reached ();
}

static void
value_stack_destroy_all (ValueStack *stack)
{
  while (stack)
    {
      ValueStack *kill = stack;
      stack = stack->up;

      if (kill->is_structure)
	{
	  gsk_xmlrpc_struct_free (kill->cur);
	  g_free (kill->name);
	}
      else
	gsk_xmlrpc_array_free (kill->cur);
      if (kill->got_value)
	gsk_xmlrpc_value_destruct (&kill->value);
      g_free (kill);
    }
}

/**
 * gsk_xmlrpc_parser_free:
 * @parser: the parser to free.
 *
 * Free the memory associated with the parser.
 */
void gsk_xmlrpc_parser_free (GskXmlrpcParser *parser)
{
  g_list_foreach (parser->messages->head, (GFunc) gsk_xmlrpc_either_unref, NULL);
  g_queue_free (parser->messages);
  g_markup_parse_context_free (parser->context);
  value_stack_destroy_all (parser->stack);
  if (parser->got_cur_param)
    gsk_xmlrpc_value_destruct (&parser->cur_param);
  g_free (parser);
}

