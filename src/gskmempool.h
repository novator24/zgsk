#ifndef __GSK_MEM_POOL_H_
#define __GSK_MEM_POOL_H_

#include <glib.h>

G_BEGIN_DECLS

typedef struct _GskMemPool GskMemPool;
typedef struct _GskMemPoolFixed GskMemPoolFixed;

/* --- Allocate-only Memory Pool --- */
struct _GskMemPool
{
  /*< private >*/
  gpointer all_chunk_list;
  char *chunk;
  guint chunk_left;
};

#define GSK_MEM_POOL_STATIC_INIT                        { NULL, NULL, 0 }


G_INLINE_FUNC void     gsk_mem_pool_construct    (GskMemPool     *pool);
G_INLINE_FUNC void     gsk_mem_pool_construct_with_scratch_buf
                                                 (GskMemPool     *pool,
                                                  gpointer        buffer,
                                                  gsize           buffer_size);
G_INLINE_FUNC gpointer gsk_mem_pool_alloc        (GskMemPool     *pool,
                                                  gsize           size);
              gpointer gsk_mem_pool_alloc0       (GskMemPool     *pool,
                                                  gsize           size);
G_INLINE_FUNC gpointer gsk_mem_pool_alloc_unaligned(GskMemPool   *pool,
                                                  gsize           size);
              char    *gsk_mem_pool_strdup       (GskMemPool     *pool,
                                                  const char     *str);
G_INLINE_FUNC void     gsk_mem_pool_destruct     (GskMemPool     *pool);

/* --- Allocate and free Memory Pool --- */
struct _GskMemPoolFixed
{
  /*< private >*/
  gpointer slab_list;
  char *chunk;
  guint pieces_left;
  guint piece_size;
  gpointer free_list;
};

#define GSK_MEM_POOL_FIXED_STATIC_INIT(size) \
                          { NULL, NULL, 0, size, NULL } 

G_INLINE_FUNC void     gsk_mem_pool_fixed_construct_with_scratch_buf
                                                 (GskMemPoolFixed *pool,
                                                  gsize            elt_size,
                                                  gpointer         buffer,
                                                  gsize            buffer_n_elements);
void     gsk_mem_pool_fixed_construct (GskMemPoolFixed  *pool,
                                       gsize             size);
gpointer gsk_mem_pool_fixed_alloc     (GskMemPoolFixed  *pool);
gpointer gsk_mem_pool_fixed_alloc0    (GskMemPoolFixed  *pool);
void     gsk_mem_pool_fixed_free      (GskMemPoolFixed  *pool,
                                       gpointer          from_pool);
void     gsk_mem_pool_fixed_destruct  (GskMemPoolFixed  *pool);



/* private */
gpointer gsk_mem_pool_must_alloc (GskMemPool *pool,
                                  gsize       size);

/* ------------------------------*/
/* -- Inline Implementations --- */

#define _GSK_MEM_POOL_ALIGN(size)	\
  (((size) + sizeof(gpointer) - 1) / sizeof (gpointer) * sizeof (gpointer))
#define _GSK_MEM_POOL_SLAB_GET_NEXT_PTR(slab) \
  (* (gpointer*) (slab))

#if defined(G_CAN_INLINE) || defined(GSK_INTERNAL_IMPLEMENT_INLINES)
G_INLINE_FUNC void     gsk_mem_pool_construct    (GskMemPool     *pool)
{
  pool->all_chunk_list = NULL;
  pool->chunk = NULL;
  pool->chunk_left = 0;
}
G_INLINE_FUNC void     gsk_mem_pool_construct_with_scratch_buf
                                                 (GskMemPool     *pool,
                                                  gpointer        buffer,
                                                  gsize           buffer_size)
{
  pool->all_chunk_list = NULL;
  pool->chunk = buffer;
  pool->chunk_left = buffer_size;
}

G_INLINE_FUNC void     gsk_mem_pool_align        (GskMemPool     *pool)
{
  guint mask = GPOINTER_TO_UINT (pool->chunk) & (sizeof(gpointer)-1);
  if (mask)
    {
      /* need to align chunk */
      guint align = sizeof (gpointer) - mask;
      pool->chunk_left -= align;
      pool->chunk = (char*)pool->chunk + align;
    }
}

G_INLINE_FUNC gpointer gsk_mem_pool_alloc_unaligned   (GskMemPool     *pool,
                                                       gsize           size)
{
  char *rv;
  if (G_LIKELY (pool->chunk_left >= size))
    {
      rv = pool->chunk;
      pool->chunk_left -= size;
      pool->chunk = rv + size;
      return rv;
    }
  else
    /* fall through to non-inline version for
       slow malloc-using case */
    return gsk_mem_pool_must_alloc (pool, size);
}

G_INLINE_FUNC gpointer gsk_mem_pool_alloc            (GskMemPool     *pool,
                                                      gsize           size)
{
  gsk_mem_pool_align (pool);
  return gsk_mem_pool_alloc_unaligned (pool, size);
}

G_INLINE_FUNC void     gsk_mem_pool_destruct     (GskMemPool     *pool)
{
  gpointer slab = pool->all_chunk_list;
  while (slab)
    {
      gpointer new_slab = _GSK_MEM_POOL_SLAB_GET_NEXT_PTR (slab);
      g_free (slab);
      slab = new_slab;
    }
}
G_INLINE_FUNC void     gsk_mem_pool_fixed_construct_with_scratch_buf
                                                 (GskMemPoolFixed *pool,
                                                  gsize            elt_size,
                                                  gpointer         buffer,
                                                  gsize            buffer_n_elements)
{
  pool->slab_list = NULL;
  pool->chunk = buffer;
  pool->pieces_left = buffer_n_elements;
  pool->piece_size = elt_size;
  pool->free_list = NULL;
}

#endif /* G_CAN_INLINE */

G_END_DECLS

#endif
