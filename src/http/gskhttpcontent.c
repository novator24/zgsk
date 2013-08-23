#include "gskhttpcontent.h"
#include "gskprefixtree.h"
#include "../url/gskurl.h"
#include "../gskmemory.h"
#include "../gskutils.h"
#include "../gskstreamfd.h"
#include "../gskstreamlistenersocket.h"
#include "../mime/gskmimemultipartdecoder.h"
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/* --- helper functions --- */
static void
reverse_string (char *out, const char *in, guint len)
{
  guint i;
  in += len;
  for (i = 0; i < len; i++)
    *out++ = *(--in);
  *out = 0;
}

/* --- a Handler: a ref-counted callback, data, destroy triple */
typedef enum
{
  HANDLER_RAW,
  HANDLER_CGI
} HandlerType;

struct _GskHttpContentHandler
{
  guint ref_count;
  HandlerType type;
  gpointer data;
  union {
    GskHttpContentFunc raw;
    GskHttpContentCGIFunc cgi;
  } func;
  GDestroyNotify destroy;

  /* these form rings of handlers, once they 
     are added to a content.  */
  GskHttpContentHandler *next, *prev;
};

typedef GskHttpContentHandler Handler;

static inline Handler *
handler_alloc (HandlerType     type,
               GHookFunc       func,
               gpointer        data,
               GDestroyNotify  destroy)
{
  Handler *rv = g_new (Handler, 1);
  rv->ref_count = 1;
  rv->type = type;
  rv->data = data;
  rv->destroy = destroy;
  rv->func.raw = (GskHttpContentFunc) func;
  rv->next = rv->prev = NULL;
  return rv;
}

void
gsk_http_content_handler_unref (Handler *handler)
{
  if (--(handler->ref_count) == 0)
    {
      if (handler->destroy)
        handler->destroy (handler->data);
      g_free (handler);
    }
}
void
gsk_http_content_handler_ref (Handler *handler)
{
  g_return_if_fail (handler->ref_count > 0);
  ++(handler->ref_count);
}
static inline void handler_ref (Handler *handler)
{
  ++(handler->ref_count);
}

/*
 *     _      _        _                      _
 *  __| |__ _| |_ __ _| |__  __ _ ___ ___ ___| |_ _  _ _ __  ___
 * / _` / _` |  _/ _` | '_ \/ _` (_-</ -_)___|  _| || | '_ \/ -_)
 * \__,_\__,_|\__\__,_|_.__/\__,_/__/\___|    \__|\_, | .__/\___|
 *                                                |__/|_|
 *     _         __  __
 *  __| |_ _  _ / _|/ _|
 * (_-<  _| || |  _|  _|
 * /__/\__|\_,_|_| |_|
 */

typedef struct _SuffixList SuffixList;
typedef struct _PathTable PathTable;
typedef struct _PathVHostTable PathVHostTable;


struct _SuffixList
{
  GskPrefixTree *suffix_to_handler;     /* reverse the string before invoking */
  Handler *no_suffix_handler;
};

struct _PathTable
{
  GHashTable *exact;
  GskPrefixTree *prefix_to_suffix_list;
  SuffixList *no_prefix_list;           /* includes no path at all case */
};

struct _PathVHostTable
{
  GHashTable *vhost_to_path_table;
  PathTable *no_vhost_path_table;
};

struct _GskHttpContent
{
  /* Table of contents */
  GskPrefixTree *user_agent_to_path_vhost_table;
  PathVHostTable *path_vhost_table;

  /* Mime types by path */
  /* final target of all these is a pair of strings concatenated
     (including the NULs) */
  GskPrefixTree *mime_suffix_to_prefix_tree_to_typepair;
  GskPrefixTree *mime_suffix_to_typepair;     /* no prefix */
  GskPrefixTree *mime_prefix_to_typepair;     /* no suffix */
  char *mime_default_typepair;

  /* misc configuration options */
  int keepalive_idle_timeout_ms;              /* or -1 for no timeout */

  GskHttpContentErrorHandler error_handler;
  gpointer error_data;
  GDestroyNotify error_destroy;
};

/* --- public interface --- */

/* construction */
static SuffixList *
suffix_list_new (void)
{
  return g_new0 (SuffixList, 1);
}

static PathTable *
path_table_new (void)
{
  PathTable *rv = g_new (PathTable, 1);
  rv->exact = g_hash_table_new (g_str_hash, g_str_equal);
  rv->prefix_to_suffix_list = NULL;
  rv->no_prefix_list = suffix_list_new ();
  return rv;
}

static PathVHostTable *
path_vhost_table_new (void)
{
  PathVHostTable *rv = g_new (PathVHostTable, 1);
  rv->vhost_to_path_table = g_hash_table_new (g_str_hash, g_str_equal);
  rv->no_vhost_path_table = path_table_new ();
  return rv;
}

static void
server_respond_printf (GskHttpServer *server,
                       GskHttpRequest *request,
                       GskHttpStatus   status_code,
                       const char     *format,
                       ...)
{
  va_list args;
  GskHttpResponse *response;
  GskStream *stream;
  guint str_len;
  char *str;
  va_start (args,format);
  str = g_strdup_vprintf (format, args);
  va_end (args);
  str_len = strlen (str);
  stream = gsk_memory_slab_source_new (str, str_len, g_free, str);
  response = gsk_http_response_from_request (request, status_code, str_len);
  gsk_http_response_set_content_type (response, "text");
  gsk_http_response_set_content_subtype (response, "html");
  gsk_http_server_respond (server, request, response, stream);
  g_object_unref (stream);
  g_object_unref (response);
}


static void
default_error_handler (GskHttpContent          *content,
                       GError                  *error,
                       GskHttpServer           *server,
                       GskHttpRequest          *request,
                       GskHttpStatus            code,
                       gpointer                 data)

{
  GEnumClass *class = g_type_class_ref (GSK_TYPE_HTTP_STATUS);
  GEnumValue *value = g_enum_get_value (class, code);
  server_respond_printf (server, request, code,
                         "<html>\n"
                          " <head><title>%u: %s</title></head>\n"
                          " <body><h1>%s</h1>\n"
                          " </body>\n"
                          "</html>\n",
                          code,
                          value ? value->value_nick : "unknown error",
                          value ? value->value_name : "Unknown Error");
  g_type_class_unref (class);
}


/**
 * gsk_http_content_new:
 *
 * Allocate a new, empty content set.
 *
 * returns: a newly allocated content database.
 */
GskHttpContent *gsk_http_content_new     (void)
{
  GskHttpContent *content = g_new0 (GskHttpContent, 1);
  content->user_agent_to_path_vhost_table = NULL;
  content->path_vhost_table = path_vhost_table_new ();
  content->error_handler = default_error_handler;
  content->keepalive_idle_timeout_ms = -1;
  return content;
}

/**
 * gsk_http_content_handler_new:
 * @func: function to call that will try and
 * handle the HTTP request.
 * @data: data to pass to function.
 * @destroy: function to call when the handler is destroyed.
 * (after all requests are done).
 *
 * Allocate a new content handler.
 *
 * This should be added to a GskHttpContent
 * using gsk_http_content_add_handler()
 * and then unrefed with gsk_http_content_handler_unref().
 *
 * returns: the newly allocated handler.
 */
GskHttpContentHandler *
gsk_http_content_handler_new (GskHttpContentFunc       func,
                              gpointer                 data,
                              GDestroyNotify           destroy)
{
  return handler_alloc (HANDLER_RAW, (GHookFunc) func, data, destroy);
}

/**
 * gsk_http_content_handler_new_cgi:
 * @func: function to call that will try and
 * handle the CGI request.
 * @data: data to pass to function.
 * @destroy: function to call when the handler is destroyed.
 * (after all requests are done).
 *
 * Allocate a new content handler.
 *
 * This should be added to a GskHttpContent
 * using gsk_http_content_add_handler()
 * and then unrefed with gsk_http_content_handler_unref().
 *
 * returns: the newly allocated handler.
 */
GskHttpContentHandler *
gsk_http_content_handler_new_cgi (GskHttpContentCGIFunc    func,
                                  gpointer                 data,
                                  GDestroyNotify           destroy)
{
  return handler_alloc (HANDLER_CGI, (GHookFunc) func, data, destroy);
}

static void
handler_ring_add (Handler            **ring,
                  Handler             *new,
                  GskHttpContentAction action)
{
  g_assert (new->next == NULL && new->prev == NULL);
  switch (action)
    {
    case GSK_HTTP_CONTENT_PREPEND:
      if (*ring == NULL)
        {
          *ring = new;
          new->next = new->prev = new;
        }
      else
        {
          new->next = *ring;
          new->prev = (*ring)->prev;
          new->next->prev = new;
          new->prev->next = new;
        }
      break;
    case GSK_HTTP_CONTENT_APPEND:
      if (*ring == NULL)
        {
          *ring = new;
          new->next = new->prev = new;
        }
      else
        {
          new->prev = *ring;
          new->next = (*ring)->next;
          new->next->prev = new;
          new->prev->next = new;
        }
      break;
    case GSK_HTTP_CONTENT_REPLACE:
      if (*ring != NULL)
        {
          Handler *at = *ring;
          do
            {
              Handler *next = at->next;
              gsk_http_content_handler_unref (at);
              at = next;
            }
          while (at != *ring);
        }
      *ring = new;
      new->next = new->prev = new;
      break;
    default:
      g_assert_not_reached ();
    }
  handler_ref (new);
}

static void
suffix_list_add (SuffixList             *sl,
                 const GskHttpContentId *id,
                 Handler                *handler,
                 GskHttpContentAction    action)
{
  if (id->path_suffix)
    {
      guint len = strlen (id->path_suffix);
      char *rev = g_alloca (len + 1);
      Handler *ring;
      reverse_string (rev, id->path_suffix, len);
      ring = gsk_prefix_tree_lookup_exact (sl->suffix_to_handler, rev);
      handler_ring_add (&ring, handler, action);
      gsk_prefix_tree_insert (&sl->suffix_to_handler, rev, ring);
    }
  else
    {
      handler_ring_add (&sl->no_suffix_handler, handler, action);
    }
}

static void
path_table_add           (PathTable              *table,
                          const GskHttpContentId *id,
                          Handler                *handler,
                          GskHttpContentAction    action)
{
  if (id->path)
    {
      char *table_path = NULL;
      Handler *ring = NULL;
      g_hash_table_lookup_extended (table->exact, id->path, (gpointer*) &table_path, (gpointer*) &ring);
      handler_ring_add (&ring, handler, action);
      if (table_path == NULL)
        table_path = g_strdup (id->path);
      g_hash_table_insert (table->exact, table_path, ring);
    }
  else if (id->path_prefix)
    {
      SuffixList *sl = gsk_prefix_tree_lookup_exact (table->prefix_to_suffix_list,
                                                     id->path_prefix);
      if (sl == NULL)
        {
          sl = suffix_list_new ();
          gsk_prefix_tree_insert (&table->prefix_to_suffix_list,
                                  id->path_prefix, sl);
        }
      suffix_list_add (sl, id, handler, action);
    }
  else
    {
      suffix_list_add (table->no_prefix_list, id, handler, action);
    }
}

static void
path_vhost_table_add     (PathVHostTable         *table,
                          const GskHttpContentId *id,
                          Handler                *handler,
                          GskHttpContentAction    action)
{
  PathTable *pt;
  if (id->host)
    {
      pt = g_hash_table_lookup (table->vhost_to_path_table, id->host);
      if (pt == NULL)
        {
          pt = path_table_new ();
          g_hash_table_insert (table->vhost_to_path_table,
                               g_strdup (id->host), pt);
        }
    }
  else
    {
      pt = table->no_vhost_path_table;
    }
  g_assert (pt != NULL);
  path_table_add (pt, id, handler, action);
}

/**
 * gsk_http_content_add_handler:
 * @content: database to add the content to.
 * @id: location within the server to add the entry to.
 * @handler: a handler to process the data.
 * @action: how this handler relates to other handlers.
 * It may be a pre-filter, or a post-filter, or replace
 * the currently registered handlers.
 *
 * Add the handler to the content database.
 * This increases the ref-count on the handler,
 * so you must also unref it.
 *
 * You cannot add a handler to more than
 * one database.
 */
void
gsk_http_content_add_handler     (GskHttpContent          *content,
                                  const GskHttpContentId  *id,
                                  GskHttpContentHandler   *handler,
                                  GskHttpContentAction     action)
{
  if (id->user_agent_prefix)
    {
      PathVHostTable *pvh_table;
      pvh_table = gsk_prefix_tree_lookup (content->user_agent_to_path_vhost_table,
                                          id->user_agent_prefix);
      if (pvh_table == NULL)
        {
          pvh_table = path_vhost_table_new ();
          gsk_prefix_tree_insert (&content->user_agent_to_path_vhost_table,
                                  id->user_agent_prefix,
                                  pvh_table);
        }
      path_vhost_table_add (pvh_table, id, handler, action);
    }
  else
    {
      path_vhost_table_add (content->path_vhost_table, id, handler, action);
    }
}

/* --- mime-type table --- */
/**
 * gsk_http_content_set_mime_type:
 * @content: content-database to register this mime information with.
 * @prefix: path prefix that maps to this type/subtype pair.
 * (may be NULL for an empty prefix test).
 * @suffix: path suffix that maps to this type/subtype pair. 
 * (may be NULL for an empty suffix test).
 * @type: main type associated with this portion of the URI space.
 * @subtype: subtype associated with this portion of the URI space.
 *
 * Register information about which paths are associated with
 * which mime types.  This is used for data and file serving,
 * or you can explicitly use it from a custom handler.
 */
void
gsk_http_content_set_mime_type (GskHttpContent *content,
                                const char     *prefix,
                                const char     *suffix,
                                const char     *type,
                                const char     *subtype)
{
  char *rev_suffix = NULL;
  char *typepair;
  if (suffix)
    {
      guint len = strlen (suffix);
      rev_suffix = g_alloca (len + 1);
      reverse_string (rev_suffix, suffix, len);
    }
  {
    guint len_type = strlen (type);
    typepair = g_malloc (len_type + 1 + strlen (subtype) + 1);
    strcpy (typepair, type);
    strcpy (typepair + len_type + 1, subtype);
  }
  if (prefix && suffix)
    {
      GskPrefixTree *pref;
      pref = gsk_prefix_tree_lookup_exact (content->mime_suffix_to_prefix_tree_to_typepair,
                                           rev_suffix);
      gsk_prefix_tree_insert (&pref, prefix, typepair);
      gsk_prefix_tree_insert (&content->mime_suffix_to_prefix_tree_to_typepair,
                              rev_suffix, pref);
    }
  else if (prefix)
    {
      char *old = gsk_prefix_tree_insert (&content->mime_prefix_to_typepair,
                                          prefix, typepair);
      g_free (old);
    }
  else if (suffix)
    {
      char *old = gsk_prefix_tree_insert (&content->mime_suffix_to_typepair,
                                          rev_suffix, typepair);
      g_free (old);
    }
  else
    {
      g_free (content->mime_default_typepair);
      content->mime_default_typepair = typepair;
    }
}

/**
 * gsk_http_content_set_default_mime_type:
 * @content: content-database to register this mime information with.
 * @type: default mime type.
 * @subtype: default mime subtype.
 *
 * If no other more specific path is found,
 * this will be the assigned type.
 *
 * You should always call this function if you plan
 * on serving files or data.
 *
 * This is equivalent to calling gsk_http_content_set_mime_type()
 * with prefix==suffix==NULL.
 */
void
gsk_http_content_set_default_mime_type (GskHttpContent *content,
                                        const char     *type,
                                        const char     *subtype)
{
  gsk_http_content_set_mime_type (content, NULL, NULL, type, subtype);
}

gboolean
gsk_http_content_get_mime_type (GskHttpContent *content,
                                const char     *path,
                                const char    **type_out,
                                const char    **subtype_out)
{
  guint path_len = strlen (path);
  char *rev_path = g_alloca (path_len + 1);
  const char *typepair = NULL;
  reverse_string (rev_path, path, path_len);

  /* try prefix/suffix tree */
  {
    GskPrefixTree *pref_tree = gsk_prefix_tree_lookup (content->mime_suffix_to_prefix_tree_to_typepair,
                                                       rev_path);
    typepair = gsk_prefix_tree_lookup (pref_tree, path);
    if (typepair)
      goto done;
  }

  /* try suffix tree */
  typepair = gsk_prefix_tree_lookup (content->mime_suffix_to_typepair, rev_path);
  if (typepair)
    goto done;

  /* try prefix tree */
  typepair = gsk_prefix_tree_lookup (content->mime_prefix_to_typepair, path);
  if (typepair)
    goto done;

  typepair = content->mime_default_typepair;

done:
  if (typepair == NULL)
    return FALSE;
  *type_out = typepair;
  *subtype_out = strchr (typepair, 0) + 1;
  return TRUE;
}

/**
 * gsk_http_content_set_error_handler:
 * @content: content-database to affect.
 * @handler: function to call to display error data.
 * @data: data to pass to @handler.
 * @destroy: function to call when handler is destroyed.
 *
 * This function is intended to respond to other handlers'
 * error returns.  It is also called in the command-not-found
 * case.
 */
void
gsk_http_content_set_error_handler  (GskHttpContent          *content,
                                     GskHttpContentErrorHandler handler,
                                     gpointer                 data,
                                     GDestroyNotify           destroy)
{
  if (content->error_destroy)
    (*content->error_destroy) (content->error_data);
  content->error_handler = handler;
  content->error_data = data;
  content->error_destroy = destroy;
}

/* --- Responding to a request:  CGI implementation  --- */
typedef struct _CGIRequestInfo CGIRequestInfo;
struct _CGIRequestInfo
{
  Handler        *handler;
  GskHttpContent *content;
  GskHttpServer  *server;
  GskHttpRequest *request;
  GPtrArray      *mime_pieces;
};

static CGIRequestInfo *
cgi_request_info_new (Handler        *handler,
                      GskHttpContent *content,
                      GskHttpServer  *server,
                      GskHttpRequest *request)
{
  CGIRequestInfo *ri = g_new (CGIRequestInfo, 1);
  ri->handler = handler;
  ri->content = content;
  ri->server = server;
  ri->request = request;
  ri->mime_pieces = g_ptr_array_new ();
  handler_ref (ri->handler);
  return ri;
}

static void
cgi_request_info_free (CGIRequestInfo *ri)
{
  guint i;
  gsk_http_content_handler_unref (ri->handler);
  for (i = 0; i < ri->mime_pieces->len; i++)
    gsk_mime_multipart_piece_unref (ri->mime_pieces->pdata[i]);
  g_ptr_array_free (ri->mime_pieces, TRUE);
  g_free (ri);
}

static void
cgi_request_info_callback (CGIRequestInfo *ri)
{
  ri->handler->func.cgi (ri->content, ri->handler,
                         ri->server, ri->request,
                         ri->mime_pieces->len,
                         (GskMimeMultipartPiece **) (ri->mime_pieces->pdata),
                         ri->handler->data);
}

static void
append_kv_as_mime_piece_to_ptr_array (gpointer key, gpointer value, gpointer data)
{
  const char *k = key;
  const char *v = value;
  GPtrArray *arr = data;
  gpointer copy = g_strdup (v);
  GskMimeMultipartPiece *piece = gsk_mime_multipart_piece_alloc ();
  gsk_mime_multipart_piece_set_id (piece, k);
  gsk_mime_multipart_piece_set_data (piece, copy, strlen (v), g_free, copy);
  g_ptr_array_add (arr, piece);
}

static void
handle_cgi_with_urlencoded_string (Handler        *handler,
                                   GskHttpContent *content,
                                   GskHttpServer  *server,
                                   GskHttpRequest *request,
                                   const char     *query_string)
{
  GPtrArray *mime_pieces = g_ptr_array_new ();
  guint i;
  char **kv_pairs = gsk_url_split_form_urlencoded (query_string);

  for (i = 0; kv_pairs[i]; i += 2)
    append_kv_as_mime_piece_to_ptr_array (kv_pairs[i], kv_pairs[i+1],
                                          mime_pieces);
  g_strfreev (kv_pairs);
  handler->func.cgi (content, handler,
                     server, request,
                     mime_pieces->len,
                     (GskMimeMultipartPiece **) (mime_pieces->pdata),
                     handler->data);
  for (i = 0; i < mime_pieces->len; i++)
    gsk_mime_multipart_piece_unref (mime_pieces->pdata[i]);
  g_ptr_array_free (mime_pieces, TRUE);
}

static void
urlencoded_buffer_ready  (GskBuffer              *buffer,
                          gpointer                data)
{
  CGIRequestInfo *ri = data;
  char *qstr = g_malloc (buffer->size + 1);
  qstr[buffer->size] = 0;
  gsk_buffer_read (buffer, qstr, buffer->size);
  handle_cgi_with_urlencoded_string (ri->handler, ri->content, ri->server,
                                     ri->request, qstr);
  g_free (qstr);
}

static void
do_cgi_with_urlencoded_form_data (Handler        *handler,
                                  GskHttpContent *content,
                                  GskHttpServer  *server,
                                  GskHttpRequest *request,
                                  GskStream      *post_data)
{
  CGIRequestInfo *ri = cgi_request_info_new (handler, content, server, request);
  GskStream *content_sink;
  content_sink = gsk_memory_buffer_sink_new (urlencoded_buffer_ready,
                                             ri,
                                             (GDestroyNotify) cgi_request_info_free);
  gsk_stream_attach (post_data, content_sink, NULL);
  g_object_unref (content_sink);
}

static gboolean
handle_piece_ready (GskMimeMultipartDecoder *decoder,
                    gpointer                 data)
{
  CGIRequestInfo *ri = data;
  GskMimeMultipartPiece *piece = gsk_mime_multipart_decoder_get_piece (decoder);
  if (piece)
    g_ptr_array_add (ri->mime_pieces, piece);
  return TRUE;
}

static gboolean
handle_decoder_shutdown (GskMimeMultipartDecoder *decoder,
                         gpointer data)
{
  CGIRequestInfo *ri = data;
  cgi_request_info_callback (ri);
  return FALSE;
}

static void
do_cgi_with_multipart_form_data (Handler        *handler,
                                 GskHttpContent *content,
                                 GskHttpServer  *server,
                                 GskHttpRequest *request,
                                 GskStream      *post_data)
{
  GskMimeMultipartDecoder *decoder = gsk_mime_multipart_decoder_new (NULL);
  CGIRequestInfo *ri = cgi_request_info_new (handler, content, server, request);
  gsk_mime_multipart_decoder_set_mode (decoder, GSK_MIME_MULTIPART_DECODER_MODE_ALWAYS_MEMORY);
  gsk_mime_multipart_decoder_trap (decoder, handle_piece_ready, handle_decoder_shutdown,
                                   ri, (GDestroyNotify) cgi_request_info_free);
}

/* precondition: request->path must contain a '?' */
static void
do_cgi_with_get_form_data (Handler        *handler,
                           GskHttpContent *content,
                           GskHttpServer  *server,
                           GskHttpRequest *request)
{
  handle_cgi_with_urlencoded_string (handler, content, server, request,
                                     strchr (request->path, '?') + 1);
}

static GskHttpContentResult
try_cgi_handler      (Handler        *handler,
                      GskHttpContent *content,
                      GskHttpServer  *server,
                      GskHttpRequest *request,
                      GskStream      *post_data)
{
  /* does this HTTP header look like a CGI? */
  /* requirement are either an interesting query string
     or the correct content type */
  const char *type = GSK_HTTP_HEADER (request)->content_type;
  const char *subtype = GSK_HTTP_HEADER (request)->content_subtype;
  gboolean has_urlencodedform_data = request->verb ==GSK_HTTP_VERB_POST
                                   && type != NULL && subtype != NULL
                                   && strcmp (type, "application") == 0
                                   && strcmp (subtype, "x-www-form-urlencoded") == 0;
  gboolean has_multipartform_data   = request->verb ==GSK_HTTP_VERB_POST
                                   && type != NULL && subtype != NULL
                                   && strcmp (type, "multipart") == 0
                                   && strcmp (subtype, "form-data") == 0;
  gboolean has_interesting_query = request->verb == GSK_HTTP_VERB_GET
                                   && strchr (request->path, '?') != NULL;
  gboolean has_form_data = has_urlencodedform_data || has_multipartform_data;
  if (!has_form_data && !has_interesting_query)
    return GSK_HTTP_CONTENT_CHAIN;

  if (has_urlencodedform_data)
    do_cgi_with_urlencoded_form_data (handler, content, server, request, post_data);
  else if (has_multipartform_data)
    do_cgi_with_multipart_form_data (handler, content, server, request, post_data);
  else
    do_cgi_with_get_form_data (handler, content, server, request);
  return GSK_HTTP_CONTENT_OK;
}

/* --- Responding to a request:  Handler finding and invocation  --- */
static GskHttpContentResult
one_handler_response (Handler        *handler,
                      GskHttpContent *content,
                      GskHttpServer  *server,
                      GskHttpRequest *request,
                      GskStream      *post_data)
{
  switch (handler->type)
    {
    case HANDLER_RAW:
      return handler->func.raw (content, handler, server, request, post_data, handler->data);
    case HANDLER_CGI:
      return try_cgi_handler (handler, content, server, request, post_data);
    default:
      g_return_val_if_reached (GSK_HTTP_CONTENT_ERROR);
    }
}

static GskHttpContentResult
handler_ring_respond     (Handler        *handlers,
                          GskHttpContent *content,
                          GskHttpServer  *server,
                          GskHttpRequest *request,
                          GskStream      *post_data)
{
  Handler *h = handlers;
  if (h == NULL)
    return GSK_HTTP_CONTENT_CHAIN;
  do
    {
      GskHttpContentResult rv = one_handler_response (h, content, server, request, post_data);
      if (rv != GSK_HTTP_CONTENT_CHAIN)
        return rv;
      h = h->next;
    }
  while (h != handlers);
  return GSK_HTTP_CONTENT_CHAIN;
}

static GskHttpContentResult
suffix_list_respond      (SuffixList     *suffix,
                          GskHttpContent *content,
                          GskHttpServer  *server,
                          GskHttpRequest *request,
                          GskStream      *post_data)
{
  const char *end = strchr (request->path, '?');
  char *rev;
  GSList *list, *at;
  if (end == NULL)
    end = strchr (request->path, 0);
  rev = g_alloca (end - request->path + 1);
  reverse_string (rev, request->path, end - request->path);
  list = gsk_prefix_tree_lookup_all (suffix->suffix_to_handler, rev);
  for (at = list; at; at = at->next)
    {
      GskHttpContentResult rv;
      rv = handler_ring_respond (at->data, content, server, request, post_data);
      if (rv != GSK_HTTP_CONTENT_CHAIN)
        {
          g_slist_free (list);
          return rv;
        }
    }
  g_slist_free (list);

  return handler_ring_respond (suffix->no_suffix_handler, content, server, request, post_data);
}

static GskHttpContentResult
path_table_respond       (PathTable      *table,
                          GskHttpContent *content,
                          GskHttpServer  *server,
                          GskHttpRequest *request,
                          GskStream      *post_data)
{
  Handler *h = g_hash_table_lookup (table->exact, request->path);
  GskHttpContentResult rv = handler_ring_respond (h, content, server, request, post_data);
  GSList *list, *at;
  if (rv != GSK_HTTP_CONTENT_CHAIN)
    return rv;

  list = gsk_prefix_tree_lookup_all (table->prefix_to_suffix_list, request->path);
  if (list == NULL)
    return GSK_HTTP_CONTENT_CHAIN;

  for (at = list; at; at = at->next)
    {
      SuffixList *sl = at->data;
      rv = suffix_list_respond (sl, content, server, request, post_data);
      if (rv != GSK_HTTP_CONTENT_CHAIN)
        {
          g_slist_free (list);
          return rv;
        }
    }
  g_slist_free (list);
  return GSK_HTTP_CONTENT_CHAIN;
}

static GskHttpContentResult
path_vhost_table_respond (PathVHostTable *table,
                          GskHttpContent *content,
                          GskHttpServer  *server,
                          GskHttpRequest *request,
                          GskStream      *post_data)
{
  GskHttpContentResult rv;
  if (request->host != NULL)
    {
      PathTable *tab = g_hash_table_lookup (table->vhost_to_path_table, request->host);
      if (tab != NULL)
        {
          rv = path_table_respond (tab, content, server, request, post_data);
          if (rv != GSK_HTTP_CONTENT_CHAIN)
            return rv;
        }
    }
  return path_table_respond (table->no_vhost_path_table, content, server, request, post_data);
}

/**
 * gsk_http_content_respond:
 * @content: the content database that will define the response.
 * @server: the server which received the request.
 * @request: the server's request.
 * @post_data: the server's post data, or NULL if none.
 *
 * Respond to the given HTTP query,
 * using the registered content in the GskHttpContent.
 */
void gsk_http_content_respond(GskHttpContent *content,
                              GskHttpServer  *server,
                              GskHttpRequest *request,
			      GskStream      *post_data)
{
  Handler *rv = NULL;
  GError *error;
  if (request->user_agent != NULL)
    {
      GSList *list;
      GSList *at;
      list = gsk_prefix_tree_lookup_all (content->user_agent_to_path_vhost_table,
                                         request->user_agent);
      for (at = list; at; at = at->next)
        {
          PathVHostTable *pvh_table = at->data;
          switch (path_vhost_table_respond (pvh_table, content, server, request, post_data))
            {
            case GSK_HTTP_CONTENT_OK:
              g_slist_free (list);
              return;
            case GSK_HTTP_CONTENT_CHAIN:
              break;
            case GSK_HTTP_CONTENT_ERROR:
              g_slist_free (list);
              goto serve_error_page;
            }
        }
      g_slist_free (list);
    }
  if (rv == NULL)
    {
      switch (path_vhost_table_respond (content->path_vhost_table, 
                                        content, server, request, post_data))
      {
      case GSK_HTTP_CONTENT_OK:
        return;
      case GSK_HTTP_CONTENT_CHAIN:
        break;
      case GSK_HTTP_CONTENT_ERROR:
        goto serve_error_page;
      }
    }

  /* nobody responded. */
  error = g_error_new (GSK_G_ERROR_DOMAIN,
                       GSK_ERROR_NO_DOCUMENT,
                       "No response to your request could be found");
  (*content->error_handler) (content, error,
                             server, request, 404, content->error_data);
  g_error_free (error);
  return;

serve_error_page:
  error = g_error_new (GSK_G_ERROR_DOMAIN,
                       GSK_ERROR_INTERNAL,
                       "An internal server error occurred");
  (*content->error_handler) (content, error,
                             server, request, 500, content->error_data);
  g_error_free (error);
  return;
}

static inline void
try_add_content_type (GskHttpContent *content,
                      GskHttpRequest *request,
                      GskHttpResponse *response)
{
  const char *type, *subtype;
  if (gsk_http_content_get_mime_type (content, request->path, &type, &subtype))
    {
      gsk_http_header_set_content_type (response, type);
      gsk_http_header_set_content_subtype (response, subtype);
    }
}

/* --- Raw Data Delivery --- */
typedef struct _DataInfo DataInfo;
struct _DataInfo
{
  gconstpointer  data;
  guint          data_len;
  gpointer       destroy_data;
  GDestroyNotify destroy;
};

static GskHttpContentResult
handle_data_request (GskHttpContent        *content,
                     GskHttpContentHandler *handler,
                     GskHttpServer         *server,
                     GskHttpRequest        *request,
                     GskStream             *post_data,
                     gpointer               data)
{
  DataInfo *di = data;
  GskHttpResponse *response;
  GskStream *stream;
  response = gsk_http_response_from_request (request,
                                             GSK_HTTP_STATUS_OK,
                                             di->data_len);
  handler_ref (handler);
  stream = gsk_memory_slab_source_new (di->data, di->data_len,
                                       di->destroy, di->destroy_data);
  try_add_content_type (content, request, response);
  gsk_http_server_respond (server, request, response, stream);
  g_object_unref (response);
  g_object_unref (stream);
  return GSK_HTTP_CONTENT_OK;
}

static DataInfo *
data_info_alloc ( gconstpointer            data,
                  guint                    data_len,
                  gpointer                 destroy_data,
                  GDestroyNotify           destroy)
{
  DataInfo *di = g_new (DataInfo, 1);
  di->data = data;
  di->data_len = data_len;
  di->destroy_data = destroy_data;
  di->destroy = destroy;
  return di;
}

static void
data_info_destroy (gpointer data)
{
  DataInfo *di = data;
  if (di->destroy)
    di->destroy (di->destroy_data);
  g_free (di);
}

/**
 * gsk_http_content_add_data:
 * @content: the content database to add data to.
 * @id: constraints on the query that will get this request.
 * @data: the binary data to serve to the client.
 * @data_len: length of the data to serve to the client.
 * @destroy_data: function that will be passed to the destroy-notify function.
 * @destroy: function to be called when this content is unregistered.
 *
 * Add fixed data to the HTTP content database.
 */
void           gsk_http_content_add_data (GskHttpContent          *content,
                                          const GskHttpContentId  *id,
                                          gconstpointer            data,
					  guint                    data_len,
					  gpointer                 destroy_data,
				          GDestroyNotify           destroy)
{
  DataInfo *di = data_info_alloc (data, data_len, destroy_data, destroy);
  GskHttpContentHandler *h;
  h = gsk_http_content_handler_new (handle_data_request, di, data_info_destroy);
  gsk_http_content_add_handler (content, id, h, GSK_HTTP_CONTENT_REPLACE);
  gsk_http_content_handler_unref (h);
}

/**
 * gsk_http_content_add_data_by_path:
 * @content: the content database to add data to.
 * @path: raw URI location of the content.
 * @data: the binary data to serve to the client.
 * @data_len: length of the data to serve to the client.
 * @destroy_data: function that will be passed to the destroy-notify function.
 * @destroy: function to be called when this content is unregistered.
 *
 * Add fixed data to the HTTP content database.
 */
void           gsk_http_content_add_data_by_path (GskHttpContent          *content,
                                                  const char              *path,
                                                  gconstpointer            data,
					          guint                    data_len,
					          gpointer                 destroy_data,
				                  GDestroyNotify           destroy)
{
  GskHttpContentId id = GSK_HTTP_CONTENT_ID_INIT;
  id.path = path;
  gsk_http_content_add_data (content, &id, data, data_len, destroy_data, destroy);
}

/* --- Raw File Delivery --- */
typedef struct _FileInfo FileInfo;
struct _FileInfo
{
  guint uri_path_len;
  char *uri_path;
  char *fs_path;
  GskHttpContentFileType file_type;
};
static GskHttpContentResult
handle_file_request (GskHttpContent        *content,
                     GskHttpContentHandler *handler,
                     GskHttpServer         *server,
                     GskHttpRequest        *request,
                     GskStream             *post_data,
                     gpointer               data)
{
  FileInfo *fi = data;
  GskHttpResponse *response;
  GskStream *stream;
  gint64 size = -1;
  struct stat stat_buf;
  const char *end;
  char *path;

  /* compute path (check if it's ok) */
  g_return_val_if_fail (memcmp (fi->uri_path, request->path, fi->uri_path_len) == 0,
                        GSK_HTTP_CONTENT_ERROR);
  end = request->path + fi->uri_path_len;
  if (memcmp (end, "../", 3) == 0
   || strstr (end, "/../") != NULL
   || g_str_has_suffix (end, "/.."))
    {
      /* security error */
      server_respond_printf (server, request, GSK_HTTP_STATUS_BAD_REQUEST,
                             "<html><head><title>Security Error</title></head>\n"
                             "<body>\n"
                             "<h1>Security Error</h1>\n"
                             "Attempting to access the path:\n"
                             "<pre>\n"
                             "  %s\n"
                             "</pre>\n"
                             "is not allowed: it may not contain '..'"
                             "</body>\n"
                             "</html>\n"
                           , request->path);
      return GSK_HTTP_CONTENT_OK;
    }

  if (fi->file_type == GSK_HTTP_CONTENT_FILE_EXACT)
    path = g_strdup (fi->fs_path);
  else
    path = g_strdup_printf ("%s/%s", fi->fs_path, request->path);

  stream = gsk_stream_fd_new_read_file (path, NULL);
  if (stream == NULL)
    {
      /* serve 404 page */
      server_respond_printf (server, request,
                             GSK_HTTP_STATUS_BAD_REQUEST,
                             "<html><head><title>Not Found</title></head>\n"
                             "<body>\n"
                             "<h1>Not Found</h1>\n"
                             "Unable to find the file '%s'\n"
                             "which was requested as the uri '%s'\n"
                             "</body>\n"
                             "</html>\n",
                             path, request->path);
      g_free (path);
      return GSK_HTTP_CONTENT_OK;
    }

  if (fstat (GSK_STREAM_FD (stream)->fd, &stat_buf) == 0)
    size = stat_buf.st_size;
  response = gsk_http_response_from_request (request, GSK_HTTP_STATUS_OK, size);
  try_add_content_type (content, request, response);
  gsk_http_server_respond (server, request, response, stream);
  g_object_unref (response);
  g_object_unref (stream);
  g_free (path);
  return GSK_HTTP_CONTENT_OK;
}

static void
file_info_destroy (gpointer data)
{
  FileInfo *fi = data;
  g_free (fi->fs_path);
  g_free (fi->uri_path);
  g_free (fi);
}

/**
 * gsk_http_content_add_file:
 * @content: the content database to add data to.
 * @path: raw URI location of the content.
 * @fs_path: location of the data source in the file-system.
 * @type: type of file service:  this may be an exact file,
 * or a directory, or a directory tree.
 *
 * Add data from files from the native filesystem.
 */
void           gsk_http_content_add_file (GskHttpContent          *content,
                                          const char              *path,
					  const char              *fs_path,
					  GskHttpContentFileType   type)
{
  GskHttpContentId id = GSK_HTTP_CONTENT_ID_INIT;
  FileInfo *fi = g_new (FileInfo, 1);
  Handler *h;
  fi->uri_path = g_strdup (path);
  fi->uri_path_len = strlen (path);
  fi->fs_path = g_strdup (fs_path);
  fi->file_type = type;
  if (type == GSK_HTTP_CONTENT_FILE_EXACT)
    id.path = path;
  else
    id.path_prefix = path;
  h = gsk_http_content_handler_new (handle_file_request, fi, file_info_destroy);
  gsk_http_content_add_handler (content, &id, h, GSK_HTTP_CONTENT_REPLACE);
  gsk_http_content_handler_unref (h);
}

/**
 * gsk_http_content_add_file_by_id:
 * @content: the content database to add data to.
 * @id: how to match for this file in the URI space.
 * This must define either 'path' or 'path_prefix'.
 * @fs_path: location of the data source in the file-system.
 * @type: type of file service:  this may be an exact file,
 * or a directory, or a directory tree.
 *
 * Add data from files from the native filesystem.
 */
void
gsk_http_content_add_file_by_id (GskHttpContent          *content,
                                 const GskHttpContentId  *id,
                                 const char              *fs_path,
                                 GskHttpContentFileType   type)
{
  FileInfo *fi;
  Handler *h;
  g_return_if_fail (id->path != NULL || id->path_prefix != NULL);
  fi = g_new (FileInfo, 1);
  fi->uri_path = g_strdup (id->path_prefix ? id->path_prefix : id->path);
  fi->uri_path_len = strlen (fi->uri_path);
  fi->fs_path = g_strdup (fs_path);
  fi->file_type = type;
  h = gsk_http_content_handler_new (handle_file_request, fi, file_info_destroy);
  gsk_http_content_add_handler (content, id, h, GSK_HTTP_CONTENT_REPLACE);
  gsk_http_content_handler_unref (h);
}


/* --- serving pages --- */
static gboolean
handle_new_request_available (GskHttpServer *server,
                              gpointer       data)
{
  GskHttpContent *content = data;
  GskHttpRequest *request;
  GskStream *post_data = NULL;
  if (gsk_http_server_get_request (server, &request, &post_data))
    {
      gsk_http_content_respond (content, server, request, post_data);
      if (post_data)
        g_object_unref (post_data);
      g_object_unref (request);
    }
  return TRUE;
}

static gboolean
handle_request_pipe_shutdown (GskHttpServer *server,
                              gpointer       data)
{
  return FALSE;
}

/**
 * gsk_http_content_manage_server:
 * @content: the content database which will handle requests.
 * @server: a GskHttpServer to manage.
 *
 * Handle requests on the given server, using the GskHttpContent.
 */
void
gsk_http_content_manage_server (GskHttpContent *content,
                                GskHttpServer  *server)
{
  if (content->keepalive_idle_timeout_ms >= 0)
    gsk_http_server_set_idle_timeout (server, content->keepalive_idle_timeout_ms);
  gsk_http_server_trap (server, handle_new_request_available,
                        handle_request_pipe_shutdown,
                        content, NULL);
}

static gboolean
handler_new_connection (GskStream    *stream,
                        gpointer      data,
                        GError      **error)
{
  GskHttpServer *server = gsk_http_server_new ();
  GskHttpContent *content = data;
  gsk_http_content_manage_server (content, server);
  if (!gsk_stream_attach_pair (GSK_STREAM (server), stream, error))
    {
      g_object_unref (server);
      return FALSE;
    }
  g_object_unref (server);
  g_object_unref (stream);
  return TRUE;
}

static void
handler_listener_failed (GError       *err,
                         gpointer      data)
{
  g_warning ("GskHttpContent: listener failed: %s", err->message);
}

/**
 * gsk_http_content_listen:
 * @content: the content database which will handle requests.
 * @address: the address to bind to, typically in the TCP or Unix namespaces.
 * @error: where to put the error if something goes wrong.
 *
 * Start an HTTP server listening on the given port,
 * using content from the given content database.
 *
 * returns: whether the listen call succeeded.
 */
gboolean
gsk_http_content_listen (GskHttpContent   *content,
                         GskSocketAddress *address,
                         GError          **error)
{
  GskStreamListener *listener = gsk_stream_listener_socket_new_bind (address, error);
  if (listener == NULL)
    return FALSE;
  gsk_fd_set_close_on_exec (GSK_STREAM_LISTENER_SOCKET (listener)->fd, TRUE);
  gsk_stream_listener_handle_accept (listener, handler_new_connection,
                                     handler_listener_failed,
                                     content, NULL);
  return TRUE;
}
