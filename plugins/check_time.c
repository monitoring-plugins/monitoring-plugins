/*****************************************************************************
 *
 * Monitoring check_time plugin
 *
 * License: GPL
 * Copyright (c) 1999-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_time plugin
 *
 * This plugin will check the time difference with the specified host.
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

#include "states.h"
const char *progname = "check_time";
const char *copyright = "1999-2024";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "netutils.h"
#include "utils.h"
#include "check_time.d/config.h"

#define UNIX_EPOCH 2208988800UL

typedef struct {
	int errorcode;
	check_time_config config;
} check_time_config_wrapper;
static check_time_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static void print_help(void);
void print_usage(void);

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_time_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	const check_time_config config = tmp_config.config;

	/* initialize alarm signal handling */
	signal(SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm(socket_timeout);
	time(&start_time);

	int socket;
	mp_state_enum result = STATE_UNKNOWN;
	/* try to connect to the host at the given port number */
	if (config.use_udp) {
		result = my_udp_connect(config.server_address, config.server_port, &socket);
	} else {
		result = my_tcp_connect(config.server_address, config.server_port, &socket);
	}

	if (result != STATE_OK) {
		if (config.check_critical_time) {
			result = STATE_CRITICAL;
		} else if (config.check_warning_time) {
			result = STATE_WARNING;
		} else {
			result = STATE_UNKNOWN;
		}
		die(result, _("TIME UNKNOWN - could not connect to server %s, port %d\n"), config.server_address, config.server_port);
	}

	if (config.use_udp) {
		if (send(socket, "", 0, 0) < 0) {
			if (config.check_critical_time) {
				result = STATE_CRITICAL;
			} else if (config.check_warning_time) {
				result = STATE_WARNING;
			} else {
				result = STATE_UNKNOWN;
			}
			die(result, _("TIME UNKNOWN - could not send UDP request to server %s, port %d\n"), config.server_address, config.server_port);
		}
	}

	/* watch for the connection string */
	uint32_t raw_server_time;
	result = recv(socket, (void *)&raw_server_time, sizeof(raw_server_time), 0);

	/* close the connection */
	close(socket);

	/* reset the alarm */
	time(&end_time);
	alarm(0);

	/* return a WARNING status if we couldn't read any data */
	if (result <= 0) {
		if (config.check_critical_time) {
			result = STATE_CRITICAL;
		} else if (config.check_warning_time) {
			result = STATE_WARNING;
		} else {
			result = STATE_UNKNOWN;
		}
		die(result, _("TIME UNKNOWN - no data received from server %s, port %d\n"), config.server_address, config.server_port);
	}

	result = STATE_OK;

	time_t conntime = (end_time - start_time);
	if (config.check_critical_time && conntime > config.critical_time) {
		result = STATE_CRITICAL;
	} else if (config.check_warning_time && conntime > config.warning_time) {
		result = STATE_WARNING;
	}

	if (result != STATE_OK) {
		die(result, _("TIME %s - %d second response time|%s\n"), state_text(result), (int)conntime,
			perfdata("time", (long)conntime, "s", config.check_warning_time, (long)config.warning_time, config.check_critical_time,
					 (long)config.critical_time, true, 0, false, 0));
	}

	unsigned long server_time;
	unsigned long diff_time;
	server_time = ntohl(raw_server_time) - UNIX_EPOCH;
	if (server_time > (unsigned long)end_time) {
		diff_time = server_time - (unsigned long)end_time;
	} else {
		diff_time = (unsigned long)end_time - server_time;
	}

	if (config.check_critical_diff && diff_time > config.critical_diff) {
		result = STATE_CRITICAL;
	} else if (config.check_warning_diff && diff_time > config.warning_diff) {
		result = STATE_WARNING;
	}

	printf(_("TIME %s - %lu second time difference|%s %s\n"), state_text(result), diff_time,
		   perfdata("time", (long)conntime, "s", config.check_warning_time, (long)config.warning_time, config.check_critical_time,
					(long)config.critical_time, true, 0, false, 0),
		   perfdata("offset", diff_time, "s", config.check_warning_diff, config.warning_diff, config.check_critical_diff,
					config.critical_diff, true, 0, false, 0));
	return result;
}

/* process command-line arguments */
check_time_config_wrapper process_arguments(int argc, char **argv) {
	static struct option longopts[] = {{"hostname", required_argument, 0, 'H'},
									   {"warning-variance", required_argument, 0, 'w'},
									   {"critical-variance", required_argument, 0, 'c'},
									   {"warning-connect", required_argument, 0, 'W'},
									   {"critical-connect", required_argument, 0, 'C'},
									   {"port", required_argument, 0, 'p'},
									   {"udp", no_argument, 0, 'u'},
									   {"timeout", required_argument, 0, 't'},
									   {"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'h'},
									   {0, 0, 0, 0}};

	if (argc < 2) {
		usage("\n");
	}

	for (int i = 1; i < argc; i++) {
		if (strcmp("-to", argv[i]) == 0) {
			strcpy(argv[i], "-t");
		} else if (strcmp("-wd", argv[i]) == 0) {
			strcpy(argv[i], "-w");
		} else if (strcmp("-cd", argv[i]) == 0) {
			strcpy(argv[i], "-c");
		} else if (strcmp("-wt", argv[i]) == 0) {
			strcpy(argv[i], "-W");
		} else if (strcmp("-ct", argv[i]) == 0) {
			strcpy(argv[i], "-C");
		}
	}

	check_time_config_wrapper result = {
		.errorcode = OK,
		.config = check_time_config_init(),
	};

	int option_char;
	while (true) {
		int option = 0;
		option_char = getopt_long(argc, argv, "hVH:w:c:W:C:p:t:u", longopts, &option);

		if (option_char == -1 || option_char == EOF) {
			break;
		}

		switch (option_char) {
		case '?': /* print short usage statement if args not parsable */
			usage5();
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'H': /* hostname */
			if (!is_host(optarg)) {
				usage2(_("Invalid hostname/address"), optarg);
			}
			result.config.server_address = optarg;
			break;
		case 'w': /* warning-variance */
			if (is_intnonneg(optarg)) {
				result.config.warning_diff = strtoul(optarg, NULL, 10);
				result.config.check_warning_diff = true;
			} else if (strspn(optarg, "0123456789:,") > 0) {
				if (sscanf(optarg, "%lu%*[:,]%d", &result.config.warning_diff, &result.config.warning_time) == 2) {
					result.config.check_warning_diff = true;
					result.config.check_warning_time = true;
				} else {
					usage4(_("Warning thresholds must be a positive integer"));
				}
			} else {
				usage4(_("Warning threshold must be a positive integer"));
			}
			break;
		case 'c': /* critical-variance */
			if (is_intnonneg(optarg)) {
				result.config.critical_diff = strtoul(optarg, NULL, 10);
				result.config.check_critical_diff = true;
			} else if (strspn(optarg, "0123456789:,") > 0) {
				if (sscanf(optarg, "%lu%*[:,]%d", &result.config.critical_diff, &result.config.critical_time) == 2) {
					result.config.check_critical_diff = true;
					result.config.check_critical_time = true;
				} else {
					usage4(_("Critical thresholds must be a positive integer"));
				}
			} else {
				usage4(_("Critical threshold must be a positive integer"));
			}
			break;
		case 'W': /* warning-connect */
			if (!is_intnonneg(optarg)) {
				usage4(_("Warning threshold must be a positive integer"));
			} else {
				result.config.warning_time = atoi(optarg);
			}
			result.config.check_warning_time = true;
			break;
		case 'C': /* critical-connect */
			if (!is_intnonneg(optarg)) {
				usage4(_("Critical threshold must be a positive integer"));
			} else {
				result.config.critical_time = atoi(optarg);
			}
			result.config.check_critical_time = true;
			break;
		case 'p': /* port */
			if (!is_intnonneg(optarg)) {
				usage4(_("Port must be a positive integer"));
			} else {
				result.config.server_port = atoi(optarg);
			}
			break;
		case 't': /* timeout */
			if (!is_intnonneg(optarg)) {
				usage2(_("Timeout interval must be a positive integer"), optarg);
			} else {
				socket_timeout = atoi(optarg);
			}
			break;
		case 'u': /* udp */
			result.config.use_udp = true;
		}
	}

	option_char = optind;
	if (result.config.server_address == NULL) {
		if (argc > option_char) {
			if (!is_host(argv[option_char])) {
				usage2(_("Invalid hostname/address"), optarg);
			}
			result.config.server_address = argv[option_char];
		} else {
			usage4(_("Hostname was not supplied"));
		}
	}

	return result;
}

void print_help(void) {
	char *myport;
	xasprintf(&myport, "%d", TIME_PORT);

	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Ethan Galstad\n");
	printf(COPYRIGHT, copyright, email);

	printf("%s\n", _("This plugin will check the time on the specified host."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(UT_HOST_PORT, 'p', myport);

	printf(" %s\n", "-u, --udp");
	printf("   %s\n", _("Use UDP to connect, not TCP"));
	printf(" %s\n", "-w, --warning-variance=INTEGER");
	printf("   %s\n", _("Time difference (sec.) necessary to result in a warning status"));
	printf(" %s\n", "-c, --critical-variance=INTEGER");
	printf("   %s\n", _("Time difference (sec.) necessary to result in a critical status"));
	printf(" %s\n", "-W, --warning-connect=INTEGER");
	printf("   %s\n", _("Response time (sec.) necessary to result in warning status"));
	printf(" %s\n", "-C, --critical-connect=INTEGER");
	printf("   %s\n", _("Response time (sec.) necessary to result in critical status"));

	printf(UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s -H <host_address> [-p port] [-u] [-w variance] [-c variance]\n", progname);
	printf(" [-W connect_time] [-C connect_time] [-t timeout]\n");
}
