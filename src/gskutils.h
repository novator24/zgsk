#ifndef __GSK_UTILS_H_
#define __GSK_UTILS_H_

#include <glib.h>

G_BEGIN_DECLS

/* make DIR and all necessary parent directories.
   if DIR exists, return TRUE.

   generally returns FALSE only if there's a file
   in the way, or permission problems. */
gboolean gsk_mkdir_p (const char *dir,
                      guint       permissions,
		      GError    **error);

gboolean gsk_rm_rf   (const char *dir_or_file,
                      GError    **error);

/* returns -1 on failure. */
int       gsk_lock_dir   (const char *dir,
                          gboolean    block,
                          GError    **error);
gboolean  gsk_unlock_dir (int         lock_rv,
                          GError    **error);

/* utility function: escape a string like a C string.
 * return value should be freed by caller. */
char *gsk_escape_memory (gconstpointer    data,
		         guint            len);
gpointer gsk_unescape_memory (const char *quoted,
                              gboolean    has_quote_marks,
                              const char**end,
                              guint      *length_out,
                              GError    **error);

char *gsk_escape_memory_hex (gconstpointer    data,
		             guint            len);

guint8 *gsk_unescape_memory_hex (const char  *str,
                                 gssize       len,
                                 gsize       *length_out,
                                 GError     **error);

void  gsk_fd_set_close_on_exec (int fd, gboolean close_on_exec);

/* print the message and a newline to stderr, then exit. */
void gsk_fatal_user_error (const char *format,
                           ...);

/* automatically retrying reads and writes */
gssize gsk_readn (guint fd, void *buf, gsize len);
gssize gsk_writen (guint fd, const void *buf, gsize len);

G_END_DECLS

#endif
