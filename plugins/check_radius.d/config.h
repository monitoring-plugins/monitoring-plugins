#pragma once

#include "../../config.h"
#include "output.h"
#include <stddef.h>
#if defined(HAVE_LIBRADCLI)
#	include <radcli/radcli.h>
#elif defined(HAVE_LIBFREERADIUS_CLIENT)
#	include <freeradius-client.h>
#elif defined(HAVE_LIBRADIUSCLIENT_NG)
#	include <radiusclient-ng.h>
#else
#	include <radiusclient.h>
#endif

typedef struct {
	char *server;
	char *username;
	char *password;
	char *config_file;
	char *nas_id;
	char *nas_ip_address;
	int retries;
	unsigned short port;

	char *expect;

	bool output_format_is_set;
	mp_output_format output_format;
} check_radius_config;

check_radius_config check_radius_config_init() {
	check_radius_config tmp = {
		.server = NULL,
		.username = NULL,
		.password = NULL,
		.config_file = NULL,
		.nas_id = NULL,
		.nas_ip_address = NULL,
		.retries = 1,
		.port = PW_AUTH_UDP_PORT,

		.expect = NULL,

		.output_format_is_set = false,
	};
	return tmp;
}
