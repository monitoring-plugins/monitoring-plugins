/*****************************************************************************
 *
 * Monitoring check_fping plugin
 *
 * License: GPL
 * Copyright (c) 2000-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_fping plugin
 *
 * This plugin will use the fping command to ping the specified host for a
 * fast check
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

const char *progname = "check_fping";
const char *copyright = "2000-2024";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "popen.h"
#include "netutils.h"
#include "utils.h"
#include <stdbool.h>
#include "check_fping.d/config.h"
#include "states.h"

enum {
	PL = 0,
	RTA = 1
};

static mp_state_enum textscan(char *buf, const char * /*server_name*/, bool /*crta_p*/, double /*crta*/, bool /*wrta_p*/, double /*wrta*/,
							  bool /*cpl_p*/, int /*cpl*/, bool /*wpl_p*/, int /*wpl*/, bool /*alive_p*/);

typedef struct {
	int errorcode;
	check_fping_config config;
} check_fping_config_wrapper;
static check_fping_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static int get_threshold(char *arg, char *rv[2]);
static void print_help(void);
void print_usage(void);

static bool verbose = false;

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_fping_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	const check_fping_config config = tmp_config.config;

	char *server = NULL;
	server = strscpy(server, config.server_name);

	char *option_string = "";
	char *fping_prog = NULL;

	/* First determine if the target is dualstack or ipv6 only. */
	bool server_is_inet6_addr = is_inet6_addr(server);

	/*
	 * If the user requested -6 OR the user made no assertion and the address is v6 or dualstack
	 *   -> we use ipv6
	 * If the user requested -4 OR the user made no assertion and the address is v4 ONLY
	 *   -> we use ipv4
	 */
	if (address_family == AF_INET6 || (address_family == AF_UNSPEC && server_is_inet6_addr)) {
		xasprintf(&option_string, "%s-6 ", option_string);
	} else {
		xasprintf(&option_string, "%s-4 ", option_string);
	}
	fping_prog = strdup(PATH_TO_FPING);

	/* compose the command */
	if (config.target_timeout) {
		xasprintf(&option_string, "%s-t %d ", option_string, config.target_timeout);
	}
	if (config.packet_interval) {
		xasprintf(&option_string, "%s-p %d ", option_string, config.packet_interval);
	}
	if (config.sourceip) {
		xasprintf(&option_string, "%s-S %s ", option_string, config.sourceip);
	}
	if (config.sourceif) {
		xasprintf(&option_string, "%s-I %s ", option_string, config.sourceif);
	}
	if (config.dontfrag) {
		xasprintf(&option_string, "%s-M ", option_string);
	}
	if (config.randomize_packet_data) {
		xasprintf(&option_string, "%s-R ", option_string);
	}

	char *command_line = NULL;
	xasprintf(&command_line, "%s %s-b %d -c %d %s", fping_prog, option_string, config.packet_size, config.packet_count, server);

	if (verbose) {
		printf("%s\n", command_line);
	}

	/* run the command */
	child_process = spopen(command_line);
	if (child_process == NULL) {
		printf(_("Could not open pipe: %s\n"), command_line);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen(child_stderr_array[fileno(child_process)], "r");
	if (child_stderr == NULL) {
		printf(_("Could not open stderr for %s\n"), command_line);
	}

	char *input_buffer = malloc(MAX_INPUT_BUFFER);
	mp_state_enum status = STATE_UNKNOWN;
	while (fgets(input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {
		if (verbose) {
			printf("%s", input_buffer);
		}
		status = max_state(status, textscan(input_buffer, config.server_name, config.crta_p, config.crta, config.wrta_p, config.wrta,
											config.cpl_p, config.cpl, config.wpl_p, config.wpl, config.alive_p));
	}

	/* If we get anything on STDERR, at least set warning */
	while (fgets(input_buffer, MAX_INPUT_BUFFER - 1, child_stderr)) {
		status = max_state(status, STATE_WARNING);
		if (verbose) {
			printf("%s", input_buffer);
		}
		status = max_state(status, textscan(input_buffer, config.server_name, config.crta_p, config.crta, config.wrta_p, config.wrta,
											config.cpl_p, config.cpl, config.wpl_p, config.wpl, config.alive_p));
	}
	(void)fclose(child_stderr);

	/* close the pipe */
	int result = spclose(child_process);
	if (result) {
		/* need to use max_state not max */
		status = max_state(status, STATE_WARNING);
	}

	if (result > 1) {
		status = max_state(status, STATE_UNKNOWN);
		if (result == 2) {
			die(STATE_UNKNOWN, _("FPING UNKNOWN - IP address not found\n"));
		}
		if (result == 3) {
			die(STATE_UNKNOWN, _("FPING UNKNOWN - invalid commandline argument\n"));
		}
		if (result == 4) {
			die(STATE_UNKNOWN, _("FPING UNKNOWN - failed system call\n"));
		}
	}

	printf("FPING %s - %s\n", state_text(status), config.server_name);

	return status;
}

mp_state_enum textscan(char *buf, const char *server_name, bool crta_p, double crta, bool wrta_p, double wrta, bool cpl_p, int cpl,
					   bool wpl_p, int wpl, bool alive_p) {
	/* stops testing after the first successful reply. */
	double rta;
	double loss;
	char *rtastr = NULL;
	if (alive_p && strstr(buf, "avg, 0% loss)")) {
		rtastr = strstr(buf, "ms (");
		rtastr = 1 + index(rtastr, '(');
		rta = strtod(rtastr, NULL);
		loss = strtod("0", NULL);
		die(STATE_OK, _("FPING %s - %s (rta=%f ms)|%s\n"), state_text(STATE_OK), server_name, rta,
			/* No loss since we only waited for the first reply
			perfdata ("loss", (long int)loss, "%", wpl_p, wpl, cpl_p, cpl, true, 0, true, 100), */
			fperfdata("rta", rta / 1.0e3, "s", wrta_p, wrta / 1.0e3, crta_p, crta / 1.0e3, true, 0, false, 0));
	}

	mp_state_enum status = STATE_UNKNOWN;
	char *xmtstr = NULL;
	double xmt;
	char *losstr = NULL;
	if (strstr(buf, "not found")) {
		die(STATE_CRITICAL, _("FPING UNKNOWN - %s not found\n"), server_name);

	} else if (strstr(buf, "is unreachable") || strstr(buf, "Unreachable")) {
		die(STATE_CRITICAL, _("FPING CRITICAL - %s is unreachable\n"), "host");

	} else if (strstr(buf, "Operation not permitted") || strstr(buf, "No such device")) {
		die(STATE_UNKNOWN, _("FPING UNKNOWN - %s parameter error\n"), "host");
	} else if (strstr(buf, "is down")) {
		die(STATE_CRITICAL, _("FPING CRITICAL - %s is down\n"), server_name);

	} else if (strstr(buf, "is alive")) {
		status = STATE_OK;

	} else if (strstr(buf, "xmt/rcv/%loss") && strstr(buf, "min/avg/max")) {
		losstr = strstr(buf, "=");
		losstr = 1 + strstr(losstr, "/");
		losstr = 1 + strstr(losstr, "/");
		rtastr = strstr(buf, "min/avg/max");
		rtastr = strstr(rtastr, "=");
		rtastr = 1 + index(rtastr, '/');
		loss = strtod(losstr, NULL);
		rta = strtod(rtastr, NULL);
		if (cpl_p && loss > cpl) {
			status = STATE_CRITICAL;
		} else if (crta_p && rta > crta) {
			status = STATE_CRITICAL;
		} else if (wpl_p && loss > wpl) {
			status = STATE_WARNING;
		} else if (wrta_p && rta > wrta) {
			status = STATE_WARNING;
		} else {
			status = STATE_OK;
		}
		die(status, _("FPING %s - %s (loss=%.0f%%, rta=%f ms)|%s %s\n"), state_text(status), server_name, loss, rta,
			perfdata("loss", (long int)loss, "%", wpl_p, wpl, cpl_p, cpl, false, 0, false, 0),
			fperfdata("rta", rta / 1.0e3, "s", wrta_p, wrta / 1.0e3, crta_p, crta / 1.0e3, true, 0, false, 0));

	} else if (strstr(buf, "xmt/rcv/%loss")) {
		/* no min/max/avg if host was unreachable in fping v2.2.b1 */
		/* in v2.4b2: 10.99.0.1 : xmt/rcv/%loss = 0/0/0% */
		losstr = strstr(buf, "=");
		xmtstr = 1 + losstr;
		xmt = strtod(xmtstr, NULL);
		if (xmt == 0) {
			die(STATE_CRITICAL, _("FPING CRITICAL - %s is down\n"), server_name);
		}
		losstr = 1 + strstr(losstr, "/");
		losstr = 1 + strstr(losstr, "/");
		loss = strtod(losstr, NULL);
		if (atoi(losstr) == 100) {
			status = STATE_CRITICAL;
		} else if (cpl_p && loss > cpl) {
			status = STATE_CRITICAL;
		} else if (wpl_p && loss > wpl) {
			status = STATE_WARNING;
		} else {
			status = STATE_OK;
		}
		/* loss=%.0f%%;%d;%d;0;100 */
		die(status, _("FPING %s - %s (loss=%.0f%% )|%s\n"), state_text(status), server_name, loss,
			perfdata("loss", (long int)loss, "%", wpl_p, wpl, cpl_p, cpl, false, 0, false, 0));

	} else {
		status = max_state(status, STATE_WARNING);
	}

	return status;
}

/* process command-line arguments */
check_fping_config_wrapper process_arguments(int argc, char **argv) {
	static struct option longopts[] = {
		{"hostname", required_argument, 0, 'H'}, {"sourceip", required_argument, 0, 'S'}, {"sourceif", required_argument, 0, 'I'},
		{"critical", required_argument, 0, 'c'}, {"warning", required_argument, 0, 'w'},  {"alive", no_argument, 0, 'a'},
		{"bytes", required_argument, 0, 'b'},    {"number", required_argument, 0, 'n'},   {"target-timeout", required_argument, 0, 'T'},
		{"interval", required_argument, 0, 'i'}, {"verbose", no_argument, 0, 'v'},        {"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},           {"use-ipv4", no_argument, 0, '4'},       {"use-ipv6", no_argument, 0, '6'},
		{"dontfrag", no_argument, 0, 'M'},       {"random", no_argument, 0, 'R'},         {0, 0, 0, 0}};

	char *rv[2];
	rv[PL] = NULL;
	rv[RTA] = NULL;

	int option = 0;

	check_fping_config_wrapper result = {
		.errorcode = OK,
		.config = check_fping_config_init(),
	};

	if (argc < 2) {
		result.errorcode = ERROR;
		return result;
	}

	if (!is_option(argv[1])) {
		result.config.server_name = argv[1];
		argv[1] = argv[0];
		argv = &argv[1];
		argc--;
	}

	while (1) {
		int option_index = getopt_long(argc, argv, "+hVvaH:S:c:w:b:n:T:i:I:M:R:46", longopts, &option);

		if (option_index == -1 || option_index == EOF || option_index == 1) {
			break;
		}

		switch (option_index) {
		case '?': /* print short usage statement if args not parsable */
			usage5();
		case 'a': /* host alive mode */
			result.config.alive_p = true;
			break;
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'v': /* verbose mode */
			verbose = true;
			break;
		case 'H': /* hostname */
			if (!is_host(optarg)) {
				usage2(_("Invalid hostname/address"), optarg);
			}
			result.config.server_name = optarg;
			break;
		case 'S': /* sourceip */
			if (!is_host(optarg)) {
				usage2(_("Invalid hostname/address"), optarg);
			}
			result.config.sourceip = optarg;
			break;
		case 'I': /* sourceip */
			result.config.sourceif = optarg;
			break;
		case '4': /* IPv4 only */
			address_family = AF_INET;
			break;
		case '6': /* IPv6 only */
			address_family = AF_INET6;
			break;
		case 'c':
			get_threshold(optarg, rv);
			if (rv[RTA]) {
				result.config.crta = strtod(rv[RTA], NULL);
				result.config.crta_p = true;
				rv[RTA] = NULL;
			}
			if (rv[PL]) {
				result.config.cpl = atoi(rv[PL]);
				result.config.cpl_p = true;
				rv[PL] = NULL;
			}
			break;
		case 'w':
			get_threshold(optarg, rv);
			if (rv[RTA]) {
				result.config.wrta = strtod(rv[RTA], NULL);
				result.config.wrta_p = true;
				rv[RTA] = NULL;
			}
			if (rv[PL]) {
				result.config.wpl = atoi(rv[PL]);
				result.config.wpl_p = true;
				rv[PL] = NULL;
			}
			break;
		case 'b': /* bytes per packet */
			if (is_intpos(optarg)) {
				result.config.packet_size = atoi(optarg);
			} else {
				usage(_("Packet size must be a positive integer"));
			}
			break;
		case 'n': /* number of packets */
			if (is_intpos(optarg)) {
				result.config.packet_count = atoi(optarg);
			} else {
				usage(_("Packet count must be a positive integer"));
			}
			break;
		case 'T': /* timeout in msec */
			if (is_intpos(optarg)) {
				result.config.target_timeout = atoi(optarg);
			} else {
				usage(_("Target timeout must be a positive integer"));
			}
			break;
		case 'i': /* interval in msec */
			if (is_intpos(optarg)) {
				result.config.packet_interval = atoi(optarg);
			} else {
				usage(_("Interval must be a positive integer"));
			}
			break;
		case 'R':
			result.config.randomize_packet_data = true;
			break;
		case 'M':
			result.config.dontfrag = true;
			break;
		}
	}

	if (result.config.server_name == NULL) {
		usage4(_("Hostname was not supplied"));
	}

	return result;
}

int get_threshold(char *arg, char *rv[2]) {
	char *arg2 = NULL;

	char *arg1 = strdup(arg);
	if (strpbrk(arg1, ",:")) {
		arg2 = 1 + strpbrk(arg1, ",:");
	}

	if (arg2) {
		arg1[strcspn(arg1, ",:")] = 0;
		if (strstr(arg1, "%") && strstr(arg2, "%")) {
			die(STATE_UNKNOWN, _("%s: Only one threshold may be packet loss (%s)\n"), progname, arg);
		}
		if (!strstr(arg1, "%") && !strstr(arg2, "%")) {
			die(STATE_UNKNOWN, _("%s: Only one threshold must be packet loss (%s)\n"), progname, arg);
		}
	}

	if (arg2 && strstr(arg2, "%")) {
		rv[PL] = arg2;
		rv[RTA] = arg1;
	} else if (arg2) {
		rv[PL] = arg1;
		rv[RTA] = arg2;
	} else if (strstr(arg1, "%")) {
		rv[PL] = arg1;
	} else {
		rv[RTA] = arg1;
	}

	return OK;
}

void print_help(void) {

	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Didi Rieder <adrieder@sbox.tu-graz.ac.at>\n");
	printf(COPYRIGHT, copyright, email);

	printf("%s\n", _("This plugin will use the fping command to ping the specified host for a fast check"));

	printf("%s\n", _("Note that it is necessary to set the suid flag on fping."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(UT_IPv46);

	printf(" %s\n", "-H, --hostname=HOST");
	printf("    %s\n", _("name or IP Address of host to ping (IP Address bypasses name lookup, reducing system load)"));
	printf(" %s\n", "-w, --warning=THRESHOLD");
	printf("    %s\n", _("warning threshold pair"));
	printf(" %s\n", "-c, --critical=THRESHOLD");
	printf("    %s\n", _("critical threshold pair"));
	printf(" %s\n", "-a, --alive");
	printf("    %s\n", _("Return OK after first successful reply"));
	printf(" %s\n", "-b, --bytes=INTEGER");
	printf("    %s (default: %d)\n", _("size of ICMP packet"), PACKET_SIZE);
	printf(" %s\n", "-n, --number=INTEGER");
	printf("    %s (default: %d)\n", _("number of ICMP packets to send"), PACKET_COUNT);
	printf(" %s\n", "-T, --target-timeout=INTEGER");
	printf("    %s (default: fping's default for -t)\n", _("Target timeout (ms)"));
	printf(" %s\n", "-i, --interval=INTEGER");
	printf("    %s (default: fping's default for -p)\n", _("Interval (ms) between sending packets"));
	printf(" %s\n", "-S, --sourceip=HOST");
	printf("    %s\n", _("name or IP Address of sourceip"));
	printf(" %s\n", "-I, --sourceif=IF");
	printf("    %s\n", _("source interface name"));
	printf(" %s\n", "-M, --dontfrag");
	printf("    %s\n", _("set the Don't Fragment flag"));
	printf(" %s\n", "-R, --random");
	printf("    %s\n", _("random packet data (to foil link data compression)"));
	printf(UT_VERBOSE);
	printf("\n");
	printf(" %s\n", _("THRESHOLD is <rta>,<pl>%% where <rta> is the round trip average travel time (ms)"));
	printf(" %s\n", _("which triggers a WARNING or CRITICAL state, and <pl> is the percentage of"));
	printf(" %s\n", _("packet loss to trigger an alarm state."));

	printf("\n");
	printf(" %s\n", _("IPv4 is used by default. Specify -6 to use IPv6."));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf(" %s <host_address> -w limit -c limit [-b size] [-n number] [-T number] [-i number]\n", progname);
}
