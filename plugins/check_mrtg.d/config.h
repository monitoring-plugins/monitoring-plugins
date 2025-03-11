#pragma once

#include "../../config.h"
#include <stddef.h>
#include <stdlib.h>

typedef struct {
	bool use_average;
	int variable_number;
	int expire_minutes;
	char *label;
	char *units;
	char *log_file;

	bool value_warning_threshold_set;
	unsigned long value_warning_threshold;
	bool value_critical_threshold_set;
	unsigned long value_critical_threshold;
} check_mrtg_config;

check_mrtg_config check_mrtg_config_init() {
	check_mrtg_config tmp = {
		.use_average = true,
		.variable_number = -1,
		.expire_minutes = 0,
		.label = NULL,
		.units = NULL,
		.log_file = NULL,

		.value_warning_threshold_set = false,
		.value_warning_threshold = 0,
		.value_critical_threshold_set = false,
		.value_critical_threshold = 0,
	};
	return tmp;
}
