/*****************************************************************************
*
* CHECK_PING.C
*
* Program: Ping plugin for Nagios
* License: GPL
* Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
*
* $Id$
*
*****************************************************************************/

const char *progname = "check_ping";
#define REVISION "$Revision$"
#define COPYRIGHT "1999-2001"
#define AUTHOR "Ethan Galstad/Karl DeBisschop"
#define EMAIL "kdebisschop@users.sourceforge.net"
#define SUMMARY "Use ping to check connection statistics for a remote host.\n"

#define OPTIONS "\
-H <host_address> -w <wrta>,<wpl>%% -c <crta>,<cpl>%%\n\
       [-p packets] [-t timeout] [-L] [-4] [-6]\n"

#define LONGOPTIONS "\
-H, --hostname=HOST\n\
   host to ping\n\
-4, --use-ipv4\n\
   Use IPv4 ICMP PING\n\
-6, --use-ipv6\n\
   Use IPv6 ICMP PING\n\
-w, --warning=THRESHOLD\n\
   warning threshold pair\n\
-c, --critical=THRESHOLD\n\
   critical threshold pair\n\
-p, --packets=INTEGER\n\
   number of ICMP ECHO packets to send (Default: %d)\n\
-t, --timeout=INTEGER\n\
   optional specified timeout in second (Default: %d)\n\
-L, --link\n\
   show HTML in the plugin output (obsoleted by urlize)\n\
THRESHOLD is <rta>,<pl>%% where <rta> is the round trip average travel\n\
time (ms) which triggers a WARNING or CRITICAL state, and <pl> is the\n\
percentage of packet loss to trigger an alarm state.\n"

#define DESCRIPTION "\
This plugin uses the ping command to probe the specified host for packet loss\n\
(percentage) and round trip average (milliseconds). It can produce HTML output\n\
linking to a traceroute CGI contributed by Ian Cass. The CGI can be found in\n\
the contrib area of the downloads section at http://www.nagios.org\n\n"

#include "config.h"
#include "common.h"
#include "netutils.h"
#include "popen.h"
#include "utils.h"

#define UNKNOWN_PACKET_LOSS 200	/* 200% */
#define UNKNOWN_TRIP_TIME -1.0	/* -1 seconds */
#define DEFAULT_MAX_PACKETS 5		/* default no. of ICMP ECHO packets */

#define WARN_DUPLICATES "DUPLICATES FOUND! "

int process_arguments (int, char **);
int get_threshold (char *, float *, int *);
int validate_arguments (void);
int run_ping (char *, char *);
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

char *warn_text = NULL;

int
main (int argc, char **argv)
{
	char *command_line = NULL;
	int result = STATE_UNKNOWN;
	int this_result = STATE_UNKNOWN;
	int i;

	addresses = malloc (max_addr);

	if (process_arguments (argc, argv) == ERROR)
		usage ("Could not parse arguments");
	exit;

	/* Set signal handling and alarm */
	if (signal (SIGALRM, popen_timeout_alarm_handler) == SIG_ERR) {
		printf ("Cannot catch SIGALRM");
		return STATE_UNKNOWN;
	}

	/* handle timeouts gracefully */
	alarm (timeout_interval);

	for (i = 0 ; i < n_addresses ; i++) {

		/* does the host address of number of packets argument come first? */
#ifdef PING6_COMMAND
# ifdef PING_PACKETS_FIRST
	if (is_inet6_addr(addresses[i]) && address_family != AF_INET)
		asprintf (&command_line, PING6_COMMAND, max_packets, addresses[i]);
	else
		asprintf (&command_line, PING_COMMAND, max_packets, addresses[i]);
# else
	if (is_inet6_addr(addresses[i]) && address_family != AF_INET) 
		asprintf (&command_line, PING6_COMMAND, addresses[i], max_packets);
	else
		asprintf (&command_line, PING_COMMAND, addresses[i], max_packets);
# endif
#else /* USE_IPV6 */
# ifdef PING_PACKETS_FIRST
		asprintf (&command_line, PING_COMMAND, max_packets, addresses[i]);
# else
		asprintf (&command_line, PING_COMMAND, addresses[i], max_packets);
# endif
#endif /* USE_IPV6 */

		if (verbose)
			printf ("%s ==> ", command_line);

		/* run the command */
		this_result = run_ping (command_line, addresses[i]);

		if (pl == UNKNOWN_PACKET_LOSS || rta == UNKNOWN_TRIP_TIME) {
			printf ("%s\n", command_line);
			terminate (STATE_UNKNOWN,
								 "Error: Could not interpret output from ping command\n");
		}

		if (pl >= cpl || rta >= crta || rta < 0)
			this_result = STATE_CRITICAL;
		else if (pl >= wpl || rta >= wrta)
			this_result = STATE_WARNING;
		else if (pl >= 0 && rta >= 0)
			this_result = max_state (STATE_OK, this_result);	
	
		if (n_addresses > 1 && this_result != STATE_UNKNOWN)
			terminate (STATE_OK, "%s is alive\n", addresses[i]);

		if (display_html == TRUE)
			printf ("<A HREF='%s/traceroute.cgi?%s'>", CGIURL, addresses[i]);
		if (pl == 100)
			printf ("PING %s - %sPacket loss = %d%%", state_text (this_result), warn_text,
							pl);
		else
			printf ("PING %s - %sPacket loss = %d%%, RTA = %2.2f ms",
							state_text (this_result), warn_text, pl, rta);
		if (display_html == TRUE)
			printf ("</A>");
		printf ("\n");

		if (verbose)
			printf ("%f:%d%% %f:%d%%\n", wrta, wpl, crta, cpl);

		result = max_state (result, this_result);

	}

	return result;
}


/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c = 1;
	char *ptr;

	int option_index = 0;
	static struct option long_options[] = {
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
		c = getopt_long (argc, argv, "VvhnL46t:c:w:H:p:", long_options, &option_index);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':	/* usage */
			usage3 ("Unknown argument", optopt);
		case 'h':	/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':	/* version */
			print_revision (progname, REVISION);
			exit (STATE_OK);
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
			address_family = AF_INET6;
			break;
		case 'H':	/* hostname */
			ptr=optarg;
			while (1) {
				n_addresses++;
				if (n_addresses > max_addr) {
					max_addr *= 2;
					addresses = realloc (addresses, max_addr);
					if (addresses == NULL)
						terminate (STATE_UNKNOWN, "Could not realloc() addresses\n");
				}
				addresses[n_addresses-1] = ptr;
				if (ptr = index (ptr, ',')) {
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
				usage2 ("<max_packets> (%s) must be a non-negative number\n", optarg);
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
			printf ("Invalid host name/address: %s\n\n", argv[c]);
			return ERROR;
		} else {
			addresses[0] = argv[c++];
			if (c == argc)
				return validate_arguments ();
		}
	}

	if (wpl == UNKNOWN_PACKET_LOSS) {
		if (is_intpercent (argv[c]) == FALSE) {
			printf ("<wpl> (%s) must be an integer percentage\n", argv[c]);
			return ERROR;
		} else {
			wpl = atoi (argv[c++]);
			if (c == argc)
				return validate_arguments ();
		}
	}

	if (cpl == UNKNOWN_PACKET_LOSS) {
		if (is_intpercent (argv[c]) == FALSE) {
			printf ("<cpl> (%s) must be an integer percentage\n", argv[c]);
			return ERROR;
		} else {
			cpl = atoi (argv[c++]);
			if (c == argc)
				return validate_arguments ();
		}
	}

	if (wrta == UNKNOWN_TRIP_TIME) {
		if (is_negative (argv[c])) {
			printf ("<wrta> (%s) must be a non-negative number\n", argv[c]);
			return ERROR;
		} else {
			wrta = atof (argv[c++]);
			if (c == argc)
				return validate_arguments ();
		}
	}

	if (crta == UNKNOWN_TRIP_TIME) {
		if (is_negative (argv[c])) {
			printf ("<crta> (%s) must be a non-negative number\n", argv[c]);
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
		}	else {
			printf ("<max_packets> (%s) must be a non-negative number\n", argv[c]);
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
	else
		usage2 ("%s: Warning threshold must be integer or percentage!\n\n", arg);
}

int
validate_arguments ()
{
	float max_seconds;
	int i;

	if (wrta == UNKNOWN_TRIP_TIME) {
		printf ("<wrta> was not set\n");
		return ERROR;
	}
	else if (crta == UNKNOWN_TRIP_TIME) {
		printf ("<crta> was not set\n");
		return ERROR;
	}
	else if (wpl == UNKNOWN_PACKET_LOSS) {
		printf ("<wpl> was not set\n");
		return ERROR;
	}
	else if (cpl == UNKNOWN_PACKET_LOSS) {
		printf ("<cpl> was not set\n");
		return ERROR;
	}
	else if (wrta > crta) {
		printf ("<wrta> (%f) cannot be larger than <crta> (%f)\n", wrta, crta);
		return ERROR;
	}
	else if (wpl > cpl) {
		printf ("<wpl> (%d) cannot be larger than <cpl> (%d)\n", wpl, cpl);
		return ERROR;
	}

	if (max_packets == -1)
		max_packets = DEFAULT_MAX_PACKETS;

	max_seconds = crta / 1000.0 * max_packets + max_packets;
	if (max_seconds > timeout_interval)
		timeout_interval = (int)max_seconds;

	for (i=0; i<n_addresses; i++) {
		if (is_host(addresses[i]) == FALSE)
			usage2 ("Invalid host name/address", addresses[i]);
	}

	return OK;
}


int
run_ping (char *command_line, char *server_address)
{
	char input_buffer[MAX_INPUT_BUFFER];
	int result = STATE_UNKNOWN;

	warn_text = malloc (1);
	if (warn_text == NULL)
		terminate (STATE_UNKNOWN, "unable to malloc warn_text");
	warn_text[0] = 0;

	if ((child_process = spopen (command_line)) == NULL) {
		printf ("Cannot open pipe: ");
		terminate (STATE_UNKNOWN, command_line);
	}
	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL)
		printf ("Cannot open stderr for %s\n", command_line);

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {

		if (strstr (input_buffer, "(DUP!)")) {
			/* cannot use the max function since STATE_UNKNOWN is max
			result = max (result, STATE_WARNING); */
			if( !(result == STATE_CRITICAL) ){
				result = STATE_WARNING;
			}
			
			warn_text = realloc (warn_text, strlen (WARN_DUPLICATES) + 1);
			if (warn_text == NULL)
				terminate (STATE_UNKNOWN, "unable to realloc warn_text");
			strcpy (warn_text, WARN_DUPLICATES);
		}

		/* get the percent loss statistics */
		if (sscanf
					(input_buffer, "%*d packets transmitted, %*d packets received, +%*d errors, %d%% packet loss",
						 &pl) == 1
				|| sscanf 
					(input_buffer, "%*d packets transmitted, %*d packets received, %d%% packet loss",
						&pl) == 1
				|| sscanf 
					(input_buffer, "%*d packets transmitted, %*d packets received, %d%% loss, time", &pl) == 1
				|| sscanf
					(input_buffer, "%*d packets transmitted, %*d received, %d%% loss, time", &pl) == 1
					/* Suse 8.0 as reported by Richard * Brodie */
				)
			continue;

		/* get the round trip average */
		else
			if (sscanf (input_buffer, "round-trip min/avg/max = %*f/%f/%*f", &rta)
					== 1
					|| sscanf (input_buffer,
										 "round-trip min/avg/max/mdev = %*f/%f/%*f/%*f",
										 &rta) == 1
					|| sscanf (input_buffer,
										 "round-trip min/avg/max/sdev = %*f/%f/%*f/%*f",
										 &rta) == 1
					|| sscanf (input_buffer,
										 "round-trip min/avg/max/stddev = %*f/%f/%*f/%*f",
										 &rta) == 1
					|| sscanf (input_buffer,
										 "round-trip min/avg/max/std-dev = %*f/%f/%*f/%*f",
										 &rta) == 1
					|| sscanf (input_buffer, "round-trip (ms) min/avg/max = %*f/%f/%*f",
										 &rta) == 1
					|| sscanf (input_buffer, "rtt min/avg/max/mdev = %*f/%f/%*f/%*f ms",
										 &rta) == 1
										)
			continue;
	}

	/* this is needed because there is no rta if all packets are lost */
	if (pl == 100)
		rta = crta;


	/* check stderr */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr)) {
		if (strstr
				(input_buffer,
				 "Warning: no SO_TIMESTAMP support, falling back to SIOCGSTAMP"))
				continue;

		if (strstr (input_buffer, "Network is unreachable"))
			terminate (STATE_CRITICAL, "PING CRITICAL - Network unreachable (%s)",
								 server_address);
		else if (strstr (input_buffer, "Destination Host Unreachable"))
			terminate (STATE_CRITICAL, "PING CRITICAL - Host Unreachable (%s)",
								 server_address);
		else if (strstr (input_buffer, "unknown host" ) )
			terminate (STATE_CRITICAL, "PING CRITICAL - Host not found (%s)",
								server_address);

		warn_text =
			realloc (warn_text, strlen (warn_text) + strlen (input_buffer) + 2);
		if (warn_text == NULL)
			terminate (STATE_UNKNOWN, "unable to realloc warn_text");
		if (strlen (warn_text) == 0)
			strcpy (warn_text, input_buffer);
		else
			sprintf (warn_text, "%s %s", warn_text, input_buffer);

		if (strstr (input_buffer, "DUPLICATES FOUND")) {
			if( !(result == STATE_CRITICAL) ){
				result = STATE_WARNING;
			}
		}
		else
			result = STATE_CRITICAL ;
	}
	(void) fclose (child_stderr);


	/* close the pipe - WARNING if status is set */
	if (spclose (child_process))
		result = max_state (result, STATE_WARNING);

	return result;
}


void
print_usage (void)
{
	printf ("Usage:\n" " %s %s\n"
					" %s (-h | --help) for detailed help\n"
					" %s (-V | --version) for version information\n",
					progname, OPTIONS, progname, progname);
}

void
print_help (void)
{
	print_revision (progname, REVISION);
	printf
		("Copyright (c) %s %s <%s>\n\n%s\n",
		 COPYRIGHT, AUTHOR, EMAIL, SUMMARY);
	print_usage ();
	printf
		("\nOptions:\n" LONGOPTIONS "\n" DESCRIPTION "\n", 
		 DEFAULT_MAX_PACKETS, DEFAULT_SOCKET_TIMEOUT);
	support ();
}
