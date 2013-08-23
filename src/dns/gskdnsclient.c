#define G_LOG_DOMAIN    "Gsk-Dns"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "gskdnsclient.h"
#include "gskdnsresolver.h"
#include "../gskmainloop.h"
#include "../gskmacros.h"
#include "../gskghelpers.h"
#include "../gsknameresolver.h"
#include "../gskdebug.h"

/* Default DNS cache sizes.

   In your program, you can call
     gsk_name_resolver_set_dns_cache_size()
   to change these. */
#define GSK_DNS_MAX_CACHE_BYTES         (128 * 1024)
#define GSK_DNS_MAX_CACHE_RECORDS       (2048)

typedef struct _GskDnsNameServerInfo GskDnsNameServerInfo;
typedef struct _ClientTask ClientTask;
typedef struct _IpPermissionTable IpPermissionTable;

#define DEBUG_IP_PERMISSION_TABLE		0

#if 0		/* is debugging on? */
#define DEBUG	                        g_message
#define DEBUG_PRINT_MESSAGE(mess)	gsk_dns_dump_message_fp(mess, stderr)
#else
#define DEBUG(args...)
#define DEBUG_PRINT_MESSAGE(mess)
#endif

struct _GskDnsClientClass 
{
  GskPacketQueueClass           base_class;
};

struct _GskDnsNameServerInfo
{
  GskSocketAddressIpv4         *address;
  guint                         is_default_ns : 1;
  guint                         num_msg_sent;
  guint                         num_msg_received;
  GskDnsNameServerInfo         *next_ns;
  GskDnsNameServerInfo         *prev_ns;
};

struct _GskDnsClient 
{
  GObject                       base_instance;
  GskPacketQueue               *packet_queue;
  GskDnsRRCache                *rr_cache;
  ClientTask                   *tasks;
  GHashTable                   *id_to_task_list;
  guint16                       last_message_id;
  guint16                       stub_resolver : 1;
  guint16                       is_blocking_write : 1;
  guint16                       recursion_desired : 1;

  guint16                       max_iterations_nonrecursive;
  guint16                       max_iterations_recursive;

  GSList                       *first_outgoing_packet;
  GSList                       *last_outgoing_packet;

  GskMainLoop                  *main_loop;

  /* temporary permissions granted to other nameservers */
  IpPermissionTable            *ip_perm_table;

  /* --- basic configuration --- */
  GskDnsNameServerInfo         *first_ns;
  GskDnsNameServerInfo         *last_ns;
  GPtrArray                    *searchpath_array;
};


struct _ClientTask
{
  GskDnsClient                 *client;

  guint16                       message_id;
  guint16                       ref_count;
  guint                         is_in_client_list : 1;
  guint                         destroyed : 1;
  guint                         failed : 1; /* => we ran on_fail */
  guint                         ran_task_func : 1;
  guint                         cancelled : 1;

  /* See RFC, 5.3.1. */
  guint                         stub_resolver : 1;

  /* Whether we've resorted to trying the configuration
     nameservers yet. */
  guint                         used_conf_nameservers : 1;

  /* Whether to use other nameservers in obtaining an answer. */
  guint                         recursive : 1;

  guint16                       n_iterations;
  guint16                       max_iterations;

  /* GskDnsRRCache to be used for this task.
   * This will never be NULL unless stub_resolver is specified,
   * although GskDnsRRCache may be specific to this query.
   */
  GskDnsRRCache                *rr_cache;

  /* a collection of resource records we've locked from
   * the cache that might be useful.
   */
  GSList                       *locked_records;

  /* The name servers which we know apply to this zone. */
  GskDnsNameServerInfo         *first_ns;
  GskDnsNameServerInfo         *last_ns;

  /* List of questions which need answering */
  GSList                       *questions;

  /* List of questions which have answers in `{ns,a,cname}_records */
  GSList                       *answered_questions;

  /* List of questions which have had explicit negative answers
     (also in answered_questions) */
  GSList                       *negatives;

  GskDnsResolverResponseFunc    func;
  GskDnsResolverFailFunc        on_fail;
  gpointer                      func_data;
  GDestroyNotify                destroy;

  /* Timeout for the query which will timeout first. */
  GskSource                    *timeout_source;

  /* list of all queries pending. */
  ClientTask                   *next;
  ClientTask                   *prev;

  /* list of all queries in this hash-bucket. */
  ClientTask                   *hash_next;
  ClientTask                   *hash_prev;
};



GSK_DECLARE_POOL_ALLOCATORS(GskDnsNameServerInfo, gsk_dns_name_server_info, 16)

/* Attempt to Resolve the given questions -- eventually we unref
 * the task, possibly before returning from this function.
 */
static void try_local_cache_or_proceed (ClientTask *task);

/* Handle a timeout expiration -- just update the tasks state,
 * possibly ending the task.
 */
static gboolean handle_timeout (ClientTask *task);

static void gsk_dns_client_transmit (GskDnsClient         *client,
				     GskSocketAddressIpv4 *address,
				     GskDnsMessage        *message);

/* Fail a single client and remove it from the client list. */
static void client_task_fail        (ClientTask           *task,
		                     GError               *error);

/* --- ip-permission table prototypes --- */
static IpPermissionTable * ip_permission_table_new ();
static void ip_permission_table_insert   (IpPermissionTable *table,
			                  GskSocketAddressIpv4  *address,
			                  gboolean           any_suffixed_domain,
			                  const char        *owner,
			                  guint              expire_time);
static void ip_permission_table_expire   (IpPermissionTable *table,
			                  guint              cur_time);
static gboolean ip_permission_table_check(IpPermissionTable *table,
                                          GskSocketAddressIpv4  *address,
                                          const char        *owner,
                                          guint              cur_time);
static void ip_permission_table_destroy  (IpPermissionTable *table);

/* --- prototypes --- */
static GObjectClass *parent_class = NULL;

/* helper ClientTask methods */
static inline void
maybe_run_destroy (ClientTask *task)
{
  if (!task->destroyed)
    {
      task->destroyed = 1;
      if (task->destroy != NULL)
	(*task->destroy) (task->func_data);
    }
}

static inline void
remove_from_client_list (ClientTask *task)
{
  g_return_if_fail (task->is_in_client_list);
  task->is_in_client_list = 0;
  if (task->next != NULL)
    task->next->prev = task->prev;
  if (task->prev != NULL)
    task->prev->next = task->next;
  else
    task->client->tasks = task->next;
}

static inline void
maybe_remove_from_client_list (ClientTask *task)
{
  if (task->is_in_client_list)
    remove_from_client_list (task);
}

static inline void
maybe_remove_timeout_source (ClientTask *task)
{
  if (task->timeout_source != NULL)
    {
      gsk_source_remove (task->timeout_source);
      task->timeout_source = NULL;
    }
}

/* --- GskDnsResolver interface --- */
static void
gsk_dns_client_task_ref (ClientTask *task)
{
  g_return_if_fail (task->ref_count > 0);
  ++(task->ref_count);
}

static void
gsk_dns_client_task_unref (ClientTask *task)
{
  g_return_if_fail (task->ref_count > 0);
  --(task->ref_count);
  if (task->ref_count == 0)
    {
      /* We should always either:  succeed, fail, or be cancelled. */
      g_return_if_fail (task->cancelled || task->failed || task->ran_task_func);

      maybe_run_destroy (task);
      maybe_remove_from_client_list (task);

      /* Remove from hash-table list. */
      if (task->hash_next != NULL)
	task->hash_next->hash_prev = task->hash_prev;
      if (task->hash_prev != NULL)
	task->hash_prev->hash_next = task->hash_next;
      else
	{
	  guint msg_id = task->message_id;
          if (task->hash_next == NULL)
            g_hash_table_remove (task->client->id_to_task_list,
                                 GUINT_TO_POINTER (msg_id));
          else
            g_hash_table_insert (task->client->id_to_task_list,
                                 GUINT_TO_POINTER (msg_id),
                                 task->hash_next);
	}

      /* remove timeout */
      maybe_remove_timeout_source (task);

      g_slist_foreach (task->answered_questions,
		       (GFunc) gsk_dns_question_free,
		       NULL);
      g_slist_free (task->answered_questions);
      g_slist_foreach (task->questions,
		       (GFunc) gsk_dns_question_free,
		       NULL);
      g_slist_free (task->questions);

      g_slist_free (task->negatives);

      while (task->first_ns != NULL)
	{
	  GskDnsNameServerInfo *ns_info = task->first_ns;
	  task->first_ns = ns_info->next_ns;
	  g_object_unref (ns_info->address);
	  gsk_dns_name_server_info_free (ns_info);
	}
      while (task->locked_records != NULL)
	{
	  GskDnsResourceRecord *rr = task->locked_records->data;
	  task->locked_records = g_slist_remove (task->locked_records, rr);
	  gsk_dns_rr_cache_unlock (task->rr_cache, rr);
	}
      if (task->rr_cache != NULL)
	gsk_dns_rr_cache_unref (task->rr_cache);
      g_free (task);
    }
}

static void
gsk_dns_client_resolver_cancel  (GskDnsResolver    *resolver,
				 gpointer           task_data)
{
  ClientTask *task = task_data;
  GskDnsClient *client = GSK_DNS_CLIENT (resolver);
  g_assert (client == task->client);

  task->cancelled = 1;
  gsk_dns_client_task_unref (task);
}

/* See RFC 1034, section 5.3.3.  */

static int
count_dots (const char *str)
{
  int rv = 0;
  while (*str != 0)
    {
      if (*str == '.')
	rv++;
      str++;
    }
  return rv;
}

static void
find_name_pieces (const char  *str,
		  guint       *n_pieces_out,
		  const char **pieces_out)
{
  const char *at = str;
  *n_pieces_out = 0;
  while (*at != 0)
    {
      pieces_out[*n_pieces_out] = at;
      (*n_pieces_out)++;
      while (*at != '\0' && *at != '.')
	at++;
      while (*at == '.')
	at++;
    }
  pieces_out[*n_pieces_out] = ".";
  (*n_pieces_out)++;
}

static inline guint
gsk_dns_client_generate_message_id (GskDnsClient *client)
{
  return ++(client->last_message_id);
}

static GskDnsMessage *
maybe_make_message (ClientTask    *task,
		    GHashTable            *table,
		    GskDnsNameServerInfo  *ns_info)
{
  GskDnsMessage *message = g_hash_table_lookup (table, ns_info);
  if (message == NULL)
    {
      guint16 msg_id = task->message_id;
      message = gsk_dns_message_new (msg_id, TRUE);
      message->recursion_desired = task->client->recursion_desired;
      g_hash_table_insert (table, ns_info, message);
    }
  return message;
}

static void
add_timeout (ClientTask *task,
	     guint               num_seconds)
{
  task->timeout_source 
    = gsk_main_loop_add_timer (task->client->main_loop,
			       (GskMainLoopTimeoutFunc) handle_timeout,
			       task,
			       NULL,
			       num_seconds * 1000 + 500,
			       -1);
}

/* Restart any queries which have timed out,
 * unless they have timed out too many times,
 * in which case, we fail.
 */
static gboolean
handle_timeout (ClientTask *task)
{
  task->timeout_source = NULL;

  try_local_cache_or_proceed (task);

  return FALSE;
}

/* hmm, this seems pretty inefficient */
static void
gsk_dns_client_task_use_conf_nameservers (ClientTask *task)
{
  GskDnsNameServerInfo *ns_info;
  g_return_if_fail (!task->used_conf_nameservers);
  task->used_conf_nameservers = 1;
  for (ns_info = task->client->first_ns;
       ns_info != NULL;
       ns_info = ns_info->next_ns)
    {
      GskDnsNameServerInfo *new_ns_info = gsk_dns_name_server_info_alloc ();
      new_ns_info->num_msg_received = 0;
      new_ns_info->num_msg_sent = 0;
      new_ns_info->address = g_object_ref (ns_info->address);
      new_ns_info->is_default_ns = 1;
      new_ns_info->prev_ns = task->last_ns;
      new_ns_info->next_ns = NULL;
      if (task->last_ns != NULL)
	task->last_ns->next_ns = new_ns_info;
      else
	task->first_ns = new_ns_info;
      task->last_ns = new_ns_info;
    }
}

/* --- translate uncategorized ResourceRecords
       into the usual lists, according to the questions
       in `task': answers, authority, additional.  --- */
typedef struct _MessageLists MessageLists;
struct _MessageLists
{
  GSList *answers;
  GSList *auth;
  GSList *additional;
  ClientTask *task;
};

/* Append a GskDnsResourceRecord to whichever return list
 * looks best (using additional as a catchall.)
 */
static void
categorize_rr (GskDnsResourceRecord *record,
	       MessageLists         *lists)
{
  ClientTask *task = lists->task;
  GSList *answered;

  /* Look through the questions to figure if
     this is a real answer (answers), an indirectly related
     nameserver (auth), or another datum (additional). */
  for (answered = task->answered_questions;
       answered != NULL;
       answered = answered->next)
    {
      GskDnsQuestion *question = answered->data;
      if (strcasecmp (question->query_name, record->owner) == 0
       && (record->type == question->query_type
	|| record->type == GSK_DNS_RR_CANONICAL_NAME
	|| question->query_type == GSK_DNS_RR_WILDCARD))
	 break;
    }
  if (answered != NULL)
    lists->answers = g_slist_prepend (lists->answers, record);
  else if (record->type == GSK_DNS_RR_NAME_SERVER)
    lists->auth = g_slist_prepend (lists->auth, record);
  else
    lists->additional = g_slist_prepend (lists->additional, record);
}

/* XXX: move this comment to dnsinterfaces.h
 *
 * Call this function once you've gotten all the
 * ResourceRecords you're going to get.
 *
 * Note that the function may sometimes
 * be run without any of the questions answered,
 * just partial data.  The `result_func' passed
 * to gsk_dns_resolver_resolve must discern
 * these cases.
 */
static void
gsk_dns_client_task_succeed (ClientTask *task)
{
  MessageLists message_lists = {NULL, NULL, NULL, NULL};
  message_lists.task = task;

  g_return_if_fail (!task->ran_task_func);

  maybe_remove_timeout_source (task);

  g_slist_foreach (task->locked_records, 
		   (GFunc) categorize_rr,
		   &message_lists);

  task->ran_task_func = 1;
  (*task->func) (message_lists.answers,
		 message_lists.auth,
		 message_lists.additional,
		 task->negatives,
		 task->func_data);

  g_slist_free (message_lists.answers);
  g_slist_free (message_lists.auth);
  g_slist_free (message_lists.additional);
}

static void
gsk_dns_client_task_fail (ClientTask *task,
			  GError             *error)
{
  g_return_if_fail (!task->failed && !task->ran_task_func && !task->destroyed);
  task->failed = 1;
  maybe_remove_timeout_source (task);
  if (task->on_fail != NULL)
    (*task->on_fail) (error, task->func_data);
  g_error_free (error);
}

static void
move_ns_to_end_of_list (ClientTask   *task,
			GskDnsNameServerInfo *name_server)
{
  /* If we are the last element, nothing to do. */
  if (name_server->next_ns == NULL)
    return;

  /* Excise. */
  if (name_server->prev_ns != NULL)
    name_server->prev_ns->next_ns = name_server->next_ns;
  else
    task->first_ns = name_server->next_ns;
  name_server->next_ns->prev_ns = name_server->prev_ns;

  /* If this were true, then name_server would have been
   * the last element originally, and we'd have short-circuited out.
   */
  g_assert (task->first_ns != NULL);

  /* Reinsert. */
  name_server->prev_ns = task->last_ns;
  name_server->next_ns = NULL;
  task->last_ns->next_ns = name_server;
  task->last_ns = name_server;
}

/* Get the number of seconds to sleep based on the
 * which packet this is in the task's requests to that NS (0,1,2,etc)
 */
static inline guint
get_expire_from_attempt_index (GskDnsNameServerInfo *ns_info)
{
  return (1 << MIN (ns_info->num_msg_sent, 6)) + 3;
}

#define MAX_TIMEOUT 90

typedef struct _DnsQueryData DnsQueryData;
struct _DnsQueryData
{
  gboolean has_expire_time;
  guint next_expire_dist;
  ClientTask *task;
};

static void
do_dns_query (GskDnsNameServerInfo *name_server,
	      GskDnsMessage        *message,
	      DnsQueryData         *query_data)
{
  ClientTask *task = query_data->task;
  if (task->failed)
    return;

  /* Handle default name servers. */
  if (name_server == NULL)
    {
      if (!task->used_conf_nameservers)
	gsk_dns_client_task_use_conf_nameservers (task);

      for (name_server = task->first_ns;
	   name_server != NULL;
	   name_server = name_server->next_ns)
	{
	  if (name_server->is_default_ns)
	    break;
	}
      if (name_server == NULL)
	{
	  if (!task->failed)
	    gsk_dns_client_task_fail (task,
		           g_error_new (GSK_G_ERROR_DOMAIN,
					GSK_ERROR_RESOLVER_NO_NAME_SERVERS,
				_("resolving name: no default name server")));
	  return;
	}

      move_ns_to_end_of_list (task, name_server);
    }

  {
    guint expire_dist = get_expire_from_attempt_index (name_server);
    if (!query_data->has_expire_time
      || expire_dist < query_data->next_expire_dist)
      query_data->next_expire_dist = expire_dist;
    query_data->has_expire_time = TRUE;
  }

  /* query `name_server' */
  name_server->num_msg_sent++;
  gsk_dns_client_transmit (task->client,
			   name_server->address,
			   message);

  /* XXX: what the fuck is this supposed to do? */
#if 0
  if (!was_default)
    {
      GSList *question_list;
      gulong cur_time;
      GskMainLoop *main_loop = task->client->main_loop;

      cur_time = main_loop->current_time.tv_sec;

      for (question_list = message->questions;
	   question_list != NULL;
	   question_list = question_list->next)
	{
	  GskSocketAddress addr;
	  addr.address_family = GSK_SOCKET_ADDRESS_IPv4;
	  addr.ipv4.ip_port = GSK_DNS_PORT;
	  memcpy (addr.ipv4.ip_address, name_server->ip_address, 4);
	}
    }
#endif
  gsk_dns_message_unref (message);
}

/* Return a (possibly newly allocated) Nameserver
 * based on a socket-address.
 */
static GskDnsNameServerInfo *
get_nameserver (ClientTask    *task,
		GskSocketAddressIpv4  *address)
{
  GskDnsNameServerInfo *rv;
  const guint8 *ip;
  ip = address->ip_address;
  for (rv = task->first_ns; rv != NULL; rv = rv->next_ns)
    if (gsk_socket_address_equals (address, rv->address))
      return rv;
  rv = gsk_dns_name_server_info_alloc ();
  rv->num_msg_sent = rv->num_msg_received = 0;
  rv->address = g_object_ref (address);
  rv->next_ns = task->first_ns;
  rv->prev_ns = NULL;
  rv->is_default_ns = 0;
  if (task->first_ns != NULL)
    task->first_ns->prev_ns = rv;
  else
    task->last_ns = rv;
  task->first_ns = rv;
  return rv;
}

static void
try_local_cache_or_proceed (ClientTask  *task)
{
  /* An map from a GskDnsNameServerInfo for this request
   * to a dns-message;  if GskDnsNameServerInfo==NULL,
   * the request should be sent to the nameservers
   * from the config file.
   */
  GHashTable *ns_to_dns_message = NULL;

  GSList *questions = task->questions;
  GskDnsRRCache *rr_cache = task->rr_cache;
  
  GSList *last = NULL;
  GskMainLoop *main_loop = task->client->main_loop;
  gulong cur_time;

  if (main_loop == NULL)
    {
      GTimeVal tv;
      g_get_current_time (&tv);
      cur_time = tv.tv_sec;
    }
  else
    cur_time = main_loop->current_time.tv_sec;

  g_assert (!task->failed);
  /* If the query requires a cache for interim results,
   * make a minimal cache for just this query.
   */
  if (rr_cache == NULL && !task->stub_resolver)
    rr_cache = task->rr_cache = gsk_dns_rr_cache_new (0, 0);

  while (questions != NULL)
    {
      GskDnsResourceRecord *record;
      GSList *results;
      GskDnsQuestion *question = questions->data;
      const char *name = question->query_name;
      GskDnsResourceClass query_class = question->query_class;
      GskDnsResourceRecordType query_type = question->query_type;
      GSList *cnames = NULL;

      guint n_dots;
      guint n_pieces;
      /* Hopefully this is safe--RFC 1034, sec. 3.1: "To simplify
       * implementations, the total number of octets that represent a
       * domain name (i.e., the sum of all label octets and label lengths)
       * is limited to 255."
       */
      const char *pieces[255];

      guint found_ns_index;
      GskSocketAddressIpv4 *address;

restart_with_local_cache:
      results = NULL;
      record = NULL;

      if (rr_cache != NULL)
	{
	  if (query_type == GSK_DNS_RR_WILDCARD)
	    results = gsk_dns_rr_cache_lookup_list (rr_cache,
						    name,
						    query_type,
						    query_class);
	  else
	    record = gsk_dns_rr_cache_lookup_one (rr_cache,
						  name,
						  query_type,
						  query_class,
                                                  0);
	  if (results != NULL || record != NULL)
	    {
	      /* Move question to the answered_questions pile,
	       * and add the necessary data to the answers list.
	       */
	      if (last == NULL)
		task->questions = questions->next;
	      else
		last->next = questions->next;
	      questions->next = task->answered_questions;
	      task->answered_questions = questions;
	      questions = last ? last->next : task->questions;

	      if (results != NULL)
		{
		  GSList *at;
		  for (at = results; at != NULL; at = at->next)
		    gsk_dns_rr_cache_lock (rr_cache, at->data);
		  task->locked_records = g_slist_concat (task->locked_records,
							  results);
		}
	      if (record != NULL)
		{
		  gsk_dns_rr_cache_lock (rr_cache, record);
		  task->locked_records = g_slist_prepend (task->locked_records,
							  record);
		}
              g_slist_free (cnames);
	      continue;
	    }

	  if (gsk_dns_rr_cache_is_negative (rr_cache, 
					    name,
					    query_type,
					    query_class))
	    {
	      /* Move question to the answered_questions pile,
	       * and add the necessary data to the answers list.
	       */
	      if (last == NULL)
		task->questions = questions->next;
	      else
		last->next = questions->next;
	      questions->next = task->answered_questions;
	      task->answered_questions = questions;
	      questions = last ? last->next : task->questions;

	      task->negatives = g_slist_prepend (task->negatives, question);

              g_slist_free (cnames);
	      continue;
	    }

	  /* ok, what if we can get a CNAME response? */
	  if (query_type != GSK_DNS_RR_CANONICAL_NAME
	   && query_type != GSK_DNS_RR_WILDCARD)
	    {
	      record = gsk_dns_rr_cache_lookup_one (rr_cache,
						    name,
						    GSK_DNS_RR_CANONICAL_NAME,
						    query_class,
                                                    0);
	      if (record != NULL)
		{
                  if (g_ascii_strcasecmp (record->rdata.domain_name, name) == 0
                   || g_slist_find_custom (cnames, name, (GCompareFunc) g_ascii_strcasecmp) != NULL)
                   {
	             gsk_dns_client_task_fail (task,
			            g_error_new (GSK_G_ERROR_DOMAIN,
				                 GSK_ERROR_RESOLVER_NO_NAME_SERVERS,
			                 _("circular reference in CNAMEs for %s"), name));
                     g_slist_free (cnames);
                     gsk_dns_client_task_unref (task);
	             return;
                   }
		  /* start the query over again, with the CNAME,
		   * adding record back to the response list.
		   */
		  gsk_dns_rr_cache_lock (rr_cache, record);
		  task->locked_records = g_slist_prepend (task->locked_records,
							  record);
                  cnames = g_slist_prepend (cnames, (gpointer) name);
		  name = record->rdata.domain_name;
		  goto restart_with_local_cache;
		}
	    }
	}

      if (task->n_iterations >= task->max_iterations)
	{
	  gsk_dns_client_task_fail (task,
			 g_error_new (GSK_G_ERROR_DOMAIN,
				      GSK_ERROR_RESOLVER_NO_NAME_SERVERS,
			      _("task timed out after %u retries"), task->n_iterations));
          g_slist_free (cnames);
          gsk_dns_client_task_unref (task);
	  return;
	}

      /* Ok, we're going to have to make a remote query.

	 Figure out who to ask. */

      /* non-recursive nameservers always know what nameserver to ask already. */
      if (!task->recursive)
        {
          g_slist_free (cnames);
	  continue;
        }

      /* Initialize the table to nameservers to query. */
      if (ns_to_dns_message == NULL)
	ns_to_dns_message = g_hash_table_new (NULL, NULL);

      /* Implement a stub resolver.  See RFC 1034, 5.3.3. */
      if (task->stub_resolver)
	{
	  GskDnsMessage *message;
	  GskDnsQuestion *q;
	  message = maybe_make_message (task, ns_to_dns_message, NULL);
	  message->recursion_desired = 1;
	  q = gsk_dns_question_copy (question, message);
	  gsk_dns_message_append_question (message, q);

	  last = questions;
	  questions = questions->next;
          g_slist_free (cnames);
	  continue;
	}

      /*
       * Find nameservers to ask for this information.
       *
       * If a nameserver is not contactable (we don't have an address for it)
       * begin queries to find those addresses.
       *
       * If we are unable to find any address at all,
       * to asking the default name servers.
       *
       * We will ask for all the intervening nameservers,
       * and the actual result we want.
       */

      // XXX: maybe we should iterate through cnames...

      n_dots = count_dots (name);
      find_name_pieces (name, &n_pieces, pieces);
      for (found_ns_index = 0; found_ns_index < n_pieces; found_ns_index++)
	{
	  const char *ns_name;
	  /* Try and get a NS record for `pieces[found_ns_index]' */
	  DEBUG ("calling gsk_dns_rr_cache_get_ns_addr(..,%s,..)", pieces[found_ns_index]);
	  if (gsk_dns_rr_cache_get_ns_addr (rr_cache,
					    pieces[found_ns_index],
					    &ns_name,
					    &address))
	    {
	      /* XXX: max timeout is clearly too long. */
	      ip_permission_table_insert (task->client->ip_perm_table,
					  address,
					  TRUE,
					  ns_name,
					  cur_time + MAX_TIMEOUT);
	      break;
	    }

	  /* Look for a nameserver of one domain up
	   * (next iteration of this loop)
	   */
	}

      {
	GskDnsMessage *message;
	GskDnsQuestion *q;
	GskDnsNameServerInfo *ns;
	char *tmp_name;
	if (found_ns_index == n_pieces)
	  ns = NULL;
	else
          {
            ns = get_nameserver (task, address);
            g_object_unref (address);
          }
	message = maybe_make_message (task, ns_to_dns_message, ns);

#if 0	/* this isn't the way the examples in 1034, 6.3.1 operate. */
	/* Ask about all the intervening nameservers. */
	for (i = 0; i < found_ns_index; i++)
	  {
	    /* Request the nameserver for pieces[i]. */
	    /* XXX: ensure that the questions here are unique:
	     *      it's lame to request the same things repeatedly!!!
	     */
	    q = gsk_dns_question_new (pieces[i],
				      GSK_DNS_RR_NAME_SERVER,
				      query_class,
				      message);
	    gsk_dns_message_append_question (message, q);
	  }
#endif

	/* Ask about the real information. */
	tmp_name = question->query_name;
	question->query_name = (char *) name;
	q = gsk_dns_question_copy (question, message);
	question->query_name = tmp_name;
	gsk_dns_message_append_question (message, q);
      }

      last = questions;
      questions = questions->next;
      g_slist_free (cnames);
    }

  g_assert (!task->failed);

  if (ns_to_dns_message == NULL)
    {
      /* Ok, all the questions were resolved through the local cache. */
      /* XXX: might there be an error to flag, if nonrecursive. */
      gsk_dns_client_task_succeed (task);
      gsk_dns_client_task_unref (task);
      return;
    }

  /* Otherwise, make the necessary queries. */
  {
    DnsQueryData query_data = { FALSE, 0, task };
    g_hash_table_foreach (ns_to_dns_message,
			  (GHFunc) do_dns_query,
			  &query_data);
    g_hash_table_destroy (ns_to_dns_message);
    if (task->failed)
      {
	gsk_dns_client_task_unref (task);
	return;
      }
    task->n_iterations++;

    /* Create a timeout source. */
    /* XXX: what about requests that were pending on entry to this function?
       I think we should *always* set a timeout here... */
    if (query_data.has_expire_time && task->timeout_source == NULL)
      add_timeout (task, query_data.next_expire_dist);
  }
}

static gpointer
gsk_dns_client_resolve (GskDnsResolver               *resolver,
			gboolean                      recursive,
			GSList                       *questions,
			GskDnsResolverResponseFunc    func,
			GskDnsResolverFailFunc        on_fail,
			gpointer                      func_data,
			GDestroyNotify                destroy,
			GskDnsResolverHints          *hints)
{
  GskDnsClient *client = GSK_DNS_CLIENT (resolver);
  ClientTask *task;
  ClientTask *rv;

  (void) hints;

  task = g_new (ClientTask, 1);
  task->client = client;
  task->message_id = gsk_dns_client_generate_message_id (client);

  /* Insert task into `id_to_task_list' */
  {
    ClientTask *hash_head;
    guint message_id = task->message_id;
    gpointer msg_id = GUINT_TO_POINTER (message_id);
    hash_head = g_hash_table_lookup (client->id_to_task_list, msg_id);
    task->hash_next = hash_head;
    if (hash_head != NULL)
      hash_head->hash_prev = task;
    task->hash_prev = NULL;
    g_hash_table_insert (client->id_to_task_list, msg_id, task);
  }

  /* one ref-count is maintained by the DnsClient,
   * the other will be undone at the end of this function
   * (to avoid having the task deleted from underneath us)
   */
  task->ref_count = 2;

  task->n_iterations = 0;
  task->max_iterations = recursive ? client->max_iterations_recursive : client->max_iterations_nonrecursive;
  task->is_in_client_list = 1;
  task->destroyed = 0;
  task->failed = 0;
  task->ran_task_func = 0;
  task->cancelled = 0;
  task->stub_resolver = client->stub_resolver;
  task->used_conf_nameservers = 0;
  task->recursive = recursive ? 1 : 0;

  task->rr_cache = client->rr_cache;
  if (task->rr_cache != NULL)
    gsk_dns_rr_cache_ref (task->rr_cache);

  task->locked_records = NULL;
  task->first_ns = NULL;
  task->last_ns = NULL;
  {
    GSList *tmp;
    GSList *copy_list = NULL;
    for (tmp = questions; tmp != NULL; tmp = tmp->next)
      {
	GskDnsQuestion *orig = tmp->data;
	GskDnsQuestion *copy = gsk_dns_question_copy (orig, NULL);
	copy_list = g_slist_prepend (copy_list, copy);
      }
    task->questions = g_slist_reverse (copy_list);
  }
  task->answered_questions = NULL;
  task->negatives = NULL;

  task->func = func;
  task->on_fail = on_fail;
  task->func_data = func_data;
  task->destroy = destroy;
  task->timeout_source = NULL;

  /* Add to the client's list of tasks. */
  task->next = client->tasks;
  task->prev = NULL;
  if (client->tasks != NULL)
    client->tasks->prev = task;
  client->tasks = task;

  try_local_cache_or_proceed (task);

  rv = (task->ref_count == 1) ? NULL : task;
  gsk_dns_client_task_unref (task);

  return rv;
}

/* --- client: incoming dns message handler --- */
/* Find out whether a ResourceRecord from a particular
 * address can be trusted enough to be put in our cache.
 */
static gboolean
check_rr_authority (GskDnsClient         *client,
		    GskSocketAddressIpv4 *address,
		    GskDnsResourceRecord *record,
		    guint                 cur_time)
{
  const guint8 *ip_address;
  GskDnsNameServerInfo *ns_info;
  ip_address = address->ip_address;

  /* See if `address' is a configured nameserver.
   * We trust those completely.
   */
  for (ns_info = client->first_ns;
       ns_info != NULL;
       ns_info = ns_info->next_ns)
    if (gsk_socket_address_equals (address, ns_info->address))
      return TRUE;

  /* See if `address' has been granted temporary
   * permission.
   */
  if (ip_permission_table_check (client->ip_perm_table,
				 address,
				 record->owner,
				 cur_time))
    return TRUE;

  return FALSE;
}

static gboolean
is_or_is_cname_for (const char            *owner,
		    const char            *ask_name,
		    GskDnsRRCache         *rr_cache)
{
  while (ask_name != NULL)
    {
      GskDnsResourceRecord *rr;
      if (strcasecmp (owner, ask_name) == 0)
	return TRUE;
      rr = gsk_dns_rr_cache_lookup_one (rr_cache,
					ask_name,
					GSK_DNS_RR_CANONICAL_NAME,
					GSK_DNS_CLASS_INTERNET,
                                        0);
      ask_name = rr ? rr->rdata.domain_name : NULL;
    }
  return FALSE;
}

static gboolean
is_suffix_for (const char *name, const char *suffix)
{
  int suffix_len = strlen (suffix);
  int query_name_len = strlen (name);
  const char *qsuffix = name + query_name_len - suffix_len;
  if ((qsuffix > name && qsuffix[-1] == '.')
   || (qsuffix == name))
    {
      if (strcasecmp (suffix, qsuffix) == 0)
	return TRUE;
    }
  return FALSE;
}

static inline gboolean
check_does_rr_answer_question (GskDnsResourceRecord *record,
			       GskDnsQuestion       *question,
			       GskDnsRRCache        *rr_cache)
{

  if (record->type == GSK_DNS_RR_NAME_SERVER)
    {
      const char *query_name = question->query_name;
      while (query_name != NULL)
	{
	  GskDnsResourceRecord *rr;
	  if (is_suffix_for (query_name, record->owner))
	    return TRUE;

	  rr = gsk_dns_rr_cache_lookup_one (rr_cache,
					    query_name,
					    GSK_DNS_RR_CANONICAL_NAME,
					    GSK_DNS_CLASS_INTERNET,
                                            0);
          query_name = rr ? rr->rdata.domain_name : NULL;
	}
    }

  /* Probably only applies to the 'additional' section. */
  if (record->type == GSK_DNS_RR_HOST_ADDRESS
   || record->type == GSK_DNS_RR_HOST_ADDRESS_IPV6)
    {
      const char *query_name = question->query_name;
      GSList *all_encountered_names = g_slist_prepend (NULL,
                                                       (gpointer) query_name);
      while (query_name != NULL)
	{
	  GskDnsResourceRecord *rr;
	  const char *qn = query_name;
	  GSList *ns_list;
	  do {
	    ns_list = gsk_dns_rr_cache_lookup_list (rr_cache, qn,
                                                    GSK_DNS_RR_NAME_SERVER,
                                                    GSK_DNS_CLASS_INTERNET);
	    qn = strchr (qn, '.');
	    if (qn)
	      qn++;
	  } while (ns_list == NULL && qn != NULL);
	  while (ns_list)
	    {
	      GskDnsResourceRecord *ns_rr = ns_list->data;
	      ns_list = g_slist_remove (ns_list, ns_rr);
	      if (strcasecmp (ns_rr->rdata.domain_name, record->owner) == 0)
                {
                  g_slist_free (ns_list);
                  g_slist_free (all_encountered_names);
                  return TRUE;
                }
	    }

	  rr = gsk_dns_rr_cache_lookup_one (rr_cache,
					    query_name,
					    GSK_DNS_RR_CANONICAL_NAME,
					    GSK_DNS_CLASS_INTERNET,
                                            0);
          query_name = rr ? rr->rdata.domain_name : NULL;
          if (query_name)
            {
              if (g_slist_find_custom (all_encountered_names, query_name,
                                       (GCompareFunc) strcmp))
                break;
              all_encountered_names = g_slist_prepend (all_encountered_names,
                                                       (gpointer) query_name);
            }
	}
      g_slist_free (all_encountered_names);
    }

  return   (question->query_type == record->type
         || record->type == GSK_DNS_RR_CANONICAL_NAME
         || question->query_type == GSK_DNS_RR_WILDCARD)
    && is_or_is_cname_for (record->owner, question->query_name, rr_cache);
}

/* Check whether a ResourceRecord appears to
 * answer any question posed to this task.
 *
 * XXX: each of the hosts needs a list of CNAME's it goes by.
 *      (Note i think this TODO is should be resolved as a TODO
 *       for check_does_rr_answer_question... not quite sure
 *       though)
 */
static gboolean
check_is_rr_relevant (ClientTask   *task,
		      GskDnsResourceRecord *record,
		      GskDnsRRCache        *rr_cache)
{
  GSList *list;
  for (list = task->questions; list != NULL; list = list->next)
    if (check_does_rr_answer_question (record,
				       (GskDnsQuestion *) list->data,
				       rr_cache))
      return TRUE;
  return FALSE;
}

static void
append_and_lock_rr_list_to_task (GSList               *rr_list,
			         ClientTask   *task,
				 GskSocketAddressIpv4 *address,
				 gboolean              is_authoritative,
				 guint                 cur_time)
{
  while (rr_list != NULL)
    {
      GskDnsResourceRecord *record = rr_list->data;
      if (!check_rr_authority (task->client, address, record, cur_time))
	{
	  rr_list = rr_list->next;
	  continue;
	}

      record = gsk_dns_rr_cache_insert (task->rr_cache,
					record,
					is_authoritative,
					cur_time);
      task->locked_records = g_slist_prepend (task->locked_records, record);
      gsk_dns_rr_cache_lock (task->rr_cache, record);
      rr_list = rr_list->next;
    }
}

static int
question_equal_or_ends_with (GskDnsQuestion *question,
			     const char     *ns_owner)
{
  const char *query_name = question->query_name;
  if (strcasecmp (query_name, ns_owner) == 0)
    {
      return 0;
    }
  else
    {
      const char *start_ns_owner = strchr (query_name, 0) - strlen (ns_owner);
      if (start_ns_owner <= query_name)
	return 1;
      if (start_ns_owner[-1] == '.'
       && strcasecmp (start_ns_owner, ns_owner) == 0)
	return 0;
    }
  return 1;
}

static int
look_for_relevant_ns_entry (GskDnsResourceRecord *record,
			    ClientTask   *task)
{
  if (record->type != GSK_DNS_RR_NAME_SERVER)
    return 1;

  /* find out if any of the questions' query_names are equal to
     ``owner'', or end with ``.owner''. */
  if (g_slist_find_custom (task->questions,
			   (gpointer) record->owner,
			   (GCompareFunc) question_equal_or_ends_with))
    return 0;

  return 1;
}

static void
task_handle_message (ClientTask   *task,
		     GskSocketAddressIpv4 *address,
		     GskDnsMessage        *message)
{
  GSList *lists[3];
  GSList *list;

  /* Set if any of the answers applied to us.
   * Blithely assume that we may enter the
   * remaining records to the locked-list,
   * if we give them authority to be added into our
   * cache.
   */
  gboolean one_answer_was_relevant = FALSE;
  guint iteration;
  guint cur_time = task->client->main_loop->current_time.tv_sec;

  lists[0] = message->answers;
  lists[1] = message->authority;
  lists[2] = message->additional;

  /*
   * Append as many answer rr's as possible into
   * the cache, and lock those records.
   */
  
  for (iteration = 0; iteration < 3; iteration++)
    for (list = lists[iteration]; list != NULL; list = list->next)
      {
	GskDnsResourceRecord *record = list->data;

	/* Check whether the source nameserver has the authority
	 * to give us a record like this.
	 */
	if (!check_rr_authority (task->client, address, record, cur_time))
	  {
	    g_warning ("ip address (%d.%d.%d.%d) did not have authority to add '%s'",
			 address->ip_address[0],
			 address->ip_address[1],
			 address->ip_address[2],
			 address->ip_address[3],
			 record->owner);
	    continue;
	  }

	//DEBUG ("task_handle_message: task->client->rr_cache=%p; record=%s", task->client->rr_cache, gsk_dns_rr_text_to_str (record, NULL));	/*XXX: memory leak */

	/* Add that RR to our cache. */
	if (task->client->rr_cache != NULL)
	  record = gsk_dns_rr_cache_insert (task->rr_cache, record,
					    message->is_authoritative,
					    cur_time);

	/* Now see if that answer is relevant to any of our questions. */
	if (!check_is_rr_relevant (task, record, task->rr_cache))
	  {
	    DEBUG ("that record was not relevant");
	    continue;
	  }

	DEBUG ("task_handle_message: the message WAS relevant");

	/* Ok, lock this RR; if the DnsClient has no cache,
	 * lock it into the task's local cache.
	 *
	 */
	if (task->client->rr_cache == NULL)
	  record = gsk_dns_rr_cache_insert (task->rr_cache, record,
					    message->is_authoritative,
					    cur_time);
	gsk_dns_rr_cache_lock (task->rr_cache, record);
	task->locked_records = g_slist_prepend (task->locked_records, record);

	if (record->type == GSK_DNS_RR_NAME_SERVER)
	  {
	    /* let's allow additional records giving ip addresses
	     * to this nameserver.
	     *
	     * TODO: figure out if the '+ 1' is really needed!
	     */
	    ip_permission_table_insert (task->client->ip_perm_table,
					address,
					FALSE,			/* just this exact name */
					record->rdata.domain_name,
					cur_time + 1);
	  }
	else if (record->type == GSK_DNS_RR_CANONICAL_NAME)
	  {
	    /* I'm not really sure that this is secure,
	       but we just pierce a hole in the ns permission
	       table to allow this name server to allow us to
	       define that CNAME a bit more.
	       
	       We should probably start issuing queries for
	       the CNAME independently however,
	       ie we should now go and ask about the CNAME
	       if this query doesn't pan out.  */

	    const char *cname = record->rdata.domain_name;
#if 1
	    const char *last_dot = NULL;
	    const char *dot = strchr (cname, '.');
	    while (dot && dot[1] != 0)
	      {
		last_dot = dot;
		dot = strchr (dot + 1, '.');
	      }
	    if (last_dot)
	      cname = last_dot + 1;
#endif
	    ip_permission_table_insert (task->client->ip_perm_table,
					address,
					TRUE,
					cname,
					cur_time + 1);
	  }

	one_answer_was_relevant = TRUE;
      }

  switch (message->error_code)
    {
    case GSK_DNS_RESPONSE_ERROR_NONE:
      /* No error:  this is the default case. */
      break;

    case GSK_DNS_RESPONSE_ERROR_FORMAT:
      {
	GError *error = g_error_new (GSK_G_ERROR_DOMAIN,
			             GSK_ERROR_RESOLVER_FORMAT,
				     _("format error from DNS request"));
	client_task_fail (task, error);
	g_error_free (error);
	return;
      }
    case GSK_DNS_RESPONSE_ERROR_SERVER_FAILURE:
      {
	GError *error = g_error_new (GSK_G_ERROR_DOMAIN,
			             GSK_ERROR_RESOLVER_SERVER_PROBLEM,
				     _("miscellaneous server problem"));
	client_task_fail (task, error);
	g_error_free (error);
	return;
      }

    case GSK_DNS_RESPONSE_ERROR_NAME_ERROR:
      {
	GskDnsQuestion *question = message->questions ? message->questions->data : NULL;
	GError *error = g_error_new (GSK_G_ERROR_DOMAIN,
			             GSK_ERROR_RESOLVER_NOT_FOUND,
				     _("name %s not found"),
				     (question ? question->query_name : "**UNKNOWN**"));
	client_task_fail (task, error);
	g_error_free (error);
	return;
      }
    case GSK_DNS_RESPONSE_ERROR_NOT_IMPLEMENTED:
      {
	GError *error = g_error_new (GSK_G_ERROR_DOMAIN,
			             GSK_ERROR_RESOLVER_SERVER_PROBLEM,
				     _("server: command not implemented"));
	client_task_fail (task, error);
	g_error_free (error);
	return;
      }
    case GSK_DNS_RESPONSE_ERROR_REFUSED:
      {
	GError *error = g_error_new (GSK_G_ERROR_DOMAIN,
			             GSK_ERROR_RESOLVER_SERVER_PROBLEM,
				     _("server: command refused"));
	client_task_fail (task, error);
	g_error_free (error);
	return;
      }
    default:
      {
	GError *error = g_error_new (GSK_G_ERROR_DOMAIN,
			             GSK_ERROR_RESOLVER_SERVER_PROBLEM,
				     _("server: unexpected error code"));
	client_task_fail (task, error);
	g_error_free (error);
	return;
      }
    }

  /* ok, must've been a mixup */
  if (!one_answer_was_relevant)
    {
      GSList *found;
      found = g_slist_find_custom (message->answers, task,
			           (GCompareFunc) look_for_relevant_ns_entry);
      if (found == NULL)
	found = g_slist_find_custom (message->authority, task,
			             (GCompareFunc) look_for_relevant_ns_entry);
      if (found == NULL)
	found = g_slist_find_custom (message->additional, task,
			             (GCompareFunc) look_for_relevant_ns_entry);
      if (found == NULL)
	{
	  /* XXX: this can actually happen if we are making so many
	          DNS requests that hash_next,hash_prev are actually being
		  used.  */
	  /* XXX: another time when this happens
	          is when the question is an empty string.
		  not completely sure what's going on in that case. */
	  gsk_g_debug ("Received useless message with matching ID.");
#if 0
	  g_message ("MESSAGE WAS:");
	  gsk_dns_dump_message_fp (message, stderr);
	  g_message ("QUESTIONS WERE:");
	  g_slist_foreach (task->questions, (GFunc) gsk_dns_dump_question_fp, stderr);
	  g_message ("ANSWERED QUESTIONS WERE:");
	  g_slist_foreach (task->answered_questions, (GFunc) gsk_dns_dump_question_fp, stderr);
#endif
	  return;
	}
    }

  /* Scan the authority and additional records. */
  append_and_lock_rr_list_to_task (message->authority, task, 
				   address, message->is_authoritative,
				   cur_time);
  append_and_lock_rr_list_to_task (message->additional, task, 
				   address, message->is_authoritative,
				   cur_time);

  /* Now, try continuing the processing. */
  try_local_cache_or_proceed (task);
}

static void
client_handle_message  (GskDnsClient           *client,
			GskDnsMessage          *message,
			GskSocketAddressIpv4   *address)
{
  ClientTask *task_list;
  guint message_id = message->id;
  if (message->is_query)
    return;

  task_list = g_hash_table_lookup (client->id_to_task_list,
				   GUINT_TO_POINTER (message_id));
  DEBUG ("client_handle_message: for message id %d, task_list=%p", message_id, task_list);
  DEBUG_PRINT_MESSAGE (message);
  while (task_list != NULL)
    {
      ClientTask *next_task;
      gsk_dns_client_task_ref (task_list);

      task_handle_message (task_list, address, message);

      next_task = task_list->hash_next;
      gsk_dns_client_task_unref (task_list);
      task_list = next_task;
    }
}

/* --- utility methods --- */
static void
gsk_dns_client_fail_all_tasks (GskDnsClient  *client,
			       GError        *error)
{
  while (client->tasks != NULL)
    {
      ClientTask *task = client->tasks;
      remove_from_client_list (task);

      if (!task->destroyed)
	{
	  if (task->on_fail != NULL)
	    (*task->on_fail) (error, task->func_data);
	  task->failed = 1;
	}
      gsk_dns_client_task_unref (task);
    }
}

/* Fail a single client and remove it from the client list. */
static void
client_task_fail (ClientTask *task,
		  GError     *error)
{
  remove_from_client_list (task);
  if (!task->destroyed)
    {
      if (task->on_fail != NULL)
	(*task->on_fail) (error, task->func_data);
      task->failed = 1;
    }
  DEBUG ("client_task_fail: %s", error->message);
  gsk_dns_client_task_unref (task);
}

static void
gsk_dns_client_transmit (GskDnsClient         *client,
			 GskSocketAddressIpv4 *address,
			 GskDnsMessage        *message)
{
  GskPacket *packet = gsk_dns_message_to_packet (message);
  gsk_packet_set_dst_address (packet, GSK_SOCKET_ADDRESS (address));
#ifdef GSK_DEBUG
  if (gsk_debug_flags & GSK_DEBUG_DNS)
    {
      char *a = gsk_socket_address_to_string (GSK_SOCKET_ADDRESS (address));
      g_printerr ("DNS: about to output message to %s:\n", a);
      gsk_dns_dump_message_fp (message, stderr);
      g_free (a);
    }
#endif
  if (client->first_outgoing_packet == NULL)
    {
      GError *error = NULL;
      /* try writing it immediately */
      if (!gsk_packet_queue_write (client->packet_queue, packet, &error))
	{
	  if (error != NULL)
	    {
	      gsk_dns_client_fail_all_tasks (client, error);
	      g_error_free (error);
	      return;
	    }
	  /* fall-through */
	}
      else
	{
	  /* successfully immeediately wrote packet: nothing else to do */
	  gsk_packet_unref (packet);
	  return;
	}
    }

  /* append the packet to the queue */
  client->last_outgoing_packet = g_slist_append (client->last_outgoing_packet, packet);
  if (client->first_outgoing_packet == NULL)
    client->first_outgoing_packet = client->last_outgoing_packet;
  else
    client->last_outgoing_packet = client->last_outgoing_packet->next;

  /* make sure we aren't blocking writable events */
  if (client->is_blocking_write)
    {
      client->is_blocking_write = 0;
      gsk_io_unblock_write (GSK_IO (client->packet_queue));
    }
}

/* --- io handlers --- */
static gboolean
handle_queue_is_readable (GskIO         *io,
			  gpointer       data)
{
  GskPacket *packet;
  GskDnsMessage *message;
  GskSocketAddress *address;
  GskDnsClient *client = GSK_DNS_CLIENT (data);
  GError *error = NULL;
  guint used;
  packet = gsk_packet_queue_read (GSK_PACKET_QUEUE (io), TRUE, &error);
  if (packet == NULL)
    {
      if (error != NULL)
	{
	  gsk_dns_client_fail_all_tasks (client, error);
	  g_error_free (error);
	  return FALSE;
	}
      return TRUE;
    }

  /* convert the packet into a dns-message */
  message = gsk_dns_message_parse_data ((const guint8 *) packet->data,
					packet->len, &used);
#ifdef GSK_DEBUG
  if (gsk_debug_flags & GSK_DEBUG_DNS)
    {
      char *a = gsk_socket_address_to_string (packet->src_address);
      g_printerr ("DNS: from address %s got message:\n", a);
      gsk_dns_dump_message_fp (message, stderr);
      g_free (a);
    }
#endif
  address = g_object_ref (packet->src_address);
  if (message != NULL && used != packet->len)
    {
      /* XXX: error handling */
      g_warning ("ignorable error parsing dns message");
    }
  gsk_packet_unref (packet);
  if (message == NULL)
    {
      /* XXX: error handling */
      g_warning ("malformed dns message - ignoring");
      g_object_unref (address);
      return TRUE;
    }
  if (!GSK_IS_SOCKET_ADDRESS_IPV4 (address))
    {
      /* XXX: error handling */
      g_warning ("only IP v4 sockets may use DNS");
      gsk_dns_message_unref (message);
      g_object_unref (address);
      return TRUE;
    }

  client_handle_message (client, message, 
			 GSK_SOCKET_ADDRESS_IPV4 (address));

  gsk_dns_message_unref (message);
  g_object_unref (address);
  return TRUE;
}

static gboolean
handle_queue_is_readable_shutdown (GskIO         *io,
			           gpointer       data)
{
  GError *error = g_error_new (GSK_G_ERROR_DOMAIN,
			       GSK_ERROR_RESOLVER_SOCKET_DIED,
			       _("got read shutdown in dns socket"));
  gsk_dns_client_fail_all_tasks (GSK_DNS_CLIENT (data), error);
  g_error_free (error);
  return FALSE;
}


static gboolean
handle_queue_is_writable (GskIO         *io,
			  gpointer       data)
{
  GskDnsClient *client = GSK_DNS_CLIENT (data);
  while (client->first_outgoing_packet != NULL)
    {
      GError *error = NULL;
      GSList *list = client->first_outgoing_packet;
      GskPacket *packet = list->data;
      if (!gsk_packet_queue_write (GSK_PACKET_QUEUE (io), packet, &error))
	{
	  if (error == NULL)
	    return TRUE;
	  gsk_dns_client_fail_all_tasks (client, error);
	  g_error_free (error);
	  return FALSE;
	}
      client->first_outgoing_packet = g_slist_remove (list, packet);
      if (client->first_outgoing_packet == NULL)
	client->last_outgoing_packet = NULL;
      gsk_packet_unref (packet);
    }
  if (client->first_outgoing_packet == NULL)
    {
      if (!client->is_blocking_write)
	{
	  client->is_blocking_write = 1;
	  gsk_io_block_write (io);
	}
    }
  else
    g_assert (!client->is_blocking_write);
  return TRUE;
}

static gboolean
handle_queue_is_writable_shutdown (GskIO         *io,
			           gpointer       data)
{
  GError *error = g_error_new (GSK_G_ERROR_DOMAIN,
			       GSK_ERROR_RESOLVER_SOCKET_DIED,
			       _("got write shutdown in dns socket"));
  gsk_dns_client_fail_all_tasks (GSK_DNS_CLIENT (data), error);
  g_error_free (error);
  return FALSE;
}

static void
unref_packet_queue (gpointer c)
{
  g_object_unref (GSK_DNS_CLIENT (c)->packet_queue);
}

/* --- GObject functions --- */

static void
gsk_dns_client_destroy_all_queries (GskDnsClient *client)
{
  GskDnsResolver *resolver = GSK_DNS_RESOLVER (client);
  while (client->tasks != NULL)
    gsk_dns_client_resolver_cancel (resolver, client->tasks);
}

static void
gsk_dns_client_finalize (GObject *object)
{
  GskDnsClient *client = GSK_DNS_CLIENT (object);
  gsk_dns_client_destroy_all_queries (client);
  ip_permission_table_destroy (client->ip_perm_table);
  g_hash_table_destroy (client->id_to_task_list);
  if (client->rr_cache != NULL)
    gsk_dns_rr_cache_unref (client->rr_cache);
  if (client->searchpath_array != NULL)
    {
      gpointer *ptr_array = client->searchpath_array->pdata;
      guint len = client->searchpath_array->len;
      while (len-- > 0)
	g_free (*ptr_array++);
      g_ptr_array_free (client->searchpath_array, TRUE);
      client->searchpath_array = NULL;
    }

  (*parent_class->finalize) (object);
}

/* --- functions --- */
static void
gsk_dns_client_init (GskDnsClient *dns_client)
{
  dns_client->last_message_id = (guint16) rand ();
  dns_client->ip_perm_table = ip_permission_table_new ();
  dns_client->id_to_task_list = g_hash_table_new (NULL, NULL);
  dns_client->main_loop = gsk_main_loop_default ();
  dns_client->recursion_desired = 1;

  /* TODO: does the RFC say anything about these defaults? */
  dns_client->max_iterations_recursive = 5; /* recursive ns shouldn't take many retries */
  dns_client->max_iterations_nonrecursive = 10;
}

static void
gsk_dns_client_resolver_init (GskDnsResolverIface *resolver_iface)
{
  resolver_iface->resolve = gsk_dns_client_resolve;
  resolver_iface->cancel = gsk_dns_client_resolver_cancel;
}

static void
gsk_dns_client_class_init (GskDnsClientClass *dns_client_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (dns_client_class);
  parent_class = g_type_class_peek_parent (object_class);
  object_class->finalize = gsk_dns_client_finalize;
}

GType gsk_dns_client_get_type()
{
  static GType dns_client_type = 0;
  if (!dns_client_type)
    {
      static const GTypeInfo dns_client_info =
      {
	sizeof(GskDnsClientClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) gsk_dns_client_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GskDnsClient),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gsk_dns_client_init,
	NULL		/* value_table */
      };
      static GInterfaceInfo client_resolver_info =
      {
	(GInterfaceInitFunc) gsk_dns_client_resolver_init,
	NULL,			/* interface_finalize */
	NULL			/* interface_data */
      };
      dns_client_type = g_type_register_static (G_TYPE_OBJECT,
						"GskDnsClient",
						&dns_client_info, 0);
      g_type_add_interface_static (dns_client_type,
				   GSK_TYPE_DNS_RESOLVER,
				   &client_resolver_info);
      gsk_dns_resolver_add_name_resolver_iface (dns_client_type);
    }
  return dns_client_type;
}

/**
 * gsk_dns_client_new:
 * @packet_queue: underlying transport layer to use.
 * The client will keep a reference to this packet-queue.
 * @rr_cache: cache of resource-records.
 * The client will keep a reference to this cache.
 * @flags: whether you want this client to be a recursive
 * or stub resolver.
 *
 * Create a new DNS client.
 * This implements the #GskDnsResolver interface.
 *
 * returns: the newly allocated DNS client.
 */
GskDnsClient   *gsk_dns_client_new           (GskPacketQueue     *packet_queue,
					      GskDnsRRCache      *rr_cache,
					      GskDnsClientFlags   flags)
{
  GskDnsClient *client;

  client = g_object_new (GSK_TYPE_DNS_CLIENT, NULL);
  client->packet_queue = g_object_ref (packet_queue);
  gsk_io_trap_readable (g_object_ref (packet_queue),
		        handle_queue_is_readable,
		        handle_queue_is_readable_shutdown,
		        client,
		        unref_packet_queue);
  gsk_io_trap_writable (g_object_ref (packet_queue),
		        handle_queue_is_writable,
		        handle_queue_is_writable_shutdown,
		        client,
		        unref_packet_queue);
  client->is_blocking_write = 1;
  gsk_io_block_write (GSK_IO (packet_queue));
  client->stub_resolver = (flags & GSK_DNS_CLIENT_STUB_RESOLVER) ? 1 : 0;
  client->rr_cache = rr_cache;
  if (rr_cache != NULL)
    gsk_dns_rr_cache_ref (rr_cache);
  else
    client->rr_cache = gsk_dns_rr_cache_new (GSK_DNS_MAX_CACHE_BYTES,
                                             GSK_DNS_MAX_CACHE_RECORDS);
  return client;
}

/**
 * gsk_dns_client_add_searchpath:
 * @client: the client to affect.
 * @searchpath: the new path to search.
 *
 * Add a new implicit domain to the list the client keeps.
 *
 * A searchpath entry is simply a domain to try
 * post-fixing to any request.  For example,
 * if you have "sourceforce.net" in your searchpath,
 * then looking "cvs.gsk" should resolve "cvs.gsk.sourceforge.net".
 *
 * Searchpath entries take priority, EXCEPT
 * if the requested domain name ends in ".".
 * If you have "sourceforce.net" in your searchpath,
 * then looking "cvs.com" should resolve "cvs.com.sourceforge.net"
 * then "cvs.com" if the former does not exist.
 * However, looking up "cvs.com." will ONLY search the global namespace.
 */
void
gsk_dns_client_add_searchpath(GskDnsClient       *client,
			      const char         *searchpath)
{
  g_return_if_fail (searchpath != NULL);

  if (client->searchpath_array == NULL)
    client->searchpath_array = g_ptr_array_new ();
  g_ptr_array_add (client->searchpath_array, g_strdup (searchpath));
}

/**
 * gsk_dns_client_add_ns:
 * @client: the client to affect.
 * @address: the numeric address of the nameserver.
 *
 * Add a new nameserver to query.
 * All nameservers will be queried simultaneously.
 */
void
gsk_dns_client_add_ns        (GskDnsClient           *client,
			      GskSocketAddressIpv4   *address)
{
  GskDnsNameServerInfo *ns_info;
  for (ns_info = client->first_ns; ns_info != NULL; ns_info = ns_info->next_ns)
    if (gsk_socket_address_equals (address, ns_info->address))
      break;
  if (ns_info != NULL)
    return;
  ns_info = gsk_dns_name_server_info_alloc ();
  ns_info->address = g_object_ref (address);
  ns_info->num_msg_sent = ns_info->num_msg_received = 0;
  ns_info->next_ns = NULL;
  ns_info->prev_ns = client->last_ns;
  ns_info->is_default_ns = 0;
  if (client->last_ns != NULL)
    client->last_ns->next_ns = ns_info;
  else
    client->first_ns = ns_info;
  client->last_ns = ns_info;
}

/**
 * gsk_dns_client_set_cache:
 * @client: the client to affect.
 * @rr_cache: the new resource-record cache to use.
 *
 * Switch the client to use a new resource-record cache.
 */
void
gsk_dns_client_set_cache     (GskDnsClient       *client,
			      GskDnsRRCache      *rr_cache)
{
  if (rr_cache != client->rr_cache)
    {
      if (rr_cache != NULL)
	gsk_dns_rr_cache_ref (rr_cache);
      if (client->rr_cache != NULL)
	gsk_dns_rr_cache_unref (client->rr_cache);
      client->rr_cache = rr_cache;
    }
}

void
gsk_dns_client_set_flags     (GskDnsClient       *client,
			      GskDnsClientFlags   flags)
{
  client->stub_resolver = (flags & GSK_DNS_CLIENT_STUB_RESOLVER) ? 1 : 0;
}

GskDnsClientFlags
gsk_dns_client_get_flags     (GskDnsClient       *client)
{
  return client->stub_resolver ? GSK_DNS_CLIENT_STUB_RESOLVER : 0;
}

/* --- System file parsing --- */

/**
 * gsk_dns_client_parse_resolv_conf_line:
 * @client: the client which should add the resolv.conf information
 * to its search parameters.
 * @text: a single line from a resolve.conf.
 *
 * A resolv.conf file can have several fields:
 *   'nameserver' gives an ip-address of a nameserver to use.
 *   'domain' gives the host's domain
 *   'search' gives alternate domains to search.
 * Also, 'sortlist' is unimplemented.
 *
 * returns: whether the line was parsed successfully.
 */

/* Format of /etc/resolv.conf:
 *
 *  `nameserver IPP-ADDRESS'     List a name server to use.
 *  `domain DOMAIN-NAME'         Domain which is added to search path.
 *                               If not set here, assume everything after
 *                               the first dot is the domain.
 *                               (XXX: domain needs to be implemented)
 *  `search PATH'                List of paths to inspect for dns data.
 *
 * Unsupported:
 *  `sortlist IP-PREFERENCES-SPEC'   List of how the return values
 *                                   should be sorted.
 */
gboolean
gsk_dns_client_parse_resolv_conf_line(GskDnsClient       *client,
				      const char         *text)
{
  GSK_SKIP_WHITESPACE (text);
  if (*text == 0 || *text == '#' || *text == ';')
    return TRUE;
  if (g_strncasecmp (text, "nameserver", 10) == 0)
    {
      GskSocketAddress *address;
      guint8 ip_address[4];
      text += 10;
      GSK_SKIP_WHITESPACE (text);
      if (!gsk_dns_parse_ip_address (&text, ip_address))
	return FALSE;
      address = gsk_socket_address_ipv4_new (ip_address, GSK_DNS_PORT);
      gsk_dns_client_add_ns (client, GSK_SOCKET_ADDRESS_IPV4 (address));
      return TRUE;
    }
  if (g_strncasecmp (text, "search", 6) == 0)
    {
      const char *end;
      char *tmp;
      text += 6;
      GSK_SKIP_WHITESPACE (text);
      tmp = alloca (strlen (text) + 1);
      while (*text != 0)
	{
	  end = text;
	  GSK_SKIP_NONWHITESPACE (end);
	  if (end == text)
	    break;
	  memcpy (tmp, text, end - text);
	  tmp[end - text] = 0;

	  gsk_dns_client_add_searchpath (client, tmp);

	  text = end;
	  GSK_SKIP_WHITESPACE (text);
	}
      return TRUE;
    }

  /* ignore domain lines?  */
  if (g_strncasecmp (text, "domain", 6) == 0)
    {
      return TRUE;
    }
  return FALSE;
}

/**
 * gsk_dns_client_parse_resolv_conf:
 * @client: the client which should add the resolv.conf information
 * @filename: name of the file containing resolv.conf information.
 * Typically "/etc/resolv.conf".
 * @may_be_missing: whether to consider it an error if the
 * file is missing.
 *
 * Parse /etc/resolv.conf information.
 *
 * returns: whether the file was parsed successfully.
 */
gboolean
gsk_dns_client_parse_resolv_conf (GskDnsClient       *client,
				  const char         *filename,
				  gboolean            may_be_missing)
{
  FILE *fp;
  char buf[8192];
  int line = 1;
  fp = fopen (filename, "r");
  if (fp == NULL)
    return may_be_missing;
  while (fgets (buf, sizeof (buf), fp) != NULL)
    {
      char *nl = strchr (buf, '\n');
      if (nl == NULL)
	{
	  g_warning ("%s: line too long or truncated file?", filename);
	  fclose (fp);
	  return FALSE;
	}
      *nl = '\0';
      if (!gsk_dns_client_parse_resolv_conf_line (client, buf))
	{
	  g_warning ("resolver: %s: error parsing line %d", filename, line);
	  fclose (fp);
	  return FALSE;
	}
      line++;
    }
  fclose (fp);
  return TRUE;
}

/**
 * gsk_dns_client_parse_system_files:
 * @client: the client which should add the system configuration information.
 *
 * Parse system DNS configuration.
 *
 * Currently, this parses /etc/hosts and /etc/resolv.conf .
 *
 * returns: whether the files were parsed successfully.
 */
gboolean
gsk_dns_client_parse_system_files(GskDnsClient       *client)
{
  gboolean rv1, rv2;
  GskDnsRRCache *rr_cache = client->rr_cache;
  g_return_val_if_fail (rr_cache != NULL, FALSE);
  rv1 = gsk_dns_client_parse_resolv_conf (client, "/etc/resolv.conf", TRUE);
  rv2 = gsk_dns_rr_cache_parse_etc_hosts (rr_cache, "/etc/hosts", TRUE);
  return rv1 && rv2;
}


/* --- ip-permission table implementation --- */
typedef struct _IpPermData IpPermData;
typedef struct _IpPermAddress IpPermAddress;

struct _IpPermData
{
  IpPermAddress   *addr_info;
  IpPermData      *next_data;
  IpPermData      *prev_data;
  guint            expire_time;

  /* If TRUE, just verify the suffix of the ResourceRecord.
   * If FALSE, verify only an exact match.
   */
  gboolean         any_suffixed_domain;
  const char      *owner;
};

struct _IpPermAddress
{
  /* must be first member for hash functions to work out! */
  GskSocketAddress *sock_addr;

  /* XXX: we may want a hashtable here instead! */
  IpPermData      *first_data;
  IpPermData      *last_data;
};

struct _IpPermissionTable
{
  GHashTable *sockaddr_to_perm_addr;
  GTree      *by_expire_time;

  /* if it turns out too much time is being spent
     in ip_permission_table_expire, turn this off
     and call flush on a schedule... */
  gboolean    autoflush;
};

/* --- helper functions --- */
static char *
lowercase_string (char *out, const char *in)
{
  char *rv = out;
  while (*in != 0)
    {
      if ('A' <= *in && *in <= 'Z')
	*out = *in + ('a' - 'A');
      else
	*out = *in;
      out++;
      in++;
    }
  *out = 0;
  return rv;
}

/* Make VAR a lower-cased copy of STR, on the stack. */
#define LOWER_CASE_COPY_ON_STACK(var,str) \
	G_STMT_START{ \
	  const char *_str = str; \
	  char *_tmp; \
	  _tmp = alloca (strlen (_str) + 1); \
	  var = lowercase_string (_tmp, _str); \
	}G_STMT_END

/*
 *  ___       ____                   _____     _     _
 * |_ _|_ __ |  _ \ ___ _ __ _ __ __|_   _|_ _| |__ | | ___
 *  | || '_ \| |_) / _ \ '__| '_ ` _ \| |/ _` | '_ \| |/ _ \
 *  | || |_) |  __/  __/ |  | | | | | | | (_| | |_) | |  __/
 * |___| .__/|_|   \___|_|  |_| |_| |_|_|\__,_|_.__/|_|\___|
 *     |_|
 *
 * A data structure used to temporary grant permission
 * to certain nameservers' IP addresses for a certain
 * subset of names.
 */
static gint
compare_ip_perm_data_times (const IpPermData *perm_data_a,
                            const IpPermData *perm_data_b)
{
  if (perm_data_a->expire_time < perm_data_b->expire_time)
    return -1;
  if (perm_data_a->expire_time > perm_data_b->expire_time)
    return +1;
  if (perm_data_a < perm_data_b)
    return -1;
  if (perm_data_a > perm_data_b)
    return +1;
  return 0;
}

static IpPermissionTable *
ip_permission_table_new ()
{
  IpPermissionTable *ip_perm_table = g_new (IpPermissionTable, 1);
  ip_perm_table->sockaddr_to_perm_addr
    = g_hash_table_new (gsk_socket_address_hash,
			gsk_socket_address_equals);
  ip_perm_table->by_expire_time 
    = g_tree_new ((GCompareFunc) compare_ip_perm_data_times);
  ip_perm_table->autoflush = TRUE;
  return ip_perm_table;
}

static void
ip_permission_table_insert (IpPermissionTable     *table,
			    GskSocketAddressIpv4  *address,
			    gboolean               any_suffixed_domain,
			    const char            *owner,
			    guint                  expire_time)
{
  IpPermAddress *perm_addr;
  IpPermData *data;
  char *lc_owner;

  LOWER_CASE_COPY_ON_STACK (lc_owner, owner);

#if DEBUG_IP_PERMISSION_TABLE
  g_message ("ip_permission_table_insert: %d.%d.%d.%d: %s: %s; expire=%d",
	     address->ipv4.ip_address[0],
	     address->ipv4.ip_address[1],
	     address->ipv4.ip_address[2],
	     address->ipv4.ip_address[3],
	     owner,
	     any_suffixed_domain ? "entire subtree" : "just that host",
	     expire_time);
#endif

  perm_addr = g_hash_table_lookup (table->sockaddr_to_perm_addr, address);
  if (perm_addr == NULL)
    {
      perm_addr = g_new (IpPermAddress, 1);
      perm_addr->sock_addr = g_object_ref (address);
      perm_addr->first_data = NULL;
      perm_addr->last_data = NULL;
      g_hash_table_insert (table->sockaddr_to_perm_addr, perm_addr->sock_addr, perm_addr);
#if DEBUG_IP_PERMISSION_TABLE
      g_message ("inserting perm_addr for %d.%d.%d.%d",
	     address->ipv4.ip_address[0],
	     address->ipv4.ip_address[1],
	     address->ipv4.ip_address[2],
	     address->ipv4.ip_address[3]);
#endif
    }

  for (data = perm_addr->first_data; data != NULL; data = data->next_data)
    if (strcmp (data->owner, lc_owner) == 0
     &&  (( any_suffixed_domain &&  data->any_suffixed_domain)
       || (!any_suffixed_domain && !data->any_suffixed_domain)))
      {
	/* Maybe lengthen expire time. */
	if (data->expire_time < expire_time)
	  {
	    g_tree_remove (table->by_expire_time, data);
	    data->expire_time = expire_time;
	    g_tree_insert (table->by_expire_time, data, data);
	  }
	return;
      }

  if (data == NULL)
    {
      /* Add a new data for this owner. */
      data = g_malloc (sizeof (IpPermData) + strlen (lc_owner) + 1);
      data->owner = strcpy ((char *) (data + 1), lc_owner);
      data->any_suffixed_domain = any_suffixed_domain;
      data->expire_time = expire_time;
      data->addr_info = perm_addr;
      data->prev_data = NULL;
      data->next_data = perm_addr->first_data;
      perm_addr->first_data = data;
      if (data->next_data != NULL)
	data->next_data->prev_data = data;
      else
	perm_addr->last_data = data;

      g_tree_insert (table->by_expire_time, data, data);
    }
}

static void
ip_permission_table_expire (IpPermissionTable *table,
			    guint              cur_time)
{
  IpPermData *data;

#if DEBUG_IP_PERMISSION_TABLE
  g_message ("ip_permission_table_expire: cur_time=%d", cur_time);
#endif

  while ((data = gsk_g_tree_min (table->by_expire_time)) != NULL)
    {
      if (data->expire_time > cur_time)
	break;

      /* remove data from lists */
      if (data->next_data == NULL)
	data->addr_info->last_data = data->prev_data;
      else
	data->next_data->prev_data = data->prev_data;
      if (data->prev_data == NULL)
	data->addr_info->first_data = data->next_data;
      else
	data->prev_data->next_data = data->next_data;

      /* and from the tree */
      g_tree_remove (table->by_expire_time, data);

      /* and if data->addr_info is now empty, delete it */
      if (data->addr_info->first_data == NULL)
	{
	  g_hash_table_remove (table->sockaddr_to_perm_addr,
			       data->addr_info->sock_addr);
          g_object_unref (data->addr_info->sock_addr);
	  g_free (data->addr_info);
	}
      g_free (data);
    }
}

static gboolean
ip_permission_table_check (IpPermissionTable     *table,
                           GskSocketAddressIpv4  *address,
                           const char            *owner,
                           guint                  cur_time)
{
  IpPermAddress *perm_addr;
  IpPermData *perm_data;
  char *lc_owner;
  const char *end_owner;

  LOWER_CASE_COPY_ON_STACK (lc_owner, owner);

  if (table->autoflush)
    ip_permission_table_expire (table, cur_time);
  end_owner = strchr (lc_owner, 0);
  perm_addr = g_hash_table_lookup (table->sockaddr_to_perm_addr, address);
  if (perm_addr == NULL)
    {
#if DEBUG_IP_PERMISSION_TABLE
      g_message ("perm_check: no list found for %d.%d.%d.%d; table_size=%d",
		 address->ipv4.ip_address[0],
		 address->ipv4.ip_address[1],
		 address->ipv4.ip_address[2],
		 address->ipv4.ip_address[3],
		 g_hash_table_size (table->sockaddr_to_perm_addr));
#endif
      return FALSE;
    }
  for (perm_data = perm_addr->first_data;
       perm_data != NULL;
       perm_data = perm_data->next_data)
    {
#if DEBUG_IP_PERMISSION_TABLE
      g_message ("perm_check: owner=%s; perm_data->owner=%s, any_suffixed_domain=%d, perm_data->expire_time=%d, cur_time=%d",
		 owner, perm_data->owner, perm_data->any_suffixed_domain, perm_data->expire_time, cur_time);
#endif
      if (strcmp (lc_owner, perm_data->owner) == 0
       && perm_data->expire_time >= cur_time)
	return TRUE;

      if (perm_data->any_suffixed_domain)
	{
	  int suffix_len = strlen (perm_data->owner);
	  if (end_owner - suffix_len - 1 >= lc_owner
	   && strcmp (end_owner - suffix_len, perm_data->owner) == 0
	   && end_owner[- suffix_len - 1] == '.'
	   && perm_data->expire_time >= cur_time)
	    return TRUE;
	}
    }
  return FALSE;
}

static void
destroy_perm_address (IpPermAddress *address)
{
  IpPermData *data = address->first_data;
  while (data != NULL)
    {
      IpPermData *next = data->next_data;
      g_free (data);
      data = next;
    }
  g_free (address);
}

static void
ip_permission_table_destroy (IpPermissionTable *table)
{
  g_hash_table_foreach (table->sockaddr_to_perm_addr,
			(GHFunc) destroy_perm_address,
			NULL);
  g_hash_table_destroy (table->sockaddr_to_perm_addr);
  g_tree_destroy (table->by_expire_time);
  g_free (table);
}

