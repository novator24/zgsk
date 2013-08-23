#include "protocols/gskhttpserver.h"

typedef struct _MimeTypeConfig {
	GHashTable*		extension_to_mimetype;
} MimeTypeConfig;

typedef struct _VirtualHostConfig {
	const char*		document_root;
	const char*		user_home;
	const char*		
} VirtualHostConfig;

typedef struct _ServerConfig {
	GHashTable*		per_host_config;
} ServerConfig;



struct _SimpleServer {
	GskHttpServer		http_server;
	ServerConfig*		server_configuration;
};


