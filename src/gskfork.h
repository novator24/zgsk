/* another interface to the fork(2) system call.
 *
 * the point is:
 *    - provide a way to make sure all outstanding fd's get freed.
 *    - provide a less weird interface to fork, by some standards ;)
 *
 * this interface is optional if you plan using 'exec(2)';
 * if you really don't want to use it for non-execing uses, at least
 * call gsk_fork_child_cleanup() from the child process.
 */
#ifndef __GSK_FORK_H_
#define __GSK_FORK_H_

#include <glib.h>

G_BEGIN_DECLS

typedef int (*GskForkFunc) (gpointer data);

gint gsk_fork (GskForkFunc action,
	       gpointer    data,
	       GError    **error);

void gsk_fork_add_cleanup_fd    (int fd);
void gsk_fork_remove_cleanup_fd (int fd);


void gsk_fork_child_cleanup(void);

G_END_DECLS

#endif
