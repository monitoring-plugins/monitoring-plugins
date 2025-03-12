#pragma once

#include "../../config.h"
#include <stddef.h>

enum {
	TIME_PORT = 37
};

typedef struct {
	char *server_address;
	int server_port;
	bool use_udp;

	int warning_time;
	bool check_warning_time;
	int critical_time;
	bool check_critical_time;
	unsigned long warning_diff;
	bool check_warning_diff;
	unsigned long critical_diff;
	bool check_critical_diff;
} check_time_config;

check_time_config check_time_config_init() {
	check_time_config tmp = {
		.server_address = NULL,
		.server_port = TIME_PORT,
		.use_udp = false,

		.warning_time = 0,
		.check_warning_time = false,
		.critical_time = 0,
		.check_critical_time = false,

		.warning_diff = 0,
		.check_warning_diff = false,
		.critical_diff = 0,
		.check_critical_diff = false,
	};
	return tmp;
}
