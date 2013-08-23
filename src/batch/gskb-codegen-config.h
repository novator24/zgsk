/*
    GSKB - a batch processing framework

    gskb-codegen-config:  code-generation configuration.

    Copyright (C) 2008 Dave Benson

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA

    Contact:
        daveb@ffem.org <Dave Benson>
*/

#ifndef __GSKB_CODEGEN_CONFIG_H_
#define __GSKB_CODEGEN_CONFIG_H_

#include "gskb-format.h"

G_BEGIN_DECLS


typedef enum
{
  GSKB_CODEGEN_SECTION_TYPEDEFS,
  GSKB_CODEGEN_SECTION_STRUCTURES,
  GSKB_CODEGEN_SECTION_FORMAT_DECLS,
  GSKB_CODEGEN_SECTION_FORMAT_PRIVATE_DECLS,
  GSKB_CODEGEN_SECTION_FORMAT_IMPLS,
  GSKB_CODEGEN_SECTION_FUNCTION_PROTOTYPES,
  GSKB_CODEGEN_SECTION_FUNCTION_IMPLS,
  GSKB_CODEGEN_SECTION_NAMESPACE_DECL,
  GSKB_CODEGEN_SECTION_NAMESPACE_IMPL
} GskbCodegenSection;

#define GSKB_N_CODEGEN_SECTIONS (GSKB_CODEGEN_SECTION_FUNCTION_IMPLS+1)

typedef enum
{
  /* if format->always_by_pointer,
         void {lctype}_pack      (const {type}  *value,
                                  GskbAppendFunc append,
                                  gpointer       append_data);
     else
         void {lctype}_pack      ({type}         value,
                                  GskbAppendFunc append,
                                  gpointer       append_data); */
  GSKB_CODEGEN_OUTPUT_PACK,


  /* if format->always_by_pointer,
         guint {lctype}_get_packed_size (const {type}  *value);
     else
         guint {lctype}_get_packed_size ({type}         value); */
  GSKB_CODEGEN_OUTPUT_GET_PACKED_SIZE,

  /* if format->always_by_pointer,
         guint {lctype}_pack_slab (const {type}  *value,
                                   guint8        *out);
     else
         guint {lctype}_pack_slab ({type}         value,
                                   guint8        *out);  */
  GSKB_CODEGEN_OUTPUT_PACK_SLAB,


  /*     guint    {lctype}_validate_partial  (guint          len,
                                              const guint8  *data,
                                              GError       **error);    */
  GSKB_CODEGEN_OUTPUT_VALIDATE_PARTIAL,

  /*     guint {lctype}_unpack     (const guint8  *data,
                                    {type}        *value_out);
      will use glib's malloc as needed (for each string or array)
   */
  GSKB_CODEGEN_OUTPUT_UNPACK,

  /*     guint {lctype}_unpack_mempool    (const guint8  *data,
                                           {type}        *value_out,
                                           GskMemPool    *mem_pool);
   */
  GSKB_CODEGEN_OUTPUT_UNPACK_MEMPOOL,

  /* destruct an object.  this method will be define to nothing to
     types that do not require destruction:
         void {lctype}_destruct   ({type}         *value,
                                   GskbUnpackFlags flags,
                                   GskbAllocator  *allocator);   */
  GSKB_CODEGEN_OUTPUT_DESTRUCT,

} GskbCodegenOutputFunction;

#define GSKB_N_CODEGEN_OUTPUT_FUNCTIONS  (GSKB_CODEGEN_OUTPUT_DESTRUCT+1)


typedef struct _GskbCodegenConfig GskbCodegenConfig;
struct _GskbCodegenConfig
{
  gboolean all_static;
  guint rv_type_space;
  guint func_name_space;
  guint type_name_space;
  guint max_width;
};

GskbCodegenConfig *
     gskb_codegen_config_new            (void);
void gskb_codegen_config_set_all_static (GskbCodegenConfig *config,
                                         gboolean           all_static);
void gskb_codegen_config_free           (GskbCodegenConfig *config);



/* internal */
#include "../gskbuffer.h"
void        gskb_format_codegen        (GskbFormat *format,
                                        GskbCodegenSection section,
                                        const GskbCodegenConfig *config,
                                        GskBuffer *output);
void        gskb_namespace_codegen     (GskbNamespace *ns,
                                        GskbCodegenSection section,
                                        const GskbCodegenConfig *config,
                                        GskBuffer *output);

G_END_DECLS

#endif
