#pragma once

#include <stddef.h>

typedef struct check_ssh_config {
	int port;
	char *server_name;
	char *remote_version;
	char *remote_protocol;
} check_ssh_config;

check_ssh_config check_ssh_config_init(void) {
	check_ssh_config tmp = {
		.port = -1,
		.server_name = NULL,
		.remote_version = NULL,
		.remote_protocol = NULL,
	};

	return tmp;
}
