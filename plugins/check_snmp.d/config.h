#pragma once

#include "states.h"
#include "thresholds.h"
#include "utils_base.h"
#include <stdlib.h>
#include <stdbool.h>
#include <regex.h>

// defines for snmp libs
#define u_char  unsigned char
#define u_long  unsigned long
#define u_short unsigned short
#define u_int   unsigned int

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/library/snmp.h>
#include <net-snmp/session_api.h>

const int DEFAULT_PROTOCOL = SNMP_VERSION_1;
const char DEFAULT_PORT[] = "161";
const char DEFAULT_OUTPUT_DELIMITER[] = " ";
const int DEFAULT_RETRIES = 5;

const int RANDOM_STATE_DATA_LENGTH_PREDICTION = 1024;

typedef struct eval_method {
	bool crit_string;
	bool crit_regex;
} eval_method;

typedef struct check_snmp_test_unit {
	char *oid;
	char *label;
	char *unit_value;
	eval_method eval_mthd;
} check_snmp_test_unit;

check_snmp_test_unit check_snmp_test_unit_init() {
	check_snmp_test_unit tmp = {};
	return tmp;
}

typedef struct check_snmp_config {
	// SNMP session to use
	struct snmp_session snmp_session;

	// use getnet instead of get
	bool use_getnext;

	// TODO actually make these useful
	bool ignore_mib_parsing_errors;
	bool need_mibs;

	check_snmp_test_unit *test_units;
	size_t num_of_test_units;
	mp_thresholds thresholds;

	// State if an empty value is encountered
	mp_state_enum nulloid_result;

	// String evaluation stuff
	bool invert_search;
	regex_t regex_cmp_value; // regex to match query results against
	char string_cmp_value[MAX_INPUT_BUFFER];

	// Modify data
	double multiplier;
	double offset;

	// Modify output
	bool use_perf_data_labels_from_input;
} check_snmp_config;

check_snmp_config check_snmp_config_init() {
	check_snmp_config tmp = {
		.use_getnext = false,

		.ignore_mib_parsing_errors = false,
		.need_mibs = false,

		.test_units = NULL,
		.num_of_test_units = 0,
		.thresholds = mp_thresholds_init(),

		.nulloid_result = STATE_UNKNOWN, // state to return if no result for query

		.invert_search = true,
		.regex_cmp_value = {},
		.string_cmp_value = "",

		.multiplier = 1.0,
		.offset = 0,

		.use_perf_data_labels_from_input = false,
	};

	snmp_sess_init(&tmp.snmp_session);

	tmp.snmp_session.retries = DEFAULT_RETRIES;
	tmp.snmp_session.version = DEFAULT_SNMP_VERSION;
	tmp.snmp_session.securityLevel = SNMP_SEC_LEVEL_NOAUTH;
	tmp.snmp_session.community = (unsigned char *)"public";
	tmp.snmp_session.community_len = strlen("public");
	return tmp;
}
