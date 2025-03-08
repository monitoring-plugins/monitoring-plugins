#pragma once

#include "../../config.h"
#include "thresholds.h"
#include <stddef.h>

#define ADDRESS_LENGTH 256

typedef struct {
	bool all_match;
	char dns_server[ADDRESS_LENGTH];
	char query_address[ADDRESS_LENGTH];
	bool expect_nxdomain;
	bool expect_authority;
	char **expected_address;
	size_t expected_address_cnt;

	thresholds *time_thresholds;
} check_dns_config;

check_dns_config check_dns_config_init() {
	check_dns_config tmp = {
		.all_match = false,
		.dns_server = "",
		.query_address = "",
		.expect_nxdomain = false,
		.expect_authority = false,
		.expected_address = NULL,
		.expected_address_cnt = 0,

		.time_thresholds = NULL,
	};
	return tmp;
}
