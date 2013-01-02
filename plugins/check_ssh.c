/*****************************************************************************
* 
* Nagios check_ssh plugin
* 
* License: GPL
* Copyright (c) 2000-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_ssh plugin
* 
* Try to connect to an SSH server at specified server and port
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

const char *progname = "check_ssh";
const char *copyright = "2000-2007";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"

#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif

#define SSH_DFL_PORT    22
#define BUFF_SZ         256

int port = -1;
char *server_name = NULL;
char *remote_version = NULL;
int verbose = FALSE;

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

int ssh_connect (char *haddr, int hport, char *remote_version);



int
main (int argc, char **argv)
{
	int result = STATE_UNKNOWN;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);

	alarm (socket_timeout);

	/* ssh_connect exits if error is found */
	result = ssh_connect (server_name, port, remote_version);

	alarm (0);

	return (result);
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option = 0;
	static struct option longopts[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"host", required_argument, 0, 'H'},	/* backward compatibility */
		{"hostname", required_argument, 0, 'H'},
		{"port", required_argument, 0, 'p'},
		{"use-ipv4", no_argument, 0, '4'},
		{"use-ipv6", no_argument, 0, '6'},
		{"timeout", required_argument, 0, 't'},
		{"verbose", no_argument, 0, 'v'},
		{"remote-version", required_argument, 0, 'r'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++)
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");

	while (1) {
		c = getopt_long (argc, argv, "+Vhv46t:r:H:p:", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':									/* help */
			usage5 ();
		case 'V':									/* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'v':									/* verbose */
			verbose = TRUE;
			break;
		case 't':									/* timeout period */
			if (!is_integer (optarg))
				usage2 (_("Timeout interval must be a positive integer"), optarg);
			else
				socket_timeout = atoi (optarg);
			break;
		case '4':
			address_family = AF_INET;
			break;
		case '6':
#ifdef USE_IPV6
			address_family = AF_INET6;
#else
			usage4 (_("IPv6 support not available"));
#endif
			break;
		case 'r':									/* remote version */
			remote_version = optarg;
			break;
		case 'H':									/* host */
			if (is_host (optarg) == FALSE)
				usage2 (_("Invalid hostname/address"), optarg);
			server_name = optarg;
			break;
		case 'p':									/* port */
			if (is_intpos (optarg)) {
				port = atoi (optarg);
			}
			else {
				usage2 (_("Port number must be a positive integer"), optarg);
			}
		}
	}

	c = optind;
	if (server_name == NULL && c < argc) {
		if (is_host (argv[c])) {
			server_name = argv[c++];
		}
	}

	if (port == -1 && c < argc) {
		if (is_intpos (argv[c])) {
			port = atoi (argv[c++]);
		}
		else {
			print_usage ();
			exit (STATE_UNKNOWN);
		}
	}

	return validate_arguments ();
}

int
validate_arguments (void)
{
	if (server_name == NULL)
		return ERROR;
	if (port == -1)								/* funky, but allows -p to override stray integer in args */
		port = SSH_DFL_PORT;
	return OK;
}


/************************************************************************
*
* Try to connect to SSH server at specified server and port
*
*-----------------------------------------------------------------------*/


int
ssh_connect (char *haddr, int hport, char *remote_version)
{
	int sd;
	int result;
	char *output = NULL;
	char *buffer = NULL;
	char *ssh_proto = NULL;
	char *ssh_server = NULL;
	static char *rev_no = VERSION;
	struct timeval tv;
	double elapsed_time;

	gettimeofday(&tv, NULL);

	result = my_tcp_connect (haddr, hport, &sd);

	if (result != STATE_OK)
		return result;

	output = (char *) malloc (BUFF_SZ + 1);
	memset (output, 0, BUFF_SZ + 1);
	recv (sd, output, BUFF_SZ, 0);
	if (strncmp (output, "SSH", 3)) {
		printf (_("Server answer: %s"), output);
		close(sd);
		exit (STATE_CRITICAL);
	}
	else {
		strip (output);
		if (verbose)
			printf ("%s\n", output);
		ssh_proto = output + 4;
		ssh_server = ssh_proto + strspn (ssh_proto, "-0123456789. ");
		ssh_proto[strspn (ssh_proto, "0123456789. ")] = 0;

		xasprintf (&buffer, "SSH-%s-check_ssh_%s\r\n", ssh_proto, rev_no);
		send (sd, buffer, strlen (buffer), MSG_DONTWAIT);
		if (verbose)
			printf ("%s\n", buffer);

		if (remote_version && strcmp(remote_version, ssh_server)) {
			printf
				(_("SSH WARNING - %s (protocol %s) version mismatch, expected '%s'\n"),
				 ssh_server, ssh_proto, remote_version);
			close(sd);
			exit (STATE_WARNING);
		}

		elapsed_time = (double)deltime(tv) / 1.0e6;

		printf
			(_("SSH OK - %s (protocol %s) | %s\n"),
			 ssh_server, ssh_proto, fperfdata("time", elapsed_time, "s",
			 FALSE, 0, FALSE, 0, TRUE, 0, TRUE, (int)socket_timeout));
		close(sd);
		exit (STATE_OK);
	}
}



void
print_help (void)
{
	char *myport;
	xasprintf (&myport, "%d", SSH_DFL_PORT);

	print_revision (progname, NP_VERSION);

	printf ("Copyright (c) 1999 Remi Paulmier <remi@sinfomic.fr>\n");
	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("Try to connect to an SSH server at specified server and port"));

  printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (UT_HOST_PORT, 'p', myport);

	printf (UT_IPv46);

	printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf (" %s\n", "-r, --remote-version=STRING");
  printf ("    %s\n", _("Warn if string doesn't match expected server version (ex: OpenSSH_3.9p1)"));

	printf (UT_VERBOSE);

	printf (UT_SUPPORT);
}



void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
	printf ("%s  [-4|-6] [-t <timeout>] [-r <remote version>] [-p <port>] <host>\n", progname);
}

