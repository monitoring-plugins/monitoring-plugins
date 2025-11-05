#pragma once

#include "../../config.h"
#include "output.h"
#include "thresholds.h"
#include <stddef.h>

typedef struct {
	char *server_address;
	char *port;

	bool quiet;
	int time_offset;

	mp_thresholds offset_thresholds;

	bool output_format_is_set;
	mp_output_format output_format;
} check_ntp_time_config;

check_ntp_time_config check_ntp_time_config_init() {
	check_ntp_time_config tmp = {
		.server_address = NULL,
		.port = "123",

		.quiet = false,
		.time_offset = 0,

		.offset_thresholds = mp_thresholds_init(),

		.output_format_is_set = false,
	};

	mp_range warning = mp_range_init();
	warning = mp_range_set_end(warning, mp_create_pd_value(60));
	tmp.offset_thresholds = mp_thresholds_set_warn(tmp.offset_thresholds, warning);

	mp_range critical = mp_range_init();
	critical = mp_range_set_end(warning, mp_create_pd_value(120));
	tmp.offset_thresholds = mp_thresholds_set_crit(tmp.offset_thresholds, critical);

	return tmp;
}
