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

#include "../protocols/gsksimpleproxylistener.h"
#include <stdio.h>
#include <ctype.h>



static void
usage(const char* program_name)
{
	fprintf(stderr, "usage: %s local_port:remote_host:remote_port ...\n"
			"\n"
			"Proxies from local_port to remote_host:remote_port.\n"
			"You may specify any number of ports to proxy.\n",
			program_name);
	exit(1);
}


#define parse_test(cond, text)						\
	do {								\
		if (!(cond)) {						\
			fprintf(stderr, "parse error: %s.\n"		\
					"(because assertion %s failed)\n",\
					text, #cond);			\
			return FALSE;					\
		}							\
	} while (0)

static gboolean
parse_specification(GskMainLoop* mainloop, const char* spec)
{
	const char* colon1;
	const char* colon2;
	char* endp;
	int local_port;
	GskSocketAddress address;
	GskActor* actor;
	colon1 = strchr(spec, ':');
	if (!colon1) { fprintf(stderr, "expected a :\n"); return FALSE; }
	parse_test(colon1 != NULL, "expected a `:'");
	colon2 = strchr(colon1 + 1, ':');
	parse_test(colon2 != NULL, "expected a second `:'");
	parse_test(isdigit(spec[0]), "first param wasn't a number");
	parse_test(isdigit(colon2[1]), "second param wasn't a number");
	local_port = (int)strtol(spec, &endp, 10);
	parse_test(*endp == ':', "error parsing local port number");
	if (!gsk_socket_address_lookup(&address, colon1 + 1, -1)) {
		fprintf(stderr, "error doing name lookup.\n");
		return FALSE;
	}
	actor = gsk_simple_proxy_listener_new(local_port, &address);
	if (!actor) {
		fprintf(stderr, "error listening on port %d\n",local_port);
		return FALSE;
	}
	gsk_main_loop_add_actor(mainloop, actor);
	return TRUE;
}

int main(int argc, char** argv)
{
	int i;
	GskMainLoop* mainloop = gsk_main_loop_new(0);
	//g_on_error_stack_trace(argv[0]);

	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			if (strcmp(argv[i], "-h") == 0
			 || strcmp(argv[i], "-help") == 0
			 || strcmp(argv[i], "--help") == 0) {
			 	usage(argv[0]);
			}
			fprintf(stderr, "unknown option %s\n", argv[i]);
			continue;
		}
		if (!parse_specification(mainloop, argv[i])) {
			fprintf(stderr, "couldn't parse spec `%s'\n",argv[i]);
			return 1;
		}
	}
	if (gsk_main_loop_is_empty(mainloop)) {
		gsk_log_err("no port to listen on: nothing to do");
		usage(argv[0]);
	}
	while (gsk_main_loop_run_once(mainloop))
		;
	gsk_main_loop_delete(mainloop);
	return 0;
}
