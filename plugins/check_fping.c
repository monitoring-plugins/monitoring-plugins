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

******************************************************************************/

const char *progname = "check_fping";
const char *revision = "$Revision$";
const char *copyright = "2000-2003";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "popen.h"
#include "netutils.h"
#include "utils.h"

enum {
	PACKET_COUNT = 1,
	PACKET_SIZE = 56,
	PL = 0,
	RTA = 1
};

int textscan (char *buf);
int process_arguments (int, char **);
int get_threshold (char *arg, char *rv[2]);
void print_help (void);
void print_usage (void);

char *server_name = NULL;
int packet_size = PACKET_SIZE;
int packet_count = PACKET_COUNT;
int verbose = FALSE;
int cpl;
int wpl;
double crta;
double wrta;
int cpl_p = FALSE;
int wpl_p = FALSE;
int crta_p = FALSE;
int wrta_p = FALSE;

int
main (int argc, char **argv)
{
	int status = STATE_UNKNOWN;
	char *server = NULL;
	char *command_line = NULL;
	char *input_buffer = NULL;
	input_buffer = malloc (MAX_INPUT_BUFFER);

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

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
		printf (_("Could not open pipe: %s\n"), command_line);
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
		die (STATE_CRITICAL, _("FPING UNKNOW - %s not found\n"), server_name);

	}
	else if (strstr (buf, "is unreachable") || strstr (buf, "Unreachable")) {
		die (STATE_CRITICAL, _("FPING CRITICAL - %s is unreachable\n"),
							 "host");

	}
	else if (strstr (buf, "is down")) {
		die (STATE_CRITICAL, _("FPING CRITICAL - %s is down\n"), server_name);

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
		if (cpl_p == TRUE && loss > cpl)
			status = STATE_CRITICAL;
		else if (crta_p == TRUE  && rta > crta)
			status = STATE_CRITICAL;
		else if (wpl_p == TRUE && loss > wpl)
			status = STATE_WARNING;
		else if (wrta_p == TRUE && rta > wrta)
			status = STATE_WARNING;
		else
			status = STATE_OK;
		die (status,
		      _("FPING %s - %s (loss=%.0f%%, rta=%f ms)|%s %s\n"),
				 state_text (status), server_name, loss, rta,
		     perfdata ("loss", (long int)loss, "%", wpl_p, wpl, cpl_p, cpl, TRUE, 0, TRUE, 100),
		     fperfdata ("rta", rta/1.0e3, "s", wrta_p, wrta/1.0e3, crta_p, crta/1.0e3, TRUE, 0, FALSE, 0));

	}
	else if(strstr (buf, "xmt/rcv/%loss") ) {
		/* no min/max/avg if host was unreachable in fping v2.2.b1 */
		losstr = strstr (buf, "=");
		losstr = 1 + strstr (losstr, "/");
		losstr = 1 + strstr (losstr, "/");
		loss = strtod (losstr, NULL);
		if (atoi(losstr) == 100)
			status = STATE_CRITICAL;
		else if (cpl_p == TRUE && loss > cpl)
			status = STATE_CRITICAL;
		else if (wpl_p == TRUE && loss > wpl)
			status = STATE_WARNING;
		else
			status = STATE_OK;
		/* loss=%.0f%%;%d;%d;0;100 */
		die (status, _("FPING %s - %s (loss=%.0f%% )|%s\n"),
		     state_text (status), server_name, loss ,
		     perfdata ("loss", (long int)loss, "%", wpl_p, wpl, cpl_p, cpl, TRUE, 0, TRUE, 100));
	
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

	int option = 0;
	static struct option longopts[] = {
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
		c = getopt_long (argc, argv, "+hVvH:c:w:b:n:", longopts, &option);

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
				usage2 (_("Invalid hostname/address"), optarg);
			}
			server_name = strscpy (server_name, optarg);
			break;
		case 'c':
			get_threshold (optarg, rv);
			if (rv[RTA]) {
				crta = strtod (rv[RTA], NULL);
				crta_p = TRUE;
				rv[RTA] = NULL;
			}
			if (rv[PL]) {
				cpl = atoi (rv[PL]);
				cpl_p = TRUE;
				rv[PL] = NULL;
			}
			break;
		case 'w':
			get_threshold (optarg, rv);
			if (rv[RTA]) {
				wrta = strtod (rv[RTA], NULL);
				wrta_p = TRUE;
				rv[RTA] = NULL;
			}
			if (rv[PL]) {
				wpl = atoi (rv[PL]);
				wpl_p = TRUE;
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
		usage (_("Hostname was not supplied\n\n"));

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
			die (STATE_UNKNOWN,
								 _("%s: Only one threshold may be packet loss (%s)\n"), progname,
								 arg);
		if (!strstr (arg1, "%") && !strstr (arg2, "%"))
			die (STATE_UNKNOWN,
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





void
print_help (void)
{

	print_revision (progname, "$Revision$");

	printf ("Copyright (c) 1999 Didi Rieder <adrieder@sbox.tu-graz.ac.at>\n");
	printf (COPYRIGHT, copyright, email);

	printf (_("\
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

	printf (_(UT_SUPPORT));
}




void
print_usage (void)
{
	printf (_("Usage: %s <host_address>\n"), progname);
	printf (_(UT_HLP_VRS), progname, progname);
}
