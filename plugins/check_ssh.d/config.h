#pragma once

#include <stddef.h>
#include "../../lib/monitoringplug.h"

const int default_ssh_port = 22;

typedef struct check_ssh_config {
	int port;
	char *server_name;
	char *remote_version;
	char *remote_protocol;

	bool output_format_is_set;
	mp_output_format output_format;
} check_ssh_config;

check_ssh_config check_ssh_config_init(void) {
	check_ssh_config tmp = {
		.port = default_ssh_port,
		.server_name = NULL,
		.remote_version = NULL,
		.remote_protocol = NULL,

		.output_format_is_set = false,
	};

	return tmp;
}
