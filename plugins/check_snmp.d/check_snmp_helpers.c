#include "./check_snmp_helpers.h"
#include <string.h>
#include "../../lib/utils_base.h"

check_snmp_test_unit check_snmp_test_unit_init() {
	check_snmp_test_unit tmp = {
		.threshold = mp_thresholds_init(),
	};
	return tmp;
}


int check_snmp_set_thresholds(char *threshold_string, check_snmp_test_unit tu[],
									 size_t max_test_units, bool is_critical) {
	if (strchr(threshold_string, ',') != NULL) {
		// Got a comma in the string, should be multiple values
		size_t tmp_counter = 0;
		mp_range range_buffer;
		bool first_value = true;
		for (char *ptr = strtok(threshold_string, ", "); ptr != NULL;
			 ptr = strtok(NULL, ", "), tmp_counter++) {

			// edge case: maybe we got `,,` to skip a value
			if (strlen(ptr) == 0) {
				// use the previous value in this case
				// or do not overwrite the loop value to be specific
				if (first_value) {
					die(STATE_UNKNOWN, "Empty threshold value");
				}
			} else {
				mp_range_parsed tmp = mp_parse_range_string(ptr);
				if (tmp.error != MP_PARSING_SUCCES) {
					die(STATE_UNKNOWN, "Unable to parse critical threshold range: %s", ptr);
				}
				range_buffer = tmp.range;
			}

			if (is_critical) {
				tu[tmp_counter].threshold.critical = range_buffer;
				tu[tmp_counter].threshold.critical_is_set = true;
			} else {
				tu[tmp_counter].threshold.warning = range_buffer;
				tu[tmp_counter].threshold.warning_is_set = true;
			}
			first_value = false;
		}
	} else {
		// Single value
		mp_range_parsed tmp = mp_parse_range_string(threshold_string);
		if (tmp.error != MP_PARSING_SUCCES) {
			die(STATE_UNKNOWN, "Unable to parse critical threshold range: %s", threshold_string);
		}

		for (size_t i = 0; i < max_test_units; i++) {
			if (is_critical) {
				tu[i].threshold.critical = tmp.range;
				tu[i].threshold.critical_is_set = true;
			} else {
				tu[i].threshold.warning = tmp.range;
				tu[i].threshold.warning_is_set = true;
			}
		}
	}

	return 0;
}

const int DEFAULT_PROTOCOL = SNMP_VERSION_1;
const char DEFAULT_OUTPUT_DELIMITER[] = " ";

const int RANDOM_STATE_DATA_LENGTH_PREDICTION = 1024;

check_snmp_config check_snmp_config_init() {
	check_snmp_config tmp = {
		.use_getnext = false,

		.ignore_mib_parsing_errors = false,
		.need_mibs = false,

		.test_units = NULL,
		.num_of_test_units = 0,

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
