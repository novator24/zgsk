#ifndef __GSK_G_HELPER_H_
#define __GSK_G_HELPER_H_

#include <glib.h>

G_BEGIN_DECLS

void gsk_g_ptr_array_foreach (GPtrArray *array,
			      GFunc      func,
			      gpointer   data);

void gsk_g_error_add_prefix  (GError   **error,
                              const char *format,
                              ...) G_GNUC_PRINTF(2,3);


gpointer gsk_g_tree_min (GTree *tree);
gpointer gsk_g_tree_max (GTree *tree);

GSList *gsk_g_tree_key_slist (GTree *tree);
GSList *gsk_g_tree_value_slist (GTree *tree);
GSList *gsk_g_hash_table_key_slist (GHashTable *table);
GSList *gsk_g_hash_table_value_slist (GHashTable *table);

/* MOVE TO `gskstr.h' ??? */
/* semi-portable 64-bit int parsing functions */
gint64  gsk_strtoll  (const char *str,
		      char      **endp,
		      int         base);
guint64  gsk_strtoull(const char *str,
		      char      **endp,
		      int         base);

guint gsk_strnlen (const char *ptr, guint max_len);

/* MOVE ELSEWHERE ??? */
gboolean gsk_fd_set_nonblocking (int fd);
gboolean gsk_fd_clear_nonblocking (int fd);
gboolean gsk_fd_is_nonblocking (int fd);
/* NOTE: gsk_fd_finish_connecting() is gsk_socket_address_finish_fd(). */

#ifndef GSK_DISABLE_DEPRECATED
/* gsk_g_debug: write debug output */
#define gsk_g_debug g_debug
#endif

G_END_DECLS

#endif
