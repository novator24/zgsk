
#include <glib.h>

typedef struct _GskQsortStackNode GskQsortStackNode;
typedef struct _GskQsortStack GskQsortStack;

/* Amount of stack to allocate: should be log2(max_array_size)+1.
   on 32-bit, this uses 33*8=264 bytes;
   on 64-bit it uses 65*16=1040 bytes. */
#if (GLIB_SIZEOF_SIZE_T == 8)
#define GSK_QSORT_STACK_MAX_SIZE  (65)
#elif (GLIB_SIZEOF_SIZE_T == 4)
#define GSK_QSORT_STACK_MAX_SIZE  (33)
#else
#error "sizeof(size_t) is neither 4 nor 8: need GSK_QSORT_STACK_MAX_SIZE def"
#endif

/* Maximum number of elements to sort with insertion sort instead of qsort */
#define GSK_INSERTION_SORT_THRESHOLD	4

struct _GskQsortStackNode
{
  gsize start, len;
};


#define GSK_QSORT(array, type, n_elements, compare)		             \
  GSK_QSORT_FULL(array, type, n_elements, compare,                           \
                 GSK_INSERTION_SORT_THRESHOLD,                               \
                 GSK_QSORT_STACK_MAX_SIZE,                                   \
                 /* no stack guard assertion */)

#define GSK_QSORT_DEBUG(array, type, n_elements, compare)		     \
  GSK_QSORT_FULL(array, type, n_elements, compare,                           \
                 GSK_INSERTION_SORT_THRESHOLD,                               \
                 GSK_QSORT_STACK_MAX_SIZE,                                   \
                 GSK_QSORT_ASSERT_STACK_SIZE)

#define GSK_QSELECT(array, type, n_elements, n_select, compare)		     \
  GSK_QSELECT_FULL(array, type, n_elements, n_select, compare,               \
                   GSK_INSERTION_SORT_THRESHOLD,                             \
                   GSK_QSORT_STACK_MAX_SIZE,                                 \
                   /* no stack guard assertion */)

#define GSK_QSORT_FULL(array, type, n_elements, compare, isort_threshold, stack_size, ss_assertion)    \
  G_STMT_START{                                                              \
    gint gsk_rv;                                                             \
    guint gsk_stack_size;                                                    \
    GskQsortStackNode gsk_stack[stack_size];                                 \
    type gsk_tmp_swap;                                                       \
    GskQsortStackNode gsk_node;                                              \
    gsk_node.start = 0;                                                      \
    gsk_node.len = (n_elements);                                             \
    gsk_stack_size = 0;                                                      \
    if (n_elements <= isort_threshold)                                       \
      GSK_INSERTION_SORT(array,type,n_elements,compare);                     \
    else                                                                     \
      for(;;)                                                                \
        {                                                                    \
          GskQsortStackNode gsk_stack_nodes[2];                              \
          /* implement median-of-three; sort so that  */                     \
          /* *gsk_a <= *gsk_b <= *gsk_c               */                     \
          type *gsk_lo = array + gsk_node.start;                             \
          type *gsk_hi = gsk_lo + gsk_node.len - 1;                          \
          type *gsk_a = gsk_lo;                                              \
          type *gsk_b = array + gsk_node.start + gsk_node.len / 2;           \
          type *gsk_c = gsk_hi;                                              \
          compare((*gsk_a), (*gsk_b), gsk_rv);                               \
          if (gsk_rv < 0)                                                    \
            {                                                                \
              compare((*gsk_b), (*gsk_c), gsk_rv);                           \
              if (gsk_rv <= 0)                                               \
                {                                                            \
                  /* a <= b <= c: already sorted */                          \
                }                                                            \
              else                                                           \
                {                                                            \
                  compare((*gsk_a), (*gsk_c), gsk_rv);                       \
                  if (gsk_rv <= 0)                                           \
                    {                                                        \
                      /* a <= c <= b */                                      \
                      gsk_tmp_swap = *gsk_b;                                 \
                      *gsk_b = *gsk_c;                                       \
                      *gsk_c = gsk_tmp_swap;                                 \
                    }                                                        \
                  else                                                       \
                    {                                                        \
                      /* c <= a <= b */                                      \
                      gsk_tmp_swap = *gsk_a;                                 \
                      *gsk_a = *gsk_c;                                       \
                      *gsk_c = *gsk_b;                                       \
                      *gsk_b = gsk_tmp_swap;                                 \
                    }                                                        \
                }                                                            \
            }                                                                \
          else                                                               \
            {                                                                \
              /* *b < *a */                                                  \
              compare((*gsk_b), (*gsk_c), gsk_rv);                           \
              if (gsk_rv >= 0)                                               \
                {                                                            \
                  /* *c <= *b < *a */                                        \
                  gsk_tmp_swap = *gsk_c;                                     \
                  *gsk_c = *gsk_a;                                           \
                  *gsk_a = gsk_tmp_swap;                                     \
                }                                                            \
              else                                                           \
                {                                                            \
                  /* b<a, b<c */                                             \
                  compare((*gsk_a), (*gsk_c), gsk_rv);                       \
                  if (gsk_rv >= 0)                                           \
                    {                                                        \
                      /* b < c <= a */                                       \
                      gsk_tmp_swap = *gsk_a;                                 \
                      *gsk_a = *gsk_b;                                       \
                      *gsk_b = *gsk_c;                                       \
                      *gsk_c = gsk_tmp_swap;                                 \
                    }                                                        \
                  else                                                       \
                    {                                                        \
                      /* b < a < c */                                        \
                      gsk_tmp_swap = *gsk_a;                                 \
                      *gsk_a = *gsk_b;                                       \
                      *gsk_b = gsk_tmp_swap;                                 \
                    }                                                        \
                }                                                            \
            }                                                                \
                                                                             \
          /* ok, phew, now *gsk_a <= *gsk_b <= *gsk_c */                     \
                                                                             \
          /* partition this range of the array */                            \
          gsk_a++;                                                           \
          gsk_c--;                                                           \
          do                                                                 \
            {                                                                \
              /* advance gsk_a to a element that violates */                 \
              /* partitioning (or it hits gsk_b) */                          \
              for (;;)                                                       \
                {                                                            \
                  compare((*gsk_a), (*gsk_b), gsk_rv);                       \
                  if (gsk_rv >= 0)                                           \
                    break;                                                   \
                  gsk_a++;                                                   \
                }                                                            \
              /* advance gsk_c to a element that violates */                 \
              /* partitioning (or it hits gsk_b) */                          \
              for (;;)                                                       \
                {                                                            \
                  compare((*gsk_b), (*gsk_c), gsk_rv);                       \
                  if (gsk_rv >= 0)                                           \
                    break;                                                   \
                  gsk_c--;                                                   \
                }                                                            \
              if (gsk_a < gsk_c)                                             \
                {                                                            \
                  gsk_tmp_swap = *gsk_a;                                     \
                  *gsk_a = *gsk_c;                                           \
                  *gsk_c = gsk_tmp_swap;                                     \
                  if (gsk_a == gsk_b)                                        \
                    gsk_b = gsk_c;                                           \
                  else if (gsk_b == gsk_c)                                   \
                    gsk_b = gsk_a;                                           \
                  gsk_a++;                                                   \
                  gsk_c--;                                                   \
                }                                                            \
              else if (gsk_a == gsk_c)                                       \
                {                                                            \
                  gsk_a++;                                                   \
                  gsk_c--;                                                   \
                  break;                                                     \
                }                                                            \
            }                                                                \
          while (gsk_a <= gsk_c);                                            \
                                                                             \
          /* the two partitions are [lo,c] and [a,hi], */                    \
          /* which are disjoint since (a > b) by the above loop guard */     \
                                                                             \
          /*{type*gsk_tmp2; for (gsk_tmp2=gsk_lo;gsk_tmp2<=gsk_c;gsk_tmp2++){ compare(*gsk_tmp2,*gsk_b,gsk_rv); g_assert(gsk_rv<=0); }}*/ \
          /*{type*gsk_tmp2; for (gsk_tmp2=gsk_a;gsk_tmp2<=gsk_hi;gsk_tmp2++){ compare(*gsk_tmp2,*gsk_b,gsk_rv); g_assert(gsk_rv>=0); }}*/ \
          /*{type*gsk_tmp2; for (gsk_tmp2=gsk_c+1;gsk_tmp2<gsk_a;gsk_tmp2++){ compare(*gsk_tmp2,*gsk_b,gsk_rv); g_assert(gsk_rv==0); }}*/ \
                                                                             \
          /* push parts onto stack:  the bigger half must be pushed    */    \
          /* on first to guarantee that the max stack depth is O(log N) */   \
          gsk_stack_nodes[0].start = gsk_node.start;                         \
          gsk_stack_nodes[0].len = gsk_c - gsk_lo + 1;                       \
          gsk_stack_nodes[1].start = gsk_a - (array);                        \
          gsk_stack_nodes[1].len = gsk_hi - gsk_a + 1;                       \
          if (gsk_stack_nodes[0].len < gsk_stack_nodes[1].len)               \
            {                                                                \
              GskQsortStackNode gsk_stack_node_tmp = gsk_stack_nodes[0];     \
              gsk_stack_nodes[0] = gsk_stack_nodes[1];                       \
              gsk_stack_nodes[1] = gsk_stack_node_tmp;                       \
            }                                                                \
          if (gsk_stack_nodes[0].len > isort_threshold)                      \
            {                                                                \
              if (gsk_stack_nodes[1].len > isort_threshold)                  \
                {                                                            \
                  gsk_stack[gsk_stack_size++] = gsk_stack_nodes[0];          \
                  gsk_node = gsk_stack_nodes[1];                             \
                }                                                            \
              else                                                           \
                {                                                            \
                  GSK_INSERTION_SORT ((array) + gsk_stack_nodes[1].start,    \
                                      type, gsk_stack_nodes[1].len, compare);\
                  gsk_node = gsk_stack_nodes[0];                             \
                }                                                            \
            }                                                                \
          else                                                               \
            {                                                                \
              GSK_INSERTION_SORT ((array) + gsk_stack_nodes[0].start,        \
                                  type, gsk_stack_nodes[0].len, compare);    \
              GSK_INSERTION_SORT ((array) + gsk_stack_nodes[1].start,        \
                                  type, gsk_stack_nodes[1].len, compare);    \
              if (gsk_stack_size == 0)                                       \
                break;                                                       \
              gsk_node = gsk_stack[--gsk_stack_size];                        \
            }                                                                \
          ss_assertion;                                                      \
        }                                                                    \
  }G_STMT_END

/* TODO: do we want GSK_INSERTION_SELECT for use here internally? */
#define GSK_QSELECT_FULL(array, type, n_elements, n_select, compare, isort_threshold, stack_size, ss_assertion)    \
  G_STMT_START{                                                              \
    gint gsk_rv;                                                             \
    guint gsk_stack_size;                                                    \
    GskQsortStackNode gsk_stack[stack_size];                                 \
    type gsk_tmp_swap;                                                       \
    GskQsortStackNode gsk_node;                                              \
    gsk_node.start = 0;                                                      \
    gsk_node.len = (n_elements);                                             \
    gsk_stack_size = 0;                                                      \
    if (n_elements <= isort_threshold)                                       \
      GSK_INSERTION_SORT(array,type,n_elements,compare);                     \
    else                                                                     \
      for(;;)                                                                \
        {                                                                    \
          GskQsortStackNode gsk_stack_nodes[2];                              \
          /* implement median-of-three; sort so that  */                     \
          /* *gsk_a <= *gsk_b <= *gsk_c               */                     \
          type *gsk_lo = array + gsk_node.start;                             \
          type *gsk_hi = gsk_lo + gsk_node.len - 1;                          \
          type *gsk_a = gsk_lo;                                              \
          type *gsk_b = array + gsk_node.start + gsk_node.len / 2;           \
          type *gsk_c = gsk_hi;                                              \
          compare((*gsk_a), (*gsk_b), gsk_rv);                               \
          if (gsk_rv < 0)                                                    \
            {                                                                \
              compare((*gsk_b), (*gsk_c), gsk_rv);                           \
              if (gsk_rv <= 0)                                               \
                {                                                            \
                  /* a <= b <= c: already sorted */                          \
                }                                                            \
              else                                                           \
                {                                                            \
                  compare((*gsk_a), (*gsk_c), gsk_rv);                       \
                  if (gsk_rv <= 0)                                           \
                    {                                                        \
                      /* a <= c <= b */                                      \
                      gsk_tmp_swap = *gsk_b;                                 \
                      *gsk_b = *gsk_c;                                       \
                      *gsk_c = gsk_tmp_swap;                                 \
                    }                                                        \
                  else                                                       \
                    {                                                        \
                      /* c <= a <= b */                                      \
                      gsk_tmp_swap = *gsk_a;                                 \
                      *gsk_a = *gsk_c;                                       \
                      *gsk_c = *gsk_b;                                       \
                      *gsk_b = gsk_tmp_swap;                                 \
                    }                                                        \
                }                                                            \
            }                                                                \
          else                                                               \
            {                                                                \
              /* *b < *a */                                                  \
              compare((*gsk_b), (*gsk_c), gsk_rv);                           \
              if (gsk_rv >= 0)                                               \
                {                                                            \
                  /* *c <= *b < *a */                                        \
                  gsk_tmp_swap = *gsk_c;                                     \
                  *gsk_c = *gsk_a;                                           \
                  *gsk_a = gsk_tmp_swap;                                     \
                }                                                            \
              else                                                           \
                {                                                            \
                  /* b<a, b<c */                                             \
                  compare((*gsk_a), (*gsk_c), gsk_rv);                       \
                  if (gsk_rv >= 0)                                           \
                    {                                                        \
                      /* b < c <= a */                                       \
                      gsk_tmp_swap = *gsk_a;                                 \
                      *gsk_a = *gsk_b;                                       \
                      *gsk_b = *gsk_c;                                       \
                      *gsk_c = gsk_tmp_swap;                                 \
                    }                                                        \
                  else                                                       \
                    {                                                        \
                      /* b < a < c */                                        \
                      gsk_tmp_swap = *gsk_a;                                 \
                      *gsk_a = *gsk_b;                                       \
                      *gsk_b = gsk_tmp_swap;                                 \
                    }                                                        \
                }                                                            \
            }                                                                \
                                                                             \
          /* ok, phew, now *gsk_a <= *gsk_b <= *gsk_c */                     \
                                                                             \
          /* partition this range of the array */                            \
          gsk_a++;                                                           \
          gsk_c--;                                                           \
          do                                                                 \
            {                                                                \
              /* advance gsk_a to a element that violates */                 \
              /* partitioning (or it hits gsk_b) */                          \
              for (;;)                                                       \
                {                                                            \
                  compare((*gsk_a), (*gsk_b), gsk_rv);                       \
                  if (gsk_rv >= 0)                                           \
                    break;                                                   \
                  gsk_a++;                                                   \
                }                                                            \
              /* advance gsk_c to a element that violates */                 \
              /* partitioning (or it hits gsk_b) */                          \
              for (;;)                                                       \
                {                                                            \
                  compare((*gsk_b), (*gsk_c), gsk_rv);                       \
                  if (gsk_rv >= 0)                                           \
                    break;                                                   \
                  gsk_c--;                                                   \
                }                                                            \
              if (gsk_a < gsk_c)                                             \
                {                                                            \
                  gsk_tmp_swap = *gsk_a;                                     \
                  *gsk_a = *gsk_c;                                           \
                  *gsk_c = gsk_tmp_swap;                                     \
                  if (gsk_a == gsk_b)                                        \
                    gsk_b = gsk_c;                                           \
                  else if (gsk_b == gsk_c)                                   \
                    gsk_b = gsk_a;                                           \
                  gsk_a++;                                                   \
                  gsk_c--;                                                   \
                }                                                            \
              else if (gsk_a == gsk_c)                                       \
                {                                                            \
                  gsk_a++;                                                   \
                  gsk_c--;                                                   \
                  break;                                                     \
                }                                                            \
            }                                                                \
          while (gsk_a <= gsk_c);                                            \
                                                                             \
          /* the two partitions are [lo,c] and [a,hi], */                    \
          /* which are disjoint since (a > b) by the above loop guard */     \
                                                                             \
          /* push parts onto stack:  the bigger half must be pushed    */    \
          /* on first to guarantee that the max stack depth is O(log N) */   \
          gsk_stack_nodes[0].start = gsk_node.start;                         \
          gsk_stack_nodes[0].len = gsk_c - gsk_lo + 1;                       \
          gsk_stack_nodes[1].start = gsk_a - (array);                        \
          gsk_stack_nodes[1].len = gsk_hi - gsk_a + 1;                       \
          if (gsk_stack_nodes[1].start >= n_select)                          \
            {                                                                \
              if (gsk_stack_nodes[0].len > isort_threshold)                  \
                {                                                            \
                  gsk_node = gsk_stack_nodes[0];                             \
                }                                                            \
              else                                                           \
                {                                                            \
                  GSK_INSERTION_SORT ((array) + gsk_stack_nodes[0].start,    \
                                      type, gsk_stack_nodes[0].len, compare);\
                  if (gsk_stack_size == 0)                                   \
                    break;                                                   \
                  gsk_node = gsk_stack[--gsk_stack_size];                    \
                }                                                            \
            }                                                                \
          else                                                               \
            {                                                                \
              if (gsk_stack_nodes[0].len < gsk_stack_nodes[1].len)           \
                {                                                            \
                  GskQsortStackNode gsk_stack_node_tmp = gsk_stack_nodes[0]; \
                  gsk_stack_nodes[0] = gsk_stack_nodes[1];                   \
                  gsk_stack_nodes[1] = gsk_stack_node_tmp;                   \
                }                                                            \
              if (gsk_stack_nodes[0].len > isort_threshold)                  \
                {                                                            \
                  if (gsk_stack_nodes[1].len > isort_threshold)              \
                    {                                                        \
                      gsk_stack[gsk_stack_size++] = gsk_stack_nodes[0];      \
                      gsk_node = gsk_stack_nodes[1];                         \
                      ss_assertion;                                          \
                    }                                                        \
                  else                                                       \
                    {                                                        \
                      GSK_INSERTION_SORT ((array) + gsk_stack_nodes[1].start,\
                                          type, gsk_stack_nodes[1].len, compare);\
                      gsk_node = gsk_stack_nodes[0];                         \
                    }                                                        \
                }                                                            \
              else                                                           \
                {                                                            \
                  GSK_INSERTION_SORT ((array) + gsk_stack_nodes[0].start,    \
                                      type, gsk_stack_nodes[0].len, compare);\
                  GSK_INSERTION_SORT ((array) + gsk_stack_nodes[1].start,    \
                                  type, gsk_stack_nodes[1].len, compare);    \
                  if (gsk_stack_size == 0)                                   \
                    break;                                                   \
                  gsk_node = gsk_stack[--gsk_stack_size];                    \
                }                                                            \
            }                                                                \
        }                                                                    \
  }G_STMT_END


/* note: do not allow equality, since that would make the next push a
   stack overflow, and we might not detect it correctly to stack corruption. */
#define GSK_QSORT_ASSERT_STACK_SIZE(stack_alloced)                          \
  g_assert(gsk_stack_size < stack_alloced)

#define GSK_INSERTION_SORT(array, type, length, compare)                     \
  G_STMT_START{                                                              \
    guint gsk_ins_i, gsk_ins_j;                                              \
    type gsk_ins_tmp;                                                        \
    for (gsk_ins_i = 1; gsk_ins_i < length; gsk_ins_i++)                     \
      {                                                                      \
        /* move (gsk_ins_i-1) into place */                                  \
        guint gsk_ins_min = gsk_ins_i - 1;                                   \
        gint gsk_ins_compare_rv;                                             \
        for (gsk_ins_j = gsk_ins_i; gsk_ins_j < length; gsk_ins_j++)         \
          {                                                                  \
            compare(((array)[gsk_ins_min]), ((array)[gsk_ins_j]),            \
                    gsk_ins_compare_rv);                                     \
            if (gsk_ins_compare_rv > 0)                                      \
              gsk_ins_min = gsk_ins_j;                                       \
          }                                                                  \
        /* swap gsk_ins_min and (gsk_ins_i-1) */                             \
        gsk_ins_tmp = (array)[gsk_ins_min];                                  \
        (array)[gsk_ins_min] = (array)[gsk_ins_i - 1];                       \
        (array)[gsk_ins_i - 1] = gsk_ins_tmp;                                \
      }                                                                      \
  }G_STMT_END

#define GSK_QSORT_SIMPLE_COMPARATOR(a,b,compare_rv)                          \
  G_STMT_START{                                                              \
    if ((a) < (b))                                                           \
      compare_rv = -1;                                                       \
    else if ((a) > (b))                                                      \
      compare_rv = 1;                                                        \
    else                                                                     \
      compare_rv = 0;                                                        \
  }G_STMT_END
