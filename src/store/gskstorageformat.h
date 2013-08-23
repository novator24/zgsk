#ifndef __GSK_STORAGE_FORMAT_H_
#define __GSK_STORAGE_FORMAT_H_

/*
 * GskStorageFormat -- interface for things that can serialize/deserialize
 * values to/from streams.
 */

#include "../gskstream.h"
#include "gskvaluerequest.h"

G_BEGIN_DECLS

typedef struct _GskStorageFormatIface GskStorageFormatIface;
typedef struct _GskStorageFormat      GskStorageFormat;

GType gsk_storage_format_get_type (void) G_GNUC_CONST;

#define GSK_TYPE_STORAGE_FORMAT (gsk_storage_format_get_type ())
#define GSK_STORAGE_FORMAT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
			       GSK_TYPE_STORAGE_FORMAT, \
			       GskStorageFormat))
#define GSK_STORAGE_FORMAT_GET_IFACE(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), \
				  GSK_TYPE_STORAGE_FORMAT, \
				  GskStorageFormatIface))
#define GSK_IS_STORAGE_FORMAT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_STORAGE_FORMAT))

struct _GskStorageFormatIface
{
  GTypeInterface base_iface;

  /* Return a read-only stream that can be deserialized into an
   * equivalent value.
   */
  GskStream *       (*serialize)   (GskStorageFormat  *format,
				    const GValue      *value,
				    GError           **error);

  /* Return a request to deserialize a value of type value_type from
   * a read-only stream.
   */
  GskValueRequest * (*deserialize) (GskStorageFormat  *format,
				    GskStream         *stream,
				    GType              value_type,
				    GError           **error);
};

/*
 * Convenience wrappers.
 */

G_INLINE_FUNC
GskStream *       gsk_storage_format_serialize   (gpointer       format,
						  const GValue  *value,
						  GError       **error);
G_INLINE_FUNC
GskValueRequest * gsk_storage_format_deserialize (gpointer       format,
						  GskStream     *stream,
						  GType          value_type,
						  GError       **error);

/*
 * Inline functions.
 */

#if defined (G_CAN_INLINE) || defined (__GSK_STORAGE_FORMAT_C__)

G_INLINE_FUNC GskStream *
gsk_storage_format_serialize (gpointer       format,
			      const GValue  *value,
			      GError       **error)
{
  GskStorageFormatIface *iface;

  g_return_val_if_fail (format, NULL);
  g_return_val_if_fail (GSK_IS_STORAGE_FORMAT (format), NULL);
  iface = GSK_STORAGE_FORMAT_GET_IFACE (format);
  g_return_val_if_fail (iface, NULL);
  g_return_val_if_fail (iface->serialize, NULL);

  return (*iface->serialize) (GSK_STORAGE_FORMAT (format), value, error);
}

G_INLINE_FUNC GskValueRequest *
gsk_storage_format_deserialize (gpointer    format,
				GskStream  *stream,
				GType       value_type,
				GError    **error)
{
  GskStorageFormatIface *iface;

  g_return_val_if_fail (format, NULL);
  g_return_val_if_fail (GSK_IS_STORAGE_FORMAT (format), NULL);
  iface = GSK_STORAGE_FORMAT_GET_IFACE (format);
  g_return_val_if_fail (iface, NULL);
  g_return_val_if_fail (iface->deserialize, NULL);

  return (*iface->deserialize) (GSK_STORAGE_FORMAT (format),
				stream,
				value_type,
				error);
}
#endif /* defined (G_CAN_INLINE) || defined (__GSK_STORAGE_FORMAT_C__) */

G_END_DECLS

#endif
