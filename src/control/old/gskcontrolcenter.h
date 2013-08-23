#ifndef __GSK_CONTROL_CENTER_H_
#define __GSK_CONTROL_CENTER_H_

typedef gboolean (*GskControlFunc) (guint              n_arguments,
                                    const GParamSpec **pspecs,
				    const GValue      *arguments,
                                    GValue            *return_value,
				    gpointer           func_data,
				    GError           **error);


/* Methods for registering controllable objects,
 * functions and methods. */


GskControlCenter *gsk_control_center_default (void);

/* managing non-global contexts */
void gsk_control_center_add_object   (GskControlCenter *context,
                                      const char       *path,
				      GObject          *object,
				      gboolean          weak_ref);
void gsk_control_center_add_function (GskControlCenter *context,
                                      const char       *name,
				      GType             ret_value_type,
				      guint             n_arguments,
				      GParamSpec      **arguments,
				      GskControlFunc    func,
				      gpointer          func_data,
				      GDestroyNotify    notify);
GskControlCenterEvent *
     gsk_control_center_add_event    (GskControlCenter *context,
                                      const char       *name,
				      guint             n_keys,
				      GParamSpec      **keys,
				      guint             n_values,
				      GParamSpec      **values);
void gsk_control_center_emit_event   (GskControlCenterEvent *event,
                                      const GValue     *keys,
                                      const GValue     *values);

typedef char *(*GskControlCenterStringify) (const GValue *value);
typedef gboolean (*GskControlCenterDestringify) (const char *str,
                                                 GValue *value);
void gsk_control_center_add_stringifier (GskControlCenter *center,
                                         GskControlCenterStringify print,
                                         GskControlCenterDestringify parse);


GskStream *gsk_control_center_stream (GskControlCenter *center);

/* --- stringification --- */
