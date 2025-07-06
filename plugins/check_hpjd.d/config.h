#pragma once

#include "../../config.h"
#include <stddef.h>
#include <stdlib.h>

#define DEFAULT_PORT "161"

typedef struct {
	char *address;
	char *community;
	unsigned int port;
	bool check_paper_out;

} check_hpjd_config;

check_hpjd_config check_hpjd_config_init() {
	check_hpjd_config tmp = {
		.address = NULL,
		.community = NULL,
		.port = (unsigned int)atoi(DEFAULT_PORT),
		.check_paper_out = true,
	};
	return tmp;
}
