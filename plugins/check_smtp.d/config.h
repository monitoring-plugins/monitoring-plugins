#pragma once

#include "../../config.h"
#include "output.h"
#include "thresholds.h"
#include <stddef.h>
#include <string.h>

enum {
	SMTP_PORT = 25,
	SMTPS_PORT = 465
};

#define SMTP_EXPECT "220"

typedef struct {
	int server_port;
	char *server_address;
	char *localhostname;
	char *server_expect;
	bool ignore_send_quit_failure;

	mp_thresholds connection_time;

	bool use_ehlo;
	bool use_lhlo;

	char *from_arg;
	bool send_mail_from;

	unsigned long ncommands;
	char **commands;

	unsigned long nresponses;
	char **responses;

	char *authtype;
	char *authuser;
	char *authpass;

	bool use_proxy_prefix;
#ifdef HAVE_SSL
	bool check_cert;
	int days_till_exp_warn;
	int days_till_exp_crit;
	bool use_ssl;
	bool use_starttls;
	bool use_sni;
#endif

	bool output_format_is_set;
	mp_output_format output_format;
} check_smtp_config;

check_smtp_config check_smtp_config_init() {
	check_smtp_config tmp = {
		.server_port = SMTP_PORT,
		.server_address = NULL,
		.localhostname = NULL,

		.server_expect = SMTP_EXPECT,
		.ignore_send_quit_failure = false,

		.connection_time = mp_thresholds_init(),
		.use_ehlo = false,
		.use_lhlo = false,

		.from_arg = strdup(" "),
		.send_mail_from = false,

		.ncommands = 0,
		.commands = NULL,

		.nresponses = 0,
		.responses = NULL,

		.authtype = NULL,
		.authuser = NULL,
		.authpass = NULL,

		.use_proxy_prefix = false,
#ifdef HAVE_SSL
		.check_cert = false,
		.days_till_exp_warn = 0,
		.days_till_exp_crit = 0,
		.use_ssl = false,
		.use_starttls = false,
		.use_sni = false,
#endif

		.output_format_is_set = false,
	};
	return tmp;
}
