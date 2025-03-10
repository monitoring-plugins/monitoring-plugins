#pragma once

#include "../../config.h"
#include "../../lib/thresholds.h"
#include <stddef.h>

enum {
	CHECK_SERVICES = 1,
	CHECK_HOSTS = 2
};

typedef struct {
	char *data_vals;
	thresholds *thresholds;
	int check_type;
	char *label;
} check_cluster_config;

check_cluster_config check_cluster_config_init() {
	check_cluster_config tmp = {
		.data_vals = NULL,
		.thresholds = NULL,
		.check_type = CHECK_SERVICES,
		.label = NULL,
	};
	return tmp;
}
