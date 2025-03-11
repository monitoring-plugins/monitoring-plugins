#pragma once

#include "../../config.h"
#include <stddef.h>
#include <stdlib.h>

typedef struct {
	char *log_file;
	int expire_minutes;
	bool use_average;
	unsigned long incoming_warning_threshold;
	unsigned long incoming_critical_threshold;
	unsigned long outgoing_warning_threshold;
	unsigned long outgoing_critical_threshold;

} check_mrtgtraf_config;

check_mrtgtraf_config check_mrtgtraf_config_init() {
	check_mrtgtraf_config tmp = {
		.log_file = NULL,
		.expire_minutes = -1,
		.use_average = true,

		.incoming_warning_threshold = 0,
		.incoming_critical_threshold = 0,
		.outgoing_warning_threshold = 0,
		.outgoing_critical_threshold = 0,
	};
	return tmp;
}
