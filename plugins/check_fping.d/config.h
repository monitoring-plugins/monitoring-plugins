#pragma once

#include "../../config.h"
#include <stddef.h>

enum {
	PACKET_SIZE = 56,
	PACKET_COUNT = 1,
};

typedef struct {
	char *server_name;
	char *sourceip;
	char *sourceif;
	int packet_size;
	int packet_count;
	int target_timeout;
	int packet_interval;
	bool randomize_packet_data;
	bool dontfrag;
	bool alive_p;

	double crta;
	bool crta_p;
	double wrta;
	bool wrta_p;

	int cpl;
	bool cpl_p;
	int wpl;
	bool wpl_p;

	// only available with fping version >= 5.2
	// for a given uint _fwmark_ fping sets _fwmark_ as a firewall mark
	// in the packets
	unsigned int fwmark;
	bool fwmark_set;

	// only available with fping version >= 5.3
	// Setting icmp_timestamp tells fping to use ICMP Timestamp (ICMP type 13) instead
	// of ICMP Echo
	bool icmp_timestamp;

	// Setting check_source lets fping  discard replies which are not from the target address
	bool check_source;
} check_fping_config;

check_fping_config check_fping_config_init() {
	check_fping_config tmp = {
		.server_name = NULL,
		.sourceip = NULL,
		.sourceif = NULL,
		.packet_size = PACKET_SIZE,
		.packet_count = PACKET_COUNT,
		.target_timeout = 0,
		.packet_interval = 0,
		.randomize_packet_data = false,
		.dontfrag = false,
		.alive_p = false,

		.crta = 0,
		.crta_p = false,
		.wrta = 0,
		.wrta_p = false,

		.cpl = 0,
		.cpl_p = false,
		.wpl = 0,
		.wpl_p = false,

		// only available with fping version >= 5.2
		.fwmark = 0,
		.fwmark_set = false, // just to be deterministic

		// only available with fping version >= 5.3
		.icmp_timestamp = false,
		.check_source = false,

	};
	return tmp;
}
