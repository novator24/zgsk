#ifndef __GSK_STDIO_H_
#define __GSK_STDIO_H_

/* stdio helper functions */
#include <stdio.h>
#include <glib.h>

G_BEGIN_DECLS

gchar * gsk_stdio_readline (FILE *fp);

G_END_DECLS

#endif
