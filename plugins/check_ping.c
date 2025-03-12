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

#include <signal.h>

#define WARN_DUPLICATES   "DUPLICATES FOUND! "
#define UNKNOWN_TRIP_TIME -1.0 /* -1 seconds */

enum {
	UNKNOWN_PACKET_LOSS = 200, /* 200% */
	DEFAULT_MAX_PACKETS = 5    /* default no. of ICMP ECHO packets */
};

static int process_arguments(int /*argc*/, char ** /*argv*/);
static int get_threshold(char * /*arg*/, float * /*trta*/, int * /*tpl*/);
static int validate_arguments(void);
static int run_ping(const char *cmd, const char *addr);
static int error_scan(char buf[MAX_INPUT_BUFFER], const char *addr);
static void print_help(void);
void print_usage(void);

static bool display_html = false;
static int wpl = UNKNOWN_PACKET_LOSS;
static int cpl = UNKNOWN_PACKET_LOSS;
static float wrta = UNKNOWN_TRIP_TIME;
static float crta = UNKNOWN_TRIP_TIME;
static char **addresses = NULL;
static int n_addresses = 0;
static int max_addr = 1;
static int max_packets = -1;
static int verbose = 0;

static float round_trip_average = UNKNOWN_TRIP_TIME;
static int packet_loss = UNKNOWN_PACKET_LOSS;

static char *warn_text;

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "C");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	addresses = malloc(sizeof(char *) * max_addr);
	addresses[0] = NULL;

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	if (process_arguments(argc, argv) == ERROR) {
		usage4(_("Could not parse arguments"));
	}

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
	for (int i = 0; i < n_addresses; i++) {
#ifdef PING6_COMMAND
		if (address_family != AF_INET && is_inet6_addr(addresses[i])) {
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
		xasprintf(&cmd, rawcmd, timeout_interval, max_packets, addresses[i]);
#	else
		xasprintf(&cmd, rawcmd, max_packets, addresses[i]);
#	endif
#else
		xasprintf(&cmd, rawcmd, addresses[i], max_packets);
#endif

		if (verbose >= 2) {
			printf("CMD: %s\n", cmd);
		}

		/* run the command */
		int this_result = run_ping(cmd, addresses[i]);

		if (packet_loss == UNKNOWN_PACKET_LOSS || round_trip_average < 0.0) {
			printf("%s\n", cmd);
			die(STATE_UNKNOWN, _("CRITICAL - Could not interpret output from ping command\n"));
		}

		if (packet_loss >= cpl || round_trip_average >= crta || round_trip_average < 0) {
			this_result = STATE_CRITICAL;
		} else if (packet_loss >= wpl || round_trip_average >= wrta) {
			this_result = STATE_WARNING;
		} else if (packet_loss >= 0 && round_trip_average >= 0) {
			this_result = max_state(STATE_OK, this_result);
		}

		if (n_addresses > 1 && this_result != STATE_UNKNOWN) {
			die(STATE_OK, "%s is alive\n", addresses[i]);
		}

		if (display_html) {
			printf("<A HREF='%s/traceroute.cgi?%s'>", CGIURL, addresses[i]);
		}
		if (packet_loss == 100) {
			printf(_("PING %s - %sPacket loss = %d%%"), state_text(this_result), warn_text, packet_loss);
		} else {
			printf(_("PING %s - %sPacket loss = %d%%, RTA = %2.2f ms"), state_text(this_result), warn_text, packet_loss,
				   round_trip_average);
		}
		if (display_html) {
			printf("</A>");
		}

		/* Print performance data */
		if (packet_loss != 100) {
			printf("|%s",
				   fperfdata("rta", (double)round_trip_average, "ms", (bool)(wrta > 0), wrta, (bool)(crta > 0), crta, true, 0, false, 0));
		} else {
			printf("| rta=U;%f;%f;;", wrta, crta);
		}

		printf(" %s\n", perfdata("pl", (long)packet_loss, "%", (bool)(wpl > 0), wpl, (bool)(cpl > 0), cpl, true, 0, false, 0));

		if (verbose >= 2) {
			printf("%f:%d%% %f:%d%%\n", wrta, wpl, crta, cpl);
		}

		result = max_state(result, this_result);
		free(rawcmd);
		free(cmd);
	}

	return result;
}

/* process command-line arguments */
int process_arguments(int argc, char **argv) {
	static struct option longopts[] = {STD_LONG_OPTS,
									   {"packets", required_argument, 0, 'p'},
									   {"nohtml", no_argument, 0, 'n'},
									   {"link", no_argument, 0, 'L'},
									   {"use-ipv4", no_argument, 0, '4'},
									   {"use-ipv6", no_argument, 0, '6'},
									   {0, 0, 0, 0}};

	if (argc < 2) {
		return ERROR;
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
	while (true) {
		int option_index = getopt_long(argc, argv, "VvhnL46t:c:w:H:p:", longopts, &option);

		if (option_index == -1 || option_index == EOF) {
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
				n_addresses++;
				if (n_addresses > max_addr) {
					max_addr *= 2;
					addresses = realloc(addresses, sizeof(char *) * max_addr);
					if (addresses == NULL) {
						die(STATE_UNKNOWN, _("Could not realloc() addresses\n"));
					}
				}
				addresses[n_addresses - 1] = ptr;
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
				max_packets = atoi(optarg);
			} else {
				usage2(_("<max_packets> (%s) must be a non-negative number\n"), optarg);
			}
			break;
		case 'n': /* no HTML */
			display_html = false;
			break;
		case 'L': /* show HTML */
			display_html = true;
			break;
		case 'c':
			get_threshold(optarg, &crta, &cpl);
			break;
		case 'w':
			get_threshold(optarg, &wrta, &wpl);
			break;
		}
	}

	int arg_counter = optind;
	if (arg_counter == argc) {
		return validate_arguments();
	}

	if (addresses[0] == NULL) {
		if (!is_host(argv[arg_counter])) {
			usage2(_("Invalid hostname/address"), argv[arg_counter]);
		} else {
			addresses[0] = argv[arg_counter++];
			n_addresses++;
			if (arg_counter == argc) {
				return validate_arguments();
			}
		}
	}

	if (wpl == UNKNOWN_PACKET_LOSS) {
		if (!is_intpercent(argv[arg_counter])) {
			printf(_("<wpl> (%s) must be an integer percentage\n"), argv[arg_counter]);
			return ERROR;
		}
		wpl = atoi(argv[arg_counter++]);
		if (arg_counter == argc) {
			return validate_arguments();
		}
	}

	if (cpl == UNKNOWN_PACKET_LOSS) {
		if (!is_intpercent(argv[arg_counter])) {
			printf(_("<cpl> (%s) must be an integer percentage\n"), argv[arg_counter]);
			return ERROR;
		}
		cpl = atoi(argv[arg_counter++]);
		if (arg_counter == argc) {
			return validate_arguments();
		}
	}

	if (wrta < 0.0) {
		if (is_negative(argv[arg_counter])) {
			printf(_("<wrta> (%s) must be a non-negative number\n"), argv[arg_counter]);
			return ERROR;
		}
		wrta = atof(argv[arg_counter++]);
		if (arg_counter == argc) {
			return validate_arguments();
		}
	}

	if (crta < 0.0) {
		if (is_negative(argv[arg_counter])) {
			printf(_("<crta> (%s) must be a non-negative number\n"), argv[arg_counter]);
			return ERROR;
		}
		crta = atof(argv[arg_counter++]);
		if (arg_counter == argc) {
			return validate_arguments();
		}
	}

	if (max_packets == -1) {
		if (is_intnonneg(argv[arg_counter])) {
			max_packets = atoi(argv[arg_counter++]);
		} else {
			printf(_("<max_packets> (%s) must be a non-negative number\n"), argv[arg_counter]);
			return ERROR;
		}
	}

	return validate_arguments();
}

int get_threshold(char *arg, float *trta, int *tpl) {
	if (is_intnonneg(arg) && sscanf(arg, "%f", trta) == 1) {
		return OK;
	}

	if (strpbrk(arg, ",:") && strstr(arg, "%") && sscanf(arg, "%f%*[:,]%d%%", trta, tpl) == 2) {
		return OK;
	}

	if (strstr(arg, "%") && sscanf(arg, "%d%%", tpl) == 1) {
		return OK;
	}

	usage2(_("%s: Warning threshold must be integer or percentage!\n\n"), arg);
	return STATE_UNKNOWN;
}

int validate_arguments() {
	if (wrta < 0.0) {
		printf(_("<wrta> was not set\n"));
		return ERROR;
	}

	if (crta < 0.0) {
		printf(_("<crta> was not set\n"));
		return ERROR;
	}

	if (wpl == UNKNOWN_PACKET_LOSS) {
		printf(_("<wpl> was not set\n"));
		return ERROR;
	}

	if (cpl == UNKNOWN_PACKET_LOSS) {
		printf(_("<cpl> was not set\n"));
		return ERROR;
	}

	if (wrta > crta) {
		printf(_("<wrta> (%f) cannot be larger than <crta> (%f)\n"), wrta, crta);
		return ERROR;
	}

	if (wpl > cpl) {
		printf(_("<wpl> (%d) cannot be larger than <cpl> (%d)\n"), wpl, cpl);
		return ERROR;
	}

	if (max_packets == -1) {
		max_packets = DEFAULT_MAX_PACKETS;
	}

	float max_seconds = (crta / 1000.0 * max_packets) + max_packets;
	if (max_seconds > timeout_interval) {
		timeout_interval = (unsigned int)max_seconds;
	}

	for (int i = 0; i < n_addresses; i++) {
		if (!is_host(addresses[i])) {
			usage2(_("Invalid hostname/address"), addresses[i]);
		}
	}

	if (n_addresses == 0) {
		usage(_("You must specify a server address or host name"));
	}

	return OK;
}

int run_ping(const char *cmd, const char *addr) {
	if ((child_process = spopen(cmd)) == NULL) {
		die(STATE_UNKNOWN, _("Could not open pipe: %s\n"), cmd);
	}

	child_stderr = fdopen(child_stderr_array[fileno(child_process)], "r");
	if (child_stderr == NULL) {
		printf(_("Cannot open stderr for %s\n"), cmd);
	}

	char buf[MAX_INPUT_BUFFER];
	int result = STATE_UNKNOWN;
	while (fgets(buf, MAX_INPUT_BUFFER - 1, child_process)) {
		if (verbose >= 3) {
			printf("Output: %s", buf);
		}

		result = max_state(result, error_scan(buf, addr));

		/* get the percent loss statistics */
		int match = 0;
		if ((sscanf(buf, "%*d packets transmitted, %*d packets received, +%*d errors, %d%% packet loss%n", &packet_loss, &match) &&
			 match) ||
			(sscanf(buf, "%*d packets transmitted, %*d packets received, +%*d duplicates, %d%% packet loss%n", &packet_loss, &match) &&
			 match) ||
			(sscanf(buf, "%*d packets transmitted, %*d received, +%*d duplicates, %d%% packet loss%n", &packet_loss, &match) && match) ||
			(sscanf(buf, "%*d packets transmitted, %*d packets received, %d%% packet loss%n", &packet_loss, &match) && match) ||
			(sscanf(buf, "%*d packets transmitted, %*d packets received, %d%% loss, time%n", &packet_loss, &match) && match) ||
			(sscanf(buf, "%*d packets transmitted, %*d received, %d%% loss, time%n", &packet_loss, &match) && match) ||
			(sscanf(buf, "%*d packets transmitted, %*d received, %d%% packet loss, time%n", &packet_loss, &match) && match) ||
			(sscanf(buf, "%*d packets transmitted, %*d received, +%*d errors, %d%% packet loss%n", &packet_loss, &match) && match) ||
			(sscanf(buf, "%*d packets transmitted %*d received, +%*d errors, %d%% packet loss%n", &packet_loss, &match) && match) ||
			(sscanf(buf, "%*[^(](%d%% %*[^)])%n", &packet_loss, &match) && match)) {
			continue;
		}

		/* get the round trip average */
		if ((sscanf(buf, "round-trip min/avg/max = %*f/%f/%*f%n", &round_trip_average, &match) && match) ||
			(sscanf(buf, "round-trip min/avg/max/mdev = %*f/%f/%*f/%*f%n", &round_trip_average, &match) && match) ||
			(sscanf(buf, "round-trip min/avg/max/sdev = %*f/%f/%*f/%*f%n", &round_trip_average, &match) && match) ||
			(sscanf(buf, "round-trip min/avg/max/stddev = %*f/%f/%*f/%*f%n", &round_trip_average, &match) && match) ||
			(sscanf(buf, "round-trip min/avg/max/std-dev = %*f/%f/%*f/%*f%n", &round_trip_average, &match) && match) ||
			(sscanf(buf, "round-trip (ms) min/avg/max = %*f/%f/%*f%n", &round_trip_average, &match) && match) ||
			(sscanf(buf, "round-trip (ms) min/avg/max/stddev = %*f/%f/%*f/%*f%n", &round_trip_average, &match) && match) ||
			(sscanf(buf, "rtt min/avg/max/mdev = %*f/%f/%*f/%*f ms%n", &round_trip_average, &match) && match) ||
			(sscanf(buf, "%*[^=] = %*fms, %*[^=] = %*fms, %*[^=] = %fms%n", &round_trip_average, &match) && match)) {
			continue;
		}
	}

	/* this is needed because there is no rta if all packets are lost */
	if (packet_loss == 100) {
		round_trip_average = crta;
	}

	/* check stderr, setting at least WARNING if there is output here */
	/* Add warning into warn_text */
	while (fgets(buf, MAX_INPUT_BUFFER - 1, child_stderr)) {
		if (!strstr(buf, "WARNING - no SO_TIMESTAMP support, falling back to SIOCGSTAMP") && !strstr(buf, "Warning: time of day goes back")

		) {
			if (verbose >= 3) {
				printf("Got stderr: %s", buf);
			}
			if ((result = error_scan(buf, addr)) == STATE_OK) {
				result = STATE_WARNING;
				if (warn_text == NULL) {
					warn_text = strdup(_("System call sent warnings to stderr "));
				} else {
					xasprintf(&warn_text, "%s %s", warn_text, _("System call sent warnings to stderr "));
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

int error_scan(char buf[MAX_INPUT_BUFFER], const char *addr) {
	if (strstr(buf, "Network is unreachable") || strstr(buf, "Destination Net Unreachable") || strstr(buf, "No route")) {
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
		} else if (!strstr(warn_text, _(WARN_DUPLICATES)) && xasprintf(&warn_text, "%s %s", warn_text, _(WARN_DUPLICATES)) == -1) {
			die(STATE_UNKNOWN, _("Unable to realloc warn_text\n"));
		}
		return (STATE_WARNING);
	}

	return (STATE_OK);
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
	printf("%s\n", _("This plugin uses the ping command to probe the specified host for packet loss"));
	printf("%s\n", _("(percentage) and round trip average (milliseconds). It can produce HTML output"));
	printf("%s\n", _("linking to a traceroute CGI contributed by Ian Cass. The CGI can be found in"));
	printf("%s\n", _("the contrib area of the downloads section at http://www.nagios.org/"));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s -H <host_address> -w <wrta>,<wpl>%% -c <crta>,<cpl>%%\n", progname);
	printf(" [-p packets] [-t timeout] [-4|-6]\n");
}
