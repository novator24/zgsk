#include "gskactorparser.h"

typedef struct _DistWhoisClient DistWhoisClient;

GskObjectClass* dist_whois_client_class_new();
#define DIST_WHOIS_CLIENT_CLASS   				\
	(dist_whois_class_new())
#define DIST_WHOIS_CLIENT(ptr)					\
	(GSK_OBJECT_CHECK_CAST(ptr, DistWhoisClient, DIST_WHOIS_CLIENT))

struct _DistWhoisClient {
	GskActorParser parser;

	char* pending;
};

static void dist_whois_client_construct(GskObject* object)
{
	DistWhoisClient* chatter = (DistWhoisClient*)object;
	GSK_ACTOR_PARSER_CLASS->construct(object);
	chatter->name = NULL;
	chatter->groups_subscribed = NULL;
	chatter->write_group = NULL;
}

static void dist_whois_client_construct_class(GskObjectClass* klass)
{
	GskActorParserClass* p_class = (GskActorParserClass*)klass;
	GskObjectClass* parent = GSK_ACTOR_PARSER_CLASS;
	parent->construct_class(klass);
	klass->construct_class = dist_whois_client_construct_class;
	klass->construct = dist_whois_client_construct;
	p_class->on_parse = dist_whois_client_parsed;
	klass->class_name = "DistWhoisClient";
	klass->destruct = dist_whois_client_destruct;
	klass->size = sizeof(DistWhoisClient);
	klass->parent = parent;
}

static gboolean transition_based_on_first_char(DistWhoisClient* client,
                                               char c)
{
	switch (c) {
		case ':': *s = DIST_WHOIS_CLIENT_NEED_LENGTH; return TRUE;
		case '!':
			dist_whois_db_resolve_bad(client->db, client->pending);
			client->pending = NULL;
			*s = DIST_WHOIS_READY;
			return TRUE;
		case '?':
			dist_whois_db_resolve_failed(client->db,
			                             client->pending);
			client->pending = NULL;
			*s = DIST_WHOIS_READY;
			return FALSE;	/* stop if they aren't working */
		default:
			g_warning("bad character 0x%02x from client",
				(guint8)c);
	}
	return FALSE;
}

/*** NOTE: NOT SAFE WITHOUT DEBUGGING ASSERTS!! (BAD CLIENTS MAY TRIP THESE) */
static gboolean dist_whois_client_parsed(GskActorParser* parser,
                                         void* parsed_data,
					 int len)
{
	DistWhoisClient* client = (DistWhoisClient*)parser;
	g_return_val_if_fail(client->state != DIST_WHOIS_CLIENT_READY);
	switch (client->state) {
		case DIST_WHOIS_CLIENT_READY:
			g_assert_not_reached();
		case DIST_WHOIS_CLIENT_PENDING: {
			g_return_val_if_fail(client->pending != NULL,
			                     FALSE);
			g_return_val_if_fail(strcmp(client->pending,
			                            (char*)parsed_data+1) != 0,
				             FALSE);
			if (!transition_based_on_first_char(
						client, *(char*)parsed_data,
					     )
					)
				return FALSE;
		}
		case DIST_WHOIS_CLIENT_NEED_LENGTH: {
			char* len_as_str = (char*)parsed_data;
			int len = strtol(len_as_str, &endnum, 10);
			...
		}
	}

	return TRUE;
}


GskObjectClass* dist_whois_client_class_new()
{
	static gboolean inited = FALSE;
	static DistWhoisClientClass klass;
	GskObjectClass* obj_class = (GskObjectClass*)&klass;
	if (!inited) {
		inited = TRUE;
		obj_class->construct_class = dist_whois_client_construct_class;
		obj_class->construct_class(obj_class);
		gsk_object_class_register(obj_class);
	}
	return obj_class;
}

