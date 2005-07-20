/******************************************************************************

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 $Id$
 
******************************************************************************/

const char *progname = "check_ping";
const char *revision = "$Revision$";
const char *copyright = "2000-2004";
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
int n_addresses;
int max_addr = 1;
int max_packets = -1;
int verbose = FALSE;

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

	setlocale (LC_NUMERIC, "C");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	addresses = malloc (sizeof(char*) * max_addr);
	addresses[0] = NULL;

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* Set signal handling and alarm */
	if (signal (SIGALRM, popen_timeout_alarm_handler) == SIG_ERR) {
		usage4 (_("Cannot catch SIGALRM"));
	}

	/* handle timeouts gracefully */
	alarm (timeout_interval);

	for (i = 0 ; i < n_addresses ; i++) {
		
#ifdef PING6_COMMAND
		if (is_inet6_addr(addresses[i]) && address_family != AF_INET)
			rawcmd = strdup(PING6_COMMAND);
		else
			rawcmd = strdup(PING_COMMAND);
#else
		rawcmd = strdup(PING_COMMAND);
#endif

		/* does the host address of number of packets argument come first? */
#ifdef PING_PACKETS_FIRST
# ifdef PING_HAS_TIMEOUT
		asprintf (&cmd, rawcmd, timeout_interval, max_packets, addresses[i]);
# else
		asprintf (&cmd, rawcmd, max_packets, addresses[i]);
# endif
#else
		asprintf (&cmd, rawcmd, addresses[i], max_packets);
#endif

		if (verbose)
			printf ("%s ==> ", cmd);

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
		printf ("\n");

		if (verbose)
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
			usage2 (_("Unknown argument"), optarg);
		case 'h':	/* help */
			print_help ();
			exit (STATE_OK);
			break;
		case 'V':	/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
			break;
		case 't':	/* timeout period */
			timeout_interval = atoi (optarg);
			break;
		case 'v':	/* verbose mode */
			verbose = TRUE;
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

	return OK;
}



int
run_ping (const char *cmd, const char *addr)
{
	char buf[MAX_INPUT_BUFFER];
	int result = STATE_UNKNOWN;

	if ((child_process = spopen (cmd)) == NULL)
		die (STATE_UNKNOWN, _("Could not open pipe: %s\n"), cmd);

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL)
		printf (_("Cannot open stderr for %s\n"), cmd);

	while (fgets (buf, MAX_INPUT_BUFFER - 1, child_process)) {

		result = max_state (result, error_scan (buf, addr));

		/* get the percent loss statistics */
		if(sscanf(buf,"%*d packets transmitted, %*d packets received, +%*d errors, %d%% packet loss",&pl)==1 ||
			 sscanf(buf,"%*d packets transmitted, %*d packets received, +%*d duplicates, %d%% packet loss", &pl) == 1 ||
			 sscanf(buf,"%*d packets transmitted, %*d received, +%*d duplicates, %d%% packet loss", &pl) == 1 ||
			 sscanf(buf,"%*d packets transmitted, %*d packets received, %d%% packet loss",&pl)==1 ||
			 sscanf(buf,"%*d packets transmitted, %*d packets received, %d%% loss, time",&pl)==1 ||
			 sscanf(buf,"%*d packets transmitted, %*d received, %d%% loss, time", &pl)==1 ||
			 sscanf(buf,"%*d packets transmitted, %*d received, %d%% packet loss, time", &pl)==1 ||
			 sscanf(buf,"%*d packets transmitted, %*d received, +%*d errors, %d%% packet loss", &pl) == 1 ||
			 sscanf(buf,"%*d packets transmitted %*d received, +%*d errors, %d%% packet loss", &pl) == 1
			 )
			continue;

		/* get the round trip average */
		else
			if(sscanf(buf,"round-trip min/avg/max = %*f/%f/%*f",&rta)==1 ||
				 sscanf(buf,"round-trip min/avg/max/mdev = %*f/%f/%*f/%*f",&rta)==1 ||
				 sscanf(buf,"round-trip min/avg/max/sdev = %*f/%f/%*f/%*f",&rta)==1 ||
				 sscanf(buf,"round-trip min/avg/max/stddev = %*f/%f/%*f/%*f",&rta)==1 ||
				 sscanf(buf,"round-trip min/avg/max/std-dev = %*f/%f/%*f/%*f",&rta)==1 ||
				 sscanf(buf,"round-trip (ms) min/avg/max = %*f/%f/%*f",&rta)==1 ||
				 sscanf(buf,"round-trip (ms) min/avg/max/stddev = %*f/%f/%*f/%*f",&rta)==1 ||
				 sscanf(buf,"rtt min/avg/max/mdev = %*f/%f/%*f/%*f ms",&rta)==1)
			continue;
	}

	/* this is needed because there is no rta if all packets are lost */
	if (pl == 100)
		rta = crta;

	/* check stderr, setting at least WARNING if there is output here */
	while (fgets (buf, MAX_INPUT_BUFFER - 1, child_stderr))
		if (! strstr(buf,"WARNING - no SO_TIMESTAMP support, falling back to SIOCGSTAMP"))
			result = max_state (STATE_WARNING, error_scan (buf, addr));

	(void) fclose (child_stderr);


	/* close the pipe - WARNING if status is set */
	if (spclose (child_process))
		result = max_state (result, STATE_WARNING);

	if (warn_text == NULL)
		warn_text = strdup("");

	return result;
}



int
error_scan (char buf[MAX_INPUT_BUFFER], const char *addr)
{
	if (strstr (buf, "Network is unreachable"))
		die (STATE_CRITICAL, _("CRITICAL - Network unreachable (%s)"), addr);
	else if (strstr (buf, "Destination Host Unreachable"))
		die (STATE_CRITICAL, _("CRITICAL - Host Unreachable (%s)"), addr);
	else if (strstr (buf, "unknown host" ))
		die (STATE_CRITICAL, _("CRITICAL - Host not found (%s)"), addr);
	else if (strstr (buf, "Time to live exceeded"))
		die (STATE_CRITICAL, _("CRITICAL - Time to live exceeded (%s)"), addr);

	if (strstr (buf, "(DUP!)") || strstr (buf, "DUPLICATES FOUND")) {
		if (warn_text == NULL)
			warn_text = strdup (_(WARN_DUPLICATES));
		else if (! strstr (warn_text, _(WARN_DUPLICATES)) &&
		         asprintf (&warn_text, "%s %s", warn_text, _(WARN_DUPLICATES)) == -1)
			die (STATE_UNKNOWN, _("Unable to realloc warn_text"));
		return (STATE_WARNING);
	}

	return (STATE_OK);
}



void
print_help (void)
{
	print_revision (progname, revision);

	printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>");
	printf (COPYRIGHT, copyright, email);

	printf (_("Use ping to check connection statistics for a remote host.\n\n"));

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_(UT_IPv46));

	printf (_("\
-H, --hostname=HOST\n\
   host to ping\n\
-w, --warning=THRESHOLD\n\
   warning threshold pair\n\
-c, --critical=THRESHOLD\n\
   critical threshold pair\n\
-p, --packets=INTEGER\n\
   number of ICMP ECHO packets to send (Default: %d)\n\
-L, --link\n\
   show HTML in the plugin output (obsoleted by urlize)\n"),
	        DEFAULT_MAX_PACKETS);

	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);

	printf (_("\
THRESHOLD is <rta>,<pl>%% where <rta> is the round trip average travel\n\
time (ms) which triggers a WARNING or CRITICAL state, and <pl> is the\n\
percentage of packet loss to trigger an alarm state.\n\n"));

	printf (_("\
This plugin uses the ping command to probe the specified host for packet loss\n\
(percentage) and round trip average (milliseconds). It can produce HTML output\n\
linking to a traceroute CGI contributed by Ian Cass. The CGI can be found in\n\
the contrib area of the downloads section at http://www.nagios.org\n\n"));

	printf (_(UT_SUPPORT));
}

void
print_usage (void)
{
	printf ("Usage: %s -H <host_address> -w <wrta>,<wpl>%% -c <crta>,<cpl>%%\n\
                     [-p packets] [-t timeout] [-L] [-4|-6]\n", progname);
}
