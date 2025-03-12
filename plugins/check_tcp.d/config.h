#pragma once

#include "../../lib/utils_tcp.h"
#include "output.h"
#include <netinet/in.h>

typedef struct {
	char *server_address;
	bool host_specified;
	int server_port; // TODO can this be a uint16?

	int protocol; /* most common is default */
	char *service;
	char *send;
	char *quit;
	char **server_expect;
	size_t server_expect_count;
#ifdef HAVE_SSL
	bool use_tls;
	char *sni;
	bool sni_specified;
	bool check_cert;
	int days_till_exp_warn;
	int days_till_exp_crit;
#endif // HAVE_SSL
	int match_flags;
	int expect_mismatch_state;
	unsigned int delay;

	bool warning_time_set;
	double warning_time;
	bool critical_time_set;
	double critical_time;

	int econn_refuse_state;

	ssize_t maxbytes;

	bool hide_output;

	bool output_format_set;
	mp_output_format output_format;
} check_tcp_config;

check_tcp_config check_tcp_config_init() {
	check_tcp_config result = {
		.server_address = "127.0.0.1",
		.host_specified = false,
		.server_port = 0,

		.protocol = IPPROTO_TCP,
		.service = "TCP",
		.send = NULL,
		.quit = NULL,
		.server_expect = NULL,
		.server_expect_count = 0,
#ifdef HAVE_SSL
		.use_tls = false,
		.sni = NULL,
		.sni_specified = false,
		.check_cert = false,
		.days_till_exp_warn = 0,
		.days_till_exp_crit = 0,
#endif // HAVE_SSL
		.match_flags = NP_MATCH_EXACT,
		.expect_mismatch_state = STATE_WARNING,
		.delay = 0,

		.warning_time_set = false,
		.warning_time = 0,
		.critical_time_set = false,
		.critical_time = 0,

		.econn_refuse_state = STATE_CRITICAL,

		.maxbytes = 0,

		.hide_output = false,

		.output_format_set = false,
	};
	return result;
}
