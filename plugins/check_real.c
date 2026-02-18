/*****************************************************************************
 *
 * Monitoring check_real plugin
 *
 * License: GPL
 * Copyright (c) 2000-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_real plugin
 *
 * This plugin tests the REAL service on the specified host.
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

#include "output.h"
#include "perfdata.h"
#include "states.h"
#include <stdio.h>
#include "common.h"
#include "netutils.h"
#include "thresholds.h"
#include "utils.h"
#include "check_real.d/config.h"

const char *progname = "check_real";
const char *copyright = "2000-2024";
const char *email = "devel@monitoring-plugins.org";

#define URL ""

typedef struct {
	int errorcode;
	check_real_config config;
} check_real_config_wrapper;
static check_real_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);

static void print_help(void);
void print_usage(void);

static bool verbose = false;

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_real_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	const check_real_config config = tmp_config.config;

	if (config.output_format_is_set) {
		mp_set_format(config.output_format);
	}

	/* initialize alarm signal handling */
	signal(SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm(socket_timeout);
	time_t start_time;
	time(&start_time);

	mp_check overall = mp_check_init();
	mp_subcheck sc_connect = mp_subcheck_init();

	/* try to connect to the host at the given port number */
	int socket;
	if (my_tcp_connect(config.server_address, config.server_port, &socket) != STATE_OK) {
		xasprintf(&sc_connect.output, _("unable to connect to %s on port %d"),
				  config.server_address, config.server_port);
		sc_connect = mp_set_subcheck_state(sc_connect, STATE_CRITICAL);
		mp_add_subcheck_to_check(&overall, sc_connect);
		mp_exit(overall);
	}

	xasprintf(&sc_connect.output, _("connected to %s on port %d"), config.server_address,
			  config.server_port);
	sc_connect = mp_set_subcheck_state(sc_connect, STATE_OK);
	mp_add_subcheck_to_check(&overall, sc_connect);

	/* Part I - Server Check */
	mp_subcheck sc_send = mp_subcheck_init();

	/* send the OPTIONS request */
	char buffer[MAX_INPUT_BUFFER];
	sprintf(buffer, "OPTIONS rtsp://%s:%d RTSP/1.0\r\n", config.host_name, config.server_port);
	ssize_t sent_bytes = send(socket, buffer, strlen(buffer), 0);
	if (sent_bytes == -1) {
		xasprintf(&sc_send.output, _("Sending options to %s failed"), config.host_name);
		sc_send = mp_set_subcheck_state(sc_send, STATE_CRITICAL);
		mp_add_subcheck_to_check(&overall, sc_send);
		mp_exit(overall);
	}

	/* send the header sync */
	sprintf(buffer, "CSeq: 1\r\n");
	sent_bytes = send(socket, buffer, strlen(buffer), 0);
	if (sent_bytes == -1) {
		xasprintf(&sc_send.output, _("Sending header sync to %s failed"), config.host_name);
		sc_send = mp_set_subcheck_state(sc_send, STATE_CRITICAL);
		mp_add_subcheck_to_check(&overall, sc_send);
		mp_exit(overall);
	}

	/* send a newline so the server knows we're done with the request */
	sprintf(buffer, "\r\n");
	sent_bytes = send(socket, buffer, strlen(buffer), 0);
	if (sent_bytes == -1) {
		xasprintf(&sc_send.output, _("Sending newline to %s failed"), config.host_name);
		sc_send = mp_set_subcheck_state(sc_send, STATE_CRITICAL);
		mp_add_subcheck_to_check(&overall, sc_send);
		mp_exit(overall);
	}

	/* watch for the REAL connection string */
	ssize_t received_bytes = recv(socket, buffer, MAX_INPUT_BUFFER - 1, 0);

	/* return a CRITICAL status if we couldn't read any data */
	if (received_bytes == -1) {
		xasprintf(&sc_send.output, _("No data received from %s"), config.host_name);
		sc_send = mp_set_subcheck_state(sc_send, STATE_CRITICAL);
		mp_add_subcheck_to_check(&overall, sc_send);
		mp_exit(overall);
	}

	time_t end_time;
	{
		mp_subcheck sc_options_request = mp_subcheck_init();
		mp_state_enum options_result = STATE_OK;
		/* make sure we find the response we are looking for */
		if (!strstr(buffer, config.server_expect)) {
			if (config.server_port == PORT) {
				xasprintf(&sc_options_request.output, "invalid REAL response received from host");
			} else {
				xasprintf(&sc_options_request.output,
						  "invalid REAL response received from host on port %d",
						  config.server_port);
			}
		} else {
			/* else we got the REAL string, so check the return code */
			time(&end_time);

			options_result = STATE_OK;

			char *status_line = strtok(buffer, "\n");
			xasprintf(&sc_options_request.output, "status line: %s", status_line);

			if (strstr(status_line, "200")) {
				options_result = STATE_OK;
			}
			/* client errors options_result in a warning state */
			else if (strstr(status_line, "400")) {
				options_result = STATE_WARNING;
			} else if (strstr(status_line, "401")) {
				options_result = STATE_WARNING;
			} else if (strstr(status_line, "402")) {
				options_result = STATE_WARNING;
			} else if (strstr(status_line, "403")) {
				options_result = STATE_WARNING;
			} else if (strstr(status_line, "404")) {
				options_result = STATE_WARNING;
			} else if (strstr(status_line, "500")) {
				/* server errors options_result in a critical state */
				options_result = STATE_CRITICAL;
			} else if (strstr(status_line, "501")) {
				options_result = STATE_CRITICAL;
			} else if (strstr(status_line, "502")) {
				options_result = STATE_CRITICAL;
			} else if (strstr(status_line, "503")) {
				options_result = STATE_CRITICAL;
			} else {
				options_result = STATE_UNKNOWN;
			}
		}

		sc_options_request = mp_set_subcheck_state(sc_options_request, options_result);
		mp_add_subcheck_to_check(&overall, sc_options_request);

		if (options_result != STATE_OK) {
			// exit here if Setting options already failed
			mp_exit(overall);
		}
	}

	/* Part II - Check stream exists and is ok */
	if (config.server_url != NULL) {
		/* Part I - Server Check */
		mp_subcheck sc_describe = mp_subcheck_init();

		/* send the DESCRIBE request */
		sprintf(buffer, "DESCRIBE rtsp://%s:%d%s RTSP/1.0\r\n", config.host_name,
				config.server_port, config.server_url);

		ssize_t sent_bytes = send(socket, buffer, strlen(buffer), 0);
		if (sent_bytes == -1) {
			sc_describe = mp_set_subcheck_state(sc_describe, STATE_CRITICAL);
			xasprintf(&sc_describe.output, "sending DESCRIBE request to %s failed",
					  config.host_name);
			mp_add_subcheck_to_check(&overall, sc_describe);
			mp_exit(overall);
		}

		/* send the header sync */
		sprintf(buffer, "CSeq: 2\r\n");
		sent_bytes = send(socket, buffer, strlen(buffer), 0);
		if (sent_bytes == -1) {
			sc_describe = mp_set_subcheck_state(sc_describe, STATE_CRITICAL);
			xasprintf(&sc_describe.output, "sending DESCRIBE request to %s failed",
					  config.host_name);
			mp_add_subcheck_to_check(&overall, sc_describe);
			mp_exit(overall);
		}

		/* send a newline so the server knows we're done with the request */
		sprintf(buffer, "\r\n");
		sent_bytes = send(socket, buffer, strlen(buffer), 0);
		if (sent_bytes == -1) {
			sc_describe = mp_set_subcheck_state(sc_describe, STATE_CRITICAL);
			xasprintf(&sc_describe.output, "sending DESCRIBE request to %s failed",
					  config.host_name);
			mp_add_subcheck_to_check(&overall, sc_describe);
			mp_exit(overall);
		}

		/* watch for the REAL connection string */
		ssize_t recv_bytes = recv(socket, buffer, MAX_INPUT_BUFFER - 1, 0);
		if (recv_bytes == -1) {
			/* return a CRITICAL status if we couldn't read any data */
			sc_describe = mp_set_subcheck_state(sc_describe, STATE_CRITICAL);
			xasprintf(&sc_describe.output, "No data received from host on DESCRIBE request");
			mp_add_subcheck_to_check(&overall, sc_describe);
			mp_exit(overall);
		} else {
			buffer[recv_bytes] = '\0'; /* null terminate received buffer */
			/* make sure we find the response we are looking for */
			if (!strstr(buffer, config.server_expect)) {
				if (config.server_port == PORT) {
					xasprintf(&sc_describe.output, "invalid REAL response received from host");
				} else {
					xasprintf(&sc_describe.output,
							  "invalid REAL response received from host on port %d",
							  config.server_port);
				}

				sc_describe = mp_set_subcheck_state(sc_describe, STATE_UNKNOWN);
				mp_add_subcheck_to_check(&overall, sc_describe);
				mp_exit(overall);
			} else {
				/* else we got the REAL string, so check the return code */

				time(&end_time);

				char *status_line = strtok(buffer, "\n");
				xasprintf(&sc_describe.output, "status line: %s", status_line);

				mp_state_enum describe_result;
				if (strstr(status_line, "200")) {
					describe_result = STATE_OK;
				}
				/* client errors describe_result in a warning state */
				else if (strstr(status_line, "400")) {
					describe_result = STATE_WARNING;
				} else if (strstr(status_line, "401")) {
					describe_result = STATE_WARNING;
				} else if (strstr(status_line, "402")) {
					describe_result = STATE_WARNING;
				} else if (strstr(status_line, "403")) {
					describe_result = STATE_WARNING;
				} else if (strstr(status_line, "404")) {
					describe_result = STATE_WARNING;
				}
				/* server errors describe_result in a critical state */
				else if (strstr(status_line, "500")) {
					describe_result = STATE_CRITICAL;
				} else if (strstr(status_line, "501")) {
					describe_result = STATE_CRITICAL;
				} else if (strstr(status_line, "502")) {
					describe_result = STATE_CRITICAL;
				} else if (strstr(status_line, "503")) {
					describe_result = STATE_CRITICAL;
				} else {
					describe_result = STATE_UNKNOWN;
				}

				sc_describe = mp_set_subcheck_state(sc_describe, describe_result);
				mp_add_subcheck_to_check(&overall, sc_describe);
			}
		}
	}

	/* Return results */
	mp_subcheck sc_timing = mp_subcheck_init();
	xasprintf(&sc_timing.output, "response time: %lds", end_time - start_time);
	sc_timing = mp_set_subcheck_default_state(sc_timing, STATE_OK);

	mp_perfdata pd_response_time = perfdata_init();
	pd_response_time = mp_set_pd_value(pd_response_time, (end_time - start_time));
	pd_response_time.label = "response_time";
	pd_response_time.uom = "s";
	pd_response_time = mp_pd_set_thresholds(pd_response_time, config.time_thresholds);
	mp_add_perfdata_to_subcheck(&sc_connect, pd_response_time);
	sc_timing = mp_set_subcheck_state(sc_timing, mp_get_pd_status(pd_response_time));

	mp_add_subcheck_to_check(&overall, sc_timing);

	/* close the connection */
	close(socket);

	/* reset the alarm */
	alarm(0);

	mp_exit(overall);
}

/* process command-line arguments */
check_real_config_wrapper process_arguments(int argc, char **argv) {
	enum {
		output_format_index = CHAR_MAX + 1,
	};

	static struct option longopts[] = {{"hostname", required_argument, 0, 'H'},
									   {"IPaddress", required_argument, 0, 'I'},
									   {"expect", required_argument, 0, 'e'},
									   {"url", required_argument, 0, 'u'},
									   {"port", required_argument, 0, 'p'},
									   {"critical", required_argument, 0, 'c'},
									   {"warning", required_argument, 0, 'w'},
									   {"timeout", required_argument, 0, 't'},
									   {"verbose", no_argument, 0, 'v'},
									   {"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'h'},
									   {"output-format", required_argument, 0, output_format_index},
									   {0, 0, 0, 0}};

	check_real_config_wrapper result = {
		.errorcode = OK,
		.config = check_real_config_init(),
	};

	if (argc < 2) {
		result.errorcode = ERROR;
		return result;
	}

	for (int i = 1; i < argc; i++) {
		if (strcmp("-to", argv[i]) == 0) {
			strcpy(argv[i], "-t");
		} else if (strcmp("-wt", argv[i]) == 0) {
			strcpy(argv[i], "-w");
		} else if (strcmp("-ct", argv[i]) == 0) {
			strcpy(argv[i], "-c");
		}
	}

	while (true) {
		int option = 0;
		int option_char = getopt_long(argc, argv, "+hvVI:H:e:u:p:w:c:t:", longopts, &option);

		if (option_char == -1 || option_char == EOF) {
			break;
		}

		switch (option_char) {
		case 'I': /* hostname */
		case 'H': /* hostname */
			if (result.config.server_address) {
				break;
			} else if (is_host(optarg)) {
				result.config.server_address = optarg;
			} else {
				usage2(_("Invalid hostname/address"), optarg);
			}
			break;
		case 'e': /* string to expect in response header */
			result.config.server_expect = optarg;
			break;
		case 'u': /* server URL */
			result.config.server_url = optarg;
			break;
		case 'p': /* port */
			if (is_intpos(optarg)) {
				result.config.server_port = atoi(optarg);
			} else {
				usage4(_("Port must be a positive integer"));
			}
			break;
		case 'w': /* warning time threshold */
		{
			mp_range_parsed critical_range = mp_parse_range_string(optarg);
			if (critical_range.error != MP_PARSING_SUCCESS) {
				die(STATE_UNKNOWN, "failed to parse warning threshold: %s", optarg);
			}
			result.config.time_thresholds =
				mp_thresholds_set_warn(result.config.time_thresholds, critical_range.range);
		} break;
		case 'c': /* critical time threshold */
		{
			mp_range_parsed critical_range = mp_parse_range_string(optarg);
			if (critical_range.error != MP_PARSING_SUCCESS) {
				die(STATE_UNKNOWN, "failed to parse critical threshold: %s", optarg);
			}
			result.config.time_thresholds =
				mp_thresholds_set_crit(result.config.time_thresholds, critical_range.range);
		} break;
		case 'v': /* verbose */
			verbose = true;
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
		case output_format_index: {
			parsed_output_format parser = mp_parse_output_format(optarg);
			if (!parser.parsing_success) {
				// TODO List all available formats here, maybe add anothoer usage function
				printf("Invalid output format: %s\n", optarg);
				exit(STATE_UNKNOWN);
			}

			result.config.output_format_is_set = true;
			result.config.output_format = parser.output_format;
			break;
		}
		case '?': /* usage */
			usage5();
		}
	}

	int option_char = optind;
	if (result.config.server_address == NULL && argc > option_char) {
		if (is_host(argv[option_char])) {
			result.config.server_address = argv[option_char++];
		} else {
			usage2(_("Invalid hostname/address"), argv[option_char]);
		}
	}

	if (result.config.server_address == NULL) {
		usage4(_("You must provide a server to check"));
	}

	if (result.config.host_name == NULL) {
		result.config.host_name = strdup(result.config.server_address);
	}

	return result;
}

void print_help(void) {
	char *myport;
	xasprintf(&myport, "%d", PORT);

	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Pedro Leite <leite@cic.ua.pt>\n");
	printf(COPYRIGHT, copyright, email);

	printf("%s\n", _("This plugin tests the REAL service on the specified host."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(UT_HOST_PORT, 'p', myport);

	printf(" %s\n", "-u, --url=STRING");
	printf("    %s\n", _("Connect to this url"));
	printf(" %s\n", "-e, --expect=STRING");
	printf(_("String to expect in first line of server response (default: %s)\n"), default_expect);

	printf(UT_WARN_CRIT);

	printf(UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf(UT_VERBOSE);

	printf("\n");
	printf("%s\n", _("This plugin will attempt to open an RTSP connection with the host."));
	printf("%s\n", _("Successful connects return STATE_OK, refusals and timeouts return"));
	printf("%s\n", _("STATE_CRITICAL, other errors return STATE_UNKNOWN.  Successful connects,"));
	printf("%s\n",
		   _("but incorrect response messages from the host result in STATE_WARNING return"));
	printf("%s\n", _("values."));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s -H host [-e expect] [-p port] [-w warn] [-c crit] [-t timeout] [-v]\n", progname);
}
