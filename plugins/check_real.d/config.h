#pragma once

#include "../../config.h"
#include <stddef.h>

enum {
	PORT = 554
};

typedef struct {
	char *server_address;
	char *host_name;
	int server_port;
	char *server_url;

	char *server_expect;
	int warning_time;
	bool check_warning_time;
	int critical_time;
	bool check_critical_time;
} check_real_config;

check_real_config check_real_config_init() {
	check_real_config tmp = {
		.server_address = NULL,
		.host_name = NULL,
		.server_port = PORT,
		.server_url = NULL,

		.server_expect = NULL,
		.warning_time = 0,
		.check_warning_time = false,
		.critical_time = 0,
		.check_critical_time = false,
	};
	return tmp;
}
