/*****************************************************************************
 *
 * Monitoring check_ping plugin
 *
 * License: GPL
 * Copyright (c) 2000-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_ping plugin
 *
 * Use the ping program to check connection statistics for a remote host.
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

const char *progname = "check_ping";
const char *copyright = "2000-2024";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "netutils.h"
#include "popen.h"
#include "utils.h"
#include "check_ping.d/config.h"
#include "../lib/states.h"

#include <signal.h>

#define WARN_DUPLICATES "DUPLICATES FOUND! "

typedef struct {
	int errorcode;
	check_ping_config config;
} check_ping_config_wrapper;
static check_ping_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static check_ping_config_wrapper validate_arguments(check_ping_config_wrapper /*config_wrapper*/);

static int get_threshold(char * /*arg*/, double * /*trta*/, int * /*tpl*/);

typedef struct {
	mp_state_enum state;
	double round_trip_average;
	int packet_loss;
} ping_result;
static ping_result run_ping(const char *cmd, const char *addr, double /*crta*/);

static mp_state_enum error_scan(char buf[MAX_INPUT_BUFFER], const char *addr);
static void print_help(void);
void print_usage(void);

static int verbose = 0;

static char *warn_text;

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "C");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_ping_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	const check_ping_config config = tmp_config.config;

	/* Set signal handling and alarm */
	if (signal(SIGALRM, popen_timeout_alarm_handler) == SIG_ERR) {
		usage4(_("Cannot catch SIGALRM"));
	}

	/* If ./configure finds ping has timeout values, set plugin alarm slightly
	 * higher so that we can use response from command line ping */
#if defined(PING_PACKETS_FIRST) && defined(PING_HAS_TIMEOUT)
	alarm(timeout_interval + 1);
#else
	alarm(timeout_interval);
#endif

	int result = STATE_UNKNOWN;
	char *rawcmd = NULL;
	for (size_t i = 0; i < config.n_addresses; i++) {
#ifdef PING6_COMMAND
		if (address_family != AF_INET && is_inet6_addr(config.addresses[i])) {
			rawcmd = strdup(PING6_COMMAND);
		} else {
			rawcmd = strdup(PING_COMMAND);
		}
#else
		rawcmd = strdup(PING_COMMAND);
#endif

		char *cmd = NULL;

		/* does the host address of number of packets argument come first? */
#ifdef PING_PACKETS_FIRST
#	ifdef PING_HAS_TIMEOUT
		xasprintf(&cmd, rawcmd, timeout_interval, config.max_packets, config.addresses[i]);
#	else
		xasprintf(&cmd, rawcmd, config.max_packets, config.addresses[i]);
#	endif
#else
		xasprintf(&cmd, rawcmd, config.addresses[i], config.max_packets);
#endif

		if (verbose >= 2) {
			printf("CMD: %s\n", cmd);
		}

		/* run the command */

		ping_result pinged = run_ping(cmd, config.addresses[i], config.crta);

		if (pinged.packet_loss == UNKNOWN_PACKET_LOSS || pinged.round_trip_average < 0.0) {
			printf("%s\n", cmd);
			die(STATE_UNKNOWN, _("CRITICAL - Could not interpret output from ping command\n"));
		}

		if (pinged.packet_loss >= config.cpl || pinged.round_trip_average >= config.crta ||
			pinged.round_trip_average < 0) {
			pinged.state = STATE_CRITICAL;
		} else if (pinged.packet_loss >= config.wpl || pinged.round_trip_average >= config.wrta) {
			pinged.state = STATE_WARNING;
		} else if (pinged.packet_loss >= 0 && pinged.round_trip_average >= 0) {
			pinged.state = max_state(STATE_OK, pinged.state);
		}

		if (config.n_addresses > 1 && pinged.state != STATE_UNKNOWN) {
			die(STATE_OK, "%s is alive\n", config.addresses[i]);
		}

		if (config.display_html) {
			printf("<A HREF='%s/traceroute.cgi?%s'>", CGIURL, config.addresses[i]);
		}
		if (pinged.packet_loss == 100) {
			printf(_("PING %s - %sPacket loss = %d%%"), state_text(pinged.state), warn_text,
				   pinged.packet_loss);
		} else {
			printf(_("PING %s - %sPacket loss = %d%%, RTA = %2.2f ms"), state_text(pinged.state),
				   warn_text, pinged.packet_loss, pinged.round_trip_average);
		}
		if (config.display_html) {
			printf("</A>");
		}

		/* Print performance data */
		if (pinged.packet_loss != 100) {
			printf("|%s",
				   fperfdata("rta", pinged.round_trip_average, "ms", (bool)(config.wrta > 0),
							 config.wrta, (bool)(config.crta > 0), config.crta, true, 0, false, 0));
		} else {
			printf("| rta=U;%f;%f;;", config.wrta, config.crta);
		}

		printf(" %s\n",
			   perfdata("pl", (long)pinged.packet_loss, "%", (bool)(config.wpl > 0), config.wpl,
						(bool)(config.cpl > 0), config.cpl, true, 0, false, 0));

		if (verbose >= 2) {
			printf("%f:%d%% %f:%d%%\n", config.wrta, config.wpl, config.crta, config.cpl);
		}

		result = max_state(result, pinged.state);
		free(rawcmd);
		free(cmd);
	}

	return result;
}

/* process command-line arguments */
check_ping_config_wrapper process_arguments(int argc, char **argv) {
	static struct option longopts[] = {STD_LONG_OPTS,
									   {"packets", required_argument, 0, 'p'},
									   {"nohtml", no_argument, 0, 'n'},
									   {"link", no_argument, 0, 'L'},
									   {"use-ipv4", no_argument, 0, '4'},
									   {"use-ipv6", no_argument, 0, '6'},
									   {0, 0, 0, 0}};

	check_ping_config_wrapper result = {
		.errorcode = OK,
		.config = check_ping_config_init(),
	};

	if (argc < 2) {
		result.errorcode = ERROR;
		return result;
	}

	for (int index = 1; index < argc; index++) {
		if (strcmp("-to", argv[index]) == 0) {
			strcpy(argv[index], "-t");
		}
		if (strcmp("-nohtml", argv[index]) == 0) {
			strcpy(argv[index], "-n");
		}
	}

	int option = 0;
	size_t max_addr = MAX_ADDR_START;
	while (true) {
		int option_index = getopt_long(argc, argv, "VvhnL46t:c:w:H:p:", longopts, &option);

		if (CHECK_EOF(option_index)) {
			break;
		}

		switch (option_index) {
		case '?': /* usage */
			usage5();
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
			break;
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
			break;
		case 't': /* timeout period */
			timeout_interval = atoi(optarg);
			break;
		case 'v': /* verbose mode */
			verbose++;
			break;
		case '4': /* IPv4 only */
			address_family = AF_INET;
			break;
		case '6': /* IPv6 only */
#ifdef USE_IPV6
			address_family = AF_INET6;
#else
			usage(_("IPv6 support not available\n"));
#endif
			break;
		case 'H': /* hostname */ {
			char *ptr = optarg;
			while (true) {
				result.config.n_addresses++;
				if (result.config.n_addresses > max_addr) {
					max_addr *= 2;
					result.config.addresses =
						realloc(result.config.addresses, sizeof(char *) * max_addr);
					if (result.config.addresses == NULL) {
						die(STATE_UNKNOWN, _("Could not realloc() addresses\n"));
					}
				}
				result.config.addresses[result.config.n_addresses - 1] = ptr;
				if ((ptr = index(ptr, ','))) {
					strcpy(ptr, "");
					ptr += sizeof(char);
				} else {
					break;
				}
			}
		} break;
		case 'p': /* number of packets to send */
			if (is_intnonneg(optarg)) {
				result.config.max_packets = atoi(optarg);
			} else {
				usage2(_("<max_packets> (%s) must be a non-negative number\n"), optarg);
			}
			break;
		case 'n': /* no HTML */
			result.config.display_html = false;
			break;
		case 'L': /* show HTML */
			result.config.display_html = true;
			break;
		case 'c':
			get_threshold(optarg, &result.config.crta, &result.config.cpl);
			break;
		case 'w':
			get_threshold(optarg, &result.config.wrta, &result.config.wpl);
			break;
		}
	}

	int arg_counter = optind;
	if (arg_counter == argc) {
		return validate_arguments(result);
	}

	if (result.config.addresses[0] == NULL) {
		if (!is_host(argv[arg_counter])) {
			usage2(_("Invalid hostname/address"), argv[arg_counter]);
		} else {
			result.config.addresses[0] = argv[arg_counter++];
			result.config.n_addresses++;
			if (arg_counter == argc) {
				return validate_arguments(result);
			}
		}
	}

	if (result.config.wpl == UNKNOWN_PACKET_LOSS) {
		if (!is_intpercent(argv[arg_counter])) {
			printf(_("<wpl> (%s) must be an integer percentage\n"), argv[arg_counter]);
			result.errorcode = ERROR;
			return result;
		}
		result.config.wpl = atoi(argv[arg_counter++]);
		if (arg_counter == argc) {
			return validate_arguments(result);
		}
	}

	if (result.config.cpl == UNKNOWN_PACKET_LOSS) {
		if (!is_intpercent(argv[arg_counter])) {
			printf(_("<cpl> (%s) must be an integer percentage\n"), argv[arg_counter]);
			result.errorcode = ERROR;
			return result;
		}
		result.config.cpl = atoi(argv[arg_counter++]);
		if (arg_counter == argc) {
			return validate_arguments(result);
		}
	}

	if (result.config.wrta < 0.0) {
		if (is_negative(argv[arg_counter])) {
			printf(_("<wrta> (%s) must be a non-negative number\n"), argv[arg_counter]);
			result.errorcode = ERROR;
			return result;
		}
		result.config.wrta = atof(argv[arg_counter++]);
		if (arg_counter == argc) {
			return validate_arguments(result);
		}
	}

	if (result.config.crta < 0.0) {
		if (is_negative(argv[arg_counter])) {
			printf(_("<crta> (%s) must be a non-negative number\n"), argv[arg_counter]);
			result.errorcode = ERROR;
			return result;
		}
		result.config.crta = atof(argv[arg_counter++]);
		if (arg_counter == argc) {
			return validate_arguments(result);
		}
	}

	if (result.config.max_packets == -1) {
		if (is_intnonneg(argv[arg_counter])) {
			result.config.max_packets = atoi(argv[arg_counter++]);
		} else {
			printf(_("<max_packets> (%s) must be a non-negative number\n"), argv[arg_counter]);
			result.errorcode = ERROR;
			return result;
		}
	}

	return validate_arguments(result);
}

int get_threshold(char *arg, double *trta, int *tpl) {
	if (is_intnonneg(arg) && sscanf(arg, "%lf", trta) == 1) {
		return OK;
	}

	if (strpbrk(arg, ",:") && strstr(arg, "%") && sscanf(arg, "%lf%*[:,]%d%%", trta, tpl) == 2) {
		return OK;
	}

	if (strstr(arg, "%") && sscanf(arg, "%d%%", tpl) == 1) {
		return OK;
	}

	usage2(_("%s: Warning threshold must be integer or percentage!\n\n"), arg);
	return STATE_UNKNOWN;
}

check_ping_config_wrapper validate_arguments(check_ping_config_wrapper config_wrapper) {
	if (config_wrapper.config.wrta < 0.0) {
		printf(_("<wrta> was not set\n"));
		config_wrapper.errorcode = ERROR;
		return config_wrapper;
	}

	if (config_wrapper.config.crta < 0.0) {
		printf(_("<crta> was not set\n"));
		config_wrapper.errorcode = ERROR;
		return config_wrapper;
	}

	if (config_wrapper.config.wpl == UNKNOWN_PACKET_LOSS) {
		printf(_("<wpl> was not set\n"));
		config_wrapper.errorcode = ERROR;
		return config_wrapper;
	}

	if (config_wrapper.config.cpl == UNKNOWN_PACKET_LOSS) {
		printf(_("<cpl> was not set\n"));
		config_wrapper.errorcode = ERROR;
		return config_wrapper;
	}

	if (config_wrapper.config.wrta > config_wrapper.config.crta) {
		printf(_("<wrta> (%f) cannot be larger than <crta> (%f)\n"), config_wrapper.config.wrta,
			   config_wrapper.config.crta);
		config_wrapper.errorcode = ERROR;
		return config_wrapper;
	}

	if (config_wrapper.config.wpl > config_wrapper.config.cpl) {
		printf(_("<wpl> (%d) cannot be larger than <cpl> (%d)\n"), config_wrapper.config.wpl,
			   config_wrapper.config.cpl);
		config_wrapper.errorcode = ERROR;
		return config_wrapper;
	}

	if (config_wrapper.config.max_packets == -1) {
		config_wrapper.config.max_packets = DEFAULT_MAX_PACKETS;
	}

	double max_seconds = (config_wrapper.config.crta / 1000.0 * config_wrapper.config.max_packets) +
						 config_wrapper.config.max_packets;
	if (max_seconds > timeout_interval) {
		timeout_interval = (unsigned int)max_seconds;
	}

	for (size_t i = 0; i < config_wrapper.config.n_addresses; i++) {
		if (!is_host(config_wrapper.config.addresses[i])) {
			usage2(_("Invalid hostname/address"), config_wrapper.config.addresses[i]);
		}
	}

	if (config_wrapper.config.n_addresses == 0) {
		usage(_("You must specify a server address or host name"));
	}

	return config_wrapper;
}

ping_result run_ping(const char *cmd, const char *addr, double crta) {
	if ((child_process = spopen(cmd)) == NULL) {
		die(STATE_UNKNOWN, _("Could not open pipe: %s\n"), cmd);
	}

	child_stderr = fdopen(child_stderr_array[fileno(child_process)], "r");
	if (child_stderr == NULL) {
		printf(_("Cannot open stderr for %s\n"), cmd);
	}

	char buf[MAX_INPUT_BUFFER];
	ping_result result = {
		.state = STATE_UNKNOWN,
		.packet_loss = UNKNOWN_PACKET_LOSS,
		.round_trip_average = UNKNOWN_TRIP_TIME,
	};

	while (fgets(buf, MAX_INPUT_BUFFER - 1, child_process)) {
		if (verbose >= 3) {
			printf("Output: %s", buf);
		}

		result.state = max_state(result.state, error_scan(buf, addr));

		/* get the percent loss statistics */
		int match = 0;
		if ((sscanf(
				 buf,
				 "%*d packets transmitted, %*d packets received, +%*d errors, %d%% packet loss%n",
				 &result.packet_loss, &match) == 1 &&
			 match) ||
			(sscanf(buf,
					"%*d packets transmitted, %*d packets received, +%*d duplicates, %d%% packet "
					"loss%n",
					&result.packet_loss, &match) == 1 &&
			 match) ||
			(sscanf(buf,
					"%*d packets transmitted, %*d received, +%*d duplicates, %d%% packet loss%n",
					&result.packet_loss, &match) == 1 &&
			 match) ||
			(sscanf(buf, "%*d packets transmitted, %*d packets received, %d%% packet loss%n",
					&result.packet_loss, &match) == 1 &&
			 match) ||
			(sscanf(buf, "%*d packets transmitted, %*d packets received, %d%% loss, time%n",
					&result.packet_loss, &match) == 1 &&
			 match) ||
			(sscanf(buf, "%*d packets transmitted, %*d received, %d%% loss, time%n",
					&result.packet_loss, &match) == 1 &&
			 match) ||
			(sscanf(buf, "%*d packets transmitted, %*d received, %d%% packet loss, time%n",
					&result.packet_loss, &match) == 1 &&
			 match) == 1 ||
			(sscanf(buf, "%*d packets transmitted, %*d received, +%*d errors, %d%% packet loss%n",
					&result.packet_loss, &match) == 1 &&
			 match) ||
			(sscanf(buf, "%*d packets transmitted %*d received, +%*d errors, %d%% packet loss%n",
					&result.packet_loss, &match) == 1 &&
			 match) ||
			(sscanf(buf, "%*[^(](%d%% %*[^)])%n", &result.packet_loss, &match) == 1 && match)) {
			continue;
		}

		/* get the round trip average */
		if ((sscanf(buf, "round-trip min/avg/max = %*f/%lf/%*f%n", &result.round_trip_average,
					&match) == 1 &&
			 match) ||
			(sscanf(buf, "round-trip min/avg/max/mdev = %*f/%lf/%*f/%*f%n",
					&result.round_trip_average, &match) == 1 &&
			 match) ||
			(sscanf(buf, "round-trip min/avg/max/sdev = %*f/%lf/%*f/%*f%n",
					&result.round_trip_average, &match) == 1 &&
			 match) ||
			(sscanf(buf, "round-trip min/avg/max/stddev = %*f/%lf/%*f/%*f%n",
					&result.round_trip_average, &match) == 1 &&
			 match) ||
			(sscanf(buf, "round-trip min/avg/max/std-dev = %*f/%lf/%*f/%*f%n",
					&result.round_trip_average, &match) == 1 &&
			 match) ||
			(sscanf(buf, "round-trip (ms) min/avg/max = %*f/%lf/%*f%n", &result.round_trip_average,
					&match) == 1 &&
			 match) ||
			(sscanf(buf, "round-trip (ms) min/avg/max/stddev = %*f/%lf/%*f/%*f%n",
					&result.round_trip_average, &match) == 1 &&
			 match) ||
			(sscanf(buf, "rtt min/avg/max/mdev = %*f/%lf/%*f/%*f ms%n", &result.round_trip_average,
					&match) == 1 &&
			 match) ||
			(sscanf(buf, "%*[^=] = %*fms, %*[^=] = %*fms, %*[^=] = %lfms%n",
					&result.round_trip_average, &match) == 1 &&
			 match)) {
			continue;
		}
	}

	/* this is needed because there is no rta if all packets are lost */
	if (result.packet_loss == 100) {
		result.round_trip_average = crta;
	}

	/* check stderr, setting at least WARNING if there is output here */
	/* Add warning into warn_text */
	while (fgets(buf, MAX_INPUT_BUFFER - 1, child_stderr)) {
		if (!strstr(buf, "WARNING - no SO_TIMESTAMP support, falling back to SIOCGSTAMP") &&
			!strstr(buf, "Warning: time of day goes back")

		) {
			if (verbose >= 3) {
				printf("Got stderr: %s", buf);
			}
			if ((result.state = error_scan(buf, addr)) == STATE_OK) {
				result.state = STATE_WARNING;
				if (warn_text == NULL) {
					warn_text = strdup(_("System call sent warnings to stderr "));
				} else {
					xasprintf(&warn_text, "%s %s", warn_text,
							  _("System call sent warnings to stderr "));
				}
			}
		}
	}

	(void)fclose(child_stderr);

	spclose(child_process);

	if (warn_text == NULL) {
		warn_text = strdup("");
	}

	return result;
}

mp_state_enum error_scan(char buf[MAX_INPUT_BUFFER], const char *addr) {
	if (strstr(buf, "Network is unreachable") || strstr(buf, "Destination Net Unreachable") ||
		strstr(buf, "No route")) {
		die(STATE_CRITICAL, _("CRITICAL - Network Unreachable (%s)\n"), addr);
	} else if (strstr(buf, "Destination Host Unreachable") || strstr(buf, "Address unreachable")) {
		die(STATE_CRITICAL, _("CRITICAL - Host Unreachable (%s)\n"), addr);
	} else if (strstr(buf, "Destination Port Unreachable") || strstr(buf, "Port unreachable")) {
		die(STATE_CRITICAL, _("CRITICAL - Bogus ICMP: Port Unreachable (%s)\n"), addr);
	} else if (strstr(buf, "Destination Protocol Unreachable")) {
		die(STATE_CRITICAL, _("CRITICAL - Bogus ICMP: Protocol Unreachable (%s)\n"), addr);
	} else if (strstr(buf, "Destination Net Prohibited")) {
		die(STATE_CRITICAL, _("CRITICAL - Network Prohibited (%s)\n"), addr);
	} else if (strstr(buf, "Destination Host Prohibited")) {
		die(STATE_CRITICAL, _("CRITICAL - Host Prohibited (%s)\n"), addr);
	} else if (strstr(buf, "Packet filtered") || strstr(buf, "Administratively prohibited")) {
		die(STATE_CRITICAL, _("CRITICAL - Packet Filtered (%s)\n"), addr);
	} else if (strstr(buf, "unknown host")) {
		die(STATE_CRITICAL, _("CRITICAL - Host not found (%s)\n"), addr);
	} else if (strstr(buf, "Time to live exceeded") || strstr(buf, "Time exceeded")) {
		die(STATE_CRITICAL, _("CRITICAL - Time to live exceeded (%s)\n"), addr);
	} else if (strstr(buf, "Destination unreachable: ")) {
		die(STATE_CRITICAL, _("CRITICAL - Destination Unreachable (%s)\n"), addr);
	}

	if (strstr(buf, "(DUP!)") || strstr(buf, "DUPLICATES FOUND")) {
		if (warn_text == NULL) {
			warn_text = strdup(_(WARN_DUPLICATES));
		} else if (!strstr(warn_text, _(WARN_DUPLICATES)) &&
				   xasprintf(&warn_text, "%s %s", warn_text, _(WARN_DUPLICATES)) == -1) {
			die(STATE_UNKNOWN, _("Unable to realloc warn_text\n"));
		}
		return STATE_WARNING;
	}

	return STATE_OK;
}

void print_help(void) {
	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf(COPYRIGHT, copyright, email);

	printf(_("Use ping to check connection statistics for a remote host."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(UT_IPv46);

	printf(" %s\n", "-H, --hostname=HOST");
	printf("    %s\n", _("host to ping"));
	printf(" %s\n", "-w, --warning=THRESHOLD");
	printf("    %s\n", _("warning threshold pair"));
	printf(" %s\n", "-c, --critical=THRESHOLD");
	printf("    %s\n", _("critical threshold pair"));
	printf(" %s\n", "-p, --packets=INTEGER");
	printf("    %s ", _("number of ICMP ECHO packets to send"));
	printf(_("(Default: %d)\n"), DEFAULT_MAX_PACKETS);
	printf(" %s\n", "-L, --link");
	printf("    %s\n", _("show HTML in the plugin output (obsoleted by urlize)"));

	printf(UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf("\n");
	printf("%s\n", _("THRESHOLD is <rta>,<pl>% where <rta> is the round trip average travel"));
	printf("%s\n", _("time (ms) which triggers a WARNING or CRITICAL state, and <pl> is the"));
	printf("%s\n", _("percentage of packet loss to trigger an alarm state."));

	printf("\n");
	printf("%s\n",
		   _("This plugin uses the ping command to probe the specified host for packet loss"));
	printf("%s\n",
		   _("(percentage) and round trip average (milliseconds). It can produce HTML output."));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s -H <host_address> -w <wrta>,<wpl>%% -c <crta>,<cpl>%%\n", progname);
	printf(" [-p packets] [-t timeout] [-4|-6]\n");
}
