#ifndef __DIST_WHOIS_H_
#define __DIST_WHOIS_H_

G_BEGIN_DECLS

typedef struct _DistWhoisDB DistWhoisDB;

DistWhoisDB*   dist_whois_db_new             (const char* pending_file,
                                              const char* queue_file,
				              const char* queue_position_file);


/* Moves the entry to "pending". */
char*          dist_whois_db_dequeue(DistWhoisDB* db);

/* Writes out a pending entry.
 * We take responsibility for the strings. */
void           dist_whois_db_resolve_dequeued(DistWhoisDB* db,
                                              char*        name,
				              char*        entry);
/* Indicate we got a definite response that the entry doesn't exist.
 * We take responsibility for the strings. */
void           dist_whois_db_resolve_bad     (DistWhoisDB* db,
                                              char*        name);
/* Indicate that our attempt to resolve the entry failed,
 * but were inconclusive. 
 *
 * We take responsibility for the strings. */
void           dist_whois_db_resolve_failed  (DistWhoisDB* db,
                                              char*        name);

G_END_DECLS

#endif
