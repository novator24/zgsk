#ifndef __GSK_CGI_CONTENT_H_
#define __GSK_CGI_CONTENT_H_

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GskCgiContent GskCgiContent;

struct _GskCgiContent
{
  char *name;		/* may be NULL */
  guint len;
  const char *data;	/* NUL-terminated, non-NULL */
  char *type;		/* may be NULL for typeless data */
  char *subtype;	/* may be NULL for typeless data */
  char *filename;	/* may be NULL (for type=file form data) */

  /*< private >*/
  GDestroyNotify destroy;
  gpointer destroy_data;
};

GskCgiContent *gsk_cgi_content_new          (const char     *name,
					     guint           len,
				             const char     *data,
				             GDestroyNotify  destroy,
				             gpointer        destroy_data);
void           gsk_cgi_content_set_type     (GskCgiContent  *content,
					     const char     *type,
					     const char     *subtype);
void           gsk_cgi_content_set_filename (GskCgiContent  *content,
					     const char     *filename);
void           gsk_cgi_content_destroy      (GskCgiContent  *content);

G_END_DECLS

#endif
