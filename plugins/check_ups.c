/*****************************************************************************
 *
 * Monitoring check_ups plugin
 *
 * License: GPL
 * Copyright (c) 2000 Tom Shields
 *               2004 Alain Richard <alain.richard@equation.fr>
 *               2004 Arnaud Quette <arnaud.quette@mgeups.com>
 * Copyright (c) 2002-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains Network UPS Tools plugin for Monitoring
 *
 * This plugin tests the UPS service on the specified host. Network UPS Tools
 * from www.networkupstools.org must be running for this plugin to work.
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

const char *progname = "check_ups";
const char *copyright = "2000-2024";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "netutils.h"
#include "utils.h"
#include "check_ups.d/config.h"
#include "states.h"

enum {
	NOSUCHVAR = ERROR - 1
};

// Forward declarations
typedef struct {
	int errorcode;
	int ups_status;
	int supported_options;
} determine_status_result;
static determine_status_result determine_status(check_ups_config /*config*/);
static int get_ups_variable(const char * /*varname*/, char * /*buf*/, check_ups_config config);

typedef struct {
	int errorcode;
	check_ups_config config;
} check_ups_config_wrapper;
static check_ups_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static check_ups_config_wrapper validate_arguments(check_ups_config_wrapper /*config_wrapper*/);

static void print_help(void);
void print_usage(void);

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_ups_config_wrapper tmp_config = process_arguments(argc, argv);

	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}
	// Config from commandline
	check_ups_config config = tmp_config.config;

	/* initialize alarm signal handling */
	signal(SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm(socket_timeout);

	/* get the ups status if possible */
	determine_status_result query_result = determine_status(config);
	if (query_result.errorcode != OK) {
		return STATE_CRITICAL;
	}

	int ups_status_flags = query_result.ups_status;
	int supported_options = query_result.supported_options;

	// Exit result
	mp_state_enum result = STATE_UNKNOWN;
	char *message = NULL;

	if (supported_options & UPS_STATUS) {
		char *ups_status = strdup("");
		result = STATE_OK;

		if (ups_status_flags & UPSSTATUS_OFF) {
			xasprintf(&ups_status, "Off");
			result = STATE_CRITICAL;
		} else if ((ups_status_flags & (UPSSTATUS_OB | UPSSTATUS_LB)) == (UPSSTATUS_OB | UPSSTATUS_LB)) {
			xasprintf(&ups_status, _("On Battery, Low Battery"));
			result = STATE_CRITICAL;
		} else {
			if (ups_status_flags & UPSSTATUS_OL) {
				xasprintf(&ups_status, "%s%s", ups_status, _("Online"));
			}
			if (ups_status_flags & UPSSTATUS_OB) {
				xasprintf(&ups_status, "%s%s", ups_status, _("On Battery"));
				result = max_state(result, STATE_WARNING);
			}
			if (ups_status_flags & UPSSTATUS_LB) {
				xasprintf(&ups_status, "%s%s", ups_status, _(", Low Battery"));
				result = max_state(result, STATE_WARNING);
			}
			if (ups_status_flags & UPSSTATUS_CAL) {
				xasprintf(&ups_status, "%s%s", ups_status, _(", Calibrating"));
			}
			if (ups_status_flags & UPSSTATUS_RB) {
				xasprintf(&ups_status, "%s%s", ups_status, _(", Replace Battery"));
				result = max_state(result, STATE_WARNING);
			}
			if (ups_status_flags & UPSSTATUS_BYPASS) {
				xasprintf(&ups_status, "%s%s", ups_status, _(", On Bypass"));
				// Bypassing the battery is likely a bad thing
				result = STATE_CRITICAL;
			}
			if (ups_status_flags & UPSSTATUS_OVER) {
				xasprintf(&ups_status, "%s%s", ups_status, _(", Overload"));
				result = max_state(result, STATE_WARNING);
			}
			if (ups_status_flags & UPSSTATUS_TRIM) {
				xasprintf(&ups_status, "%s%s", ups_status, _(", Trimming"));
			}
			if (ups_status_flags & UPSSTATUS_BOOST) {
				xasprintf(&ups_status, "%s%s", ups_status, _(", Boosting"));
			}
			if (ups_status_flags & UPSSTATUS_CHRG) {
				xasprintf(&ups_status, "%s%s", ups_status, _(", Charging"));
			}
			if (ups_status_flags & UPSSTATUS_DISCHRG) {
				xasprintf(&ups_status, "%s%s", ups_status, _(", Discharging"));
				result = max_state(result, STATE_WARNING);
			}
			if (ups_status_flags & UPSSTATUS_ALARM) {
				xasprintf(&ups_status, "%s%s", ups_status, _(", ALARM"));
				result = STATE_CRITICAL;
			}
			if (ups_status_flags & UPSSTATUS_UNKNOWN) {
				xasprintf(&ups_status, "%s%s", ups_status, _(", Unknown"));
			}
		}
		xasprintf(&message, "%sStatus=%s ", message, ups_status);
	}

	int res;
	char temp_buffer[MAX_INPUT_BUFFER];
	char *performance_data = strdup("");
	/* get the ups utility voltage if possible */
	res = get_ups_variable("input.voltage", temp_buffer, config);
	if (res == NOSUCHVAR) {
		supported_options &= ~UPS_UTILITY;
	} else if (res != OK) {
		return STATE_CRITICAL;
	} else {
		supported_options |= UPS_UTILITY;

		double ups_utility_voltage = 0.0;
		ups_utility_voltage = atof(temp_buffer);
		xasprintf(&message, "%sUtility=%3.1fV ", message, ups_utility_voltage);

		double ups_utility_deviation = 0.0;

		if (ups_utility_voltage > 120.0) {
			ups_utility_deviation = 120.0 - ups_utility_voltage;
		} else {
			ups_utility_deviation = ups_utility_voltage - 120.0;
		}

		if (config.check_variable == UPS_UTILITY) {
			if (config.check_crit && ups_utility_deviation >= config.critical_value) {
				result = STATE_CRITICAL;
			} else if (config.check_warn && ups_utility_deviation >= config.warning_value) {
				result = max_state(result, STATE_WARNING);
			}
			xasprintf(&performance_data, "%s",
					  perfdata("voltage", (long)(1000 * ups_utility_voltage), "mV", config.check_warn, (long)(1000 * config.warning_value),
							   config.check_crit, (long)(1000 * config.critical_value), true, 0, false, 0));
		} else {
			xasprintf(&performance_data, "%s",
					  perfdata("voltage", (long)(1000 * ups_utility_voltage), "mV", false, 0, false, 0, true, 0, false, 0));
		}
	}

	/* get the ups battery percent if possible */
	res = get_ups_variable("battery.charge", temp_buffer, config);
	if (res == NOSUCHVAR) {
		supported_options &= ~UPS_BATTPCT;
	} else if (res != OK) {
		return STATE_CRITICAL;
	} else {
		supported_options |= UPS_BATTPCT;

		double ups_battery_percent = 0.0;
		ups_battery_percent = atof(temp_buffer);
		xasprintf(&message, "%sBatt=%3.1f%% ", message, ups_battery_percent);

		if (config.check_variable == UPS_BATTPCT) {
			if (config.check_crit && ups_battery_percent <= config.critical_value) {
				result = STATE_CRITICAL;
			} else if (config.check_warn && ups_battery_percent <= config.warning_value) {
				result = max_state(result, STATE_WARNING);
			}
			xasprintf(&performance_data, "%s %s", performance_data,
					  perfdata("battery", (long)ups_battery_percent, "%", config.check_warn, (long)(config.warning_value),
							   config.check_crit, (long)(config.critical_value), true, 0, true, 100));
		} else {
			xasprintf(&performance_data, "%s %s", performance_data,
					  perfdata("battery", (long)ups_battery_percent, "%", false, 0, false, 0, true, 0, true, 100));
		}
	}

	/* get the ups load percent if possible */
	res = get_ups_variable("ups.load", temp_buffer, config);
	if (res == NOSUCHVAR) {
		supported_options &= ~UPS_LOADPCT;
	} else if (res != OK) {
		return STATE_CRITICAL;
	} else {
		supported_options |= UPS_LOADPCT;

		double ups_load_percent = 0.0;
		ups_load_percent = atof(temp_buffer);
		xasprintf(&message, "%sLoad=%3.1f%% ", message, ups_load_percent);

		if (config.check_variable == UPS_LOADPCT) {
			if (config.check_crit && ups_load_percent >= config.critical_value) {
				result = STATE_CRITICAL;
			} else if (config.check_warn && ups_load_percent >= config.warning_value) {
				result = max_state(result, STATE_WARNING);
			}
			xasprintf(&performance_data, "%s %s", performance_data,
					  perfdata("load", (long)ups_load_percent, "%", config.check_warn, (long)(config.warning_value), config.check_crit,
							   (long)(config.critical_value), true, 0, true, 100));
		} else {
			xasprintf(&performance_data, "%s %s", performance_data,
					  perfdata("load", (long)ups_load_percent, "%", false, 0, false, 0, true, 0, true, 100));
		}
	}

	/* get the ups temperature if possible */
	res = get_ups_variable("ups.temperature", temp_buffer, config);
	if (res == NOSUCHVAR) {
		supported_options &= ~UPS_TEMP;
	} else if (res != OK) {
		return STATE_CRITICAL;
	} else {
		supported_options |= UPS_TEMP;

		double ups_temperature = 0.0;
		char *tunits;

		if (config.temp_output_c) {
			tunits = "degC";
			ups_temperature = atof(temp_buffer);
			xasprintf(&message, "%sTemp=%3.1fC", message, ups_temperature);
		} else {
			tunits = "degF";
			ups_temperature = (atof(temp_buffer) * 1.8) + 32;
			xasprintf(&message, "%sTemp=%3.1fF", message, ups_temperature);
		}

		if (config.check_variable == UPS_TEMP) {
			if (config.check_crit && ups_temperature >= config.critical_value) {
				result = STATE_CRITICAL;
			} else if (config.check_warn && ups_temperature >= config.warning_value) {
				result = max_state(result, STATE_WARNING);
			}
			xasprintf(&performance_data, "%s %s", performance_data,
					  perfdata("temp", (long)ups_temperature, tunits, config.check_warn, (long)(config.warning_value), config.check_crit,
							   (long)(config.critical_value), true, 0, false, 0));
		} else {
			xasprintf(&performance_data, "%s %s", performance_data,
					  perfdata("temp", (long)ups_temperature, tunits, false, 0, false, 0, true, 0, false, 0));
		}
	}

	/* get the ups real power if possible */
	res = get_ups_variable("ups.realpower", temp_buffer, config);
	if (res == NOSUCHVAR) {
		supported_options &= ~UPS_REALPOWER;
	} else if (res != OK) {
		return STATE_CRITICAL;
	} else {
		supported_options |= UPS_REALPOWER;
		double ups_realpower = 0.0;
		ups_realpower = atof(temp_buffer);
		xasprintf(&message, "%sReal power=%3.1fW ", message, ups_realpower);

		if (config.check_variable == UPS_REALPOWER) {
			if (config.check_crit && ups_realpower >= config.critical_value) {
				result = STATE_CRITICAL;
			} else if (config.check_warn && ups_realpower >= config.warning_value) {
				result = max_state(result, STATE_WARNING);
			}
			xasprintf(&performance_data, "%s %s", performance_data,
					  perfdata("realpower", (long)ups_realpower, "W", config.check_warn, (long)(config.warning_value), config.check_crit,
							   (long)(config.critical_value), true, 0, false, 0));
		} else {
			xasprintf(&performance_data, "%s %s", performance_data,
					  perfdata("realpower", (long)ups_realpower, "W", false, 0, false, 0, true, 0, false, 0));
		}
	}

	/* if the UPS does not support any options we are looking for, report an
	 * error */
	if (supported_options == UPS_NONE) {
		result = STATE_CRITICAL;
		xasprintf(&message, _("UPS does not support any available options\n"));
	}

	/* reset timeout */
	alarm(0);

	printf("UPS %s - %s|%s\n", state_text(result), message, performance_data);
	exit(result);
}

/* determines what options are supported by the UPS */
determine_status_result determine_status(const check_ups_config config) {

	determine_status_result result = {
		.errorcode = OK,
		.ups_status = UPSSTATUS_NONE,
		.supported_options = 0,
	};

	char recv_buffer[MAX_INPUT_BUFFER];
	int res = get_ups_variable("ups.status", recv_buffer, config);
	if (res == NOSUCHVAR) {
		return result;
	}

	if (res != STATE_OK) {
		printf("%s\n", _("Invalid response received from host"));
		result.errorcode = ERROR;
		return result;
	}

	result.supported_options |= UPS_STATUS;

	char temp_buffer[MAX_INPUT_BUFFER];

	strcpy(temp_buffer, recv_buffer);
	for (char *ptr = strtok(temp_buffer, " "); ptr != NULL; ptr = strtok(NULL, " ")) {
		if (!strcmp(ptr, "OFF")) {
			result.ups_status |= UPSSTATUS_OFF;
		} else if (!strcmp(ptr, "OL")) {
			result.ups_status |= UPSSTATUS_OL;
		} else if (!strcmp(ptr, "OB")) {
			result.ups_status |= UPSSTATUS_OB;
		} else if (!strcmp(ptr, "LB")) {
			result.ups_status |= UPSSTATUS_LB;
		} else if (!strcmp(ptr, "CAL")) {
			result.ups_status |= UPSSTATUS_CAL;
		} else if (!strcmp(ptr, "RB")) {
			result.ups_status |= UPSSTATUS_RB;
		} else if (!strcmp(ptr, "BYPASS")) {
			result.ups_status |= UPSSTATUS_BYPASS;
		} else if (!strcmp(ptr, "OVER")) {
			result.ups_status |= UPSSTATUS_OVER;
		} else if (!strcmp(ptr, "TRIM")) {
			result.ups_status |= UPSSTATUS_TRIM;
		} else if (!strcmp(ptr, "BOOST")) {
			result.ups_status |= UPSSTATUS_BOOST;
		} else if (!strcmp(ptr, "CHRG")) {
			result.ups_status |= UPSSTATUS_CHRG;
		} else if (!strcmp(ptr, "DISCHRG")) {
			result.ups_status |= UPSSTATUS_DISCHRG;
		} else if (!strcmp(ptr, "ALARM")) {
			result.ups_status |= UPSSTATUS_ALARM;
		} else {
			result.ups_status |= UPSSTATUS_UNKNOWN;
		}
	}

	return result;
}

/* gets a variable value for a specific UPS  */
int get_ups_variable(const char *varname, char *buf, const check_ups_config config) {
	char send_buffer[MAX_INPUT_BUFFER];

	/* create the command string to send to the UPS daemon */
	/* Add LOGOUT to avoid read failure logs */
	int res = snprintf(send_buffer, sizeof(send_buffer), "GET VAR %s %s\nLOGOUT\n", config.ups_name, varname);
	if ((res > 0) && ((size_t)res >= sizeof(send_buffer))) {
		printf("%s\n", _("UPS name to long for buffer"));
		return ERROR;
	}

	char temp_buffer[MAX_INPUT_BUFFER];

	/* send the command to the daemon and get a response back */
	if (process_tcp_request(config.server_address, config.server_port, send_buffer, temp_buffer, sizeof(temp_buffer)) != STATE_OK) {
		printf("%s\n", _("Invalid response received from host"));
		return ERROR;
	}

	char *ptr = temp_buffer;
	int len = strlen(ptr);
	const char *logout = "OK Goodbye\n";
	const int logout_len = strlen(logout);

	if (len > logout_len && strcmp(ptr + len - logout_len, logout) == 0) {
		len -= logout_len;
	}
	if (len > 0 && ptr[len - 1] == '\n') {
		ptr[len - 1] = 0;
	}
	if (strcmp(ptr, "ERR UNKNOWN-UPS") == 0) {
		printf(_("CRITICAL - no such UPS '%s' on that host\n"), config.ups_name);
		return ERROR;
	}

	if (strcmp(ptr, "ERR VAR-NOT-SUPPORTED") == 0) {
		/*printf ("Error: Variable '%s' is not supported\n", varname);*/
		return NOSUCHVAR;
	}

	if (strcmp(ptr, "ERR DATA-STALE") == 0) {
		printf("%s\n", _("CRITICAL - UPS data is stale"));
		return ERROR;
	}

	if (strncmp(ptr, "ERR", 3) == 0) {
		printf(_("Unknown error: %s\n"), ptr);
		return ERROR;
	}

	ptr = temp_buffer + strlen(varname) + strlen(config.ups_name) + 6;
	len = strlen(ptr);
	if (len < 2 || ptr[0] != '"' || ptr[len - 1] != '"') {
		printf("%s\n", _("Error: unable to parse variable"));
		return ERROR;
	}

	*buf = 0;
	strncpy(buf, ptr + 1, len - 2);
	buf[len - 2] = 0;

	return OK;
}

/* Command line: CHECK_UPS -H <host_address> -u ups [-p port] [-v variable]
			   [-wv warn_value] [-cv crit_value] [-to to_sec] */

/* process command-line arguments */
check_ups_config_wrapper process_arguments(int argc, char **argv) {

	static struct option longopts[] = {{"hostname", required_argument, 0, 'H'},
									   {"ups", required_argument, 0, 'u'},
									   {"port", required_argument, 0, 'p'},
									   {"critical", required_argument, 0, 'c'},
									   {"warning", required_argument, 0, 'w'},
									   {"timeout", required_argument, 0, 't'},
									   {"temperature", no_argument, 0, 'T'},
									   {"variable", required_argument, 0, 'v'},
									   {"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'h'},
									   {0, 0, 0, 0}};

	check_ups_config_wrapper result = {
		.errorcode = OK,
		.config = check_ups_config_init(),
	};

	if (argc < 2) {
		result.errorcode = ERROR;
		return result;
	}

	int c;
	for (c = 1; c < argc; c++) {
		if (strcmp("-to", argv[c]) == 0) {
			strcpy(argv[c], "-t");
		} else if (strcmp("-wt", argv[c]) == 0) {
			strcpy(argv[c], "-w");
		} else if (strcmp("-ct", argv[c]) == 0) {
			strcpy(argv[c], "-c");
		}
	}

	int option = 0;
	while (1) {
		c = getopt_long(argc, argv, "hVTH:u:p:v:c:w:t:", longopts, &option);

		if (c == -1 || c == EOF) {
			break;
		}

		switch (c) {
		case '?': /* help */
			usage5();
		case 'H': /* hostname */
			if (is_host(optarg)) {
				result.config.server_address = optarg;
			} else {
				usage2(_("Invalid hostname/address"), optarg);
			}
			break;
		case 'T': /* FIXME: to be improved (ie "-T C" for Celsius or "-T F" for
					 Fahrenheit) */
			result.config.temp_output_c = true;
			break;
		case 'u': /* ups name */
			result.config.ups_name = optarg;
			break;
		case 'p': /* port */
			if (is_intpos(optarg)) {
				result.config.server_port = atoi(optarg);
			} else {
				usage2(_("Port must be a positive integer"), optarg);
			}
			break;
		case 'c': /* critical time threshold */
			if (is_intnonneg(optarg)) {
				result.config.critical_value = atoi(optarg);
				result.config.check_crit = true;
			} else {
				usage2(_("Critical time must be a positive integer"), optarg);
			}
			break;
		case 'w': /* warning time threshold */
			if (is_intnonneg(optarg)) {
				result.config.warning_value = atoi(optarg);
				result.config.check_warn = true;
			} else {
				usage2(_("Warning time must be a positive integer"), optarg);
			}
			break;
		case 'v': /* variable */
			if (!strcmp(optarg, "LINE")) {
				result.config.check_variable = UPS_UTILITY;
			} else if (!strcmp(optarg, "TEMP")) {
				result.config.check_variable = UPS_TEMP;
			} else if (!strcmp(optarg, "BATTPCT")) {
				result.config.check_variable = UPS_BATTPCT;
			} else if (!strcmp(optarg, "LOADPCT")) {
				result.config.check_variable = UPS_LOADPCT;
			} else if (!strcmp(optarg, "REALPOWER")) {
				result.config.check_variable = UPS_REALPOWER;
			} else {
				usage2(_("Unrecognized UPS variable"), optarg);
			}
			break;
		case 't': /* timeout */
			if (is_intnonneg(optarg)) {
				socket_timeout = atoi(optarg);
			} else {
				usage4(_("Timeout interval must be a positive integer"));
			}
			break;
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		}
	}

	if (result.config.server_address == NULL && argc > optind) {
		if (is_host(argv[optind])) {
			result.config.server_address = argv[optind++];
		} else {
			usage2(_("Invalid hostname/address"), optarg);
		}
	}

	if (result.config.server_address == NULL) {
		result.config.server_address = strdup("127.0.0.1");
	}

	return validate_arguments(result);
}

check_ups_config_wrapper validate_arguments(check_ups_config_wrapper config_wrapper) {
	if (config_wrapper.config.ups_name) {
		printf("%s\n", _("Error : no UPS indicated"));
		config_wrapper.errorcode = ERROR;
	}
	return config_wrapper;
}

void print_help(void) {
	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 2000 Tom Shields\n");
	printf("Copyright (c) 2004 Alain Richard <alain.richard@equation.fr>\n");
	printf("Copyright (c) 2004 Arnaud Quette <arnaud.quette@mgeups.com>\n");
	printf(COPYRIGHT, copyright, email);

	printf("%s\n", _("This plugin tests the UPS service on the specified host. "
					 "Network UPS Tools"));
	printf("%s\n", _("from www.networkupstools.org must be running for this "
					 "plugin to work."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	char *myport;
	xasprintf(&myport, "%d", PORT);
	printf(UT_HOST_PORT, 'p', myport);

	printf(" %s\n", "-u, --ups=STRING");
	printf("    %s\n", _("Name of UPS"));
	printf(" %s\n", "-T, --temperature");
	printf("    %s\n", _("Output of temperatures in Celsius"));
	printf(" %s\n", "-v, --variable=STRING");
	printf("    %s %s\n", _("Valid values for STRING are"), "LINE, TEMP, BATTPCT, LOADPCT or REALPOWER");

	printf(UT_WARN_CRIT);

	printf(UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	/* TODO: -v clashing with -v/-variable. Commenting out help text since
	   verbose is unused up to now */
	/*	printf (UT_VERBOSE); */

	printf("\n");
	printf("%s\n", _("This plugin attempts to determine the status of a UPS "
					 "(Uninterruptible Power"));
	printf("%s\n", _("Supply) on a local or remote host. If the UPS is online "
					 "or calibrating, the"));
	printf("%s\n", _("plugin will return an OK state. If the battery is on it "
					 "will return a WARNING"));
	printf("%s\n", _("state. If the UPS is off or has a low battery the plugin "
					 "will return a CRITICAL"));
	printf("%s\n", _("state."));

	printf("\n");
	printf("%s\n", _("Notes:"));
	printf(" %s\n", _("You may also specify a variable to check (such as "
					  "temperature, utility voltage,"));
	printf(" %s\n", _("battery load, etc.) as well as warning and critical "
					  "thresholds for the value"));
	printf(" %s\n", _("of that variable.  If the remote host has multiple UPS "
					  "that are being monitored"));
	printf(" %s\n", _("you will have to use the --ups option to specify which "
					  "UPS to check."));
	printf("\n");
	printf(" %s\n", _("This plugin requires that the UPSD daemon distributed "
					  "with Russell Kroll's"));
	printf(" %s\n", _("Network UPS Tools be installed on the remote host. If "
					  "you do not have the"));
	printf(" %s\n", _("package installed on your system, you can download it from"));
	printf(" %s\n", _("http://www.networkupstools.org"));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s -H host -u ups [-p port] [-v variable] [-w warn_value] [-c "
		   "crit_value] [-to to_sec] [-T]\n",
		   progname);
}
