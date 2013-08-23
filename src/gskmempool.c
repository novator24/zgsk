#include "gskmempool.h"
#include <string.h>

#define ALIGN(size)	        _GSK_MEM_POOL_ALIGN(size)
#define SLAB_GET_NEXT_PTR(slab) _GSK_MEM_POOL_SLAB_GET_NEXT_PTR(slab)
#define CHUNK_SIZE	        8192

/* --- Allocate-only Memory Pool --- */
/**
 * gsk_mem_pool_construct:
 * @pool: the mem-pool to initialize.
 *
 * Initialize the members of a mem-pool.
 * (The memory for the mem-pool structure itself
 * must be provided: either as a part of another
 * structure or on the stack.)
 */

/**
 * gsk_mem_pool_construct_with_scratch_buf:
 * @pool: the mem-pool to initialize.
 * @buffer: the buffer to use.
 * @buffer_size: the number of bytes in buffer
 * to use as storage.
 *
 * Initialize the members of a mem-pool,
 * using a scratch-buffer that the user provides.
 * (The caller is responsible for ensuring that
 * the buffer exists long enough)
 */


/**
 * gsk_mem_pool_alloc:
 * @pool: area to allocate memory from.
 * @size: number of bytes to allocate.
 *
 * Allocate memory from a pool,
 * This function terminates the program if there is an
 * out-of-memory condition.
 *
 * returns: the slab of memory allocated from the pool.
 */


/**
 * gsk_mem_pool_alloc_unaligned:
 * @pool: area to allocate memory from.
 * @size: number of bytes to allocate.
 *
 * Allocate memory from a pool, without ensuring that
 * it is aligned.
 *
 * returns: the slab of memory allocated from the pool.
 */

/**
 * gsk_mem_pool_must_alloc:
 * @pool: area to allocate memory from.
 * @size: number of bytes to allocate.
 *
 * private
 *
 * returns: the slab of memory allocated from the pool.
 */

gpointer
gsk_mem_pool_must_alloc   (GskMemPool     *pool,
			   gsize           size)
{
  char *rv;
  if (size < CHUNK_SIZE)
    {
      /* Allocate a new chunk. */
      gpointer carved = g_malloc (CHUNK_SIZE + sizeof (gpointer));
      SLAB_GET_NEXT_PTR (carved) = pool->all_chunk_list;
      pool->all_chunk_list = carved;
      rv = carved;
      rv += sizeof (gpointer);
      pool->chunk = rv + size;
      pool->chunk_left = CHUNK_SIZE - size;
    }
  else
    {
      /* Allocate a chunk of exactly the right size. */
      gpointer carved = g_malloc (size + sizeof (gpointer));
      if (pool->all_chunk_list)
	{
	  SLAB_GET_NEXT_PTR (carved) = SLAB_GET_NEXT_PTR (pool->all_chunk_list);
	  SLAB_GET_NEXT_PTR (pool->all_chunk_list) = carved;
	}
      else
	{
	  SLAB_GET_NEXT_PTR (carved) = NULL;
	  pool->all_chunk_list = carved;
	}
      rv = carved;
      rv += sizeof (gpointer);
    }
  return rv;
}

/**
 * gsk_mem_pool_alloc0:
 * @pool: area to allocate memory from.
 * @size: number of bytes to allocate.
 *
 * Allocate memory from a pool, and initializes it to 0.
 * This function terminates the program if there is an
 * out-of-memory condition.
 *
 * returns: the slab of memory allocated from the pool.
 */
gpointer gsk_mem_pool_alloc0           (GskMemPool     *pool,
                                        gsize           size)
{
  return memset (gsk_mem_pool_alloc (pool, size), 0, size);
}

/**
 * gsk_mem_pool_strdup:
 * @pool: area to allocate memory from.
 * @str: a string to duplicate, or NULL.
 *
 * Allocated space from the mem-pool to store
 * the given string (including its terminal
 * NUL character) and copy the string onto that buffer.
 *
 * If @str is NULL, then NULL is returned.
 *
 * returns: a copy of @str, allocated from @pool.
 */
char *
gsk_mem_pool_strdup       (GskMemPool     *pool,
			   const char     *str)
{
  guint L;
  if (str == NULL)
    return NULL;
  L = strlen (str) + 1;
  return memcpy (gsk_mem_pool_alloc_unaligned (pool, L), str, L);
}

/**
 * gsk_mem_pool_destruct:
 * @pool: the pool to destroy.
 *
 * Destroy all chunk associated with the given mem-pool.
 */
#if 0           /* inlined */
void
gsk_mem_pool_destruct     (GskMemPool     *pool)
{
  gpointer slab = pool->all_chunk_list;
  while (slab)
    {
      gpointer new_slab = SLAB_GET_NEXT_PTR (slab);
      g_free (slab);
      slab = new_slab;
    }
#ifdef GSK_DEBUG
  memset (pool, 0xea, sizeof (GskMemPool));
#endif
}
#endif

/* === Fixed-Size MemPool's === */
/**
 * gsk_mem_pool_fixed_construct:
 * @pool: the fixed-size memory pool to construct.
 * @size: size of the allocation to take from the mem-pool.
 *
 * Set up a fixed-size memory allocator for use.
 */
void
gsk_mem_pool_fixed_construct (GskMemPoolFixed     *pool,
			      gsize           size)
{
  pool->slab_list = NULL;
  pool->chunk = NULL;
  pool->pieces_left = 0;
  pool->piece_size = MAX (ALIGN (size), sizeof (gpointer));
  pool->free_list = NULL;
}

/**
 * gsk_mem_pool_fixed_alloc:
 * @pool: the pool to allocate a block from.
 *
 * Allocate a block of the Fixed-Pool's size.
 *
 * returns: the allocated memory.
 */
gpointer
gsk_mem_pool_fixed_alloc     (GskMemPoolFixed     *pool)
{
  if (pool->free_list)
    {
      gpointer rv = pool->free_list;
      pool->free_list = SLAB_GET_NEXT_PTR (rv);
      return rv;
    }
  if (pool->pieces_left == 0)
    {
      gpointer slab = g_malloc (256 * pool->piece_size + sizeof (gpointer));
      SLAB_GET_NEXT_PTR (slab) = pool->slab_list;
      pool->slab_list = slab;
      pool->chunk = slab;
      pool->chunk += sizeof (gpointer);
      pool->pieces_left = 256;
    }
  {
    gpointer rv = pool->chunk;
    pool->chunk += pool->piece_size;
    pool->pieces_left--;
    return rv;
  }
}

/**
 * gsk_mem_pool_fixed_alloc0:
 * @pool: the pool to allocate a block from.
 *
 * Allocate a block of the Fixed-Pool's size.
 * Set its contents to 0.
 *
 * returns: the allocated, zeroed memory.
 */
gpointer gsk_mem_pool_fixed_alloc0    (GskMemPoolFixed     *pool)
{
  return memset (gsk_mem_pool_fixed_alloc (pool), 0, pool->piece_size);
}

/**
 * gsk_mem_pool_fixed_free:
 * @pool: the pool to return memory to.
 * @from_pool: the memory to return to this pool.
 * It must have been allocated from this pool.
 *
 * Recycle some of the pool's memory back to it.
 */
void     gsk_mem_pool_fixed_free      (GskMemPoolFixed     *pool,
                                       gpointer        from_pool)
{
  SLAB_GET_NEXT_PTR (from_pool) = pool->free_list;
  pool->free_list = from_pool;
}

/**
 * gsk_mem_pool_fixed_destruct:
 * @pool: the pool to destroy.
 *
 * Free all memory associated with this pool.
 */
void     gsk_mem_pool_fixed_destruct  (GskMemPoolFixed     *pool)
{
  while (pool->slab_list)
    {
      gpointer kill = pool->slab_list;
      pool->slab_list = SLAB_GET_NEXT_PTR (kill);
      g_free (kill);
    }
}
