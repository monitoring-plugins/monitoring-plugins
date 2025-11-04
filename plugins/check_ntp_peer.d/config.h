#pragma once

#include "../../config.h"
#include "perfdata.h"
#include "thresholds.h"
#include <stddef.h>

enum {
	DEFAULT_NTP_PORT = 123,
};

typedef struct {
	char *server_address;
	int port;

	bool quiet;

	// truechimer stuff
	bool do_truechimers;
	mp_thresholds truechimer_thresholds;

	// offset thresholds
	mp_thresholds offset_thresholds;

	// stratum stuff
	bool do_stratum;
	mp_thresholds stratum_thresholds;

	// jitter stuff
	bool do_jitter;
	mp_thresholds jitter_thresholds;
} check_ntp_peer_config;

check_ntp_peer_config check_ntp_peer_config_init() {
	check_ntp_peer_config tmp = {
		.server_address = NULL,
		.port = DEFAULT_NTP_PORT,

		.quiet = false,
		.do_truechimers = false,
		.truechimer_thresholds = mp_thresholds_init(),

		.offset_thresholds = mp_thresholds_init(),

		.do_stratum = false,
		.stratum_thresholds = mp_thresholds_init(),

		.do_jitter = false,
		.jitter_thresholds = mp_thresholds_init(),
	};

	mp_range stratum_default = mp_range_init();
	stratum_default = mp_range_set_start(stratum_default, mp_create_pd_value(-1));
	stratum_default = mp_range_set_end(stratum_default, mp_create_pd_value(16));
	tmp.stratum_thresholds = mp_thresholds_set_warn(tmp.stratum_thresholds, stratum_default);
	tmp.stratum_thresholds = mp_thresholds_set_crit(tmp.stratum_thresholds, stratum_default);

	mp_range jitter_w_default = mp_range_init();
	jitter_w_default = mp_range_set_start(jitter_w_default, mp_create_pd_value(-1));
	jitter_w_default = mp_range_set_end(jitter_w_default, mp_create_pd_value(5000));
	tmp.jitter_thresholds = mp_thresholds_set_warn(tmp.jitter_thresholds, jitter_w_default);

	mp_range jitter_c_default = mp_range_init();
	jitter_c_default = mp_range_set_start(jitter_c_default, mp_create_pd_value(-1));
	jitter_c_default = mp_range_set_end(jitter_c_default, mp_create_pd_value(10000));
	tmp.jitter_thresholds = mp_thresholds_set_crit(tmp.jitter_thresholds, jitter_c_default);

	mp_range offset_w_default = mp_range_init();
	offset_w_default = mp_range_set_start(offset_w_default, mp_create_pd_value(60));
	tmp.offset_thresholds = mp_thresholds_set_warn(tmp.offset_thresholds, offset_w_default);
	mp_range offset_c_default = mp_range_init();
	offset_c_default = mp_range_set_start(offset_c_default, mp_create_pd_value(120));
	tmp.offset_thresholds = mp_thresholds_set_crit(tmp.offset_thresholds, offset_c_default);
	return tmp;
}
