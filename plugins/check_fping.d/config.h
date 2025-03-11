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
	};
	return tmp;
}
