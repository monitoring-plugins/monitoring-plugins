#pragma once

#include "../../config.h"
#include "output.h"
#include "thresholds.h"
#include <stddef.h>
#include <time.h>

/* Time in microseconds to delay between polling to avoid a blocking response. */
const struct timespec default_polling_delay = {.tv_nsec = 500000000L, .tv_sec = 0};
const long MAX_POLL = 5000000000L; // nanoseconds, 5 seconds

typedef struct {
	char *server_address;
	char *port;

	bool quiet;
	int time_offset;

	mp_thresholds offset_thresholds;

	bool output_format_is_set;
	struct timespec poll_delay;
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
		.poll_delay = default_polling_delay,
	};

	mp_range warning = mp_range_init();
	warning = mp_range_set_end(warning, mp_create_pd_value(60));
	tmp.offset_thresholds = mp_thresholds_set_warn(tmp.offset_thresholds, warning);

	mp_range critical = mp_range_init();
	critical = mp_range_set_end(warning, mp_create_pd_value(120));
	tmp.offset_thresholds = mp_thresholds_set_crit(tmp.offset_thresholds, critical);

	return tmp;
}
