#ifndef __GSK_MODULE_H_
#define __GSK_MODULE_H_

#include <glib.h>
#include <gmodule.h>

G_BEGIN_DECLS

typedef struct _GskCompileContext GskCompileContext;
typedef struct _GskModule GskModule;

GskCompileContext *gsk_compile_context_new        (void);
void               gsk_compile_context_add_cflags (GskCompileContext *context,
                                                   const char        *flags);
void               gsk_compile_context_add_ldflags(GskCompileContext *context,
                                                   const char        *flags);
void               gsk_compile_context_add_pkg    (GskCompileContext *context,
                                                   const char        *pkg);
void               gsk_compile_context_set_tmp_dir(GskCompileContext *context,
                                                   const char        *tmp_dir);
void               gsk_compile_context_set_gdb    (GskCompileContext *context,
                                                   gboolean           support);
void               gsk_compile_context_set_verbose(GskCompileContext *context,
                                                   gboolean           support);
void               gsk_compile_context_free       (GskCompileContext *context);

/* a wrapper around GModule with ref-counting,
 * and the ability to delete itself. */

GskModule *gsk_module_compile (GskCompileContext *context,
                               guint              n_sources,
                               char             **sources,
                               GModuleFlags       flags,
                               gboolean           delete_sources,
                               char             **program_output,
                               GError           **error);
GskModule *gsk_module_open    (const char        *filename,
                               GModuleFlags       flags,
                               GError           **error);

GskModule *gsk_module_ref     (GskModule *module);
void       gsk_module_unref   (GskModule *module);
gpointer   gsk_module_lookup  (GskModule *module,
                               const char *symbol_name);



G_END_DECLS

#endif
