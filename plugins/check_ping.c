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

#define PROGNAME "check_ping"
#define REVISION "$Revision$"
#define COPYRIGHT "1999-2001"
#define AUTHOR "Ethan Galstad/Karl DeBisschop"
#define EMAIL "kdebisschop@users.sourceforge.net"
#define SUMMARY "Use ping to check connection statistics for a remote host.\n"

#define OPTIONS "\
-H <host_address> -w <wrta>,<wpl>%% -c <crta>,<cpl>%%\n\
       [-p packets] [-t timeout] [-L]\n"

#define LONGOPTIONS "\
-H, --hostname=HOST\n\
   host to ping\n\
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
#include "popen.h"
#include "utils.h"

#define UNKNOWN_PACKET_LOSS 200	/* 200% */
#define UNKNOWN_TRIP_TIME -1.0	/* -1 seconds */
#define DEFAULT_MAX_PACKETS 5		/* default no. of ICMP ECHO packets */

#define WARN_DUPLICATES "DUPLICATES FOUND! "

int process_arguments (int, char **);
int call_getopt (int, char **);
int get_threshold (char *, float *, int *);
int validate_arguments (void);
int run_ping (char *);
void print_usage (void);
void print_help (void);

int display_html = FALSE;
int wpl = UNKNOWN_PACKET_LOSS;
int cpl = UNKNOWN_PACKET_LOSS;
float wrta = UNKNOWN_TRIP_TIME;
float crta = UNKNOWN_TRIP_TIME;
char *server_address = NULL;
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

	if (process_arguments (argc, argv) == ERROR)
		usage ("Could not parse arguments");

	/* does the host address of number of packets argument come first? */
#ifdef PING_PACKETS_FIRST
	command_line =
		ssprintf (command_line, PING_COMMAND, max_packets, server_address);
#else
	command_line =
		ssprintf (command_line, PING_COMMAND, server_address, max_packets);
#endif

	/* Set signal handling and alarm */
	if (signal (SIGALRM, popen_timeout_alarm_handler) == SIG_ERR) {
		printf ("Cannot catch SIGALRM");
		return STATE_UNKNOWN;
	}

	/* handle timeouts gracefully */
	alarm (timeout_interval);

	if (verbose)
		printf ("%s ==> ", command_line);

	/* run the command */
	run_ping (command_line);

	if (pl == UNKNOWN_PACKET_LOSS || rta == UNKNOWN_TRIP_TIME) {
		printf ("%s\n", command_line);
		terminate (STATE_UNKNOWN,
							 "Error: Could not interpret output from ping command\n");
	}

	if (pl >= cpl || rta >= crta || rta < 0)
		result = STATE_CRITICAL;
	else if (pl >= wpl || rta >= wrta)
		result = STATE_WARNING;
	else if (pl < wpl && rta < wrta && pl >= 0 && rta >= 0)
		/* cannot use the max function because STATE_UNKNOWN is now 3 gt STATE_OK			
		result = max (result, STATE_OK);  */
		if( !( (result == STATE_WARNING) || (result == STATE_CRITICAL) )  ) {
			result = STATE_OK;	
		}
	
	if (display_html == TRUE)
		printf ("<A HREF='%s/traceroute.cgi?%s'>", CGIURL, server_address);
	if (pl == 100)
		printf ("PING %s - %sPacket loss = %d%%", state_text (result), warn_text,
						pl);
	else
		printf ("PING %s - %sPacket loss = %d%%, RTA = %2.2f ms",
						state_text (result), warn_text, pl, rta);
	if (display_html == TRUE)
		printf ("</A>");
	printf ("\n");

	if (verbose)
		printf ("%f:%d%% %f:%d%%\n", wrta, wpl, crta, cpl);

	return result;
}


/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
		if (strcmp ("-nohtml", argv[c]) == 0)
			strcpy (argv[c], "-n");
	}

	c = 0;
	while ((c += call_getopt (argc - c, &argv[c])) < argc) {

		if (is_option (argv[c]))
			continue;

		if (server_address == NULL) {
			if (is_host (argv[c]) == FALSE) {
				printf ("Invalid host name/address: %s\n\n", argv[c]);
				return ERROR;
			}
			server_address = argv[c];
		}
		else if (wpl == UNKNOWN_PACKET_LOSS) {
			if (is_intpercent (argv[c]) == FALSE) {
				printf ("<wpl> (%s) must be an integer percentage\n", argv[c]);
				return ERROR;
			}
			wpl = atoi (argv[c]);
		}
		else if (cpl == UNKNOWN_PACKET_LOSS) {
			if (is_intpercent (argv[c]) == FALSE) {
				printf ("<cpl> (%s) must be an integer percentage\n", argv[c]);
				return ERROR;
			}
			cpl = atoi (argv[c]);
		}
		else if (wrta == UNKNOWN_TRIP_TIME) {
			if (is_negative (argv[c])) {
				printf ("<wrta> (%s) must be a non-negative number\n", argv[c]);
				return ERROR;
			}
			wrta = atof (argv[c]);
		}
		else if (crta == UNKNOWN_TRIP_TIME) {
			if (is_negative (argv[c])) {
				printf ("<crta> (%s) must be a non-negative number\n", argv[c]);
				return ERROR;
			}
			crta = atof (argv[c]);
		}
		else if (max_packets == -1) {
			if (is_intnonneg (argv[c])) {
				max_packets = atoi (argv[c]);
			}
			else {
				printf ("<max_packets> (%s) must be a non-negative number\n",
								argv[c]);
				return ERROR;
			}
		}

	}

	return validate_arguments ();
}

int
call_getopt (int argc, char **argv)
{
	int c, i = 0;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"verbose", no_argument, 0, 'v'},
		{"nohtml", no_argument, 0, 'n'},
		{"link", no_argument, 0, 'L'},
		{"timeout", required_argument, 0, 't'},
		{"critical", required_argument, 0, 'c'},
		{"warning", required_argument, 0, 'w'},
		{"hostname", required_argument, 0, 'H'},
		{"packets", required_argument, 0, 'p'},
		{0, 0, 0, 0}
	};
#endif

	while (1) {
#ifdef HAVE_GETOPT_H
		c =
			getopt_long (argc, argv, "+hVvt:c:w:H:p:nL", long_options,
									 &option_index);
#else
		c = getopt (argc, argv, "+hVvt:c:w:H:p:nL");
#endif

		i++;

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case 't':
		case 'c':
		case 'w':
		case 'H':
		case 'p':
			i++;
		}

		switch (c) {
		case '?':									/* print short usage statement if args not parsable */
			usage2 ("Unknown argument", optarg);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (PROGNAME, REVISION);
			exit (STATE_OK);
		case 't':									/* timeout period */
			timeout_interval = atoi (optarg);
			break;
		case 'v':									/* verbose mode */
			verbose = TRUE;
			break;
		case 'H':									/* hostname */
			if (is_host (optarg) == FALSE)
				usage2 ("Invalid host name/address", optarg);
			server_address = optarg;
			break;
		case 'p':									/* number of packets to send */
			if (is_intnonneg (optarg))
				max_packets = atoi (optarg);
			else
				usage2 ("<max_packets> (%s) must be a non-negative number\n", optarg);
			break;
		case 'n':									/* no HTML */
			display_html = FALSE;
			break;
		case 'L':									/* show HTML */
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

	return i;
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

	return OK;
}


int
run_ping (char *command_line)
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

		if (strstr (input_buffer, "DUPLICATES FOUND"))
			/* cannot use the max function since STATE_UNKNOWN is max
			result = max (result, STATE_WARNING); */
			if( !(result == STATE_CRITICAL) ){
				result = STATE_WARNING;
			}
		else
			/* cannot use the max function since STATE_UNKNOWN is max
			result = max (result, STATE_CRITICAL); */
			result = STATE_CRITICAL ;
	}
	(void) fclose (child_stderr);


	/* close the pipe - WARNING if status is set */
	if (spclose (child_process))
		result = max (result, STATE_WARNING);

	return result;
}


void
print_usage (void)
{
	printf ("Usage:\n" " %s %s\n"
#ifdef HAVE_GETOPT_H
					" %s (-h | --help) for detailed help\n"
					" %s (-V | --version) for version information\n",
#else
					" %s -h for detailed help\n"
					" %s -V for version information\n",
#endif
					PROGNAME, OPTIONS, PROGNAME, PROGNAME);
}

void
print_help (void)
{
	print_revision (PROGNAME, REVISION);
	printf
		("Copyright (c) %s %s <%s>\n\n%s\n",
		 COPYRIGHT, AUTHOR, EMAIL, SUMMARY);
	print_usage ();
	printf
		("\nOptions:\n" LONGOPTIONS "\n" DESCRIPTION "\n", 
		 DEFAULT_MAX_PACKETS, DEFAULT_SOCKET_TIMEOUT);
	support ();
}
