#include "./check_snmp_helpers.h"
#include <string.h>
#include "../../lib/utils_base.h"
#include "config.h"
#include <assert.h>
#include "../utils.h"
#include "output.h"
#include "states.h"
#include <sys/stat.h>
#include <ctype.h>

extern int verbose;

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

const int RANDOM_STATE_DATA_LENGTH_PREDICTION = 8192;

check_snmp_config check_snmp_config_init() {
	check_snmp_config tmp = {
		.snmp_params =
			{
				.use_getnext = false,

				.ignore_mib_parsing_errors = false,
				.need_mibs = false,

				.test_units = NULL,
				.num_of_test_units = 0,
			},

		.evaluation_params =
			{
				.nulloid_result = STATE_UNKNOWN, // state to return if no result for query

				.invert_search = true,
				.regex_cmp_value = {},
				.string_cmp_value = "",

				.multiplier = 1.0,
				.multiplier_set = false,
				.offset = 0,
				.offset_set = false,

				.use_oid_as_perf_data_label = false,

				.calculate_rate = false,
				.rate_multiplier = 1,
			},
	};

	snmp_sess_init(&tmp.snmp_params.snmp_session);

	tmp.snmp_params.snmp_session.retries = DEFAULT_RETRIES;
	tmp.snmp_params.snmp_session.version = DEFAULT_SNMP_VERSION;
	tmp.snmp_params.snmp_session.securityLevel = SNMP_SEC_LEVEL_NOAUTH;
	tmp.snmp_params.snmp_session.community = (unsigned char *)"public";
	tmp.snmp_params.snmp_session.community_len = strlen("public");
	return tmp;
}

snmp_responces do_snmp_query(check_snmp_config_snmp_parameters parameters) {
	if (parameters.ignore_mib_parsing_errors) {
		char *opt_toggle_res = snmp_mib_toggle_options("e");
		if (opt_toggle_res != NULL) {
			die(STATE_UNKNOWN, "Unable to disable MIB parsing errors");
		}
	}

	struct snmp_pdu *pdu = NULL;
	if (parameters.use_getnext) {
		pdu = snmp_pdu_create(SNMP_MSG_GETNEXT);
	} else {
		pdu = snmp_pdu_create(SNMP_MSG_GET);
	}

	for (size_t i = 0; i < parameters.num_of_test_units; i++) {
		assert(parameters.test_units[i].oid != NULL);
		if (verbose > 0) {
			printf("OID %zu to parse: %s\n", i, parameters.test_units[i].oid);
		}

		oid tmp_OID[MAX_OID_LEN];
		size_t tmp_OID_len = MAX_OID_LEN;
		if (snmp_parse_oid(parameters.test_units[i].oid, tmp_OID, &tmp_OID_len) != NULL) {
			// success
			snmp_add_null_var(pdu, tmp_OID, tmp_OID_len);
		} else {
			// failed
			snmp_perror("Parsing failure");
			die(STATE_UNKNOWN, "Failed to parse OID\n");
		}
	}

	const int timeout_safety_tolerance = 5;
	alarm((timeout_interval * (unsigned int)parameters.snmp_session.retries) +
		  timeout_safety_tolerance);

	struct snmp_session *active_session = snmp_open(&parameters.snmp_session);
	if (active_session == NULL) {
		int pcliberr = 0;
		int psnmperr = 0;
		char *pperrstring = NULL;
		snmp_error(&parameters.snmp_session, &pcliberr, &psnmperr, &pperrstring);
		die(STATE_UNKNOWN, "Failed to open SNMP session: %s\n", pperrstring);
	}

	struct snmp_pdu *response = NULL;
	int snmp_query_status = snmp_synch_response(active_session, pdu, &response);

	if (!(snmp_query_status == STAT_SUCCESS && response->errstat == SNMP_ERR_NOERROR)) {
		int pcliberr = 0;
		int psnmperr = 0;
		char *pperrstring = NULL;
		snmp_error(active_session, &pcliberr, &psnmperr, &pperrstring);

		if (psnmperr == SNMPERR_TIMEOUT) {
			// We exit with critical here for some historical reason
			die(STATE_CRITICAL, "SNMP query ran into a timeout\n");
		}
		die(STATE_UNKNOWN, "SNMP query failed: %s\n", pperrstring);
	}

	snmp_close(active_session);

	/* disable alarm again */
	alarm(0);

	snmp_responces result = {
		.errorcode = OK,
		.response_values = calloc(parameters.num_of_test_units, sizeof(response_value)),
	};

	if (result.response_values == NULL) {
		result.errorcode = ERROR;
		return result;
	}

	// We got the the query results, now process them
	size_t loop_index = 0;
	for (netsnmp_variable_list *vars = response->variables; vars;
		 vars = vars->next_variable, loop_index++) {

		for (size_t jdx = 0; jdx < vars->name_length; jdx++) {
			result.response_values[loop_index].oid[jdx] = vars->name[jdx];
		}
		result.response_values[loop_index].oid_length = vars->name_length;

		switch (vars->type) {
		case ASN_OCTET_STR: {
			result.response_values[loop_index].string_response = strdup((char *)vars->val.string);
			result.response_values[loop_index].type = vars->type;
			if (verbose) {
				printf("Debug: Got a string as response: %s\n", vars->val.string);
			}
		}
			continue;
		case ASN_OPAQUE:
			if (verbose) {
				printf("Debug: Got OPAQUE\n");
			}
			break;
		/* Numerical values */
		case ASN_COUNTER64: {
			if (verbose) {
				printf("Debug: Got counter64\n");
			}
			struct counter64 tmp = *(vars->val.counter64);
			uint64_t counter = (tmp.high << 32) + tmp.low;
			result.response_values[loop_index].value.uIntVal = counter;
			result.response_values[loop_index].type = vars->type;
		} break;
		case ASN_GAUGE: // same as ASN_UNSIGNED
		case ASN_TIMETICKS:
		case ASN_COUNTER:
		case ASN_UINTEGER: {
			if (verbose) {
				printf("Debug: Got a Integer like\n");
			}
			result.response_values[loop_index].value.uIntVal = (unsigned long)*(vars->val.integer);
			result.response_values[loop_index].type = vars->type;
		} break;
		case ASN_INTEGER: {
			if (verbose) {
				printf("Debug: Got a Integer\n");
			}
			result.response_values[loop_index].value.intVal = *(vars->val.integer);
			result.response_values[loop_index].type = vars->type;
		} break;
		case ASN_FLOAT: {
			if (verbose) {
				printf("Debug: Got a float\n");
			}
			result.response_values[loop_index].value.doubleVal = *(vars->val.floatVal);
			result.response_values[loop_index].type = vars->type;
		} break;
		case ASN_DOUBLE: {
			if (verbose) {
				printf("Debug: Got a double\n");
			}
			result.response_values[loop_index].value.doubleVal = *(vars->val.doubleVal);
			result.response_values[loop_index].type = vars->type;
		} break;
		case ASN_IPADDRESS:
			if (verbose) {
				printf("Debug: Got an IP address\n");
			}
			result.response_values[loop_index].type = vars->type;

			// TODO: print address here, state always ok? or regex match?
			break;
		default:
			if (verbose) {
				printf("Debug: Got a unmatched result type: %hhu\n", vars->type);
			}
			// TODO: Error here?
			break;
		}
	}

	return result;
}

check_snmp_evaluation evaluate_single_unit(response_value response,
										   check_snmp_evaluation_parameters eval_params,
										   check_snmp_test_unit test_unit, time_t query_timestamp,
										   check_snmp_state_entry prev_state,
										   bool have_previous_state) {
	mp_subcheck sc_oid_test = mp_subcheck_init();

	if ((test_unit.label != NULL) && (strcmp(test_unit.label, "") != 0)) {
		xasprintf(&sc_oid_test.output, "%s - ", test_unit.label);
	} else {
		sc_oid_test.output = strdup("");
	}

	char oid_string[(MAX_OID_LEN * 2) + 1] = {};

	int oid_string_result =
		snprint_objid(oid_string, (MAX_OID_LEN * 2) + 1, response.oid, response.oid_length);
	if (oid_string_result <= 0) {
		// TODO error here
		die(STATE_UNKNOWN, "snprint_objid failed\n");
	}

	xasprintf(&sc_oid_test.output, "%sOID: %s", sc_oid_test.output, oid_string);
	sc_oid_test = mp_set_subcheck_default_state(sc_oid_test, STATE_OK);

	if (verbose > 2) {
		printf("Processing oid %s\n", oid_string);
	}

	bool got_a_numerical_value = false;
	mp_perfdata_value pd_result_val = {0};

	check_snmp_state_entry result_state = {
		.timestamp = query_timestamp,
		.oid_length = response.oid_length,
		.type = response.type,
	};

	for (size_t i = 0; i < response.oid_length; i++) {
		result_state.oid[i] = response.oid[i];
	}

	if (have_previous_state) {
		if (query_timestamp == prev_state.timestamp) {
			// somehow we have the same timestamp again, that can't be good
			sc_oid_test = mp_set_subcheck_state(sc_oid_test, STATE_UNKNOWN);
			xasprintf(&sc_oid_test.output, "Time duration between plugin calls is invalid");

			check_snmp_evaluation result = {
				.sc = sc_oid_test,
				.state = result_state,
			};

			return result;
		}
	}
	// compute rate time difference
	double timeDiff = 0;
	if (have_previous_state) {
		if (verbose) {
			printf("Previous timestamp: %s", ctime(&prev_state.timestamp));
			printf("Current timestamp: %s", ctime(&query_timestamp));
		}
		timeDiff = difftime(query_timestamp, prev_state.timestamp) / eval_params.rate_multiplier;
	}

	mp_perfdata pd_num_val = {};

	switch (response.type) {
	case ASN_OCTET_STR: {
		char *tmp = response.string_response;
		if (strchr(tmp, '"') != NULL) {
			// got double quote in the string
			if (strchr(tmp, '\'') != NULL) {
				// got single quote in the string too
				// dont quote that at all to avoid even more confusion
				xasprintf(&sc_oid_test.output, "%s - Value: %s", sc_oid_test.output, tmp);
			} else {
				// quote with single quotes
				xasprintf(&sc_oid_test.output, "%s - Value: '%s'", sc_oid_test.output, tmp);
			}
		} else {
			// quote with double quotes
			xasprintf(&sc_oid_test.output, "%s - Value: \"%s\"", sc_oid_test.output, tmp);
		}

		if (strlen(tmp) == 0) {
			sc_oid_test = mp_set_subcheck_state(sc_oid_test, eval_params.nulloid_result);
		}

		// String matching test
		if ((test_unit.eval_mthd.crit_string)) {
			if (strcmp(tmp, eval_params.string_cmp_value)) {
				sc_oid_test = mp_set_subcheck_state(
					sc_oid_test, (eval_params.invert_search) ? STATE_CRITICAL : STATE_OK);
			} else {
				sc_oid_test = mp_set_subcheck_state(
					sc_oid_test, (eval_params.invert_search) ? STATE_OK : STATE_CRITICAL);
			}
		} else if (test_unit.eval_mthd.crit_regex) {
			const size_t nmatch = eval_params.regex_cmp_value.re_nsub + 1;
			regmatch_t pmatch[nmatch];
			memset(pmatch, '\0', sizeof(regmatch_t) * nmatch);

			int excode = regexec(&eval_params.regex_cmp_value, tmp, nmatch, pmatch, 0);
			if (excode == 0) {
				sc_oid_test = mp_set_subcheck_state(
					sc_oid_test, (eval_params.invert_search) ? STATE_OK : STATE_CRITICAL);
			} else if (excode != REG_NOMATCH) {
				char errbuf[MAX_INPUT_BUFFER] = "";
				regerror(excode, &eval_params.regex_cmp_value, errbuf, MAX_INPUT_BUFFER);
				printf(_("Execute Error: %s\n"), errbuf);
				exit(STATE_CRITICAL);
			} else { // REG_NOMATCH
				sc_oid_test = mp_set_subcheck_state(
					sc_oid_test, eval_params.invert_search ? STATE_CRITICAL : STATE_OK);
			}
		}
	} break;
	case ASN_COUNTER64:
		got_a_numerical_value = true;

		result_state.value.uIntVal = response.value.uIntVal;
		result_state.type = response.type;

		// TODO: perfdata unit counter
		if (eval_params.calculate_rate && have_previous_state) {
			if (prev_state.value.uIntVal > response.value.uIntVal) {
				// overflow
				unsigned long long tmp =
					(UINT64_MAX - prev_state.value.uIntVal) + response.value.uIntVal;

				tmp /= timeDiff;
				pd_result_val = mp_create_pd_value(tmp);
			} else {
				pd_result_val = mp_create_pd_value(
					(response.value.uIntVal - prev_state.value.uIntVal) / timeDiff);
			}
		} else {
			// It's only a counter if we cont compute rate
			pd_num_val.uom = "c";
			pd_result_val = mp_create_pd_value(response.value.uIntVal);
		}
		break;
	case ASN_GAUGE: // same as ASN_UNSIGNED
	case ASN_TIMETICKS:
	case ASN_COUNTER:
	case ASN_UINTEGER: {
		got_a_numerical_value = true;
		long long treated_value = (long long)response.value.uIntVal;

		if (eval_params.multiplier_set || eval_params.offset_set) {
			double processed = (double)response.value.uIntVal;

			if (eval_params.offset_set) {
				processed += eval_params.offset;
			}

			if (eval_params.multiplier_set) {
				processed = processed * eval_params.multiplier;
			}

			treated_value = lround(processed);
		}

		result_state.value.intVal = treated_value;

		if (eval_params.calculate_rate && have_previous_state) {
			if (verbose > 2) {
				printf("%s: Rate calculation (int/counter/gauge): prev: %lli\n", __FUNCTION__,
					   prev_state.value.intVal);
				printf("%s: Rate calculation (int/counter/gauge): current: %lli\n", __FUNCTION__,
					   treated_value);
			}
			double rate = (treated_value - prev_state.value.intVal) / timeDiff;
			pd_result_val = mp_create_pd_value(rate);
		} else {
			pd_result_val = mp_create_pd_value(treated_value);

			if (response.type == ASN_COUNTER) {
				pd_num_val.uom = "c";
			}
		}

	} break;
	case ASN_INTEGER: {
		if (eval_params.multiplier_set || eval_params.offset_set) {
			double processed = (double)response.value.intVal;

			if (eval_params.offset_set) {
				processed += eval_params.offset;
			}

			if (eval_params.multiplier_set) {
				processed *= eval_params.multiplier;
			}

			result_state.value.doubleVal = processed;

			if (eval_params.calculate_rate && have_previous_state) {
				pd_result_val =
					mp_create_pd_value((processed - prev_state.value.doubleVal) / timeDiff);
			} else {
				pd_result_val = mp_create_pd_value(processed);
			}
		} else {
			result_state.value.intVal = response.value.intVal;

			if (eval_params.calculate_rate && have_previous_state) {
				pd_result_val = mp_create_pd_value(
					(response.value.intVal - prev_state.value.intVal) / timeDiff);
			} else {
				pd_result_val = mp_create_pd_value(response.value.intVal);
			}
		}

		got_a_numerical_value = true;
	} break;
	case ASN_FLOAT: // fallthrough
	case ASN_DOUBLE: {
		got_a_numerical_value = true;
		double tmp = response.value.doubleVal;
		if (eval_params.offset_set) {
			tmp += eval_params.offset;
		}

		if (eval_params.multiplier_set) {
			tmp *= eval_params.multiplier;
		}

		if (eval_params.calculate_rate && have_previous_state) {
			pd_result_val = mp_create_pd_value((tmp - prev_state.value.doubleVal) / timeDiff);
		} else {
			pd_result_val = mp_create_pd_value(tmp);
		}
		got_a_numerical_value = true;

		result_state.value.doubleVal = tmp;
	} break;
	case ASN_IPADDRESS:
		// TODO
		break;
	}

	if (got_a_numerical_value) {
		if (eval_params.use_oid_as_perf_data_label) {
			// Use oid for perdata label
			pd_num_val.label = strdup(oid_string);
			// TODO strdup error checking
		} else if (test_unit.label != NULL && strcmp(test_unit.label, "") != 0) {
			pd_num_val.label = strdup(test_unit.label);
		} else {
			pd_num_val.label = strdup(test_unit.oid);
		}

		if (!(eval_params.calculate_rate && !have_previous_state)) {
			// some kind of numerical value
			if (test_unit.unit_value != NULL && strcmp(test_unit.unit_value, "") != 0) {
				pd_num_val.uom = test_unit.unit_value;
			}

			pd_num_val.value = pd_result_val;

			xasprintf(&sc_oid_test.output, "%s Value: %s", sc_oid_test.output,
					  pd_value_to_string(pd_result_val));

			if (test_unit.unit_value != NULL && strcmp(test_unit.unit_value, "") != 0) {
				xasprintf(&sc_oid_test.output, "%s%s", sc_oid_test.output, test_unit.unit_value);
			}

			if (test_unit.threshold.warning_is_set || test_unit.threshold.critical_is_set) {
				pd_num_val = mp_pd_set_thresholds(pd_num_val, test_unit.threshold);
				mp_state_enum tmp_state = mp_get_pd_status(pd_num_val);

				if (tmp_state == STATE_WARNING) {
					sc_oid_test = mp_set_subcheck_state(sc_oid_test, STATE_WARNING);
					xasprintf(&sc_oid_test.output, "%s - number violates warning threshold",
							  sc_oid_test.output);
				} else if (tmp_state == STATE_CRITICAL) {
					sc_oid_test = mp_set_subcheck_state(sc_oid_test, STATE_CRITICAL);
					xasprintf(&sc_oid_test.output, "%s - number violates critical threshold",
							  sc_oid_test.output);
				}
			}

			mp_add_perfdata_to_subcheck(&sc_oid_test, pd_num_val);
		} else {
			// should calculate rate, but there is no previous state, so first run
			// exit with ok now
			sc_oid_test = mp_set_subcheck_state(sc_oid_test, STATE_OK);
			xasprintf(&sc_oid_test.output, "%s - No previous data to calculate rate - assume okay",
					  sc_oid_test.output);
		}
	}

	check_snmp_evaluation result = {
		.sc = sc_oid_test,
		.state = result_state,
	};

	return result;
}

char *_np_state_generate_key(int argc, char **argv);

/*
 * If time=NULL, use current time. Create state file, with state format
 * version, default text. Writes version, time, and data. Avoid locking
 * problems - use mv to write and then swap. Possible loss of state data if
 * two things writing to same key at same time.
 * Will die with UNKNOWN if errors
 */
void np_state_write_string(state_key stateKey, time_t timestamp, char *stringToStore) {
	time_t current_time;
	if (timestamp == 0) {
		time(&current_time);
	} else {
		current_time = timestamp;
	}

	int result = 0;

	/* If file doesn't currently exist, create directories */
	if (access(stateKey._filename, F_OK) != 0) {
		char *directories = NULL;
		result = asprintf(&directories, "%s", stateKey._filename);
		if (result < 0) {
			die(STATE_UNKNOWN, _("Cannot allocate memory: %s"), strerror(errno));
		}

		for (char *p = directories + 1; *p; p++) {
			if (*p == '/') {
				*p = '\0';
				if ((access(directories, F_OK) != 0) && (mkdir(directories, S_IRWXU) != 0)) {
					/* Can't free this! Otherwise error message is wrong! */
					/* np_free(directories); */
					die(STATE_UNKNOWN, _("Cannot create directory: %s"), directories);
				}
				*p = '/';
			}
		}

		if (directories) {
			free(directories);
		}
	}

	char *temp_file = NULL;
	result = asprintf(&temp_file, "%s.XXXXXX", stateKey._filename);
	if (result < 0) {
		die(STATE_UNKNOWN, _("Cannot allocate memory: %s"), strerror(errno));
	}

	int temp_file_desc = 0;
	if ((temp_file_desc = mkstemp(temp_file)) == -1) {
		if (temp_file) {
			free(temp_file);
		}
		die(STATE_UNKNOWN, _("Cannot create temporary filename"));
	}

	FILE *temp_file_pointer = fdopen(temp_file_desc, "w");
	if (temp_file_pointer == NULL) {
		close(temp_file_desc);
		unlink(temp_file);
		if (temp_file) {
			free(temp_file);
		}
		die(STATE_UNKNOWN, _("Unable to open temporary state file"));
	}

	fprintf(temp_file_pointer, "# NP State file\n");
	fprintf(temp_file_pointer, "%d\n", NP_STATE_FORMAT_VERSION);
	fprintf(temp_file_pointer, "%d\n", stateKey.data_version);
	fprintf(temp_file_pointer, "%lu\n", current_time);
	fprintf(temp_file_pointer, "%s\n", stringToStore);

	fchmod(temp_file_desc, S_IRUSR | S_IWUSR | S_IRGRP);

	fflush(temp_file_pointer);

	result = fclose(temp_file_pointer);

	fsync(temp_file_desc);

	if (result != 0) {
		unlink(temp_file);
		if (temp_file) {
			free(temp_file);
		}
		die(STATE_UNKNOWN, _("Error writing temp file"));
	}

	if (rename(temp_file, stateKey._filename) != 0) {
		unlink(temp_file);
		if (temp_file) {
			free(temp_file);
		}
		die(STATE_UNKNOWN, _("Cannot rename state temp file"));
	}

	if (temp_file) {
		free(temp_file);
	}
}

/*
 * Read the state file
 */
bool _np_state_read_file(FILE *state_file, state_key stateKey) {
	time_t current_time;
	time(&current_time);

	/* Note: This introduces a limit of 8192 bytes in the string data */
	char *line = (char *)calloc(1, 8192);
	if (line == NULL) {
		die(STATE_UNKNOWN, _("Cannot allocate memory: %s"), strerror(errno));
	}

	bool status = false;
	enum {
		STATE_FILE_VERSION,
		STATE_DATA_VERSION,
		STATE_DATA_TIME,
		STATE_DATA_TEXT,
		STATE_DATA_END
	} expected = STATE_FILE_VERSION;

	int failure = 0;
	while (!failure && (fgets(line, 8192, state_file)) != NULL) {
		size_t pos = strlen(line);
		if (line[pos - 1] == '\n') {
			line[pos - 1] = '\0';
		}

		if (line[0] == '#') {
			continue;
		}

		switch (expected) {
		case STATE_FILE_VERSION: {
			int i = atoi(line);
			if (i != NP_STATE_FORMAT_VERSION) {
				failure++;
			} else {
				expected = STATE_DATA_VERSION;
			}
		} break;
		case STATE_DATA_VERSION: {
			int i = atoi(line);
			if (i != stateKey.data_version) {
				failure++;
			} else {
				expected = STATE_DATA_TIME;
			}
		} break;
		case STATE_DATA_TIME: {
			/* If time > now, error */
			time_t data_time = strtoul(line, NULL, 10);
			if (data_time > current_time) {
				failure++;
			} else {
				stateKey.state_data->time = data_time;
				expected = STATE_DATA_TEXT;
			}
		} break;
		case STATE_DATA_TEXT:
			stateKey.state_data->data = strdup(line);
			if (stateKey.state_data->data == NULL) {
				die(STATE_UNKNOWN, _("Cannot execute strdup: %s"), strerror(errno));
			}
			stateKey.state_data->length = strlen(line);
			expected = STATE_DATA_END;
			status = true;
			break;
		case STATE_DATA_END:;
		}
	}

	if (line) {
		free(line);
	}
	return status;
}
/*
 * Will return NULL if no data is available (first run). If key currently
 * exists, read data. If state file format version is not expected, return
 * as if no data. Get state data version number and compares to expected.
 * If numerically lower, then return as no previous state. die with UNKNOWN
 * if exceptional error.
 */
state_data *np_state_read(state_key stateKey) {
	/* Open file. If this fails, no previous state found */
	FILE *statefile = fopen(stateKey._filename, "r");
	state_data *this_state_data = (state_data *)calloc(1, sizeof(state_data));
	if (statefile != NULL) {

		if (this_state_data == NULL) {
			die(STATE_UNKNOWN, _("Cannot allocate memory: %s"), strerror(errno));
		}

		this_state_data->data = NULL;
		stateKey.state_data = this_state_data;

		if (_np_state_read_file(statefile, stateKey)) {
			this_state_data->errorcode = OK;
		} else {
			this_state_data->errorcode = ERROR;
		}

		fclose(statefile);
	} else {
		// Failed to open state file
		this_state_data->errorcode = ERROR;
	}

	return stateKey.state_data;
}

/*
 * Internal function. Returns either:
 *   envvar NAGIOS_PLUGIN_STATE_DIRECTORY
 *   statically compiled shared state directory
 */
char *_np_state_calculate_location_prefix(void) {
	char *env_dir;

	/* Do not allow passing MP_STATE_PATH in setuid plugins
	 * for security reasons */
	if (!mp_suid()) {
		env_dir = getenv("MP_STATE_PATH");
		if (env_dir && env_dir[0] != '\0') {
			return env_dir;
		}
		/* This is the former ENV, for backward-compatibility */
		env_dir = getenv("NAGIOS_PLUGIN_STATE_DIRECTORY");
		if (env_dir && env_dir[0] != '\0') {
			return env_dir;
		}
	}

	return NP_STATE_DIR_PREFIX;
}

/*
 * Initiatializer for state routines.
 * Sets variables. Generates filename. Returns np_state_key. die with
 * UNKNOWN if exception
 */
state_key np_enable_state(char *keyname, int expected_data_version, char *plugin_name, int argc,
						  char **argv) {
	state_key *this_state = (state_key *)calloc(1, sizeof(state_key));
	if (this_state == NULL) {
		die(STATE_UNKNOWN, _("Cannot allocate memory: %s"), strerror(errno));
	}

	char *temp_keyname = NULL;
	if (keyname == NULL) {
		temp_keyname = _np_state_generate_key(argc, argv);
	} else {
		temp_keyname = strdup(keyname);
		if (temp_keyname == NULL) {
			die(STATE_UNKNOWN, _("Cannot execute strdup: %s"), strerror(errno));
		}
	}

	/* Die if invalid characters used for keyname */
	char *tmp_char = temp_keyname;
	while (*tmp_char != '\0') {
		if (!(isalnum(*tmp_char) || *tmp_char == '_')) {
			die(STATE_UNKNOWN, _("Invalid character for keyname - only alphanumerics or '_'"));
		}
		tmp_char++;
	}
	this_state->name = temp_keyname;
	this_state->plugin_name = plugin_name;
	this_state->data_version = expected_data_version;
	this_state->state_data = NULL;

	/* Calculate filename */
	char *temp_filename = NULL;
	int error = asprintf(&temp_filename, "%s/%lu/%s/%s", _np_state_calculate_location_prefix(),
						 (unsigned long)geteuid(), plugin_name, this_state->name);
	if (error < 0) {
		die(STATE_UNKNOWN, _("Cannot allocate memory: %s"), strerror(errno));
	}

	this_state->_filename = temp_filename;

	return *this_state;
}

/*
 * Returns a string to use as a keyname, based on an md5 hash of argv, thus
 * hopefully a unique key per service/plugin invocation. Use the extra-opts
 * parse of argv, so that uniqueness in parameters are reflected there.
 */
char *_np_state_generate_key(int argc, char **argv) {
	unsigned char result[256];

#ifdef USE_OPENSSL
	/*
	 * This code path is chosen if openssl is available (which should be the most common
	 * scenario). Alternatively, the gnulib implementation/
	 *
	 */
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();

	EVP_DigestInit(ctx, EVP_sha256());

	for (int i = 0; i < argc; i++) {
		EVP_DigestUpdate(ctx, argv[i], strlen(argv[i]));
	}

	EVP_DigestFinal(ctx, result, NULL);
#else

	struct sha256_ctx ctx;

	for (int i = 0; i < this_monitoring_plugin->argc; i++) {
		sha256_process_bytes(argv[i], strlen(argv[i]), &ctx);
	}

	sha256_finish_ctx(&ctx, result);
#endif // FOUNDOPENSSL

	char keyname[41];
	for (int i = 0; i < 20; ++i) {
		sprintf(&keyname[2 * i], "%02x", result[i]);
	}

	keyname[40] = '\0';

	char *keyname_copy = strdup(keyname);
	if (keyname_copy == NULL) {
		die(STATE_UNKNOWN, _("Cannot execute strdup: %s"), strerror(errno));
	}

	return keyname_copy;
}
