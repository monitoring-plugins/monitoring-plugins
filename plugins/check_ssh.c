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

#include "common.h"
#include "netutils.h"
#include "utils.h"

const char *progname = "check_ssh";
const char *revision = "$Revision$";
const char *copyright = "2000-2003";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif

#define SSH_DFL_PORT    22
#define BUFF_SZ         256

int port = -1;
char *server_name = NULL;
int verbose = FALSE;

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

int ssh_connect (char *haddr, int hport);

int
main (int argc, char **argv)
{
	int result;

	if (process_arguments (argc, argv) == ERROR)
		usage (_("Could not parse arguments\n"));

	/* initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);
	alarm (socket_timeout);

	/* ssh_connect exits if error is found */
	result = ssh_connect (server_name, port);

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
		{"host", required_argument, 0, 'H'},
		{"port", required_argument, 0, 'p'},
		{"use-ipv4", no_argument, 0, '4'},
		{"use-ipv6", no_argument, 0, '6'},
		{"timeout", required_argument, 0, 't'},
		{"verbose", no_argument, 0, 'v'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++)
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");

	while (1) {
		c = getopt_long (argc, argv, "+Vhv46t:H:p:", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':									/* help */
			usage ("");
		case 'V':									/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'v':									/* verose */
			verbose = TRUE;
			break;
		case 't':									/* timeout period */
			if (!is_integer (optarg))
				usage (_("Timeout Interval must be an integer!\n\n"));
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
			usage (_("IPv6 support not available\n"));
#endif
			break;
		case 'H':									/* host */
			if (is_host (optarg) == FALSE)
				usage ("Invalid hostname/address\n");
			server_name = optarg;
			break;
		case 'p':									/* port */
			if (is_intpos (optarg)) {
				port = atoi (optarg);
			}
			else {
				printf ("Port number nust be a positive integer: %s\n", optarg);
				usage ("");
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
ssh_connect (char *haddr, int hport)
{
	int sd;
	int result;
	char *output = NULL;
	char *buffer = NULL;
	char *ssh_proto = NULL;
	char *ssh_server = NULL;
	char rev_no[20];

	sscanf ("$Revision$", "$Revision: %[0123456789.]", rev_no);

	result = my_tcp_connect (haddr, hport, &sd);

	if (result != STATE_OK)
		return result;

	output = (char *) malloc (BUFF_SZ + 1);
	memset (output, 0, BUFF_SZ + 1);
	recv (sd, output, BUFF_SZ, 0);
	if (strncmp (output, "SSH", 3)) {
		printf (_("Server answer: %s"), output);
		exit (STATE_CRITICAL);
	}
	else {
		strip (output);
		if (verbose)
			printf ("%s\n", output);
		ssh_proto = output + 4;
		ssh_server = ssh_proto + strspn (ssh_proto, "-0123456789. ");
		ssh_proto[strspn (ssh_proto, "0123456789. ")] = 0;
		printf
			(_("SSH OK - %s (protocol %s)\n"),
			 ssh_server, ssh_proto);
		asprintf (&buffer, "SSH-%s-check_ssh_%s\r\n", ssh_proto, rev_no);
		send (sd, buffer, strlen (buffer), MSG_DONTWAIT);
		if (verbose)
			printf ("%s\n", buffer);
		exit (STATE_OK);
	}
}

void
print_help (void)
{
	char *myport;
	asprintf (&myport, "%d", SSH_DFL_PORT);

	print_revision (progname, revision);

	printf (_("Copyright (c) 1999 Remi Paulmier <remi@sinfomic.fr>\n"));
	printf (_(COPYRIGHT), copyright, email);

	printf (_("Try to connect to SSH server at specified server and port\n\n"));

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_(UT_HOST_PORT), 'p', myport);

	printf (_(UT_IPv46));

	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);

	printf (_(UT_VERBOSE));

	printf (_(UT_SUPPORT));
}

void
print_usage (void)
{
	printf (_("\
Usage: %s [-46] [-t <timeout>] [-p <port>] <host>\n"), progname);
	printf (_(UT_HLP_VRS), progname, progname);
}

/* end of check_ssh.c */
