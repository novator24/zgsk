#ifndef __GSK_MEMORY_BARRIER_H_
#define __GSK_MEMORY_BARRIER_H_

/* generated by gsk's configure script */
#define GSK_MEMORY_BARRIER()   __asm__ __volatile__ ("")

/* on later GCC's, there's a builtin for this. */
#ifdef __GCC__
# if (__GCC__ > 4 || (__GCC__ == 4 && __GCC_MINOR__ >= 1))
#  ifdef GSK_MEMORY_BARRIER
#   undef GSK_MEMORY_BARRIER
#  endif
#  define GSK_MEMORY_BARRIER  __sync_synchronize()
# endif
#endif

/** GSK_MEMORY_BARRIER:
 *  Implement a memory-barrier.
 *
 *  When defined, this ensures that all writes to the
 *  memory have been committed.  This also ensures
 *  that compiler optimization (which could allow reordering
 *  of writes) will not be done.
 *
 *  To really effectively use this,
 *  you should read something about memory-models,
 *  preferably something pessimistic, like:
 *      http://www.hpl.hp.com/techreports/2004/HPL-2004-209.pdf
 */

#endif