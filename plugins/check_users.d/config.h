#pragma once

#include "output.h"
#include "thresholds.h"

typedef struct check_users_config {
	mp_thresholds thresholds;

	bool output_format_is_set;
	mp_output_format output_format;
} check_users_config;

check_users_config check_users_config_init() {
	check_users_config tmp = {
		.thresholds = mp_thresholds_init(),

		.output_format_is_set = false,
	};
	return tmp;
}
