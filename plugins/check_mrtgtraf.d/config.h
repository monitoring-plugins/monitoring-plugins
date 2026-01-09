#pragma once

#include "../../config.h"
#include "output.h"
#include "thresholds.h"
#include <stddef.h>
#include <stdlib.h>

typedef struct {
	char *log_file;
	int expire_minutes;
	bool use_average;

	mp_thresholds incoming_thresholds;
	mp_thresholds outgoing_thresholds;

	bool output_format_is_set;
	mp_output_format output_format;
} check_mrtgtraf_config;

check_mrtgtraf_config check_mrtgtraf_config_init() {
	check_mrtgtraf_config tmp = {
		.log_file = NULL,
		.expire_minutes = -1,
		.use_average = true,

		.incoming_thresholds = mp_thresholds_init(),
		.outgoing_thresholds = mp_thresholds_init(),

		.output_format_is_set = false,
	};
	return tmp;
}
