/******************************************************************************
*
* CHECK_FPING.C
*
* Program: Fping plugin for Nagios
* License: GPL
* Copyright (c) 1999 Didi Rieder (adrieder@sbox.tu-graz.ac.at)
* $Id$
*
* Modifications:
*
* 08-24-1999 Didi Rieder (adrieder@sbox.tu-graz.ac.at)
*            Intial Coding
* 09-11-1999 Karl DeBisschop (kdebiss@alum.mit.edu)
*            Change to spopen
*            Fix so that state unknown is returned by default
*            (formerly would give state ok if no fping specified)
*            Add server_name to output
*            Reformat to 80-character standard screen
* 11-18-1999 Karl DeBisschop (kdebiss@alum.mit.edu)
*            set STATE_WARNING of stderr written or nonzero status returned
*
* Description:
*
* This plugin will use the /bin/fping command (from saint) to ping
* the specified host for a fast check if the host is alive. Note that
* it is necessary to set the suid flag on fping.
******************************************************************************/

const char *progname = "check_fping";
const char *revision = "$Revision$";
const char *copyright = "1999-2003";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "popen.h"
#include "utils.h"

#define PACKET_COUNT 1
#define PACKET_SIZE 56
#define UNKNOWN_PACKET_LOSS 200	/* 200% */
#define UNKNOWN_TRIP_TIME -1.0	/* -1 seconds */

#define PL 0
#define RTA 1

void
print_usage (void)
{
	printf (_("Usage: %s <host_address>\n"), progname);
}

void
print_help (void)
{

	print_revision (progname, "$Revision$");

	printf (_("\
Copyright (c) 1999 Didi Rieder (adrieder@sbox.tu-graz.ac.at)\n\n\
This plugin will use the /bin/fping command (from saint) to ping the\n\
specified host for a fast check if the host is alive. Note that it is\n\
necessary to set the suid flag on fping.\n\n"));

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_("\
 -H, --hostname=HOST\n\
    Name or IP Address of host to ping (IP Address bypasses name lookup,\n\
    reducing system load)\n\
 -w, --warning=THRESHOLD\n\
    warning threshold pair\n\
 -c, --critical=THRESHOLD\n\
    critical threshold pair\n\
 -b, --bytes=INTEGER\n\
    Size of ICMP packet (default: %d)\n\
 -n, --number=INTEGER\n\
    Number of ICMP packets to send (default: %d)\n"),
	        PACKET_SIZE, PACKET_COUNT);

	printf (_(UT_VERBOSE));

	printf (_("\n\
THRESHOLD is <rta>,<pl>%% where <rta> is the round trip average travel\n\
time (ms) which triggers a WARNING or CRITICAL state, and <pl> is the\n\
percentage of packet loss to trigger an alarm state.\n"));

}

int textscan (char *buf);
int process_arguments (int, char **);
int get_threshold (char *arg, char *rv[2]);

char *server_name = NULL;
int cpl = UNKNOWN_PACKET_LOSS;
int wpl = UNKNOWN_PACKET_LOSS;
double crta = UNKNOWN_TRIP_TIME;
double wrta = UNKNOWN_TRIP_TIME;
int packet_size = PACKET_SIZE;
int packet_count = PACKET_COUNT;
int verbose = FALSE;

int
main (int argc, char **argv)
{
	int status = STATE_UNKNOWN;
	char *server = NULL;
	char *command_line = NULL;
	char *input_buffer = NULL;
	input_buffer = malloc (MAX_INPUT_BUFFER);

	if (process_arguments (argc, argv) == ERROR)
		usage (_("Could not parse arguments\n"));

	server = strscpy (server, server_name);

	/* compose the command */
	asprintf (&command_line, "%s -b %d -c %d %s", PATH_TO_FPING,
	          packet_size, packet_count, server);

	if (verbose)
		printf ("%s\n", command_line);

	/* run the command */
	child_process = spopen (command_line);
	if (child_process == NULL) {
		printf (_("Unable to open pipe: %s\n"), command_line);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL) {
		printf (_("Could not open stderr for %s\n"), command_line);
	}

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {
		if (verbose)
			printf ("%s", input_buffer);
		status = max_state (status, textscan (input_buffer));
	}

	/* If we get anything on STDERR, at least set warning */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr)) {
		status = max_state (status, STATE_WARNING);
		if (verbose)
			printf ("%s", input_buffer);
		status = max_state (status, textscan (input_buffer));
	}
	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process))
		/* need to use max_state not max */
		status = max_state (status, STATE_WARNING);

	printf ("FPING %s - %s\n", state_text (status), server_name);

	return status;
}




int
textscan (char *buf)
{
	char *rtastr = NULL;
	char *losstr = NULL;
	double loss;
	double rta;
	int status = STATE_UNKNOWN;

	if (strstr (buf, "not found")) {
		terminate (STATE_CRITICAL, _("FPING unknown - %s not found\n"), server_name);

	}
	else if (strstr (buf, "is unreachable") || strstr (buf, "Unreachable")) {
		terminate (STATE_CRITICAL, _("FPING critical - %s is unreachable\n"),
							 "host");

	}
	else if (strstr (buf, "is down")) {
		terminate (STATE_CRITICAL, _("FPING critical - %s is down\n"), server_name);

	}
	else if (strstr (buf, "is alive")) {
		status = STATE_OK;

	}
	else if (strstr (buf, "xmt/rcv/%loss") && strstr (buf, "min/avg/max")) {
		losstr = strstr (buf, "=");
		losstr = 1 + strstr (losstr, "/");
		losstr = 1 + strstr (losstr, "/");
		rtastr = strstr (buf, "min/avg/max");
		rtastr = strstr (rtastr, "=");
		rtastr = 1 + index (rtastr, '/');
		loss = strtod (losstr, NULL);
		rta = strtod (rtastr, NULL);
		if (cpl != UNKNOWN_PACKET_LOSS && loss > cpl)
			status = STATE_CRITICAL;
		else if (crta != UNKNOWN_TRIP_TIME && rta > crta)
			status = STATE_CRITICAL;
		else if (wpl != UNKNOWN_PACKET_LOSS && loss > wpl)
			status = STATE_WARNING;
		else if (wrta != UNKNOWN_TRIP_TIME && rta > wrta)
			status = STATE_WARNING;
		else
			status = STATE_OK;
		terminate (status, _("FPING %s - %s (loss=%f%%, rta=%f ms)\n"),
							 state_text (status), server_name, loss, rta);

	}
	else if(strstr (buf, "xmt/rcv/%loss") ) {
		/* no min/max/avg if host was unreachable in fping v2.2.b1 */
		losstr = strstr (buf, "=");
		losstr = 1 + strstr (losstr, "/");
		losstr = 1 + strstr (losstr, "/");
		loss = strtod (losstr, NULL);
		if (loss == 100)
			status = STATE_CRITICAL;
		else if (cpl != UNKNOWN_PACKET_LOSS && loss > cpl)
			status = STATE_CRITICAL;
		else if (wpl != UNKNOWN_PACKET_LOSS && loss > wpl)
			status = STATE_WARNING;
		else
			status = STATE_OK;
		
		terminate (status, _("FPING %s - %s (loss=%f%% )\n"),
							 state_text (status), server_name, loss );		
	
	}
	else {
		status = max_state (status, STATE_WARNING);
	}

	return status;
}




/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;
	char *rv[2];

	int option_index = 0;
	static struct option long_options[] = {
		{"hostname", required_argument, 0, 'H'},
		{"critical", required_argument, 0, 'c'},
		{"warning", required_argument, 0, 'w'},
		{"bytes", required_argument, 0, 'b'},
		{"number", required_argument, 0, 'n'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	rv[PL] = NULL;
	rv[RTA] = NULL;

	if (argc < 2)
		return ERROR;

	if (!is_option (argv[1])) {
		server_name = argv[1];
		argv[1] = argv[0];
		argv = &argv[1];
		argc--;
	}

	while (1) {
		c = getopt_long (argc, argv, "+hVvH:c:w:b:n:", long_options, &option_index);

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case '?':									/* print short usage statement if args not parsable */
			printf (_("%s: Unknown argument: %s\n\n"), progname, optarg);
			print_usage ();
			exit (STATE_UNKNOWN);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 'v':									/* verbose mode */
			verbose = TRUE;
			break;
		case 'H':									/* hostname */
			if (is_host (optarg) == FALSE) {
				printf (_("Invalid host name/address\n\n"));
				print_usage ();
				exit (STATE_UNKNOWN);
			}
			server_name = strscpy (server_name, optarg);
			break;
		case 'c':
			get_threshold (optarg, rv);
			if (rv[RTA]) {
				crta = strtod (rv[RTA], NULL);
				rv[RTA] = NULL;
			}
			if (rv[PL]) {
				cpl = atoi (rv[PL]);
				rv[PL] = NULL;
			}
			break;
		case 'w':
			get_threshold (optarg, rv);
			if (rv[RTA]) {
				wrta = strtod (rv[RTA], NULL);
				rv[RTA] = NULL;
			}
			if (rv[PL]) {
				wpl = atoi (rv[PL]);
				rv[PL] = NULL;
			}
			break;
		case 'b':									/* bytes per packet */
			if (is_intpos (optarg))
				packet_size = atoi (optarg);
			else
				usage (_("Packet size must be a positive integer"));
			break;
		case 'n':									/* number of packets */
			if (is_intpos (optarg))
				packet_count = atoi (optarg);
			else
				usage (_("Packet count must be a positive integer"));
			break;
		}
	}


	if (server_name == NULL)
		usage (_("Host name was not supplied\n\n"));

	return OK;
}





int
get_threshold (char *arg, char *rv[2])
{
	char *arg1 = NULL;
	char *arg2 = NULL;

	arg1 = strscpy (arg1, arg);
	if (strpbrk (arg1, ",:"))
		arg2 = 1 + strpbrk (arg1, ",:");

	if (arg2) {
		arg1[strcspn (arg1, ",:")] = 0;
		if (strstr (arg1, "%") && strstr (arg2, "%"))
			terminate (STATE_UNKNOWN,
								 _("%s: Only one threshold may be packet loss (%s)\n"), progname,
								 arg);
		if (!strstr (arg1, "%") && !strstr (arg2, "%"))
			terminate (STATE_UNKNOWN,
								 _("%s: Only one threshold must be packet loss (%s)\n"),
								 progname, arg);
	}

	if (arg2 && strstr (arg2, "%")) {
		rv[PL] = arg2;
		rv[RTA] = arg1;
	}
	else if (arg2) {
		rv[PL] = arg1;
		rv[RTA] = arg2;
	}
	else if (strstr (arg1, "%")) {
		rv[PL] = arg1;
	}
	else {
		rv[RTA] = arg1;
	}

	return OK;
}
