/* Insert header here. */
#ifndef __GSK_SMALL_HTTP_CLIENT_CONFIG_H_
#define __GSK_SMALL_HTTP_CLIENT_CONFIG_H_


G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskSmallHttpClientConfig GskSmallHttpClientConfig;
typedef struct _GskSmallHttpClientConfigClass GskSmallHttpClientConfigClass;

/* --- type macros --- */
GtkType gsk_small_http_client_config_get_type();
#define GSK_TYPE_SMALL_HTTP_CLIENT_CONFIG		(gsk_small_http_client_config_get_type ())
#define GSK_SMALL_HTTP_CLIENT_CONFIG(obj)              (GTK_CHECK_CAST ((obj), GSK_TYPE_SMALL_HTTP_CLIENT_CONFIG, GskSmallHttpClientConfig))
#define GSK_SMALL_HTTP_CLIENT_CONFIG_CLASS(klass)      (GTK_CHECK_CLASS_CAST ((klass), GSK_TYPE_SMALL_HTTP_CLIENT_CONFIG, GskSmallHttpClientConfigClass))
#define GSK_SMALL_HTTP_CLIENT_CONFIG_GET_CLASS(obj)    (GSK_SMALL_HTTP_CLIENT_CONFIG_CLASS(GTK_OBJECT(obj)->klass))
#define GSK_IS_SMALL_HTTP_CLIENT_CONFIG(obj)           (GTK_CHECK_TYPE ((obj), GSK_TYPE_SMALL_HTTP_CLIENT_CONFIG))
#define GSK_IS_SMALL_HTTP_CLIENT_CONFIG_CLASS(klass)   (GTK_CHECK_CLASS_TYPE ((klass), GSK_TYPE_SMALL_HTTP_CLIENT_CONFIG))

/* --- structures --- */
struct _GskSmallHttpClientConfigClass 
{
  GtkObjectClass	object_class;
};
struct _GskSmallHttpClientConfig 
{
  GtkObject		object;

  char                 *user_agent;
  char                 *host;
  int                   port;
  char                 *request_uri;


  unsigned              use_http_11 : 1;
};

/* --- prototypes --- */

GskSmallHttpClientConfig *gsk_small_http_client_config_new ();

/* Configuration */
void   gsk_small_http_client_config_set_user_agent (GskSmallHttpClientConfig*,
                                                    const char *user_agent);
void   gsk_small_http_client_config_set_host       (GskSmallHttpClientConfig*,
                                                    const char *host);
void   gsk_small_http_client_config_set_port       (GskSmallHttpClientConfig*,
                                                    int         port);
void   gsk_small_http_client_config_set_request_uri(GskSmallHttpClientConfig*,
                                                    const char *request_uri);
void   gsk_small_http_client_config_set_http_11    (GskSmallHttpClientConfig*,
                                                    gboolean     http_11);

/* Making a request. */
void   gsk_small_http_client_config_do_request     (GskSmallHttpClientConfig*,
                                                    GskHttpClient *client);

void   gsk_small_http_client_config_maybe_subrequest(GskSmallHttpClientConfig*,
						     const char *url,
						     GskSmallHttpClientPool*);

G_END_DECLS

#endif
