#include "gskxmlrpc.h"
#include "../common/gskdate.h"
#include "../common/gskbase64.h"
#include <string.h>

static void
append_value (GskBuffer *buffer, const GskXmlrpcValue *value)
{
  switch (value->type)
    {
    case GSK_XMLRPC_INT32:
      gsk_buffer_printf (buffer, "    <value><int>%d</int></value>\n",
			 value->data.v_int32);
      break;
    case GSK_XMLRPC_BOOLEAN:
      gsk_buffer_printf (buffer, "    <value><boolean>%d</boolean></value>\n",
			 value->data.v_boolean ? 1 : 0);
      break;
    case GSK_XMLRPC_DOUBLE:
      gsk_buffer_printf (buffer, "    <value><double>%.21g</double></value>\n",
			 value->data.v_double);
      break;
    case GSK_XMLRPC_STRING:
      {
	char *encoded = g_markup_escape_text (value->data.v_string, -1);
	gsk_buffer_printf (buffer, "    <value><string>%s</string></value>\n",
			   encoded); 
	g_free (encoded);
      }
    break;
    case GSK_XMLRPC_DATE:
      {
	char date_buf[GSK_DATE_MAX_LENGTH];
	gsk_date_print_timet (value->data.v_date,
			      date_buf, GSK_DATE_MAX_LENGTH,
			      GSK_DATE_FORMAT_ISO8601);
	gsk_buffer_printf (buffer, "    <value><dateTime.iso8601>%s</dateTime.iso8601></value>\n",
			   date_buf); 
      }
      break;
    case GSK_XMLRPC_BINARY_DATA:
      {
	GByteArray *data =value->data.v_binary_data;
	char *encoded = gsk_base64_encode_alloc ((char*)(data->data), data->len);
	gsk_buffer_append_string (buffer, "  <value><base64>\n");
	gsk_buffer_append_foreign (buffer, encoded, strlen (encoded),
				   g_free, encoded);
	gsk_buffer_append_string (buffer, "  </base64></value>\n");
      }
      break;
    case GSK_XMLRPC_STRUCT:
      {
	GskXmlrpcStruct *st = value->data.v_struct;
	guint i;
	gsk_buffer_append_string (buffer, "  <value><struct>\n");
	for (i = 0; i < st->n_members; i++)
	  {
	    gsk_buffer_printf (buffer, "    <member>\n"
			               "      <name>%s</name>\n", st->members[i].name);
	    append_value (buffer, &st->members[i].value);
	    gsk_buffer_append_string (buffer, "    </member>\n");
	  }
	gsk_buffer_append_string (buffer, "  </struct></value>\n");
	
      }
      break;
    case GSK_XMLRPC_ARRAY:
      {
	GskXmlrpcArray *ar = value->data.v_array;
	guint i;
	gsk_buffer_append_string (buffer, "  <value><array><data>\n");
	for (i = 0; i < ar->len; i++)
	  {
	    append_value (buffer, ar->values + i);
	  }
	gsk_buffer_append_string (buffer, "  </data></array></value>\n");
	
      }
      break;
    default:
      g_assert_not_reached ();
    }
}

/**
 * gsk_xmlrpc_response_to_buffer:
 * @response: the XMLRPC response to serialize.
 * @buffer: the buffer to append to.
 *
 * Write the XML corresponding to this response to the buffer.
 */
void gsk_xmlrpc_response_to_buffer (GskXmlrpcResponse *response,
				    GskBuffer         *buffer)
{
  gsk_buffer_append_string (buffer, "<methodResponse>\n");
  if (response->has_fault)
    {
      gsk_buffer_append_string (buffer, " <fault>\n");
      append_value (buffer, &response->fault);
      gsk_buffer_append_string (buffer, " </fault>\n");
    }
  else
    {
      guint i;
      gsk_buffer_append_string (buffer, " <params>\n");
      for (i = 0; i < response->params->len; i++)
	{
	  gsk_buffer_append_string (buffer, " <param>\n");
	  append_value (buffer, response->params->values + i);
	  gsk_buffer_append_string (buffer, " </param>\n");
	}
      gsk_buffer_append_string (buffer, " </params>\n");
    }
  gsk_buffer_append_string (buffer, "</methodResponse>\n");
}

/* gsk_xmlrpc_request_to_buffer:
 * @request: the XMLRPC request to serialize.
 * @buffer: the buffer to append to.
 *
 * Write the XML corresponding to this request to the buffer.
 */
void gsk_xmlrpc_request_to_buffer  (GskXmlrpcRequest  *request,
				    GskBuffer         *buffer)
{
  guint i;
  gsk_buffer_append_string (buffer, "<methodCall>\n");
  gsk_buffer_printf (buffer, "  <methodName>%s</methodName>\n", request->method_name);
  gsk_buffer_append_string (buffer, " <params>\n");
  for (i = 0; i < request->params->len; i++)
    {
      gsk_buffer_append_string (buffer, " <param>\n");
      append_value (buffer, request->params->values + i);
      gsk_buffer_append_string (buffer, " </param>\n");
    }
  gsk_buffer_append_string (buffer, " </params>\n");
  gsk_buffer_append_string (buffer, "</methodCall>\n");
}
