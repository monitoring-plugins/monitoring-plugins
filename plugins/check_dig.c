/*****************************************************************************
 *
 * Monitoring check_dig plugin
 *
 * License: GPL
 * Copyright (c) 2002-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_dig plugin
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

/* Hackers note:
 *  There are typecasts to (char *) from _("foo bar") in this file.
 *  They prevent compiler warnings. Never (ever), permute strings obtained
 *  that are typecast from (const char *) (which happens when --disable-nls)
 *  because on some architectures those strings are in non-writable memory */

const char *progname = "check_dig";
const char *copyright = "2002-2024";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "netutils.h"
#include "utils.h"
#include "runcmd.h"

#include "check_dig.d/config.h"
#include "states.h"

typedef struct {
	int errorcode;
	check_dig_config config;
} check_dig_config_wrapper;
static check_dig_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static check_dig_config_wrapper validate_arguments(check_dig_config_wrapper /*config_wrapper*/);

static void print_help(void);
void print_usage(void);

static int verbose = 0;

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Set signal handling and alarm */
	if (signal(SIGALRM, runcmd_timeout_alarm_handler) == SIG_ERR) {
		usage_va(_("Cannot catch SIGALRM"));
	}

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_dig_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage_va(_("Could not parse arguments"));
	}

	const check_dig_config config = tmp_config.config;

	/* dig applies the timeout to each try, so we need to work around this */
	int timeout_interval_dig = ((int)timeout_interval / config.number_tries) + config.number_tries;

	char *command_line;
	/* get the command to run */
	xasprintf(&command_line, "%s %s %s -p %d @%s %s %s +retry=%d +time=%d", PATH_TO_DIG, config.dig_args, config.query_transport,
			  config.server_port, config.dns_server, config.query_address, config.record_type, config.number_tries, timeout_interval_dig);

	alarm(timeout_interval);
	struct timeval start_time;
	gettimeofday(&start_time, NULL);

	if (verbose) {
		printf("%s\n", command_line);
		if (config.expected_address != NULL) {
			printf(_("Looking for: '%s'\n"), config.expected_address);
		} else {
			printf(_("Looking for: '%s'\n"), config.query_address);
		}
	}

	output chld_out;
	output chld_err;
	char *msg = NULL;
	mp_state_enum result = STATE_UNKNOWN;
	/* run the command */
	if (np_runcmd(command_line, &chld_out, &chld_err, 0) != 0) {
		result = STATE_WARNING;
		msg = (char *)_("dig returned an error status");
	}

	for (size_t i = 0; i < chld_out.lines; i++) {
		/* the server is responding, we just got the host name... */
		if (strstr(chld_out.line[i], ";; ANSWER SECTION:")) {

			/* loop through the whole 'ANSWER SECTION' */
			for (; i < chld_out.lines; i++) {
				/* get the host address */
				if (verbose) {
					printf("%s\n", chld_out.line[i]);
				}

				if (strcasestr(chld_out.line[i], (config.expected_address == NULL ? config.query_address : config.expected_address)) !=
					NULL) {
					msg = chld_out.line[i];
					result = STATE_OK;

					/* Translate output TAB -> SPACE */
					char *temp = msg;
					while ((temp = strchr(temp, '\t')) != NULL) {
						*temp = ' ';
					}
					break;
				}
			}

			if (result == STATE_UNKNOWN) {
				msg = (char *)_("Server not found in ANSWER SECTION");
				result = STATE_WARNING;
			}

			/* we found the answer section, so break out of the loop */
			break;
		}
	}

	if (result == STATE_UNKNOWN) {
		msg = (char *)_("No ANSWER SECTION found");
		result = STATE_CRITICAL;
	}

	/* If we get anything on STDERR, at least set warning */
	if (chld_err.buflen > 0) {
		result = max_state(result, STATE_WARNING);
		if (!msg) {
			for (size_t i = 0; i < chld_err.lines; i++) {
				msg = strchr(chld_err.line[0], ':');
				if (msg) {
					msg++;
					break;
				}
			}
		}
	}

	long microsec = deltime(start_time);
	double elapsed_time = (double)microsec / 1.0e6;

	if (config.critical_interval > UNDEFINED && elapsed_time > config.critical_interval) {
		result = STATE_CRITICAL;
	}

	else if (config.warning_interval > UNDEFINED && elapsed_time > config.warning_interval) {
		result = STATE_WARNING;
	}

	printf("DNS %s - %.3f seconds response time (%s)|%s\n", state_text(result), elapsed_time,
		   msg ? msg : _("Probably a non-existent host/domain"),
		   fperfdata("time", elapsed_time, "s", (config.warning_interval > UNDEFINED), config.warning_interval,
					 (config.critical_interval > UNDEFINED), config.critical_interval, true, 0, false, 0));
	exit(result);
}

/* process command-line arguments */
check_dig_config_wrapper process_arguments(int argc, char **argv) {
	static struct option longopts[] = {{"hostname", required_argument, 0, 'H'},
									   {"query_address", required_argument, 0, 'l'},
									   {"warning", required_argument, 0, 'w'},
									   {"critical", required_argument, 0, 'c'},
									   {"timeout", required_argument, 0, 't'},
									   {"dig-arguments", required_argument, 0, 'A'},
									   {"verbose", no_argument, 0, 'v'},
									   {"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'h'},
									   {"record_type", required_argument, 0, 'T'},
									   {"expected_address", required_argument, 0, 'a'},
									   {"port", required_argument, 0, 'p'},
									   {"use-ipv4", no_argument, 0, '4'},
									   {"use-ipv6", no_argument, 0, '6'},
									   {0, 0, 0, 0}};

	check_dig_config_wrapper result = {
		.errorcode = OK,
		.config = check_dig_config_init(),
	};

	if (argc < 2) {
		result.errorcode = ERROR;
		return result;
	}

	int option = 0;
	while (true) {
		int option_index = getopt_long(argc, argv, "hVvt:l:H:w:c:T:p:a:A:46", longopts, &option);

		if (option_index == -1 || option_index == EOF) {
			break;
		}

		switch (option_index) {
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'H': /* hostname */
			host_or_die(optarg);
			result.config.dns_server = optarg;
			break;
		case 'p': /* server port */
			if (is_intpos(optarg)) {
				result.config.server_port = atoi(optarg);
			} else {
				usage_va(_("Port must be a positive integer - %s"), optarg);
			}
			break;
		case 'l': /* address to lookup */
			result.config.query_address = optarg;
			break;
		case 'w': /* warning */
			if (is_nonnegative(optarg)) {
				result.config.warning_interval = strtod(optarg, NULL);
			} else {
				usage_va(_("Warning interval must be a positive integer - %s"), optarg);
			}
			break;
		case 'c': /* critical */
			if (is_nonnegative(optarg)) {
				result.config.critical_interval = strtod(optarg, NULL);
			} else {
				usage_va(_("Critical interval must be a positive integer - %s"), optarg);
			}
			break;
		case 't': /* timeout */
			if (is_intnonneg(optarg)) {
				timeout_interval = atoi(optarg);
			} else {
				usage_va(_("Timeout interval must be a positive integer - %s"), optarg);
			}
			break;
		case 'A': /* dig arguments */
			result.config.dig_args = strdup(optarg);
			break;
		case 'v': /* verbose */
			verbose++;
			break;
		case 'T':
			result.config.record_type = optarg;
			break;
		case 'a':
			result.config.expected_address = optarg;
			break;
		case '4':
			result.config.query_transport = "-4";
			break;
		case '6':
			result.config.query_transport = "-6";
			break;
		default: /* usage5 */
			usage5();
		}
	}

	int index = optind;
	if (result.config.dns_server == NULL) {
		if (index < argc) {
			host_or_die(argv[index]);
			result.config.dns_server = argv[index];
		} else {
			if (strcmp(result.config.query_transport, "-6") == 0) {
				result.config.dns_server = strdup("::1");
			} else {
				result.config.dns_server = strdup("127.0.0.1");
			}
		}
	}

	return validate_arguments(result);
}

check_dig_config_wrapper validate_arguments(check_dig_config_wrapper config_wrapper) {
	if (config_wrapper.config.query_address == NULL) {
		config_wrapper.errorcode = ERROR;
	}
	return config_wrapper;
}

void print_help(void) {
	char *myport;

	xasprintf(&myport, "%d", DEFAULT_PORT);

	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 2000 Karl DeBisschop <kdebisschop@users.sourceforge.net>\n");
	printf(COPYRIGHT, copyright, email);

	printf(_("This plugin tests the DNS service on the specified host using dig"));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);

	printf(UT_EXTRA_OPTS);

	printf(UT_HOST_PORT, 'p', myport);

	printf(" %s\n", "-4, --use-ipv4");
	printf("    %s\n", _("Force dig to only use IPv4 query transport"));
	printf(" %s\n", "-6, --use-ipv6");
	printf("    %s\n", _("Force dig to only use IPv6 query transport"));
	printf(" %s\n", "-l, --query_address=STRING");
	printf("    %s\n", _("Machine name to lookup"));
	printf(" %s\n", "-T, --record_type=STRING");
	printf("    %s\n", _("Record type to lookup (default: A)"));
	printf(" %s\n", "-a, --expected_address=STRING");
	printf("    %s\n", _("An address expected to be in the answer section. If not set, uses whatever"));
	printf("    %s\n", _("was in -l"));
	printf(" %s\n", "-A, --dig-arguments=STRING");
	printf("    %s\n", _("Pass STRING as argument(s) to dig"));
	printf(UT_WARN_CRIT);
	printf(UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);
	printf(UT_VERBOSE);

	printf("\n");
	printf("%s\n", _("Examples:"));
	printf(" %s\n", "check_dig -H DNSSERVER -l www.example.com -A \"+tcp\"");
	printf(" %s\n", "This will send a tcp query to DNSSERVER for www.example.com");

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s -l <query_address> [-H <host>] [-p <server port>]\n", progname);
	printf(" [-T <query type>] [-w <warning interval>] [-c <critical interval>]\n");
	printf(" [-t <timeout>] [-a <expected answer address>] [-v]\n");
}
