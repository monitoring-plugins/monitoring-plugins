#pragma once

#include "../../config.h"
#include "output.h"
#include "thresholds.h"
#include <stddef.h>

enum {
	PORT = 554
};

const char *default_expect = "RTSP/1.";

typedef struct {
	char *server_address;
	char *host_name;
	int server_port;
	char *server_url;

	char *server_expect;

	mp_thresholds time_thresholds;

	bool output_format_is_set;
	mp_output_format output_format;
} check_real_config;

check_real_config check_real_config_init() {
	check_real_config tmp = {
		.server_address = NULL,
		.host_name = NULL,
		.server_port = PORT,
		.server_url = NULL,

		.server_expect = default_expect,

		.time_thresholds = mp_thresholds_init(),

		.output_format_is_set = false,
	};
	return tmp;
}
