#pragma once

#include "../../config.h"
#include <stddef.h>
#include "../../lib/monitoringplug.h"

typedef enum {
	METRIC_CONN_TIME,
	METRIC_SERVER_VERSION,
	METRIC_QUERY_RESULT,
	METRIC_QUERY_TIME,
} mp_dbi_metric;

typedef enum {
	TYPE_NUMERIC,
	TYPE_STRING,
} mp_dbi_type;

typedef struct {
	char *key;
	char *value;
} driver_option_t;

typedef struct {
	char *dbi_driver;
	char *host;
	driver_option_t *dbi_options;
	size_t dbi_options_num;
	char *dbi_database;
	char *dbi_query;

	char *expect;
	char *expect_re_str;
	int expect_re_cflags;
	mp_dbi_metric metric;
	mp_dbi_type type;
	char *warning_range;
	char *critical_range;
	thresholds *dbi_thresholds;

	bool output_format_is_set;
	mp_output_format output_format;
} check_dbi_config;

check_dbi_config check_dbi_config_init() {
	check_dbi_config tmp = {
		.dbi_driver = NULL,
		.host = NULL,
		.dbi_options = NULL,
		.dbi_options_num = 0,
		.dbi_database = NULL,
		.dbi_query = NULL,

		.expect = NULL,
		.expect_re_str = NULL,
		.expect_re_cflags = 0,
		.metric = METRIC_QUERY_RESULT,
		.type = TYPE_NUMERIC,

		.warning_range = NULL,
		.critical_range = NULL,
		.dbi_thresholds = NULL,

		.output_format_is_set = false,
	};
	return tmp;
}
