#pragma once

#include "../../config.h"
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
	char *twarn;
	char *tcrit;
	thresholds *truechimer_thresholds;

	char *owarn;
	char *ocrit;
	thresholds *offset_thresholds;

	// stratum stuff
	bool do_stratum;
	char *swarn;
	char *scrit;
	thresholds *stratum_thresholds;

	// jitter stuff
	bool do_jitter;
	char *jwarn;
	char *jcrit;
	thresholds *jitter_thresholds;

} check_ntp_peer_config;

check_ntp_peer_config check_ntp_peer_config_init() {
	check_ntp_peer_config tmp = {
		.server_address = NULL,
		.port = DEFAULT_NTP_PORT,

		.quiet = false,
		.do_truechimers = false,
		.twarn = "0:",
		.tcrit = "0:",
		.truechimer_thresholds = NULL,

		.owarn = "60",
		.ocrit = "120",
		.offset_thresholds = NULL,

		.do_stratum = false,
		.swarn = "-1:16",
		.scrit = "-1:16",
		.stratum_thresholds = NULL,

		.do_jitter = false,
		.jwarn = "-1:5000",
		.jcrit = "-1:10000",
		.jitter_thresholds = NULL,
	};
	return tmp;
}
