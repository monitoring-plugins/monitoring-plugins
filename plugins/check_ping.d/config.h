#pragma once

#include "../../config.h"
#include <stddef.h>
#include <stdlib.h>

enum {
	UNKNOWN_PACKET_LOSS = 200, /* 200% */
	DEFAULT_MAX_PACKETS = 5    /* default no. of ICMP ECHO packets */
};

#define UNKNOWN_TRIP_TIME -1.0 /* -1 seconds */

typedef struct {
	int max_packets;

	char *address;

	int wpl;
	int cpl;
	double wrta;
	double crta;
} check_ping_config;

check_ping_config check_ping_config_init() {
	check_ping_config tmp = {
		.max_packets = -1,

		.address = NULL,

		.wpl = UNKNOWN_PACKET_LOSS,
		.cpl = UNKNOWN_PACKET_LOSS,
		.wrta = UNKNOWN_TRIP_TIME,
		.crta = UNKNOWN_TRIP_TIME,
	};

	tmp.address = NULL;
	return tmp;
}
