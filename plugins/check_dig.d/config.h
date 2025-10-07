#pragma once

#include "../../config.h"
#include <stddef.h>

#define UNDEFINED     0
#define DEFAULT_PORT  53
#define DEFAULT_TRIES 2

typedef struct {
	char *query_address;
	char *record_type;
	char *expected_address;
	char *dns_server;
	char *query_transport;
	int server_port;
	char *dig_args;
	int number_tries;

	double warning_interval;
	double critical_interval;
	char *require_flags;
	char *forbid_flags;
} check_dig_config;

check_dig_config check_dig_config_init() {
	check_dig_config tmp = {
		.query_address = NULL,
		.record_type = "A",
		.expected_address = NULL,
		.dns_server = NULL,
		.query_transport = "",
		.server_port = DEFAULT_PORT,
		.dig_args = "",
		.number_tries = DEFAULT_TRIES,

		.warning_interval = UNDEFINED,
		.critical_interval = UNDEFINED,
		.require_flags = NULL,
		.forbid_flags = NULL,

	};
	return tmp;
}
