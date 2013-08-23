/*
    GSK - a library to write servers
    Copyright (C) 1999-2000 Dave Benson

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA

    Contact:
        daveb@ffem.org <Dave Benson>
*/

#include "../gskactorparser.h"
#include "../gskmainloop.h"
#include "../gsksocketlistener.h"
#include "../gskactorlistener.h"
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <ctype.h>

typedef struct _Group Group;
typedef struct _GskChatter GskChatter;
typedef GskActorParserClass GskChatterClass;

GskObjectClass* gsk_chatter_class_new();
#define GSK_CHATTER_CLASS   					\
	(gsk_chatter_class_new())
#define GSK_CHATTER(ptr)					\
	(GSK_OBJECT_CHECK_CAST(ptr, GskChatter, GSK_CHATTER_CLASS))

struct _GskChatter {
	GskActorParser		parser;
	char*			name;
	GList*			groups_subscribed;
	Group*			write_group;
};
	

struct _Group {
	char*			name;
	GList*			users_subscribed;
};

Group*   group_get_by_name(const char*);
void     group_delete(Group* group);
gboolean gsk_chatter_subscribe(GskChatter*, Group*);
gboolean gsk_chatter_unsubscribe(GskChatter*, Group*);
void     gsk_chatter_unsubscribe_all(GskChatter*);
void     gsk_chatter_write(GskChatter*, const char* fmt, ...);
void     group_broadcast(Group*, const char* fmt, ...);


static GHashTable* groups_by_name = 0;

/*
 * Find, perhaps creating, a group by name.
 * A name uniquely determines a group.
 */
Group* group_get_by_name(const char* name)
{
	Group* group;
	if (!groups_by_name)
		groups_by_name = g_hash_table_new(g_str_hash, g_str_equal);
	group = (Group*) g_hash_table_lookup(groups_by_name, name);
	if (!group) {
		group = g_new(Group, 1);
		group->name = g_strdup(name);
		group->users_subscribed = NULL;
		g_hash_table_insert(groups_by_name, group->name, group);
	}
	return group;
}

/*
 * Find a group.  Return NULL if it doesn't exist.
 */
Group* group_maybe_get_by_name(const char* name)
{
	if (!groups_by_name) return 0;
	return (Group*)g_hash_table_lookup(groups_by_name, name);
}

/*
 * Delete a group.  Unsubscribe everyone.
 */
void group_delete(Group* group)
{
	g_hash_table_remove(groups_by_name, group->name);
	while (group->users_subscribed) {
		gpointer usr = group->users_subscribed->data;
		gsk_chatter_unsubscribe((GskChatter*)usr, group);
	}
	g_free(group->name);
	g_free(group);
}

/*
 * Subscribe a user to a group.
 */
gboolean gsk_chatter_subscribe(GskChatter* user, Group* group)
{
	GList* list;
	list = g_list_find(user->groups_subscribed, group);
	user->write_group = group;
	if (list) return FALSE;
	group_broadcast(group, "SYSOP: %s has entered the group %s.\n",
			user->name, group->name);
	user->groups_subscribed =
				g_list_prepend(user->groups_subscribed, group);
	group->users_subscribed =
				g_list_prepend(group->users_subscribed, user);
	gsk_log_debug("gsk_chatter_subscribe: user %s joining group %s",
		user->name, group->name);
	return TRUE;
}

/*
 * Unsubscribe a user from a group.
 */
gboolean gsk_chatter_unsubscribe(GskChatter* user, Group* group)
{
	GList* list;
	list = g_list_find(user->groups_subscribed, group);
	if (!list) return FALSE;
	user->groups_subscribed = g_list_remove(user->groups_subscribed, group);
	group->users_subscribed = g_list_remove(group->users_subscribed, user);
	group_broadcast(group, "SYSOP: %s has left the group %s.\n",
			user->name, group->name);
	gsk_log_debug("gsk_chatter_unsubscribe: user %s leaving group %s",user->name,
		group->name);
	if (group->users_subscribed == NULL)
		group_delete(group);
	return TRUE;
}

/*
 * Unsubscribe a user from all groups.
 */
void gsk_chatter_unsubscribe_all(GskChatter* user)
{
	while (user->groups_subscribed) {
		gpointer grp = user->groups_subscribed->data;
		gsk_chatter_unsubscribe(user, (Group*)grp);
	}
}

/* 
 * Write to a user in a printf-like manner.
 */
void gsk_chatter_write(GskChatter* user, const char* fmt, ...)
{
	GskActorBuffered* buffered = (GskActorBuffered*)user;
	char buf[2048];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	buf[sizeof(buf)-1] = 0;
	if (buf[sizeof(buf)-3] != '\n') buf[sizeof(buf)-2] = '\n';
	va_end(args);
	gsk_buffer_append(&buffered->outgoing_data, buf, strlen(buf));
}

/* 
 * Write to a group in a printf-like manner.
 */
void group_broadcast(Group* group, const char* fmt, ...)
{
	char buf[2048];
	va_list args;
	GList* list;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	buf[sizeof(buf)-1] = 0;
	if (buf[sizeof(buf)-3] != '\n') buf[sizeof(buf)-2] = '\n';
	va_end(args);
	for (list = group->users_subscribed; list; list = list->next) {
		GskActorBuffered* buffered = (GskActorBuffered*)(list->data);
		gsk_buffer_append(&buffered->outgoing_data, buf, strlen(buf));
	}
}

#define SKIP_CHAR_PROPERTY(ptr, fct) while (*(ptr) && fct(*(ptr))) ptr++
#define SKIP_WS(ptr)                 SKIP_CHAR_PROPERTY(ptr, isspace)
#define SKIP_NON_WS(ptr)             SKIP_CHAR_PROPERTY(ptr, !isspace)

/*
 * Parse a /command.
 */
static gboolean parse_command(char* start_word, GskChatter* user)
{
	char* end_word;
	char* cmd_word;
	end_word = start_word;
	SKIP_NON_WS(end_word);
	cmd_word = (char*)alloca(end_word - start_word + 1);
	memcpy(cmd_word, start_word, end_word - start_word);
	cmd_word[end_word - start_word] = 0;
	if (strcasecmp(cmd_word, "join") == 0) {
		Group* group;
		start_word = end_word;
		SKIP_WS(start_word);
		end_word = start_word;
		SKIP_NON_WS(end_word);
		*end_word = 0;
		group = group_get_by_name(start_word);
		if (!gsk_chatter_subscribe(user, group)) {
			gsk_chatter_write(user, "simplechat> already "
				"a member of group %s\n", group->name);
		}
	} else if (strcasecmp(cmd_word, "unjoin") == 0) {
		Group* group;
		start_word = end_word;
		SKIP_WS(start_word);
		end_word = start_word;
		SKIP_NON_WS(end_word);
		*end_word = 0;
		group = group_maybe_get_by_name(start_word);
		if (!group) {
			gsk_chatter_write(user, "simplechat> group %s does "
				"not exist.\n", start_word);
		} else if (!gsk_chatter_unsubscribe(user, group)) {
			gsk_chatter_write(user, "simplechat> not "
				"a member of group %s\n", group->name);
		}
	} else if (strcasecmp(cmd_word, "quit") == 0
	       ||  strcasecmp(cmd_word, "q") == 0) {
		return FALSE;
	} else {
		gsk_chatter_write(user, "simplechat> unknown command %s\n",
			cmd_word);
	}
	return TRUE;
}

/*
 * Parse a line of text from a user.
 */
static gboolean gsk_chatter_parsed(GskActorParser* parser, void* parsed_data, int len)
{
	GskChatter* user = (GskChatter*)parser;
	char* cmd = (char*)parsed_data;
	{
		char* end = strchr(cmd, 0);
		while (end > cmd && isspace(end[-1]))
			end--;
		*end = 0;
	}
	if (user->name == 0) {
		user->name = cmd;
		gsk_chatter_subscribe(user, group_get_by_name("default"));
		return TRUE;
	}
	if (cmd[0] == '/') {
		char* start_word = cmd + 1;
		SKIP_WS(start_word);
		return parse_command(start_word, user);
	} else {
		group_broadcast(user->write_group,
		                "%s@%s> %s\n",
				user->name, user->write_group->name, cmd);
	}
	return TRUE;
}

/*
 * Callback to destroy GskChatter-specific data.
 */
static void gsk_chatter_destruct(GskObject* object)
{
	GskChatter* user = GSK_CHATTER(object);
	gsk_chatter_unsubscribe_all(user);
	g_free(user->name);
	object->klass->parent->destruct(object);
}

static void gsk_chatter_construct(GskObject* object)
{
	GskChatter* chatter = (GskChatter*)object;
	GSK_ACTOR_PARSER_CLASS->construct(object);
	chatter->name = NULL;
	chatter->groups_subscribed = NULL;
	chatter->write_group = NULL;
}

static void gsk_chatter_construct_class(GskObjectClass* klass)
{
	GskActorParserClass* p_class = (GskActorParserClass*)klass;
	GskObjectClass* parent = GSK_ACTOR_PARSER_CLASS;
	parent->construct_class(klass);
	klass->construct_class = gsk_chatter_construct_class;
	klass->construct = gsk_chatter_construct;
	p_class->on_parse = gsk_chatter_parsed;
	klass->class_name = "GskChatter";
	klass->destruct = gsk_chatter_destruct;
	klass->size = sizeof(GskChatter);
	klass->parent = parent;
}

/* 
 * Create a GskChatter (return it cast to an GskActor).
 */
static GskActor* gsk_chatter_new(GskSocket* client)
{
	GskActor* chatter;
	g_return_val_if_fail(client != NULL, NULL);

	chatter = (GskActor*) gsk_object_new_generic(GSK_CHATTER_CLASS);
	g_return_val_if_fail(chatter != NULL, NULL);

	gsk_actor_buffered_set_socket(GSK_ACTOR_BUFFERED(chatter), client);
	return (GskActor*)chatter;
}

GskObjectClass* gsk_chatter_class_new()
{
	static gboolean inited = FALSE;
	static GskChatterClass klass;
	GskObjectClass* obj_class = (GskObjectClass*)&klass;
	if (!inited) {
		inited = TRUE;
		obj_class->construct_class = gsk_chatter_construct_class;
		obj_class->construct_class(obj_class);
		gsk_object_class_register(obj_class);
	}
	return obj_class;
}


/* 
 * Do the accept for a client.
 */
static gboolean gsk_chatter_accept(GskActorListener* listener,
                                   GskMainLoop* mainloop,
				   GskSocket* client,
		                   GskSocketAddress* addr)
{
	GskActor* actor = gsk_chatter_new(client);
	gsk_main_loop_add_actor(mainloop, actor);
	return TRUE;
}

#define GSK_CHATTER_LISTENER_CLASS (gsk_chatter_listener_class_new())

static void gsk_chatter_listener_construct_class(GskObjectClass* klass)
{
	GskActorListenerClass* lis_class = (GskActorListenerClass*)klass;
	GskObjectClass* parent = GSK_ACTOR_LISTENER_CLASS;
	parent->construct_class(klass);
	klass->parent = parent;
	klass->class_name = "GskChatterListener";
	//klass->size = sizeof(GskActorListener);
	lis_class->on_accept = gsk_chatter_accept;
}

GskObjectClass* gsk_chatter_listener_class_new()
{
	static gboolean inited = FALSE;
	static GskActorListenerClass klass;
	GskObjectClass* obj_class = (GskObjectClass*)(&klass);
	if (!inited) {
		inited = TRUE;
		obj_class->construct_class =
					gsk_chatter_listener_construct_class;
		obj_class->construct_class(obj_class);
		gsk_object_class_register(obj_class);
	}
	return obj_class;
}



/*
 * Add a listener for user connections to the mainloop.
 */
static void add_gsk_chatter_listener(GskMainLoop* mainloop,
                              GskSocketListener* socket_listener)
{
	GskObject* lis = gsk_object_new_generic(GSK_CHATTER_LISTENER_CLASS);
	g_return_if_fail(lis != NULL);
	gsk_actor_listener_set_listener(GSK_ACTOR_LISTENER(lis),
	                                socket_listener);
	gsk_main_loop_add_actor(mainloop, GSK_ACTOR(lis));
}

/*
 * Add a listener for user connections to the mainloop just by port number.
 */
static void add_port(GskMainLoop* mainloop, int port)
{
	GskSocketListener* lis;
	GskObject* obj;

	obj = gsk_object_new_generic(GSK_SOCKET_LISTENER_CLASS);
	lis = (GskSocketListener*)obj;
	gsk_socket_listener_set_local_port(lis, port);

	if (!lis) {
		gsk_log_err("error binding to port %d", port);
		return;
	}
	add_gsk_chatter_listener(mainloop, lis);
}

static gboolean handle_sig_int(gpointer ptr)
{
	g_message("sigint detected");
	return TRUE;
}

/*
 * Main.
 *   1.  add the listening ports.
 *   2.  run the mainloop.
 */
int main(int argc, char** argv)
{
	int i;
	GskMainLoop* mainloop = gsk_main_loop_new(0);
	for (i = 1; i < argc; i++) {
		int port = atoi(argv[i]);
		if (port <= 0) {
			fprintf(stderr, "error parsing port number [%s]\n",
				argv[i]);
			exit(1);
		}
		add_port(mainloop, port);
	}

	gsk_main_loop_add_signal_handler(mainloop, SIGINT, handle_sig_int, 0);
					

	if (gsk_main_loop_is_empty(mainloop)) {
		gsk_log_err("no port to listen on: nothing to do");
		return 1;
	}

	while (gsk_main_loop_run_once(mainloop))
		;
	gsk_main_loop_delete(mainloop);
	return 0;
}
