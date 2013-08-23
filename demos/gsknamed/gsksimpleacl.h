#ifndef __GSK_SIMPLE_ACL_H_
#define __GSK_SIMPLE_ACL_H_

#include <glib.h>
#include <gsk/gskbasic.h>

G_BEGIN_DECLS

typedef struct _GskSimpleAcl GskSimpleAcl;

GskSimpleAcl *gsk_simple_acl_new           (gboolean        accept_by_default);
void          gsk_simple_acl_append_accept (GskSimpleAcl   *acl,
					    const guint8   *ip_address,
                                            guint           significant_bits);
void          gsk_simple_acl_prepend_accept(GskSimpleAcl   *acl,
					    const guint8   *ip_address,
                                            guint           significant_bits);
void          gsk_simple_acl_append_reject (GskSimpleAcl   *acl,
					    const guint8   *ip_address,
                                            guint           significant_bits);
void          gsk_simple_acl_prepend_reject(GskSimpleAcl   *acl,
					    const guint8   *ip_address,
                                            guint           significant_bits);
void          gsk_simple_acl_append_sub    (GskSimpleAcl   *acl,
					    gboolean        accept_if_matched,
                                            GskSimpleAcl   *sub_acl);
void          gsk_simple_acl_prepend_sub   (GskSimpleAcl   *acl,
					    gboolean        accept_if_matched,
                                            GskSimpleAcl   *sub_acl);
void          gsk_simple_acl_set_default   (GskSimpleAcl   *acl,
                                            gboolean        accept_by_default);
void          gsk_simple_acl_destroy       (GskSimpleAcl   *simple_acl);
gboolean      gsk_simple_acl_check         (GskSimpleAcl   *simple_acl,
                                            const guint8   *ip_address);

G_END_DECLS

#endif
