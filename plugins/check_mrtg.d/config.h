#pragma once

#include "../../config.h"
#include "output.h"
#include "thresholds.h"
#include <stddef.h>
#include <stdlib.h>

typedef struct {
	bool use_average;
	int variable_number;
	int expire_minutes;
	char *label;
	char *units;
	char *log_file;

	mp_thresholds values_threshold;

	bool output_format_is_set;
	mp_output_format output_format;
} check_mrtg_config;

check_mrtg_config check_mrtg_config_init() {
	check_mrtg_config tmp = {
		.use_average = true,
		.variable_number = -1,
		.expire_minutes = 0,
		.label = NULL,
		.units = NULL,
		.log_file = NULL,

		.values_threshold = mp_thresholds_init(),

		.output_format_is_set = false,
	};
	return tmp;
}
