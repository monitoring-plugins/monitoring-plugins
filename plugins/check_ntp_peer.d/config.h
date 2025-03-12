#pragma once

#include "../../config.h"
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

	char *owarn;
	char *ocrit;

	// stratum stuff
	bool do_stratum;
	char *swarn;
	char *scrit;

	// jitter stuff
	bool do_jitter;
	char *jwarn;
	char *jcrit;
} check_ntp_peer_config;

check_ntp_peer_config check_ntp_peer_config_init() {
	check_ntp_peer_config tmp = {
		.server_address = NULL,
		.port = DEFAULT_NTP_PORT,

		.quiet = false,
		.do_truechimers = false,
		.twarn = "0:",
		.tcrit = "0:",
		.owarn = "60",
		.ocrit = "120",
		.do_stratum = false,
		.swarn = "-1:16",
		.scrit = "-1:16",
		.do_jitter = false,
		.jwarn = "-1:5000",
		.jcrit = "-1:10000",
	};
	return tmp;
}
