#pragma once

#include "thresholds.h"
#include "states.h"
#include <stdlib.h>
#include <stdbool.h>
#include <regex.h>
#include "../common.h"

// defines for snmp libs
#define u_char  unsigned char
#define u_long  unsigned long
#define u_short unsigned short
#define u_int   unsigned int

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/library/snmp.h>
#include <net-snmp/session_api.h>

#define DEFAULT_PORT "161"
#define DEFAULT_RETRIES 5

typedef struct eval_method {
	bool crit_string;
	bool crit_regex;
} eval_method;

typedef struct check_snmp_test_unit {
	char *oid;
	char *label;
	char *unit_value;
	eval_method eval_mthd;
	mp_thresholds threshold;
} check_snmp_test_unit;

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
