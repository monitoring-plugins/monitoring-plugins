#include "./check_snmp_helpers.h"
#include <string.h>
#include "../../lib/utils_base.h"

check_snmp_test_unit check_snmp_test_unit_init() {
	check_snmp_test_unit tmp = {
		.threshold = mp_thresholds_init(),
	};
	return tmp;
}

int check_snmp_set_thresholds(const char *threshold_string, check_snmp_test_unit test_units[],
							  size_t max_test_units, bool is_critical) {

	if (threshold_string == NULL || strlen(threshold_string) == 0) {
		// No input, do nothing
		return 0;
	}

	if (strchr(threshold_string, ',') != NULL) {
		// Got a comma in the string, should be multiple values
		size_t tu_index = 0;

		while (threshold_string[0] == ',') {
			// got commas at the beginning, so skip some values
			tu_index++;
			threshold_string++;
		}

		for (char *ptr = strtok(threshold_string, ", "); ptr != NULL;
			 ptr = strtok(NULL, ", "), tu_index++) {

			if (tu_index > max_test_units) {
				// More thresholds then values, just ignore them
				return 0;
			}

			// edge case: maybe we got `,,` to skip a value
			if (strlen(ptr) == 0) {
				// no threshold given, do not set it then
				continue;
			}

			mp_range_parsed tmp = mp_parse_range_string(ptr);
			if (tmp.error != MP_PARSING_SUCCES) {
				die(STATE_UNKNOWN, "Unable to parse critical threshold range: %s", ptr);
			}

			if (is_critical) {
				test_units[tu_index].threshold.critical = tmp.range;
				test_units[tu_index].threshold.critical_is_set = true;
			} else {
				test_units[tu_index].threshold.warning = tmp.range;
				test_units[tu_index].threshold.warning_is_set = true;
			}
		}

	} else {
		// Single value
		// only valid for the first test unit
		mp_range_parsed tmp = mp_parse_range_string(threshold_string);
		if (tmp.error != MP_PARSING_SUCCES) {
			die(STATE_UNKNOWN, "Unable to parse critical threshold range: %s", threshold_string);
		}

		if (is_critical) {
			test_units[0].threshold.critical = tmp.range;
			test_units[0].threshold.critical_is_set = true;
		} else {
			test_units[0].threshold.warning = tmp.range;
			test_units[0].threshold.warning_is_set = true;
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
		.multiplier_set = false,
		.offset = 0,
		.offset_set = false,

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
