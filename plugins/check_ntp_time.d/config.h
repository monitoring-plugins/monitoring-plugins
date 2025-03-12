#pragma once

#include "../../config.h"
#include "thresholds.h"
#include <stddef.h>

typedef struct {
	char *server_address;
	char *port;

	bool quiet;
	int time_offset;

	thresholds *offset_thresholds;
} check_ntp_time_config;

check_ntp_time_config check_ntp_time_config_init() {
	check_ntp_time_config tmp = {
		.server_address = NULL,
		.port = "123",

		.quiet = false,
		.time_offset = 0,

		.offset_thresholds = NULL,
	};
	return tmp;
}
