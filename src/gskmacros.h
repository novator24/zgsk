

/* TODO: these need to actually pool at some point ! */
#define GSK_DECLARE_POOL_ALLOCATORS(Type, type, pool_count)	\
static inline Type *						\
type ## _alloc ()						\
{								\
  return g_new (Type, 1);					\
}								\
								\
static inline void						\
type ## _free (Type *p)						\
{								\
  g_free (p);							\
}

#define GSK_STRUCT_MEMBER_SIZE(type, member)    (sizeof(((type *)0)->member))
#define GSK_STRUCT_MEMBER_END_OFFSET(struct, member)			\
  (GSK_STRUCT_MEMBER_SIZE (struct, member) + G_STRUCT_OFFSET (struct, member))
#define GSK_STRUCT_IS_LAST_MEMBER(struct, member)			\
   (GSK_STRUCT_MEMBER_END_OFFSET(struct, member) == sizeof(struct))

/* TODO: figure out what we're *supposed to do... */
#ifndef _
#define _(s) s
#endif


#define GSK_SKIP_CHAR_TYPE(ptr, condition)			\
       G_STMT_START{						\
	 while (*(ptr) && condition (* (ptr)))			\
	   (ptr)++;						\
       }G_STMT_END

#ifdef G_HAVE_ISO_VARARGS
#define GSK_DO_NOTHING(...)
#elif defined(G_HAVE_GNUC_VARARGS)
#define GSK_DO_NOTHING(whatever...)
#else
static inline void GSK_DO_NOTHING(void *arg0, ...) { }
#endif

/* not utf8 friendly... */
#define GSK_SKIP_WHITESPACE(char_ptr)		GSK_SKIP_CHAR_TYPE(char_ptr, isspace)
#define GSK_SKIP_NONWHITESPACE(char_ptr)	GSK_SKIP_CHAR_TYPE(char_ptr, !isspace)

