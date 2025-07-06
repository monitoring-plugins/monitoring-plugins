#pragma once

#include "../../config.h"
#include <stddef.h>

typedef struct {
	char *status_log;
	char *process_string;
	int expire_minutes;
} check_nagios_config;

check_nagios_config check_nagios_config_init() {
	check_nagios_config tmp = {
		.status_log = NULL,
		.process_string = NULL,
		.expire_minutes = 0,
	};
	return tmp;
}
