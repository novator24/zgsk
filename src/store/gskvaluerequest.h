#ifndef __GSK_VALUE_REQUEST_H_
#define __GSK_VALUE_REQUEST_H_

#include "../gskrequest.h"

G_BEGIN_DECLS

typedef GskRequestClass         GskValueRequestClass;
typedef struct _GskValueRequest GskValueRequest;

GType gsk_value_request_get_type (void) G_GNUC_CONST;

#define GSK_TYPE_VALUE_REQUEST (gsk_value_request_get_type ())
#define GSK_VALUE_REQUEST(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_VALUE_REQUEST, GskValueRequest))
#define GSK_IS_VALUE_REQUEST(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_VALUE_REQUEST))

struct _GskValueRequest
{
  GskRequest request;

  GValue value;
};

G_INLINE_FUNC
G_CONST_RETURN GValue * gsk_value_request_get_value (gpointer request);

#if defined (G_CAN_INLINE) || defined (__GSK_VALUE_REQUEST_C__)

G_INLINE_FUNC G_CONST_RETURN GValue *
gsk_value_request_get_value (gpointer request)
{
  GskValueRequest *value_request = GSK_VALUE_REQUEST (request);

  g_return_val_if_fail (!gsk_request_get_is_running (request), NULL);
  g_return_val_if_fail (!gsk_request_get_is_cancelled (request), NULL);
  g_return_val_if_fail (gsk_request_get_is_done (request), NULL);
  g_return_val_if_fail (!gsk_request_had_error (request), NULL);
  return &value_request->value;
}

#endif /* defined (G_CAN_INLINE) || defined (__GSK_VALUE_REQUEST_C__) */

G_END_DECLS

#endif
