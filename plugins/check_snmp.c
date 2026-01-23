/*****************************************************************************
 *
 * Monitoring check_snmp plugin
 *
 * License: GPL
 * Copyright (c) 1999-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_snmp plugin
 *
 * Check status of remote machines and obtain system information via SNMP
 *
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *****************************************************************************/

const char *progname = "check_snmp";
const char *copyright = "1999-2024";
const char *email = "devel@monitoring-plugins.org";

#include "./common.h"
#include "./runcmd.h"
#include "./utils.h"
#include "../lib/states.h"

#include "../lib/utils_base.h"
#include "../lib/output.h"
#include "check_snmp.d/check_snmp_helpers.h"

#include <strings.h>
#include <stdint.h>

#include "check_snmp.d/config.h"
#include <stdlib.h>
#include <arpa/inet.h>
#include <net-snmp/library/parse.h>
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/library/snmp.h>
#include <net-snmp/library/keytools.h>
#include <net-snmp/library/snmp_api.h>
#include <net-snmp/session_api.h>
#include <net-snmp/definitions.h>
#include <net-snmp/library/asn1.h>
#include <net-snmp/mib_api.h>
#include <net-snmp/library/snmp_impl.h>
#include <string.h>
#include "../gl/regex.h"
#include "../gl/base64.h"
#include <assert.h>

const char DEFAULT_COMMUNITY[] = "public";
const char DEFAULT_MIBLIST[] = "ALL";
#define DEFAULT_AUTH_PROTOCOL "MD5"

#ifdef HAVE_USM_DES_PRIV_PROTOCOL
#	define DEFAULT_PRIV_PROTOCOL "DES"
#else
#	define DEFAULT_PRIV_PROTOCOL "AES"
#endif

typedef struct proces_arguments_wrapper {
	int errorcode;
	check_snmp_config config;
} process_arguments_wrapper;

static process_arguments_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static char *trim_whitespaces_and_check_quoting(char *str);
static char *get_next_argument(char *str);
void print_usage(void);
void print_help(void);

int verbose = 0;

typedef struct {
	int errorcode;
	char *state_string;
} gen_state_string_type;
gen_state_string_type gen_state_string(check_snmp_state_entry *entries, size_t num_of_entries) {
	char *encoded_string = NULL;
	gen_state_string_type result = {.errorcode = OK, .state_string = NULL};

	if (verbose > 1) {
		printf("%s:\n", __FUNCTION__);
		for (size_t i = 0; i < num_of_entries; i++) {
			printf("Entry timestamp %lu: %s", entries[i].timestamp, ctime(&entries[i].timestamp));
			switch (entries[i].type) {
			case ASN_GAUGE:
				printf("Type GAUGE\n");
				break;
			case ASN_TIMETICKS:
				printf("Type TIMETICKS\n");
				break;
			case ASN_COUNTER:
				printf("Type COUNTER\n");
				break;
			case ASN_UINTEGER:
				printf("Type UINTEGER\n");
				break;
			case ASN_COUNTER64:
				printf("Type COUNTER64\n");
				break;
			case ASN_FLOAT:
				printf("Type FLOAT\n");
				break;
			case ASN_DOUBLE:
				printf("Type DOUBLE\n");
				break;
			case ASN_INTEGER:
				printf("Type INTEGER\n");
				break;
			}

			switch (entries[i].type) {
			case ASN_GAUGE:
			case ASN_TIMETICKS:
			case ASN_COUNTER:
			case ASN_UINTEGER:
			case ASN_COUNTER64:
				printf("Value %llu\n", entries[i].value.uIntVal);
				break;
			case ASN_FLOAT:
			case ASN_DOUBLE:
				printf("Value %f\n", entries[i].value.doubleVal);
				break;
			case ASN_INTEGER:
				printf("Value %lld\n", entries[i].value.intVal);
				break;
			}
		}
	}

	idx_t encoded = base64_encode_alloc((const char *)entries,
										(idx_t)(num_of_entries * sizeof(check_snmp_state_entry)),
										&encoded_string);

	if (encoded > 0 && encoded_string != NULL) {
		// success
		if (verbose > 1) {
			printf("encoded string: %s\n", encoded_string);
			printf("encoded string length: %lu\n", strlen(encoded_string));
		}
		result.state_string = encoded_string;
		return result;
	}
	result.errorcode = ERROR;
	return result;
}

typedef struct {
	int errorcode;
	check_snmp_state_entry *state;
} recover_state_data_type;
recover_state_data_type recover_state_data(char *state_string, idx_t state_string_length) {
	recover_state_data_type result = {.errorcode = OK, .state = NULL};

	if (verbose > 1) {
		printf("%s:\n", __FUNCTION__);
		printf("State string: %s\n", state_string);
		printf("State string length: %lu\n", state_string_length);
	}

	idx_t outlen = 0;
	bool decoded =
		base64_decode_alloc(state_string, state_string_length, (char **)&result.state, &outlen);

	if (!decoded) {
		if (verbose) {
			printf("Failed to decode state string\n");
		}
		// failure to decode
		result.errorcode = ERROR;
		return result;
	}

	if (result.state == NULL) {
		// Memory Error?
		result.errorcode = ERROR;
		return result;
	}

	if (verbose > 1) {
		printf("Recovered %lu entries of size %lu\n",
			   (size_t)outlen / sizeof(check_snmp_state_entry), outlen);

		for (size_t i = 0; i < (size_t)outlen / sizeof(check_snmp_state_entry); i++) {
			printf("Entry timestamp %lu: %s", result.state[i].timestamp,
				   ctime(&result.state[i].timestamp));
			switch (result.state[i].type) {
			case ASN_GAUGE:
				printf("Type GAUGE\n");
				break;
			case ASN_TIMETICKS:
				printf("Type TIMETICKS\n");
				break;
			case ASN_COUNTER:
				printf("Type COUNTER\n");
				break;
			case ASN_UINTEGER:
				printf("Type UINTEGER\n");
				break;
			case ASN_COUNTER64:
				printf("Type COUNTER64\n");
				break;
			case ASN_FLOAT:
				printf("Type FLOAT\n");
				break;
			case ASN_DOUBLE:
				printf("Type DOUBLE\n");
				break;
			case ASN_INTEGER:
				printf("Type INTEGER\n");
				break;
			}

			switch (result.state[i].type) {
			case ASN_GAUGE:
			case ASN_TIMETICKS:
			case ASN_COUNTER:
			case ASN_UINTEGER:
			case ASN_COUNTER64:
				printf("Value %llu\n", result.state[i].value.uIntVal);
				break;
			case ASN_FLOAT:
			case ASN_DOUBLE:
				printf("Value %f\n", result.state[i].value.doubleVal);
				break;
			case ASN_INTEGER:
				printf("Value %lld\n", result.state[i].value.intVal);
				break;
			}
		}
	}

	return result;
}

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	timeout_interval = DEFAULT_SOCKET_TIMEOUT;

	np_init((char *)progname, argc, argv);

	state_key stateKey = np_enable_state(NULL, 1, progname, argc, argv);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	np_set_args(argc, argv);

	// Initialize net-snmp before touching the session we are going to use
	init_snmp("check_snmp");

	process_arguments_wrapper paw_tmp = process_arguments(argc, argv);
	if (paw_tmp.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	check_snmp_config config = paw_tmp.config;

	if (config.output_format_is_set) {
		mp_set_format(config.output_format);
	}

	/* Set signal handling and alarm */
	if (signal(SIGALRM, runcmd_timeout_alarm_handler) == SIG_ERR) {
		usage4(_("Cannot catch SIGALRM"));
	}

	time_t current_time;
	time(&current_time);

	if (verbose > 2) {
		printf("current time: %s (timestamp: %lu)\n", ctime(&current_time), current_time);
	}

	snmp_responces response = do_snmp_query(config.snmp_params);

	mp_check overall = mp_check_init();

	if (response.errorcode == OK) {
		mp_subcheck sc_successfull_query = mp_subcheck_init();
		xasprintf(&sc_successfull_query.output, "SNMP query was successful");
		sc_successfull_query = mp_set_subcheck_state(sc_successfull_query, STATE_OK);
		mp_add_subcheck_to_check(&overall, sc_successfull_query);
	} else {
		// Error treatment here, either partial or whole
		mp_subcheck sc_failed_query = mp_subcheck_init();
		xasprintf(&sc_failed_query.output, "SNMP query failed");
		sc_failed_query = mp_set_subcheck_state(sc_failed_query, STATE_OK);
		mp_add_subcheck_to_check(&overall, sc_failed_query);
		mp_exit(overall);
	}

	check_snmp_state_entry *prev_state = NULL;
	bool have_previous_state = false;

	if (config.evaluation_params.calculate_rate) {
		state_data *previous_state = np_state_read(stateKey);
		if (previous_state == NULL) {
			// failed to recover state
			// or no previous state
			have_previous_state = false;
		} else {
			// sanity check
			recover_state_data_type prev_state_wrapper =
				recover_state_data(previous_state->data, (idx_t)previous_state->length);

			if (prev_state_wrapper.errorcode == OK) {
				have_previous_state = true;
				prev_state = prev_state_wrapper.state;
			} else {
				have_previous_state = false;
				prev_state = NULL;
			}
		}
	}

	check_snmp_state_entry *new_state = NULL;
	if (config.evaluation_params.calculate_rate) {
		new_state = calloc(config.snmp_params.num_of_test_units, sizeof(check_snmp_state_entry));
		if (new_state == NULL) {
			die(STATE_UNKNOWN, "memory allocation failed");
		}
	}

	// We got the the query results, now process them
	for (size_t loop_index = 0; loop_index < config.snmp_params.num_of_test_units; loop_index++) {
		if (verbose > 0) {
			printf("loop_index: %zu\n", loop_index);
		}

		check_snmp_state_entry previous_unit_state = {};
		if (config.evaluation_params.calculate_rate && have_previous_state) {
			previous_unit_state = prev_state[loop_index];
		}

		check_snmp_evaluation single_eval =
			evaluate_single_unit(response.response_values[loop_index], config.evaluation_params,
								 config.snmp_params.test_units[loop_index], current_time,
								 previous_unit_state, have_previous_state);

		if (config.evaluation_params.calculate_rate &&
			mp_compute_subcheck_state(single_eval.sc) != STATE_UNKNOWN) {
			new_state[loop_index] = single_eval.state;
		}

		mp_add_subcheck_to_check(&overall, single_eval.sc);
	}

	if (config.evaluation_params.calculate_rate) {
		// store state
		gen_state_string_type current_state_wrapper =
			gen_state_string(new_state, config.snmp_params.num_of_test_units);

		if (current_state_wrapper.errorcode == OK) {
			np_state_write_string(stateKey, current_time, current_state_wrapper.state_string);
		} else {
			die(STATE_UNKNOWN, "failed to create state string");
		}
	}
	mp_exit(overall);
}

/* process command-line arguments */
static process_arguments_wrapper process_arguments(int argc, char **argv) {
	enum {
		/* Longopts only arguments */
		invert_search_index = CHAR_MAX + 1,
		offset_index,
		ignore_mib_parsing_errors_index,
		connection_prefix_index,
		output_format_index,
		calculate_rate,
		rate_multiplier
	};

	static struct option longopts[] = {
		STD_LONG_OPTS,
		{"community", required_argument, 0, 'C'},
		{"oid", required_argument, 0, 'o'},
		{"object", required_argument, 0, 'o'},
		{"delimiter", required_argument, 0, 'd'},
		{"nulloid", required_argument, 0, 'z'},
		{"output-delimiter", required_argument, 0, 'D'},
		{"string", required_argument, 0, 's'},
		{"timeout", required_argument, 0, 't'},
		{"regex", required_argument, 0, 'r'},
		{"ereg", required_argument, 0, 'r'},
		{"eregi", required_argument, 0, 'R'},
		{"label", required_argument, 0, 'l'},
		{"units", required_argument, 0, 'u'},
		{"port", required_argument, 0, 'p'},
		{"retries", required_argument, 0, 'e'},
		{"miblist", required_argument, 0, 'm'},
		{"protocol", required_argument, 0, 'P'},
		{"context", required_argument, 0, 'N'},
		{"seclevel", required_argument, 0, 'L'},
		{"secname", required_argument, 0, 'U'},
		{"authproto", required_argument, 0, 'a'},
		{"privproto", required_argument, 0, 'x'},
		{"authpasswd", required_argument, 0, 'A'},
		{"privpasswd", required_argument, 0, 'X'},
		{"next", no_argument, 0, 'n'},
		{"offset", required_argument, 0, offset_index},
		{"invert-search", no_argument, 0, invert_search_index},
		{"perf-oids", no_argument, 0, 'O'},
		{"ipv4", no_argument, 0, '4'},
		{"ipv6", no_argument, 0, '6'},
		{"multiplier", required_argument, 0, 'M'},
		{"ignore-mib-parsing-errors", no_argument, 0, ignore_mib_parsing_errors_index},
		{"connection-prefix", required_argument, 0, connection_prefix_index},
		{"output-format", required_argument, 0, output_format_index},
		{"rate", no_argument, 0, calculate_rate},
		{"rate-multiplier", required_argument, 0, rate_multiplier},
		{0, 0, 0, 0}};

	if (argc < 2) {
		process_arguments_wrapper result = {
			.errorcode = ERROR,
		};
		return result;
	}

	// Count number of OIDs here first
	int option = 0;
	size_t oid_counter = 0;
	while (true) {
		int option_char = getopt_long(
			argc, argv,
			"nhvVO46t:c:w:H:C:o:e:E:d:D:s:t:R:r:l:u:p:m:P:N:L:U:a:x:A:X:M:f:z:", longopts, &option);

		if (CHECK_EOF(option_char)) {
			break;
		}

		switch (option_char) {
		case 'o': {
			// we are going to parse this again, so we work on a copy of that string
			char *tmp_oids = strdup(optarg);
			if (tmp_oids == NULL) {
				die(STATE_UNKNOWN, "strdup failed");
			}

			for (char *ptr = strtok(tmp_oids, ", "); ptr != NULL;
				 ptr = strtok(NULL, ", "), oid_counter++) {
			}
			break;
		}
		case '?': /* usage */
			usage5();
			// fallthrough
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);

		default:
			continue;
		}
	}

	/* Check whether at least one OID was given */
	if (oid_counter == 0) {
		die(STATE_UNKNOWN, _("No OIDs specified\n"));
	}

	// Allocate space for test units
	check_snmp_test_unit *tmp = calloc(oid_counter, sizeof(check_snmp_test_unit));
	if (tmp == NULL) {
		die(STATE_UNKNOWN, "Failed to calloc");
	}

	for (size_t i = 0; i < oid_counter; i++) {
		tmp[i] = check_snmp_test_unit_init();
	}

	check_snmp_config config = check_snmp_config_init();
	config.snmp_params.test_units = tmp;
	config.snmp_params.num_of_test_units = oid_counter;

	option = 0;
	optind = 1; // Reset argument scanner
	size_t tmp_oid_counter = 0;
	size_t eval_counter = 0;
	size_t unitv_counter = 0;
	size_t labels_counter = 0;
	unsigned char *authpasswd = NULL;
	unsigned char *privpasswd = NULL;
	int cflags = REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
	char *port = NULL;
	char *miblist = NULL;
	char *connection_prefix = NULL;
	bool snmp_version_set_explicitely = false;
	// TODO error checking
	while (true) {
		int option_char = getopt_long(
			argc, argv,
			"nhvVO46t:c:w:H:C:o:e:E:d:D:s:t:R:r:l:u:p:m:P:N:L:U:a:x:A:X:M:f:z:", longopts, &option);

		if (CHECK_EOF(option_char)) {
			break;
		}

		switch (option_char) {
		case '?': /* usage */
			usage5();
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'v': /* verbose */
			verbose++;
			break;

			/* Connection info */
		case 'C': /* group or community */
			config.snmp_params.snmp_session.community = (unsigned char *)optarg;
			config.snmp_params.snmp_session.community_len = strlen(optarg);
			break;
		case 'H': /* Host or server */
			config.snmp_params.snmp_session.peername = optarg;
			break;
		case 'p': /*port number */
			// Add port to "peername" below to not rely on argument order
			port = optarg;
			break;
		case 'm': /* List of MIBS */
			miblist = optarg;
			break;
		case 'n': /* use_getnext instead of get */
			config.snmp_params.use_getnext = true;
			break;
		case 'P': /* SNMP protocol version */
			if (strcasecmp("1", optarg) == 0) {
				config.snmp_params.snmp_session.version = SNMP_VERSION_1;
			} else if (strcasecmp("2c", optarg) == 0) {
				config.snmp_params.snmp_session.version = SNMP_VERSION_2c;
			} else if (strcasecmp("3", optarg) == 0) {
				config.snmp_params.snmp_session.version = SNMP_VERSION_3;
			} else {
				die(STATE_UNKNOWN, "invalid SNMP version/protocol: %s", optarg);
			}
			snmp_version_set_explicitely = true;

			break;
		case 'N': /* SNMPv3 context name */
			config.snmp_params.snmp_session.contextName = optarg;
			config.snmp_params.snmp_session.contextNameLen = strlen(optarg);
			break;
		case 'L': /* security level */
			if (strcasecmp("noAuthNoPriv", optarg) == 0) {
				config.snmp_params.snmp_session.securityLevel = SNMP_SEC_LEVEL_NOAUTH;
			} else if (strcasecmp("authNoPriv", optarg) == 0) {
				config.snmp_params.snmp_session.securityLevel = SNMP_SEC_LEVEL_AUTHNOPRIV;
			} else if (strcasecmp("authPriv", optarg) == 0) {
				config.snmp_params.snmp_session.securityLevel = SNMP_SEC_LEVEL_AUTHPRIV;
			} else {
				die(STATE_UNKNOWN, "invalid security level: %s", optarg);
			}
			break;
		case 'U': /* security username */
			config.snmp_params.snmp_session.securityName = optarg;
			config.snmp_params.snmp_session.securityNameLen = strlen(optarg);
			break;
		case 'a': /* auth protocol */
			// SNMPv3: SHA or MD5
			// TODO Test for availability of individual protocols
			if (strcasecmp("MD5", optarg) == 0) {
				config.snmp_params.snmp_session.securityAuthProto = usmHMACMD5AuthProtocol;
				config.snmp_params.snmp_session.securityAuthProtoLen =
					OID_LENGTH(usmHMACMD5AuthProtocol);
			} else if (strcasecmp("SHA", optarg) == 0) {
				config.snmp_params.snmp_session.securityAuthProto = usmHMACSHA1AuthProtocol;
				config.snmp_params.snmp_session.securityAuthProtoLen =
					OID_LENGTH(usmHMACSHA1AuthProtocol);
			} else if (strcasecmp("SHA224", optarg) == 0) {
				config.snmp_params.snmp_session.securityAuthProto = usmHMAC128SHA224AuthProtocol;
				config.snmp_params.snmp_session.securityAuthProtoLen =
					OID_LENGTH(usmHMAC128SHA224AuthProtocol);
			} else if (strcasecmp("SHA256", optarg) == 0) {
				config.snmp_params.snmp_session.securityAuthProto = usmHMAC192SHA256AuthProtocol;
				config.snmp_params.snmp_session.securityAuthProtoLen =
					OID_LENGTH(usmHMAC192SHA256AuthProtocol);
			} else if (strcasecmp("SHA384", optarg) == 0) {
				config.snmp_params.snmp_session.securityAuthProto = usmHMAC256SHA384AuthProtocol;
				config.snmp_params.snmp_session.securityAuthProtoLen =
					OID_LENGTH(usmHMAC256SHA384AuthProtocol);
			} else if (strcasecmp("SHA512", optarg) == 0) {
				config.snmp_params.snmp_session.securityAuthProto = usmHMAC384SHA512AuthProtocol;
				config.snmp_params.snmp_session.securityAuthProtoLen =
					OID_LENGTH(usmHMAC384SHA512AuthProtocol);
			} else {
				die(STATE_UNKNOWN, "Unknown authentication protocol");
			}
			break;
		case 'x': /* priv protocol */
			if (strcasecmp("DES", optarg) == 0) {
#ifdef HAVE_USM_DES_PRIV_PROTOCOL
				config.snmp_params.snmp_session.securityAuthProto = usmDESPrivProtocol;
				config.snmp_params.snmp_session.securityAuthProtoLen =
					OID_LENGTH(usmDESPrivProtocol);
#else
				die(STATE_UNKNOWN, "DES Privacy Protocol not available on this platform");
#endif
			} else if (strcasecmp("AES", optarg) == 0) {
				config.snmp_params.snmp_session.securityAuthProto = usmAESPrivProtocol;
				config.snmp_params.snmp_session.securityAuthProtoLen =
					OID_LENGTH(usmAESPrivProtocol);
				// } else if (strcasecmp("AES128", optarg)) {
				// 	config.snmp_session.securityAuthProto = usmAES128PrivProtocol;
				// 	config.snmp_session.securityAuthProtoLen = OID_LENGTH(usmAES128PrivProtocol)
				// / OID_LENGTH(oid);
			} else if (strcasecmp("AES192", optarg) == 0) {
				config.snmp_params.snmp_session.securityAuthProto = usmAES192PrivProtocol;
				config.snmp_params.snmp_session.securityAuthProtoLen =
					OID_LENGTH(usmAES192PrivProtocol);
			} else if (strcasecmp("AES256", optarg) == 0) {
				config.snmp_params.snmp_session.securityAuthProto = usmAES256PrivProtocol;
				config.snmp_params.snmp_session.securityAuthProtoLen =
					OID_LENGTH(usmAES256PrivProtocol);
				// } else if (strcasecmp("AES192Cisco", optarg)) {
				// 	config.snmp_session.securityAuthProto = usmAES192CiscoPrivProtocol;
				// 	config.snmp_session.securityAuthProtoLen =
				// sizeof(usmAES192CiscoPrivProtocol) / sizeof(oid); } else if
				// (strcasecmp("AES256Cisco", optarg)) { config.snmp_session.securityAuthProto =
				// usmAES256CiscoPrivProtocol; 	config.snmp_session.securityAuthProtoLen =
				// sizeof(usmAES256CiscoPrivProtocol) / sizeof(oid); } else if
				// (strcasecmp("AES192Cisco2", optarg)) { config.snmp_session.securityAuthProto
				// = usmAES192Cisco2PrivProtocol; 	config.snmp_session.securityAuthProtoLen =
				// sizeof(usmAES192Cisco2PrivProtocol) / sizeof(oid); } else if
				// (strcasecmp("AES256Cisco2", optarg)) { config.snmp_session.securityAuthProto
				// = usmAES256Cisco2PrivProtocol; 	config.snmp_session.securityAuthProtoLen =
				// sizeof(usmAES256Cisco2PrivProtocol) / sizeof(oid);
			} else {
				die(STATE_UNKNOWN, "Unknown privacy protocol");
			}
			break;
		case 'A': /* auth passwd */
			authpasswd = (unsigned char *)optarg;
			break;
		case 'X': /* priv passwd */
			privpasswd = (unsigned char *)optarg;
			break;
		case 'e':
		case 'E':
			if (!is_integer(optarg)) {
				usage2(_("Retries interval must be a positive integer"), optarg);
			} else {
				config.snmp_params.snmp_session.retries = atoi(optarg);
			}
			break;
		case 't': /* timeout period */
			if (!is_integer(optarg)) {
				usage2(_("Timeout interval must be a positive integer"), optarg);
			} else {
				timeout_interval = (unsigned int)atoi(optarg);
			}
			break;

			/* Test parameters */
		case 'c': /* critical threshold */
			check_snmp_set_thresholds(optarg, config.snmp_params.test_units, oid_counter, true);
			break;
		case 'w': /* warning threshold */
			check_snmp_set_thresholds(optarg, config.snmp_params.test_units, oid_counter, false);
			break;
		case 'o': /* object identifier */
			if (strspn(optarg, "0123456789.,") != strlen(optarg)) {
				/*
				 * we have something other than digits, periods and comas,
				 * so we have a mib variable, rather than just an SNMP OID,
				 * so we have to actually read the mib files
				 */
				config.snmp_params.need_mibs = true;
			}

			for (char *ptr = strtok(optarg, ", "); ptr != NULL;
				 ptr = strtok(NULL, ", "), tmp_oid_counter++) {
				config.snmp_params.test_units[tmp_oid_counter].oid = strdup(ptr);
			}
			break;
		case 'z': /* Null OID Return Check */
			if (!is_integer(optarg)) {
				usage2(_("Exit status must be a positive integer"), optarg);
			} else {
				config.evaluation_params.nulloid_result = atoi(optarg);
			}
			break;
		case 's': /* string or substring */
			strncpy(config.evaluation_params.string_cmp_value, optarg,
					sizeof(config.evaluation_params.string_cmp_value) - 1);
			config.evaluation_params
				.string_cmp_value[sizeof(config.evaluation_params.string_cmp_value) - 1] = 0;
			config.snmp_params.test_units[eval_counter++].eval_mthd.crit_string = true;
			break;
		case 'R': /* regex */
			cflags = REG_ICASE;
			// fall through
		case 'r': /* regex */
		{
			char regex_expect[MAX_INPUT_BUFFER] = "";
			cflags |= REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
			strncpy(regex_expect, optarg, sizeof(regex_expect) - 1);
			regex_expect[sizeof(regex_expect) - 1] = 0;
			int errcode = regcomp(&config.evaluation_params.regex_cmp_value, regex_expect, cflags);
			if (errcode != 0) {
				char errbuf[MAX_INPUT_BUFFER] = "";
				regerror(errcode, &config.evaluation_params.regex_cmp_value, errbuf,
						 MAX_INPUT_BUFFER);
				printf("Could Not Compile Regular Expression: %s", errbuf);
				process_arguments_wrapper result = {
					.errorcode = ERROR,
				};
				return result;
			}
			config.snmp_params.test_units[eval_counter++].eval_mthd.crit_regex = true;
		} break;
		case 'l': /* label */
		{
			if (labels_counter >= config.snmp_params.num_of_test_units) {
				break;
			}
			char *ptr = trim_whitespaces_and_check_quoting(optarg);
			if (ptr[0] == '\'') {
				config.snmp_params.test_units[labels_counter].label = ptr + 1;
			} else {
				config.snmp_params.test_units[labels_counter].label = ptr;
			}

			while (ptr && (ptr = get_next_argument(ptr))) {
				labels_counter++;
				ptr = trim_whitespaces_and_check_quoting(ptr);
				if (ptr[0] == '\'') {
					config.snmp_params.test_units[labels_counter].label = ptr + 1;
				} else {
					config.snmp_params.test_units[labels_counter].label = ptr;
				}
			}
			labels_counter++;
		} break;
		case 'u': /* units */
		{
			if (unitv_counter >= config.snmp_params.num_of_test_units) {
				break;
			}
			char *ptr = trim_whitespaces_and_check_quoting(optarg);
			if (ptr[0] == '\'') {
				config.snmp_params.test_units[unitv_counter].unit_value = ptr + 1;
			} else {
				config.snmp_params.test_units[unitv_counter].unit_value = ptr;
			}
			while (ptr && (ptr = get_next_argument(ptr))) {
				unitv_counter++;
				ptr = trim_whitespaces_and_check_quoting(ptr);
				if (ptr[0] == '\'') {
					config.snmp_params.test_units[unitv_counter].unit_value = ptr + 1;
				} else {
					config.snmp_params.test_units[unitv_counter].unit_value = ptr;
				}
			}
			unitv_counter++;
		} break;
		case offset_index:
			config.evaluation_params.offset = strtod(optarg, NULL);
			config.evaluation_params.offset_set = true;
			break;
		case invert_search_index:
			config.evaluation_params.invert_search = false;
			break;
		case 'O':
			config.evaluation_params.use_oid_as_perf_data_label = true;
			break;
		case '4':
			// The default, do something here to be exclusive to -6 instead of doing nothing?
			connection_prefix = "udp";
			break;
		case '6':
			connection_prefix = "udp6";
			break;
		case connection_prefix_index:
			connection_prefix = optarg;
			break;
		case 'M':
			if (strspn(optarg, "0123456789.,") == strlen(optarg)) {
				config.evaluation_params.multiplier = strtod(optarg, NULL);
				config.evaluation_params.multiplier_set = true;
			}
			break;
		case ignore_mib_parsing_errors_index:
			config.snmp_params.ignore_mib_parsing_errors = true;
			break;
		case 'f': // Deprecated format option for floating point values
			break;
		case output_format_index: {
			parsed_output_format parser = mp_parse_output_format(optarg);
			if (!parser.parsing_success) {
				// TODO List all available formats here, maybe add anothoer usage function
				printf("Invalid output format: %s\n", optarg);
				exit(STATE_UNKNOWN);
			}

			config.output_format_is_set = true;
			config.output_format = parser.output_format;
			break;
		}
		case calculate_rate:
			config.evaluation_params.calculate_rate = true;
			break;
		case rate_multiplier:
			if (!is_integer(optarg) ||
				((config.evaluation_params.rate_multiplier = (unsigned int)atoi(optarg)) <= 0)) {
				usage2(_("Rate multiplier must be a positive integer"), optarg);
			}
			break;
		default:
			die(STATE_UNKNOWN, "Unknown option");
		}
	}

	if (config.snmp_params.snmp_session.peername == NULL) {
		config.snmp_params.snmp_session.peername = argv[optind];
	}

	// Build true peername here if necessary
	if (connection_prefix != NULL) {
		// We got something in the connection prefix
		if (strcasecmp(connection_prefix, "udp") == 0) {
			// The default, do nothing
		} else if (strcasecmp(connection_prefix, "tcp") == 0) {
			// use tcp/ipv4
			xasprintf(&config.snmp_params.snmp_session.peername, "tcp:%s",
					  config.snmp_params.snmp_session.peername);
		} else if (strcasecmp(connection_prefix, "tcp6") == 0 ||
				   strcasecmp(connection_prefix, "tcpv6") == 0 ||
				   strcasecmp(connection_prefix, "tcpipv6") == 0 ||
				   strcasecmp(connection_prefix, "udp6") == 0 ||
				   strcasecmp(connection_prefix, "udpipv6") == 0 ||
				   strcasecmp(connection_prefix, "udpv6") == 0) {
			// Man page (or net-snmp) code says IPv6 addresses should be wrapped in [], but it
			// works anyway therefore do nothing here
			xasprintf(&config.snmp_params.snmp_session.peername, "%s:%s", connection_prefix,
					  config.snmp_params.snmp_session.peername);
		} else if (strcmp(connection_prefix, "tls") == 0) {
			// TODO: Anything else to do here?
			xasprintf(&config.snmp_params.snmp_session.peername, "tls:%s",
					  config.snmp_params.snmp_session.peername);
		} else if (strcmp(connection_prefix, "dtls") == 0) {
			// TODO: Anything else to do here?
			xasprintf(&config.snmp_params.snmp_session.peername, "dtls:%s",
					  config.snmp_params.snmp_session.peername);
		} else if (strcmp(connection_prefix, "unix") == 0) {
			// TODO: Check whether this is a valid path?
			xasprintf(&config.snmp_params.snmp_session.peername, "unix:%s",
					  config.snmp_params.snmp_session.peername);
		} else if (strcmp(connection_prefix, "ipx") == 0) {
			xasprintf(&config.snmp_params.snmp_session.peername, "ipx:%s",
					  config.snmp_params.snmp_session.peername);
		} else {
			// Don't know that prefix, die here
			die(STATE_UNKNOWN, "Unknown connection prefix");
		}
	}

	/* Check server_address is given */
	if (config.snmp_params.snmp_session.peername == NULL) {
		die(STATE_UNKNOWN, _("No host specified\n"));
	}

	if (port != NULL) {
		xasprintf(&config.snmp_params.snmp_session.peername, "%s:%s",
				  config.snmp_params.snmp_session.peername, port);
	}

	/* check whether to load locally installed MIBS (CPU/disk intensive) */
	if (miblist == NULL) {
		if (config.snmp_params.need_mibs) {
			setenv("MIBLS", DEFAULT_MIBLIST, 1);
		} else {
			setenv("MIBLS", "NONE", 1);
			miblist = ""; /* don't read any mib files for numeric oids */
		}
	} else {
		// Blatantly stolen from snmplib/snmp_parse_args
		setenv("MIBS", miblist, 1);
	}

	// Historical default is SNMP v2c
	if (!snmp_version_set_explicitely && config.snmp_params.snmp_session.community != NULL) {
		config.snmp_params.snmp_session.version = SNMP_VERSION_2c;
	}

	if ((config.snmp_params.snmp_session.version == SNMP_VERSION_1) ||
		(config.snmp_params.snmp_session.version == SNMP_VERSION_2c)) {     /* snmpv1 or snmpv2c */
																			/*
																			config.numauthpriv = 2;
																			config.authpriv = calloc(config.numauthpriv, sizeof(char *));
																			config.authpriv[0] = strdup("-c");
																			config.authpriv[1] = strdup(community);
																			*/
	} else if (config.snmp_params.snmp_session.version == SNMP_VERSION_3) { /* snmpv3 args */
		// generate keys for priv and auth here (if demanded)

		if (config.snmp_params.snmp_session.securityName == NULL) {
			die(STATE_UNKNOWN, _("Required parameter: %s\n"), "secname");
		}

		switch (config.snmp_params.snmp_session.securityLevel) {
		case SNMP_SEC_LEVEL_AUTHPRIV: {
			if (authpasswd == NULL) {
				die(STATE_UNKNOWN,
					"No authentication passphrase was given, but authorization was requested");
			}
			// auth and priv
			int priv_key_generated = generate_Ku(
				config.snmp_params.snmp_session.securityPrivProto,
				(unsigned int)config.snmp_params.snmp_session.securityPrivProtoLen, authpasswd,
				strlen((const char *)authpasswd), config.snmp_params.snmp_session.securityPrivKey,
				&config.snmp_params.snmp_session.securityPrivKeyLen);

			if (priv_key_generated != SNMPERR_SUCCESS) {
				die(STATE_UNKNOWN, "Failed to generate privacy key");
			}
		}
		// fall through
		case SNMP_SEC_LEVEL_AUTHNOPRIV: {
			if (privpasswd == NULL) {
				die(STATE_UNKNOWN, "No privacy passphrase was given, but privacy was requested");
			}
			int auth_key_generated = generate_Ku(
				config.snmp_params.snmp_session.securityAuthProto,
				(unsigned int)config.snmp_params.snmp_session.securityAuthProtoLen, privpasswd,
				strlen((const char *)privpasswd), config.snmp_params.snmp_session.securityAuthKey,
				&config.snmp_params.snmp_session.securityAuthKeyLen);

			if (auth_key_generated != SNMPERR_SUCCESS) {
				die(STATE_UNKNOWN, "Failed to generate privacy key");
			}
		} break;
		case SNMP_SEC_LEVEL_NOAUTH:
			// No auth, no priv, not much todo
			break;
		}
	}

	process_arguments_wrapper result = {
		.config = config,
		.errorcode = OK,
	};
	return result;
}

/* trim leading whitespace
	 if there is a leading quote, make sure it balances */
char *trim_whitespaces_and_check_quoting(char *str) {
	str += strspn(str, " \t\r\n"); /* trim any leading whitespace */
	if (str[0] == '\'') {          /* handle SIMPLE quoted strings */
		if (strlen(str) == 1 || !strstr(str + 1, "'")) {
			die(STATE_UNKNOWN, _("Unbalanced quotes\n"));
		}
	}
	return str;
}

/* if there's a leading quote, advance to the trailing quote
	 set the trailing quote to '\x0'
	 if the string continues, advance beyond the comma */

char *get_next_argument(char *str) {
	if (str[0] == '\'') {
		str[0] = 0;
		if (strlen(str) > 1) {
			str = strstr(str + 1, "'");
			return (++str);
		}
		return NULL;
	}
	if (str[0] == ',') {
		str[0] = 0;
		if (strlen(str) > 1) {
			return (++str);
		}
		return NULL;
	}
	if ((str = strstr(str, ",")) && strlen(str) > 1) {
		str[0] = 0;
		return (++str);
	}
	return NULL;
}

void print_help(void) {
	print_revision(progname, NP_VERSION);

	printf(COPYRIGHT, copyright, email);

	printf("%s\n", _("Check status of remote machines and obtain system information via SNMP"));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);
	printf(UT_HOST_PORT, 'p', DEFAULT_PORT);

	/* SNMP and Authentication Protocol */
	printf(" %s\n", "-n, --next");
	printf("    %s\n", _("Use SNMP GETNEXT instead of SNMP GET"));
	printf(" %s\n", "-P, --protocol=[1|2c|3]");
	printf("    %s\n", _("SNMP protocol version"));
	printf(" %s\n", "-N, --context=CONTEXT");
	printf("    %s\n", _("SNMPv3 context"));
	printf(" %s\n", "-L, --seclevel=[noAuthNoPriv|authNoPriv|authPriv]");
	printf("    %s\n", _("SNMPv3 securityLevel"));
	printf(" %s\n", "-a, --authproto=[MD5|SHA]");
	printf("    %s\n", _("SNMPv3 auth proto"));
#ifdef HAVE_USM_DES_PRIV_PROTOCOL
	printf(" %s\n", "-x, --privproto=[DES|AES]");
	printf("    %s\n", _("SNMPv3 priv proto (default DES)"));
#else
	printf(" %s\n", "-x, --privproto=[AES]");
	printf("    %s\n", _("SNMPv3 priv proto (default AES)"));
#endif

	/* Authentication Tokens*/
	printf(" %s\n", "-C, --community=STRING");
	printf("    %s ", _("Optional community string for SNMP communication"));
	printf("(%s \"%s\")\n", _("default is"), DEFAULT_COMMUNITY);
	printf(" %s\n", "-U, --secname=USERNAME");
	printf("    %s\n", _("SNMPv3 username"));
	printf(" %s\n", "-A, --authpasswd=PASSWORD");
	printf("    %s\n", _("SNMPv3 authentication password"));
	printf(" %s\n", "-X, --privpasswd=PASSWORD");
	printf("    %s\n", _("SNMPv3 privacy password"));
	printf(" %s\n", "--connection-prefix");
	printf("    Connection prefix, may be one of udp, udp6, tcp, unix, ipx, udp6, udpv6, udpipv6, "
		   "tcp6, tcpv6, tcpipv6, tls, dtls - "
		   "default is \"udp\"\n");

	/* OID Stuff */
	printf(" %s\n", "-o, --oid=OID(s)");
	printf("    %s\n", _("Object identifier(s) or SNMP variables whose value you wish to query"));
	printf(" %s\n", "-m, --miblist=STRING");
	printf("    %s\n",
		   _("List of MIBS to be loaded (default = none if using numeric OIDs or 'ALL'"));
	printf("    %s\n", _("for symbolic OIDs.)"));
	printf("    %s\n", _("Any data on the right hand side of the delimiter is considered"));
	printf("    %s\n", _("to be the data that should be used in the evaluation."));
	printf(" %s\n", "-z, --nulloid=#");
	printf("    %s\n", _("If the check returns a 0 length string or NULL value"));
	printf("    %s\n", _("This option allows you to choose what status you want it to exit"));
	printf("    %s\n", _("Excluding this option renders the default exit of 3(STATE_UNKNOWN)"));
	printf("    %s\n", _("0 = OK"));
	printf("    %s\n", _("1 = WARNING"));
	printf("    %s\n", _("2 = CRITICAL"));
	printf("    %s\n", _("3 = UNKNOWN"));

	/* Tests Against Integers */
	printf(" %s\n", "-w, --warning=THRESHOLD(s)");
	printf("    %s\n", _("Warning threshold range(s)"));
	printf(" %s\n", "-c, --critical=THRESHOLD(s)");
	printf("    %s\n", _("Critical threshold range(s)"));
	printf(" %s\n", "--offset=OFFSET");
	printf("    %s\n", _("Add/subtract the specified OFFSET to numeric sensor data"));

	/* Tests Against Strings */
	printf(" %s\n", "-s, --string=STRING");
	printf("    %s\n", _("Return OK state (for that OID) if STRING is an exact match"));
	printf(" %s\n", "-r, --ereg=REGEX");
	printf("    %s\n",
		   _("Return OK state (for that OID) if extended regular expression REGEX matches"));
	printf(" %s\n", "-R, --eregi=REGEX");
	printf("    %s\n",
		   _("Return OK state (for that OID) if case-insensitive extended REGEX matches"));
	printf(" %s\n", "--invert-search");
	printf("    %s\n", _("Invert search result (CRITICAL if found)"));

	/* Output Formatting */
	printf(" %s\n", "-l, --label=STRING");
	printf("    %s\n", _("Prefix label for output from plugin"));
	printf(" %s\n", "-u, --units=STRING");
	printf("    %s\n", _("Units label(s) for output data (e.g., 'sec.')."));
	printf(" %s\n", "-M, --multiplier=FLOAT");
	printf("    %s\n", _("Multiplies current value, 0 < n < 1 works as divider, defaults to 1"));
	printf(UT_OUTPUT_FORMAT);

	printf(UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);
	printf("    %s\n", _("NOTE the final timeout value is calculated using this formula: "
						 "timeout_interval * retries + 5"));
	printf(" %s\n", "-e, --retries=INTEGER");
	printf("    %s%i\n", _("Number of retries to be used in the requests, default: "),
		   DEFAULT_RETRIES);

	printf(" %s\n", "-O, --perf-oids");
	printf("    %s\n", _("Label performance data with OIDs instead of --label's"));

	printf(" %s\n", "--ignore-mib-parsing-errors");
	printf("    %s\n", _("Do to not print errors encountered when parsing MIB files"));

	printf(UT_VERBOSE);

	printf("\n");
	printf("%s\n", _("This plugin relies (links against) on the NET-SNMP libraries."));
	printf("%s\n",
		   _("if you don't have the libraries installed, you will need to download them from"));
	printf("%s\n", _("http://net-snmp.sourceforge.net before you can use this plugin."));

	printf("\n");
	printf("%s\n", _("Notes:"));
	printf(" %s\n",
		   _("- Multiple OIDs (and labels) may be indicated by a comma or space-delimited  "));
	printf("   %s\n", _("list (lists with internal spaces must be quoted)."));

	printf(" -%s", UT_THRESHOLDS_NOTES);

	printf(" %s\n",
		   _("- When checking multiple OIDs, separate ranges by commas like '-w 1:10,1:,:20'"));
	printf(" %s\n", _("- Note that only one string and one regex may be checked at present"));
	printf(" %s\n",
		   _("- All evaluation methods other than PR, STR, and SUBSTR expect that the value"));
	printf("   %s\n", _("returned from the SNMP query is an unsigned integer."));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s -H <ip_address> -o <OID> [-w warn_range] [-c crit_range]\n", progname);
	printf("[-C community] [-s string] [-r regex] [-R regexi] [-t timeout] [-e retries]\n");
	printf("[-l label] [-u units] [-p port-number] [-d delimiter] [-D output-delimiter]\n");
	printf("[-m miblist] [-P snmp version] [-N context] [-L seclevel] [-U secname]\n");
	printf("[-a authproto] [-A authpasswd] [-x privproto] [-X privpasswd] [-4|6]\n");
	printf("[-M multiplier]\n");
}
