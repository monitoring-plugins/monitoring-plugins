/*****************************************************************************
* 
* Nagios check_ping plugin
* 
* License: GPL
* Copyright (c) 2000-2007 Nagios Plugins Development Team
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
const char *copyright = "2000-2007";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "popen.h"
#include "utils.h"

#define WARN_DUPLICATES "DUPLICATES FOUND! "
#define UNKNOWN_TRIP_TIME -1.0	/* -1 seconds */

enum {
	UNKNOWN_PACKET_LOSS = 200,    /* 200% */
	DEFAULT_MAX_PACKETS = 5       /* default no. of ICMP ECHO packets */
};

int process_arguments (int, char **);
int get_threshold (char *, float *, int *);
int validate_arguments (void);
int run_ping (const char *cmd, const char *addr);
int error_scan (char buf[MAX_INPUT_BUFFER], const char *addr);
void print_usage (void);
void print_help (void);

int display_html = FALSE;
int wpl = UNKNOWN_PACKET_LOSS;
int cpl = UNKNOWN_PACKET_LOSS;
float wrta = UNKNOWN_TRIP_TIME;
float crta = UNKNOWN_TRIP_TIME;
char **addresses = NULL;
int n_addresses = 0;
int max_addr = 1;
int max_packets = -1;
int verbose = 0;

float rta = UNKNOWN_TRIP_TIME;
int pl = UNKNOWN_PACKET_LOSS;

char *warn_text;



int
main (int argc, char **argv)
{
	char *cmd = NULL;
	char *rawcmd = NULL;
	int result = STATE_UNKNOWN;
	int this_result = STATE_UNKNOWN;
	int i;

	setlocale (LC_ALL, "");
	setlocale (LC_NUMERIC, "C");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	addresses = malloc (sizeof(char*) * max_addr);
	addresses[0] = NULL;

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* Set signal handling and alarm */
	if (signal (SIGALRM, popen_timeout_alarm_handler) == SIG_ERR) {
		usage4 (_("Cannot catch SIGALRM"));
	}

	/* If ./configure finds ping has timeout values, set plugin alarm slightly
	 * higher so that we can use response from command line ping */
#if defined(PING_PACKETS_FIRST) && defined(PING_HAS_TIMEOUT)
	alarm (timeout_interval + 1);
#else
	alarm (timeout_interval);
#endif

	for (i = 0 ; i < n_addresses ; i++) {

#ifdef PING6_COMMAND
		if (address_family != AF_INET && is_inet6_addr(addresses[i]))
			rawcmd = strdup(PING6_COMMAND);
		else
			rawcmd = strdup(PING_COMMAND);
#else
		rawcmd = strdup(PING_COMMAND);
#endif

		/* does the host address of number of packets argument come first? */
#ifdef PING_PACKETS_FIRST
# ifdef PING_HAS_TIMEOUT
		xasprintf (&cmd, rawcmd, timeout_interval, max_packets, addresses[i]);
# else
		xasprintf (&cmd, rawcmd, max_packets, addresses[i]);
# endif
#else
		xasprintf (&cmd, rawcmd, addresses[i], max_packets);
#endif

		if (verbose >= 2)
			printf ("CMD: %s\n", cmd);

		/* run the command */
		this_result = run_ping (cmd, addresses[i]);

		if (pl == UNKNOWN_PACKET_LOSS || rta < 0.0) {
			printf ("%s\n", cmd);
			die (STATE_UNKNOWN,
			           _("CRITICAL - Could not interpret output from ping command\n"));
		}

		if (pl >= cpl || rta >= crta || rta < 0)
			this_result = STATE_CRITICAL;
		else if (pl >= wpl || rta >= wrta)
			this_result = STATE_WARNING;
		else if (pl >= 0 && rta >= 0)
			this_result = max_state (STATE_OK, this_result);

		if (n_addresses > 1 && this_result != STATE_UNKNOWN)
			die (STATE_OK, "%s is alive\n", addresses[i]);

		if (display_html == TRUE)
			printf ("<A HREF='%s/traceroute.cgi?%s'>", CGIURL, addresses[i]);
		if (pl == 100)
			printf (_("PING %s - %sPacket loss = %d%%"), state_text (this_result), warn_text,
							pl);
		else
			printf (_("PING %s - %sPacket loss = %d%%, RTA = %2.2f ms"),
							state_text (this_result), warn_text, pl, rta);
		if (display_html == TRUE)
			printf ("</A>");

		/* Print performance data */
		printf("|%s", fperfdata ("rta", (double) rta, "ms",
		                          wrta>0?TRUE:FALSE, wrta,
		                          crta>0?TRUE:FALSE, crta,
		                          TRUE, 0, FALSE, 0));
		printf(" %s\n", perfdata ("pl", (long) pl, "%",
		                          wpl>0?TRUE:FALSE, wpl,
		                          cpl>0?TRUE:FALSE, cpl,
		                          TRUE, 0, FALSE, 0));

		if (verbose >= 2)
			printf ("%f:%d%% %f:%d%%\n", wrta, wpl, crta, cpl);

		result = max_state (result, this_result);
		free (rawcmd);
		free (cmd);
	}

	return result;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c = 1;
	char *ptr;

	int option = 0;
	static struct option longopts[] = {
		STD_LONG_OPTS,
		{"packets", required_argument, 0, 'p'},
		{"nohtml", no_argument, 0, 'n'},
		{"link", no_argument, 0, 'L'},
		{"use-ipv4", no_argument, 0, '4'},
		{"use-ipv6", no_argument, 0, '6'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
		if (strcmp ("-nohtml", argv[c]) == 0)
			strcpy (argv[c], "-n");
	}

	while (1) {
		c = getopt_long (argc, argv, "VvhnL46t:c:w:H:p:", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':	/* usage */
			usage5 ();
		case 'h':	/* help */
			print_help ();
			exit (STATE_OK);
			break;
		case 'V':	/* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_OK);
			break;
		case 't':	/* timeout period */
			timeout_interval = atoi (optarg);
			break;
		case 'v':	/* verbose mode */
			verbose++;
			break;
		case '4':	/* IPv4 only */
			address_family = AF_INET;
			break;
		case '6':	/* IPv6 only */
#ifdef USE_IPV6
			address_family = AF_INET6;
#else
			usage (_("IPv6 support not available\n"));
#endif
			break;
		case 'H':	/* hostname */
			ptr=optarg;
			while (1) {
				n_addresses++;
				if (n_addresses > max_addr) {
					max_addr *= 2;
					addresses = realloc (addresses, sizeof(char*) * max_addr);
					if (addresses == NULL)
						die (STATE_UNKNOWN, _("Could not realloc() addresses\n"));
				}
				addresses[n_addresses-1] = ptr;
				if ((ptr = index (ptr, ','))) {
					strcpy (ptr, "");
					ptr += sizeof(char);
				} else {
					break;
				}
			}
			break;
		case 'p':	/* number of packets to send */
			if (is_intnonneg (optarg))
				max_packets = atoi (optarg);
			else
				usage2 (_("<max_packets> (%s) must be a non-negative number\n"), optarg);
			break;
		case 'n':	/* no HTML */
			display_html = FALSE;
			break;
		case 'L':	/* show HTML */
			display_html = TRUE;
			break;
		case 'c':
			get_threshold (optarg, &crta, &cpl);
			break;
		case 'w':
			get_threshold (optarg, &wrta, &wpl);
			break;
		}
	}

	c = optind;
	if (c == argc)
		return validate_arguments ();

	if (addresses[0] == NULL) {
		if (is_host (argv[c]) == FALSE) {
			usage2 (_("Invalid hostname/address"), argv[c]);
		} else {
			addresses[0] = argv[c++];
			n_addresses++;
			if (c == argc)
				return validate_arguments ();
		}
	}

	if (wpl == UNKNOWN_PACKET_LOSS) {
		if (is_intpercent (argv[c]) == FALSE) {
			printf (_("<wpl> (%s) must be an integer percentage\n"), argv[c]);
			return ERROR;
		} else {
			wpl = atoi (argv[c++]);
			if (c == argc)
				return validate_arguments ();
		}
	}

	if (cpl == UNKNOWN_PACKET_LOSS) {
		if (is_intpercent (argv[c]) == FALSE) {
			printf (_("<cpl> (%s) must be an integer percentage\n"), argv[c]);
			return ERROR;
		} else {
			cpl = atoi (argv[c++]);
			if (c == argc)
				return validate_arguments ();
		}
	}

	if (wrta < 0.0) {
		if (is_negative (argv[c])) {
			printf (_("<wrta> (%s) must be a non-negative number\n"), argv[c]);
			return ERROR;
		} else {
			wrta = atof (argv[c++]);
			if (c == argc)
				return validate_arguments ();
		}
	}

	if (crta < 0.0) {
		if (is_negative (argv[c])) {
			printf (_("<crta> (%s) must be a non-negative number\n"), argv[c]);
			return ERROR;
		} else {
			crta = atof (argv[c++]);
			if (c == argc)
				return validate_arguments ();
		}
	}

	if (max_packets == -1) {
		if (is_intnonneg (argv[c])) {
			max_packets = atoi (argv[c++]);
		} else {
			printf (_("<max_packets> (%s) must be a non-negative number\n"), argv[c]);
			return ERROR;
		}
	}

	return validate_arguments ();
}



int
get_threshold (char *arg, float *trta, int *tpl)
{
	if (is_intnonneg (arg) && sscanf (arg, "%f", trta) == 1)
		return OK;
	else if (strpbrk (arg, ",:") && strstr (arg, "%") && sscanf (arg, "%f%*[:,]%d%%", trta, tpl) == 2)
		return OK;
	else if (strstr (arg, "%") && sscanf (arg, "%d%%", tpl) == 1)
		return OK;

	usage2 (_("%s: Warning threshold must be integer or percentage!\n\n"), arg);
	return STATE_UNKNOWN;
}



int
validate_arguments ()
{
	float max_seconds;
	int i;

	if (wrta < 0.0) {
		printf (_("<wrta> was not set\n"));
		return ERROR;
	}
	else if (crta < 0.0) {
		printf (_("<crta> was not set\n"));
		return ERROR;
	}
	else if (wpl == UNKNOWN_PACKET_LOSS) {
		printf (_("<wpl> was not set\n"));
		return ERROR;
	}
	else if (cpl == UNKNOWN_PACKET_LOSS) {
		printf (_("<cpl> was not set\n"));
		return ERROR;
	}
	else if (wrta > crta) {
		printf (_("<wrta> (%f) cannot be larger than <crta> (%f)\n"), wrta, crta);
		return ERROR;
	}
	else if (wpl > cpl) {
		printf (_("<wpl> (%d) cannot be larger than <cpl> (%d)\n"), wpl, cpl);
		return ERROR;
	}

	if (max_packets == -1)
		max_packets = DEFAULT_MAX_PACKETS;

	max_seconds = crta / 1000.0 * max_packets + max_packets;
	if (max_seconds > timeout_interval)
		timeout_interval = (int)max_seconds;

	for (i=0; i<n_addresses; i++) {
		if (is_host(addresses[i]) == FALSE)
			usage2 (_("Invalid hostname/address"), addresses[i]);
	}

	if (n_addresses == 0) {
		usage (_("You must specify a server address or host name"));
	}

	return OK;
}



int
run_ping (const char *cmd, const char *addr)
{
	char buf[MAX_INPUT_BUFFER];
	int result = STATE_UNKNOWN;
	int match;

	if ((child_process = spopen (cmd)) == NULL)
		die (STATE_UNKNOWN, _("Could not open pipe: %s\n"), cmd);

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL)
		printf (_("Cannot open stderr for %s\n"), cmd);

	while (fgets (buf, MAX_INPUT_BUFFER - 1, child_process)) {

		if (verbose >= 3)
			printf("Output: %s", buf);

		result = max_state (result, error_scan (buf, addr));

		/* get the percent loss statistics */
		match = 0;
		if((sscanf(buf,"%*d packets transmitted, %*d packets received, +%*d errors, %d%% packet loss%n",&pl,&match) && match) ||
			 (sscanf(buf,"%*d packets transmitted, %*d packets received, +%*d duplicates, %d%% packet loss%n",&pl,&match) && match) ||
			 (sscanf(buf,"%*d packets transmitted, %*d received, +%*d duplicates, %d%% packet loss%n",&pl,&match) && match) ||
			 (sscanf(buf,"%*d packets transmitted, %*d packets received, %d%% packet loss%n",&pl,&match) && match) ||
			 (sscanf(buf,"%*d packets transmitted, %*d packets received, %d%% loss, time%n",&pl,&match) && match) ||
			 (sscanf(buf,"%*d packets transmitted, %*d received, %d%% loss, time%n",&pl,&match) && match) ||
			 (sscanf(buf,"%*d packets transmitted, %*d received, %d%% packet loss, time%n",&pl,&match) && match) ||
			 (sscanf(buf,"%*d packets transmitted, %*d received, +%*d errors, %d%% packet loss%n",&pl,&match) && match) ||
			 (sscanf(buf,"%*d packets transmitted %*d received, +%*d errors, %d%% packet loss%n",&pl,&match) && match)
			 )
			continue;

		/* get the round trip average */
		else
			if((sscanf(buf,"round-trip min/avg/max = %*f/%f/%*f%n",&rta,&match) && match) ||
				 (sscanf(buf,"round-trip min/avg/max/mdev = %*f/%f/%*f/%*f%n",&rta,&match) && match) ||
				 (sscanf(buf,"round-trip min/avg/max/sdev = %*f/%f/%*f/%*f%n",&rta,&match) && match) ||
				 (sscanf(buf,"round-trip min/avg/max/stddev = %*f/%f/%*f/%*f%n",&rta,&match) && match) ||
				 (sscanf(buf,"round-trip min/avg/max/std-dev = %*f/%f/%*f/%*f%n",&rta,&match) && match) ||
				 (sscanf(buf,"round-trip (ms) min/avg/max = %*f/%f/%*f%n",&rta,&match) && match) ||
				 (sscanf(buf,"round-trip (ms) min/avg/max/stddev = %*f/%f/%*f/%*f%n",&rta,&match) && match) ||
				 (sscanf(buf,"rtt min/avg/max/mdev = %*f/%f/%*f/%*f ms%n",&rta,&match) && match))
			continue;
	}

	/* this is needed because there is no rta if all packets are lost */
	if (pl == 100)
		rta = crta;

	/* check stderr, setting at least WARNING if there is output here */
	/* Add warning into warn_text */
	while (fgets (buf, MAX_INPUT_BUFFER - 1, child_stderr)) {
		if (! strstr(buf,"WARNING - no SO_TIMESTAMP support, falling back to SIOCGSTAMP")) {
			if (verbose >= 3) {
				printf("Got stderr: %s", buf);
			}
			if ((result=error_scan(buf, addr)) == STATE_OK) {
				result = STATE_WARNING;
				if (warn_text == NULL) {
					warn_text = strdup(_("System call sent warnings to stderr "));
				} else {
					xasprintf(&warn_text, "%s %s", warn_text, _("System call sent warnings to stderr "));
				}
			}
		}
	}

	(void) fclose (child_stderr);


	spclose (child_process);

	if (warn_text == NULL)
		warn_text = strdup("");

	return result;
}



int
error_scan (char buf[MAX_INPUT_BUFFER], const char *addr)
{
	if (strstr (buf, "Network is unreachable") ||
		strstr (buf, "Destination Net Unreachable")
		)
		die (STATE_CRITICAL, _("CRITICAL - Network Unreachable (%s)\n"), addr);
	else if (strstr (buf, "Destination Host Unreachable"))
		die (STATE_CRITICAL, _("CRITICAL - Host Unreachable (%s)\n"), addr);
	else if (strstr (buf, "Destination Port Unreachable"))
		die (STATE_CRITICAL, _("CRITICAL - Bogus ICMP: Port Unreachable (%s)\n"), addr);
	else if (strstr (buf, "Destination Protocol Unreachable"))
		die (STATE_CRITICAL, _("CRITICAL - Bogus ICMP: Protocol Unreachable (%s)\n"), addr);
	else if (strstr (buf, "Destination Net Prohibited"))
		die (STATE_CRITICAL, _("CRITICAL - Network Prohibited (%s)\n"), addr);
	else if (strstr (buf, "Destination Host Prohibited"))
		die (STATE_CRITICAL, _("CRITICAL - Host Prohibited (%s)\n"), addr);
	else if (strstr (buf, "Packet filtered"))
		die (STATE_CRITICAL, _("CRITICAL - Packet Filtered (%s)\n"), addr);
	else if (strstr (buf, "unknown host" ))
		die (STATE_CRITICAL, _("CRITICAL - Host not found (%s)\n"), addr);
	else if (strstr (buf, "Time to live exceeded"))
		die (STATE_CRITICAL, _("CRITICAL - Time to live exceeded (%s)\n"), addr);
	else if (strstr (buf, "Destination unreachable: "))
		die (STATE_CRITICAL, _("CRITICAL - Destination Unreachable (%s)\n"), addr);

	if (strstr (buf, "(DUP!)") || strstr (buf, "DUPLICATES FOUND")) {
		if (warn_text == NULL)
			warn_text = strdup (_(WARN_DUPLICATES));
		else if (! strstr (warn_text, _(WARN_DUPLICATES)) &&
		         xasprintf (&warn_text, "%s %s", warn_text, _(WARN_DUPLICATES)) == -1)
			die (STATE_UNKNOWN, _("Unable to realloc warn_text\n"));
		return (STATE_WARNING);
	}

	return (STATE_OK);
}



void
print_help (void)
{
	print_revision (progname, NP_VERSION);

	printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf (COPYRIGHT, copyright, email);

	printf (_("Use ping to check connection statistics for a remote host."));

  printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (UT_IPv46);

	printf (" %s\n", "-H, --hostname=HOST");
  printf ("    %s\n", _("host to ping"));
  printf (" %s\n", "-w, --warning=THRESHOLD");
  printf ("    %s\n", _("warning threshold pair"));
  printf (" %s\n", "-c, --critical=THRESHOLD");
  printf ("    %s\n", _("critical threshold pair"));
  printf (" %s\n", "-p, --packets=INTEGER");
  printf ("    %s ", _("number of ICMP ECHO packets to send"));
  printf (_("(Default: %d)\n"), DEFAULT_MAX_PACKETS);
  printf (" %s\n", "-L, --link");
  printf ("    %s\n", _("show HTML in the plugin output (obsoleted by urlize)"));

	printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

  printf ("\n");
	printf ("%s\n", _("THRESHOLD is <rta>,<pl>% where <rta> is the round trip average travel"));
  printf ("%s\n", _("time (ms) which triggers a WARNING or CRITICAL state, and <pl> is the"));
  printf ("%s\n", _("percentage of packet loss to trigger an alarm state."));

  printf ("\n");
	printf ("%s\n", _("This plugin uses the ping command to probe the specified host for packet loss"));
  printf ("%s\n", _("(percentage) and round trip average (milliseconds). It can produce HTML output"));
  printf ("%s\n", _("linking to a traceroute CGI contributed by Ian Cass. The CGI can be found in"));
  printf ("%s\n", _("the contrib area of the downloads section at http://www.nagios.org/"));

	printf (UT_SUPPORT);
}

void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
	printf ("%s -H <host_address> -w <wrta>,<wpl>%% -c <crta>,<cpl>%%\n", progname);
  printf (" [-p packets] [-t timeout] [-4|-6]\n");
}
