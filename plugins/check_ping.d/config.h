#pragma once

#include "../../config.h"
#include <stddef.h>
#include <stdlib.h>

enum {
	UNKNOWN_PACKET_LOSS = 200, /* 200% */
	DEFAULT_MAX_PACKETS = 5    /* default no. of ICMP ECHO packets */
};

#define UNKNOWN_TRIP_TIME -1.0 /* -1 seconds */

#define MAX_ADDR_START 1

typedef struct {
	bool display_html;
	int max_packets;

	char **addresses;
	size_t n_addresses;

	int wpl;
	int cpl;
	double wrta;
	double crta;
} check_ping_config;

check_ping_config check_ping_config_init() {
	check_ping_config tmp = {
		.display_html = false,
		.max_packets = -1,

		.addresses = NULL,
		.n_addresses = 0,

		.wpl = UNKNOWN_PACKET_LOSS,
		.cpl = UNKNOWN_PACKET_LOSS,
		.wrta = UNKNOWN_TRIP_TIME,
		.crta = UNKNOWN_TRIP_TIME,
	};

	tmp.addresses = calloc(MAX_ADDR_START, sizeof(char *));
	tmp.addresses[0] = NULL;
	return tmp;
}
