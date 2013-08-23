#include "../xmlrpc/gskxmlrpc.h"
#include <stdlib.h>
#include <string.h>

int main (int argc, char **argv)
{
  GskXmlrpcParser *parser;
  GskXmlrpcRequest *request;
  GskXmlrpcResponse *response;
  GskXmlrpcArray *array;
  GskXmlrpcStruct *structure;
  double delta;
  GError *error = NULL;
  parser = gsk_xmlrpc_parser_new (NULL);
  if (!gsk_xmlrpc_parser_feed (parser, 
			      "<?xml version=\"1.0\"?>\n"
			      "<methodCall>\n"
			      "   <methodName>examples.getStateName</methodName>\n"
			      "   <params>\n"
			      "      <param>\n"
			      "         <value><i4>41</i4></value>\n"
			      "       </param>\n"
			      "    </params>\n"
			      "</methodCall>\n",
                             -1, &error))
    g_error ("error parsing xmlrpc: %s", error->message);
  request = gsk_xmlrpc_parser_get_request (parser);
  response = gsk_xmlrpc_parser_get_response (parser);
  g_assert (request != NULL);
  g_assert (response == NULL);
  g_assert (request->params->len == 1);
  g_assert (strcmp (request->method_name, "examples.getStateName") == 0);
  g_assert (request->params->values[0].type == GSK_XMLRPC_INT32);
  g_assert (request->params->values[0].data.v_int32 == 41);
  gsk_xmlrpc_request_unref (request);
  request = gsk_xmlrpc_parser_get_request (parser);
  g_assert (request == NULL);

  if (!gsk_xmlrpc_parser_feed (parser, 
				"<?xml version=\"1.0\"?>\n"
				"<methodResponse>\n"
				"<params>\n"
				"<param>\n"
				"<value><string>South Dakota</string></value>\n"
				"</param>\n"
				"</params>\n"
				"</methodResponse>\n",
                             -1, &error))
    g_error ("error parsing xmlrpc: %s", error->message);
  request = gsk_xmlrpc_parser_get_request (parser);
  response = gsk_xmlrpc_parser_get_response (parser);
  g_assert (request == NULL);
  g_assert (response != NULL);
  g_assert (!response->has_fault);
  g_assert (response->params->len == 1);
  g_assert (response->params->values[0].type == GSK_XMLRPC_STRING);
  g_assert (strcmp (response->params->values[0].data.v_string, "South Dakota") == 0);
  gsk_xmlrpc_response_unref (response);
  response = gsk_xmlrpc_parser_get_response (parser);
  g_assert (response == NULL);

  if (!gsk_xmlrpc_parser_feed (parser, 
			       "<?xml version=\"1.0\"?>\n"
			       "<methodResponse>\n"
			       "<fault> \n"
			       "<value>\n"
			       "<struct>\n"
			       "<member>\n"
			       "  <name>faultCode</name>\n"
			       "  <value><int>4</int></value>\n"
			       "</member>\n"
			       "<member>\n"
			       "  <name>faultString</name>\n"
			       "  <value><string>Too many parameters.</string></value>\n"
			       "</member>\n"
			       "</struct>\n"
			       "</value>\n"
			       "</fault>\n"
			       "</methodResponse>\n",
                             -1, &error))
    g_error ("error parsing xmlrpc: %s", error->message);
  request = gsk_xmlrpc_parser_get_request (parser);
  response = gsk_xmlrpc_parser_get_response (parser);
  g_assert (request == NULL);
  g_assert (response != NULL);

  g_assert (response->params->len == 0);
  g_assert (response->has_fault);
  g_assert (response->fault.type == GSK_XMLRPC_STRUCT);
  gsk_xmlrpc_response_unref (response);
  response = gsk_xmlrpc_parser_get_response (parser);
  g_assert (response == NULL);

  if (!gsk_xmlrpc_parser_feed (parser, 
			       "<?xml version=\"1.0\"?>\n"
			       "<methodResponse>\n"
			       "<params> \n"
			       "<param>\n"
			       "<value>\n"
			       "<struct>\n"
			       "<member>\n"
			       "  <name>a</name>\n"
			       "  <value><array>\n"
			       "    <data>\n"
			       "      <value><int>34</int></value>\n"
			       "      <value><int>42</int></value>\n"
			       "      <value><int>16</int></value>\n"
			       "      <value>hello</value>\n"
			       "    </data>\n"
			       "  </array></value>\n"
			       "</member>\n"
			       "<member>\n"
			       "  <name>b</name>\n"
			       "  <value><struct>\n"
			       "     <member><name>q</name><value><string>fuck</string></value></member>\n"
			       "     <member><name>r</name><value><string>dump</string></value></member>\n"
			       "     <member><name>z</name><value><double>3.1415</double></value></member>\n"
			       "  </struct></value>\n"
			       "</member>\n"
			       "</struct>\n"
			       "</value>\n"
			       "</param>\n"
			       "<param>\n"
			       " <value><string>hello</string></value>\n"
			       "</param>\n"
			       "</params>\n"
			       "</methodResponse>\n",
                             -1, &error))
    g_error ("error parsing xmlrpc: %s", error->message);
  request = gsk_xmlrpc_parser_get_request (parser);
  response = gsk_xmlrpc_parser_get_response (parser);
  g_assert (request == NULL);
  g_assert (response != NULL);
  g_assert (response->has_fault == FALSE);
  g_assert (response->params->len == 2);
  g_assert (response->params->values[0].type == GSK_XMLRPC_STRUCT);
  g_assert (response->params->values[0].data.v_struct->n_members == 2);
  g_assert (strcmp(response->params->values[0].data.v_struct->members[0].name, "a") == 0);
  g_assert (response->params->values[0].data.v_struct->members[0].value.type == GSK_XMLRPC_ARRAY);
  array = response->params->values[0].data.v_struct->members[0].value.data.v_array;
  g_assert (array->len == 4);
  g_assert (array->values[0].type == GSK_XMLRPC_INT32);
  g_assert (array->values[0].data.v_int32 == 34);
  g_assert (array->values[1].type == GSK_XMLRPC_INT32);
  g_assert (array->values[1].data.v_int32 == 42);
  g_assert (array->values[2].type == GSK_XMLRPC_INT32);
  g_assert (array->values[2].data.v_int32 == 16);
  g_assert (array->values[3].type == GSK_XMLRPC_STRING);
  g_assert (strcmp (array->values[3].data.v_string, "hello") == 0);
  g_assert (strcmp(response->params->values[0].data.v_struct->members[1].name, "b") == 0);
  g_assert (response->params->values[0].data.v_struct->members[1].value.type == GSK_XMLRPC_STRUCT);
  structure = response->params->values[0].data.v_struct->members[1].value.data.v_struct;
  g_assert (structure->n_members == 3);
  g_assert (strcmp(structure->members[0].name,"q") == 0);
  g_assert (structure->members[0].value.type == GSK_XMLRPC_STRING);
  g_assert (strcmp (structure->members[0].value.data.v_string, "fuck") == 0);
  g_assert (strcmp(structure->members[1].name,"r") == 0);
  g_assert (structure->members[1].value.type == GSK_XMLRPC_STRING);
  g_assert (strcmp (structure->members[1].value.data.v_string, "dump") == 0);
  g_assert (strcmp(structure->members[2].name,"z") == 0);
  g_assert (structure->members[2].value.type == GSK_XMLRPC_DOUBLE);
  delta = structure->members[2].value.data.v_double - 3.1415;
  g_assert (-0.00001 <= delta && delta <= 0.00001);
  g_assert (response->params->values[1].type == GSK_XMLRPC_STRING);
  g_assert (strcmp(response->params->values[1].data.v_string, "hello") == 0);

  gsk_xmlrpc_response_unref (response);
  response = gsk_xmlrpc_parser_get_response (parser);
  g_assert (response == NULL);

  {
    char rand_buf[100];
    request = gsk_xmlrpc_request_new (NULL);

    gsk_xmlrpc_request_add_int32 (request, 101);
    gsk_xmlrpc_request_add_boolean (request, TRUE);
    gsk_xmlrpc_request_add_double (request, 2.7182182);
    gsk_xmlrpc_request_add_string (request, "hello world");
    gsk_xmlrpc_request_add_date (request, 1066441969);
    {
      GByteArray *ba = g_byte_array_new ();
      guint i;
      g_byte_array_set_size (ba, 100);
      for (i = 0; i < 25; i++)
	((guint32 *) ba->data)[i] = rand();
      gsk_xmlrpc_request_add_data (request, ba);
      memcpy (rand_buf, ba->data, 100);
    }

    { GskBuffer buf;
      GskBufferFragment *frag;
      gsk_buffer_construct (&buf);
      gsk_xmlrpc_request_to_buffer (request, &buf);
      for (frag = buf.first_frag; frag != NULL; frag = frag->next)
	if (!gsk_xmlrpc_parser_feed (parser, frag->buf + frag->buf_start, frag->buf_length, &error))
	  g_error ("error parsing printed xmlrpc: %s", error->message);

      gsk_xmlrpc_request_unref (request);
      request = gsk_xmlrpc_parser_get_request (parser);
      g_assert (request != NULL);
      g_assert (request->params->len == 6);
      g_assert (request->params->values[0].type == GSK_XMLRPC_INT32);
      g_assert (request->params->values[0].data.v_int32 == 101);
      g_assert (request->params->values[1].type == GSK_XMLRPC_BOOLEAN);
      g_assert (request->params->values[1].data.v_boolean == TRUE);
      g_assert (request->params->values[2].type == GSK_XMLRPC_DOUBLE);
      g_assert (2.7182181 <= request->params->values[2].data.v_double
	     && 2.7182183 >= request->params->values[2].data.v_double);
      g_assert (request->params->values[3].type == GSK_XMLRPC_STRING);
      g_assert (strcmp (request->params->values[3].data.v_string, "hello world") == 0);
      g_assert (request->params->values[4].type == GSK_XMLRPC_DATE);
      g_assert (request->params->values[4].data.v_date == 1066441969);

      g_assert (request->params->values[5].type == GSK_XMLRPC_BINARY_DATA);
      g_assert (request->params->values[5].data.v_binary_data->len == 100);
      g_assert (memcmp (request->params->values[5].data.v_binary_data->data, rand_buf, 100) == 0);
      gsk_xmlrpc_request_unref (request);
      g_assert (gsk_xmlrpc_parser_get_request (parser) == NULL);
      g_assert (gsk_xmlrpc_parser_get_response (parser) == NULL);
    }
  }

  return 0;
}
