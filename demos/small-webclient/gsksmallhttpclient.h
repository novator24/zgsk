/* Insert header here. */
#ifndef __GSK_SMALL_HTTP_CLIENT_H_
#define __GSK_SMALL_HTTP_CLIENT_H_


G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskSmallHttpClient GskSmallHttpClient;
typedef struct _GskSmallHttpClientClass GskSmallHttpClientClass;

/* --- type macros --- */
GtkType gsk_small_http_client_get_type();
#define GSK_TYPE_SMALL_HTTP_CLIENT			(gsk_small_http_client_get_type ())
#define GSK_SMALL_HTTP_CLIENT(obj)              (GTK_CHECK_CAST ((obj), GSK_TYPE_SMALL_HTTP_CLIENT, GskSmallHttpClient))
#define GSK_SMALL_HTTP_CLIENT_CLASS(klass)      (GTK_CHECK_CLASS_CAST ((klass), GSK_TYPE_SMALL_HTTP_CLIENT, GskSmallHttpClientClass))
#define GSK_SMALL_HTTP_CLIENT_GET_CLASS(obj)    (GSK_SMALL_HTTP_CLIENT_CLASS(GTK_OBJECT(obj)->klass))
#define GSK_IS_SMALL_HTTP_CLIENT(obj)           (GTK_CHECK_TYPE ((obj), GSK_TYPE_SMALL_HTTP_CLIENT))
#define GSK_IS_SMALL_HTTP_CLIENT_CLASS(klass)   (GTK_CHECK_CLASS_TYPE ((klass), GSK_TYPE_SMALL_HTTP_CLIENT))

/* --- structures --- */
struct _GskSmallHttpClientClass 
{
  GskHttpClientClass		http_client_class;
};
struct _GskSmallHttpClient 
{
  GskHttpClient		http_client;
};

/* --- prototypes --- */



G_END_DECLS

#endif
