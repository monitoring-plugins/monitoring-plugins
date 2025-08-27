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
#include "../lib/utils_cmd.h"
#include "../lib/thresholds.h"
#include "../lib/utils_base.h"
#include "../lib/output.h"
#include "../lib/perfdata.h"
#include "check_snmp.d/check_snmp_helpers.h"

#include <bits/getopt_core.h>
#include <bits/getopt_ext.h>
#include <strings.h>
#include <stdint.h>

#include "check_snmp.d/config.h"
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
#include <assert.h>

const char DEFAULT_COMMUNITY[] = "public";
const char DEFAULT_MIBLIST[] = "ALL";
#define DEFAULT_AUTH_PROTOCOL "MD5"
#define DEFAULT_PRIV_PROTOCOL "DES"
#define DEFAULT_DELIMITER     "="
#define DEFAULT_BUFFER_SIZE   100

/* Longopts only arguments */
#define L_INVERT_SEARCH             CHAR_MAX + 3
#define L_OFFSET                    CHAR_MAX + 4
#define L_IGNORE_MIB_PARSING_ERRORS CHAR_MAX + 5
#define L_CONNECTION_PREFIX         CHAR_MAX + 6

typedef struct proces_arguments_wrapper {
	int errorcode;
	check_snmp_config config;
} process_arguments_wrapper;

static process_arguments_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static char *trim_whitespaces_and_check_quoting(char *str);
static char *get_next_argument(char *str);
void print_usage(void);
void print_help(void);

static int verbose = 0;

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	timeout_interval = DEFAULT_SOCKET_TIMEOUT;

	np_init((char *)progname, argc, argv);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	np_set_args(argc, argv);

	// Initialize net-snmp before touching the session we are going to use
	init_snmp("check_snmp");

	time_t current_time;
	time(&current_time);

	process_arguments_wrapper paw_tmp = process_arguments(argc, argv);
	if (paw_tmp.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	check_snmp_config config = paw_tmp.config;

	if (config.ignore_mib_parsing_errors) {
		char *opt_toggle_res = snmp_mib_toggle_options("e");
		if (opt_toggle_res != NULL) {
			die(STATE_UNKNOWN, "Unable to disable MIB parsing errors");
		}
	}

	struct snmp_pdu *pdu = NULL;
	if (config.use_getnext) {
		pdu = snmp_pdu_create(SNMP_MSG_GETNEXT);
	} else {
		pdu = snmp_pdu_create(SNMP_MSG_GET);
	}

	for (size_t i = 0; i < config.num_of_test_units; i++) {
		assert(config.test_units[i].oid != NULL);
		if (verbose > 0) {
			printf("OID %zu to parse: %s\n", i, config.test_units[i].oid);
		}

		oid tmp_OID[MAX_OID_LEN];
		size_t tmp_OID_len = MAX_OID_LEN;
		if (snmp_parse_oid(config.test_units[i].oid, tmp_OID, &tmp_OID_len) != NULL) {
			// success
			snmp_add_null_var(pdu, tmp_OID, tmp_OID_len);
		} else {
			// failed
			snmp_perror("Parsing failure");
			die(STATE_UNKNOWN, "Failed to parse OID\n");
		}
	}

	/* Set signal handling and alarm */
	if (signal(SIGALRM, runcmd_timeout_alarm_handler) == SIG_ERR) {
		usage4(_("Cannot catch SIGALRM"));
	}

	const int timeout_safety_tolerance = 5;
	alarm(timeout_interval * config.snmp_session.retries + timeout_safety_tolerance);

	struct snmp_session *active_session = snmp_open(&config.snmp_session);
	if (active_session == NULL) {
		int pcliberr = 0;
		int psnmperr = 0;
		char *pperrstring = NULL;
		snmp_error (&config.snmp_session, &pcliberr , &psnmperr, &pperrstring);
		die(STATE_UNKNOWN, "Failed to open SNMP session: %s\n", pperrstring);
	}

	struct snmp_pdu *response = NULL;
	int snmp_query_status = snmp_synch_response(active_session, pdu, &response);

	if (!(snmp_query_status == STAT_SUCCESS && response->errstat == SNMP_ERR_NOERROR)) {
		int pcliberr = 0;
		int psnmperr = 0;
		char *pperrstring = NULL;
		snmp_error (active_session, &pcliberr , &psnmperr, &pperrstring);

		if (psnmperr == SNMPERR_TIMEOUT) {
			// We exit with critical here for some historical reason
			die(STATE_CRITICAL, "SNMP query ran into a timeout\n");
		}
		die(STATE_UNKNOWN, "SNMP query failed: %s\n", pperrstring);
	}

	snmp_close(active_session);

	/* disable alarm again */
	alarm(0);

	mp_check overall = mp_check_init();

	mp_subcheck sc_successfull_query = mp_subcheck_init();
	xasprintf(&sc_successfull_query.output, "SNMP query was successful");
	sc_successfull_query = mp_set_subcheck_state(sc_successfull_query, STATE_OK);
	mp_add_subcheck_to_check(&overall, sc_successfull_query);

	// We got the the query results, now process them
	size_t loop_index = 0;
	for (netsnmp_variable_list *vars = response->variables; vars;
		 vars = vars->next_variable, loop_index++) {
		mp_subcheck sc_oid_test = mp_subcheck_init();

		if (verbose > 0) {
			printf("loop_index: %zu\n", loop_index);
		}

		if ((config.test_units[loop_index].label != NULL) &&
			(strcmp(config.test_units[loop_index].label, "") != 0)) {
			xasprintf(&sc_oid_test.output, "%s - ", config.test_units[loop_index].label);
		} else {
			sc_oid_test.output = strdup("");
		}

		char oid_string[(MAX_OID_LEN * 2) + 1];
		memset(oid_string, 0, (MAX_OID_LEN * 2) + 1);

		int oid_string_result =
			snprint_objid(oid_string, (MAX_OID_LEN * 2) + 1, vars->name, vars->name_length);
		if (oid_string_result <= 0) {
			// TODO error here
		}

		if (verbose > 2) {
			printf("Processing oid %s\n", oid_string);
		}

		mp_perfdata_value pd_result_val = {0};
		xasprintf(&sc_oid_test.output, "%sOID: %s", sc_oid_test.output, oid_string);
		sc_oid_test = mp_set_subcheck_default_state(sc_oid_test, STATE_OK);

		switch (vars->type) {
		case ASN_OCTET_STR: {
			if (verbose) {
				printf("Debug: Got a string\n");
			}
			char *tmp = (char *)vars->val.string;
			xasprintf(&sc_oid_test.output, "%s - Value: %s", sc_oid_test.output, tmp);

			if (strlen(tmp) == 0) {
				sc_oid_test = mp_set_subcheck_state(sc_oid_test, config.nulloid_result);
			}

			// String matching test
			if ((config.test_units[loop_index].eval_mthd.crit_string)) {
				if (strcmp(tmp, config.string_cmp_value)) {
					sc_oid_test = mp_set_subcheck_state(
						sc_oid_test, (config.invert_search) ? STATE_CRITICAL : STATE_OK);
				} else {
					sc_oid_test = mp_set_subcheck_state(
						sc_oid_test, (config.invert_search) ? STATE_OK : STATE_CRITICAL);
				}
			} else if (config.test_units[loop_index].eval_mthd.crit_regex) {
				const int nmatch = config.regex_cmp_value.re_nsub + 1;
				regmatch_t pmatch[nmatch];
				memset(pmatch, '\0', sizeof(regmatch_t) * nmatch);

				int excode = regexec(&config.regex_cmp_value, tmp, nmatch, pmatch, 0);
				if (excode == 0) {
					sc_oid_test = mp_set_subcheck_state(
						sc_oid_test, (config.invert_search) ? STATE_OK : STATE_CRITICAL);
				} else if (excode != REG_NOMATCH) {
					char errbuf[MAX_INPUT_BUFFER] = "";
					regerror(excode, &config.regex_cmp_value, errbuf, MAX_INPUT_BUFFER);
					printf(_("Execute Error: %s\n"), errbuf);
					exit(STATE_CRITICAL);
				} else { // REG_NOMATCH
					sc_oid_test = mp_set_subcheck_state(
						sc_oid_test, config.invert_search ? STATE_CRITICAL : STATE_OK);
				}
			}

			mp_add_subcheck_to_check(&overall, sc_oid_test);
		}
			continue;
		case ASN_OPAQUE:
			if (verbose) {
				printf("Debug: Got OPAQUE\n");
			}
			break;
		case ASN_COUNTER64: {
			if (verbose) {
				printf("Debug: Got counter64\n");
			}
			struct counter64 tmp = *(vars->val.counter64);
			uint64_t counter = (tmp.high << 32) + tmp.low;
			counter *= config.multiplier;
			counter += config.offset;
			pd_result_val = mp_create_pd_value(counter);
		} break;
		/* Numerical values */
		case ASN_GAUGE: // same as ASN_UNSIGNED
		case ASN_TIMETICKS:
		case ASN_COUNTER:
		case ASN_UINTEGER: {
			if (verbose) {
				printf("Debug: Got a Integer like\n");
			}
			unsigned long tmp = *(vars->val.integer);
			tmp *= config.multiplier;

			tmp += config.offset;
			pd_result_val = mp_create_pd_value(tmp);
			break;
		}
		case ASN_INTEGER: {
			if (verbose) {
				printf("Debug: Got a Integer\n");
			}
			unsigned long tmp = *(vars->val.integer);
			tmp *= config.multiplier;

			tmp += config.offset;
			pd_result_val = mp_create_pd_value(tmp);
		} break;
		case ASN_FLOAT: {
			if (verbose) {
				printf("Debug: Got a float\n");
			}
			float tmp = *(vars->val.floatVal);
			tmp *= config.multiplier;

			tmp += config.offset;
			pd_result_val = mp_create_pd_value(tmp);
			break;
		}
		case ASN_DOUBLE: {
			if (verbose) {
				printf("Debug: Got a double\n");
			}
			double tmp = *(vars->val.doubleVal);
			tmp *= config.multiplier;
			tmp += config.offset;
			pd_result_val = mp_create_pd_value(tmp);
			break;
		}
		case ASN_IPADDRESS:
			if (verbose) {
				printf("Debug: Got an IP address\n");
			}
			continue;
		default:
			if (verbose) {
				printf("Debug: Got a unmatched result type: %hhu\n", vars->type);
			}
			// TODO: Error here?
			continue;
		}

		// some kind of numerical value
		mp_perfdata pd_num_val = {
			.value = pd_result_val,
		};

		if (!config.use_perf_data_labels_from_input) {
			// Use oid for perdata label
			pd_num_val.label = strdup(oid_string);
			// TODO strdup error checking
		} else if (config.test_units[loop_index].label != NULL ||
				   strcmp(config.test_units[loop_index].label, "") != 0) {
			pd_num_val.label = config.test_units[loop_index].label;
		}

		if (config.test_units[loop_index].unit_value != NULL &&
			strcmp(config.test_units[loop_index].unit_value, "") != 0) {
			pd_num_val.uom = config.test_units[loop_index].unit_value;
		}

		xasprintf(&sc_oid_test.output, "%s Value: %s", sc_oid_test.output,
				  pd_value_to_string(pd_result_val));

		if (config.test_units[loop_index].unit_value != NULL &&
			strcmp(config.test_units[loop_index].unit_value, "") != 0) {
			xasprintf(&sc_oid_test.output, "%s%s", sc_oid_test.output,
					  config.test_units[loop_index].unit_value);
		}

		if (config.test_units[loop_index].threshold.warning_is_set ||
			config.test_units[loop_index].threshold.critical_is_set) {
			pd_num_val = mp_pd_set_thresholds(pd_num_val, config.test_units[loop_index].threshold);
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

		mp_add_subcheck_to_check(&overall, sc_oid_test);
	}

	mp_exit(overall);
}

/* process command-line arguments */
static process_arguments_wrapper process_arguments(int argc, char **argv) {
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
		{"offset", required_argument, 0, L_OFFSET},
		{"invert-search", no_argument, 0, L_INVERT_SEARCH},
		{"perf-oids", no_argument, 0, 'O'},
		{"ipv4", no_argument, 0, '4'},
		{"ipv6", no_argument, 0, '6'},
		{"multiplier", required_argument, 0, 'M'},
		{"ignore-mib-parsing-errors", no_argument, false, L_IGNORE_MIB_PARSING_ERRORS},
		{"connection-prefix", required_argument, 0, L_CONNECTION_PREFIX},
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

		if (option_char == -1 || option_char == EOF) {
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
	config.test_units = tmp;
	config.num_of_test_units = oid_counter;

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
	// TODO error checking
	while (true) {
		int option_char = getopt_long(
			argc, argv,
			"nhvVO46t:c:w:H:C:o:e:E:d:D:s:t:R:r:l:u:p:m:P:N:L:U:a:x:A:X:M:f:z:", longopts, &option);

		if (option_char == -1 || option_char == EOF) {
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
			config.snmp_session.community = (unsigned char *)optarg;
			config.snmp_session.community_len = strlen(optarg);
			break;
		case 'H': /* Host or server */
			config.snmp_session.peername = optarg;
			break;
		case 'p': /*port number */
			// Add port to "peername" below to not rely on argument order
			port = optarg;
			break;
		case 'm': /* List of MIBS */
			miblist = optarg;
			break;
		case 'n': /* use_getnext instead of get */
			config.use_getnext = true;
			break;
		case 'P': /* SNMP protocol version */
			if (strcasecmp("1", optarg) == 0) {
				config.snmp_session.version = SNMP_VERSION_1;
			} else if (strcasecmp("2c", optarg) == 0) {
				config.snmp_session.version = SNMP_VERSION_2c;
			} else if (strcasecmp("3", optarg) == 0) {
				config.snmp_session.version = SNMP_VERSION_3;
			} else {
				die(STATE_UNKNOWN, "invalid SNMP version/protocol: %s", optarg);
			}
			break;
		case 'N': /* SNMPv3 context name */
			config.snmp_session.contextName = optarg;
			config.snmp_session.contextNameLen = strlen(optarg);
			break;
		case 'L': /* security level */
			if (strcasecmp("noAuthNoPriv", optarg) == 0) {
				config.snmp_session.securityLevel = SNMP_SEC_LEVEL_NOAUTH;
			} else if (strcasecmp("authNoPriv", optarg) == 0) {
				config.snmp_session.securityLevel = SNMP_SEC_LEVEL_AUTHNOPRIV;
			} else if (strcasecmp("authPriv", optarg) == 0) {
				config.snmp_session.securityLevel = SNMP_SEC_LEVEL_AUTHPRIV;
			} else {
				die(STATE_UNKNOWN, "invalid security level: %s", optarg);
			}
			break;
		case 'U': /* security username */
			config.snmp_session.securityName = optarg;
			config.snmp_session.securityNameLen = strlen(optarg);
			break;
		case 'a': /* auth protocol */
			// SNMPv3: SHA or MD5
			// TODO Test for availability of individual protocols
			if (strcasecmp("MD5", optarg) == 0) {
				config.snmp_session.securityAuthProto = usmHMACMD5AuthProtocol;
				config.snmp_session.securityAuthProtoLen = OID_LENGTH(usmHMACMD5AuthProtocol);
			} else if (strcasecmp("SHA", optarg) == 0) {
				config.snmp_session.securityAuthProto = usmHMACSHA1AuthProtocol;
				config.snmp_session.securityAuthProtoLen = OID_LENGTH(usmHMACSHA1AuthProtocol);
			} else if (strcasecmp("SHA224", optarg) == 0) {
				config.snmp_session.securityAuthProto = usmHMAC128SHA224AuthProtocol;
				config.snmp_session.securityAuthProtoLen = OID_LENGTH(usmHMAC128SHA224AuthProtocol);
			} else if (strcasecmp("SHA256", optarg) == 0) {
				config.snmp_session.securityAuthProto = usmHMAC192SHA256AuthProtocol;
				config.snmp_session.securityAuthProtoLen = OID_LENGTH(usmHMAC192SHA256AuthProtocol);
			} else if (strcasecmp("SHA384", optarg) == 0) {
				config.snmp_session.securityAuthProto = usmHMAC256SHA384AuthProtocol;
				config.snmp_session.securityAuthProtoLen = OID_LENGTH(usmHMAC256SHA384AuthProtocol);
			} else if (strcasecmp("SHA512", optarg) == 0) {
				config.snmp_session.securityAuthProto = usmHMAC384SHA512AuthProtocol;
				config.snmp_session.securityAuthProtoLen = OID_LENGTH(usmHMAC384SHA512AuthProtocol);
			} else {
				die(STATE_UNKNOWN, "Unknown authentication protocol");
			}
			break;
		case 'x': /* priv protocol */
			if (strcasecmp("DES", optarg) == 0) {
				config.snmp_session.securityAuthProto = usmDESPrivProtocol;
				config.snmp_session.securityAuthProtoLen = OID_LENGTH(usmDESPrivProtocol);
			} else if (strcasecmp("AES", optarg) == 0) {
				config.snmp_session.securityAuthProto = usmAESPrivProtocol;
				config.snmp_session.securityAuthProtoLen = OID_LENGTH(usmAESPrivProtocol);
				// } else if (strcasecmp("AES128", optarg)) {
				// 	config.snmp_session.securityAuthProto = usmAES128PrivProtocol;
				// 	config.snmp_session.securityAuthProtoLen = OID_LENGTH(usmAES128PrivProtocol)
				// / OID_LENGTH(oid);
			} else if (strcasecmp("AES192", optarg) == 0) {
				config.snmp_session.securityAuthProto = usmAES192PrivProtocol;
				config.snmp_session.securityAuthProtoLen = OID_LENGTH(usmAES192PrivProtocol);
			} else if (strcasecmp("AES256", optarg) == 0) {
				config.snmp_session.securityAuthProto = usmAES256PrivProtocol;
				config.snmp_session.securityAuthProtoLen = OID_LENGTH(usmAES256PrivProtocol);
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
				config.snmp_session.retries = atoi(optarg);
			}
			break;
		case 't': /* timeout period */
			if (!is_integer(optarg)) {
				usage2(_("Timeout interval must be a positive integer"), optarg);
			} else {
				timeout_interval = atoi(optarg);
			}
			break;

			/* Test parameters */
		case 'c': /* critical threshold */
			check_snmp_set_thresholds(optarg, config.test_units, oid_counter, true);
			break;
		case 'w': /* warning threshold */
			check_snmp_set_thresholds(optarg, config.test_units, oid_counter, false);
			break;
		case 'o': /* object identifier */
			if (strspn(optarg, "0123456789.,") != strlen(optarg)) {
				/*
				 * we have something other than digits, periods and comas,
				 * so we have a mib variable, rather than just an SNMP OID,
				 * so we have to actually read the mib files
				 */
				config.need_mibs = true;
			}

			for (char *ptr = strtok(optarg, ", "); ptr != NULL;
				 ptr = strtok(NULL, ", "), tmp_oid_counter++) {
				config.test_units[tmp_oid_counter].oid = strdup(ptr);
			}
			break;
		case 'z': /* Null OID Return Check */
			if (!is_integer(optarg)) {
				usage2(_("Exit status must be a positive integer"), optarg);
			} else {
				config.nulloid_result = atoi(optarg);
			}
			break;
		case 's': /* string or substring */
			strncpy(config.string_cmp_value, optarg, sizeof(config.string_cmp_value) - 1);
			config.string_cmp_value[sizeof(config.string_cmp_value) - 1] = 0;
			config.test_units[eval_counter++].eval_mthd.crit_string = true;
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
			int errcode = regcomp(&config.regex_cmp_value, regex_expect, cflags);
			if (errcode != 0) {
				char errbuf[MAX_INPUT_BUFFER] = "";
				regerror(errcode, &config.regex_cmp_value, errbuf, MAX_INPUT_BUFFER);
				printf("Could Not Compile Regular Expression: %s", errbuf);
				process_arguments_wrapper result = {
					.errorcode = ERROR,
				};
				return result;
			}
			config.test_units[eval_counter++].eval_mthd.crit_regex = true;
		} break;
		case 'l': /* label */
		{
			if (labels_counter >= config.num_of_test_units) {
				break;
			}
			char *ptr = trim_whitespaces_and_check_quoting(optarg);
			if (ptr[0] == '\'') {
				config.test_units[labels_counter].label = ptr + 1;
			} else {
				config.test_units[labels_counter].label = ptr;
			}

			while (ptr && (ptr = get_next_argument(ptr))) {
				labels_counter++;
				ptr = trim_whitespaces_and_check_quoting(ptr);
				if (ptr[0] == '\'') {
					config.test_units[labels_counter].label = ptr + 1;
				} else {
					config.test_units[labels_counter].label = ptr;
				}
			}
			labels_counter++;
		} break;
		case 'u': /* units */
		{
			if (unitv_counter >= config.num_of_test_units) {
				break;
			}
			char *ptr = trim_whitespaces_and_check_quoting(optarg);
			if (ptr[0] == '\'') {
				config.test_units[unitv_counter].unit_value = ptr + 1;
			} else {
				config.test_units[unitv_counter].unit_value = ptr;
			}
			while (ptr && (ptr = get_next_argument(ptr))) {
				unitv_counter++;
				ptr = trim_whitespaces_and_check_quoting(ptr);
				if (ptr[0] == '\'') {
					config.test_units[unitv_counter].unit_value = ptr + 1;
				} else {
					config.test_units[unitv_counter].unit_value = ptr;
				}
			}
			unitv_counter++;
		} break;
		case L_OFFSET:
			config.offset = strtod(optarg, NULL);
			break;
		case L_INVERT_SEARCH:
			config.invert_search = false;
			break;
		case 'O':
			config.use_perf_data_labels_from_input = true;
			break;
		case '4':
			// The default, do something here to be exclusive to -6 instead of doing nothing?
			connection_prefix = "udp";
			break;
		case '6':
			connection_prefix = "udp6";
			break;
		case L_CONNECTION_PREFIX:
			connection_prefix = optarg;
			break;
		case 'M':
			if (strspn(optarg, "0123456789.,") == strlen(optarg)) {
				config.multiplier = strtod(optarg, NULL);
			}
			break;
		case L_IGNORE_MIB_PARSING_ERRORS:
			config.ignore_mib_parsing_errors = true;
		}
	}

	if (config.snmp_session.peername == NULL) {
		config.snmp_session.peername = argv[optind];
	}

	// Build true peername here if necessary
	if (connection_prefix != NULL) {
		// We got something in the connection prefix
		if (strcasecmp(connection_prefix, "udp") == 0) {
			// The default, do nothing
		} else if (strcasecmp(connection_prefix, "tcp") == 0) {
			// use tcp/ipv4
			xasprintf(&config.snmp_session.peername, "tcp:%s", config.snmp_session.peername);
		} else if (strcasecmp(connection_prefix, "tcp6") == 0 ||
				   strcasecmp(connection_prefix, "tcpv6") == 0 ||
				   strcasecmp(connection_prefix, "tcpipv6") == 0 ||
				   strcasecmp(connection_prefix, "udp6") == 0 ||
				   strcasecmp(connection_prefix, "udpipv6") == 0 ||
				   strcasecmp(connection_prefix, "udpv6") == 0) {
			// Man page (or net-snmp) code says IPv6 addresses should be wrapped in [], but it
			// works anyway therefore do nothing here
			xasprintf(&config.snmp_session.peername, "%s:%s", connection_prefix,
					  config.snmp_session.peername);
		} else if (strcmp(connection_prefix, "tls") == 0) {
			// TODO: Anything else to do here?
			xasprintf(&config.snmp_session.peername, "tls:%s", config.snmp_session.peername);
		} else if (strcmp(connection_prefix, "dtls") == 0) {
			// TODO: Anything else to do here?
			xasprintf(&config.snmp_session.peername, "dtls:%s", config.snmp_session.peername);
		} else if (strcmp(connection_prefix, "unix") == 0) {
			// TODO: Check whether this is a valid path?
			xasprintf(&config.snmp_session.peername, "unix:%s", config.snmp_session.peername);
		} else if (strcmp(connection_prefix, "ipx") == 0) {
			xasprintf(&config.snmp_session.peername, "ipx:%s", config.snmp_session.peername);
		} else {
			// Don't know that prefix, die here
			die(STATE_UNKNOWN, "Unknown connection prefix");
		}
	}

	/* Check server_address is given */
	if (config.snmp_session.peername == NULL) {
		die(STATE_UNKNOWN, _("No host specified\n"));
	}

	if (port != NULL) {
		xasprintf(&config.snmp_session.peername, "%s:%s", config.snmp_session.peername, port);
	}

	/* check whether to load locally installed MIBS (CPU/disk intensive) */
	if (miblist == NULL) {
		if (config.need_mibs) {
			setenv("MIBLS", DEFAULT_MIBLIST, 1);
		} else {
			setenv("MIBLS", "NONE", 1);
			miblist = ""; /* don't read any mib files for numeric oids */
		}
	} else {
		// Blatantly stolen from snmplib/snmp_parse_args
		setenv("MIBS", miblist, 1);
	}

	if ((config.snmp_session.version == SNMP_VERSION_1) ||
		(config.snmp_session.version == SNMP_VERSION_2c)) {     /* snmpv1 or snmpv2c */
																/*
																config.numauthpriv = 2;
																config.authpriv = calloc(config.numauthpriv, sizeof(char *));
																config.authpriv[0] = strdup("-c");
																config.authpriv[1] = strdup(community);
																*/
	} else if (config.snmp_session.version == SNMP_VERSION_3) { /* snmpv3 args */
		// generate keys for priv and auth here (if demanded)

		if (config.snmp_session.securityName == NULL) {
			die(STATE_UNKNOWN, _("Required parameter: %s\n"), "secname");
		}

		switch (config.snmp_session.securityLevel) {
		case SNMP_SEC_LEVEL_AUTHPRIV: {
			if (authpasswd == NULL) {
				die(STATE_UNKNOWN,
					"No authentication passphrase was given, but authorization was requested");
			}
			// auth and priv
			size_t priv_key_generated = generate_Ku(
				config.snmp_session.securityPrivProto, config.snmp_session.securityPrivProtoLen,
				authpasswd, strlen((const char *)authpasswd), config.snmp_session.securityPrivKey,
				&config.snmp_session.securityPrivKeyLen);
			if (priv_key_generated != SNMPERR_SUCCESS) {
				die(STATE_UNKNOWN, "Failed to generate privacy key");
			}
		}
		// fall through
		case SNMP_SEC_LEVEL_AUTHNOPRIV: {
			if (privpasswd == NULL) {
				die(STATE_UNKNOWN, "No privacy passphrase was given, but privacy was requested");
			}
			size_t auth_key_generated = generate_Ku(
				config.snmp_session.securityAuthProto, config.snmp_session.securityAuthProtoLen,
				privpasswd, strlen((const char *)privpasswd), config.snmp_session.securityAuthKey,
				&config.snmp_session.securityAuthKeyLen);
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
	printf(" %s\n", "-x, --privproto=[DES|AES]");
	printf("    %s\n", _("SNMPv3 priv proto (default DES)"));

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
	printf(" %s\n", "-d, --delimiter=STRING");
	printf("    %s \"%s\"\n", _("Delimiter to use when parsing returned data. Default is"),
		   DEFAULT_DELIMITER);
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
	printf("[-M multiplier [-f format]]\n");
}
