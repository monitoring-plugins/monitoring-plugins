/*
* check_ssh.c
* 
* Made by (Remi PAULMIER)
* Login   <remi@sinfomic.fr>
* 
* Started on  Fri Jul  9 09:18:23 1999 Remi PAULMIER
* Update Thu Jul 22 12:50:04 1999 remi paulmier
* $Id$
*
*/

#include "config.h"
#include "common.h"
#include "netutils.h"
#include "utils.h"

const char *progname = "check_ssh";
#define REVISION "$Revision$"

#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif

#define SSH_DFL_PORT    22
#define BUFF_SZ         256

short port = -1;
char *server_name = NULL;
int verbose = FALSE;

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

int ssh_connect (char *haddr, short hport);

int
main (int argc, char **argv)
{
	int result;

	if (process_arguments (argc, argv) == ERROR)
		usage ("Could not parse arguments\n");

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
	char *tmp = NULL;

	int option_index = 0;
	static struct option long_options[] = {
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{"verbose", no_argument, 0, 'v'},
		{"timeout", required_argument, 0, 't'},
		{"host", required_argument, 0, 'H'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++)
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");

	while (1) {
		c = getopt_long (argc, argv, "+Vhvt:H:p:", long_options, &option_index);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':									/* help */
			usage ("");
		case 'V':									/* version */
			print_revision (progname, REVISION);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'v':									/* verose */
			verbose = TRUE;
			break;
		case 't':									/* timeout period */
			if (!is_integer (optarg))
				usage ("Timeout Interval must be an integer!\n\n");
			socket_timeout = atoi (optarg);
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
ssh_connect (char *haddr, short hport)
{
	int sd;
	int result;
	char *output = NULL;
	char *buffer = NULL;
	char *ssh_proto = NULL;
	char *ssh_server = NULL;
	char revision[20];

	sscanf ("$Revision$", "$Revision: %[0123456789.]", revision);

	result = my_tcp_connect (haddr, hport, &sd);

	if (result != STATE_OK)
		return result;

	output = (char *) malloc (BUFF_SZ + 1);
	memset (output, 0, BUFF_SZ + 1);
	recv (sd, output, BUFF_SZ, 0);
	if (strncmp (output, "SSH", 3)) {
		printf ("Server answer: %s", output);
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
			("SSH OK - %s (protocol %s)\n",
			 ssh_server, ssh_proto);
		asprintf (&buffer, "SSH-%s-check_ssh_%s\r\n", ssh_proto, revision);
		send (sd, buffer, strlen (buffer), MSG_DONTWAIT);
		if (verbose)
			printf ("%s\n", buffer);
		exit (STATE_OK);
	}
}

void
print_help (void)
{
	print_revision (progname, REVISION);
	printf ("Copyright (c) 1999 Remi Paulmier (remi@sinfomic.fr)\n\n");
	print_usage ();
	printf ("by default, port is %d\n", SSH_DFL_PORT);
}

void
print_usage (void)
{
	printf
		("Usage:\n"
		 " %s -t [timeout] -p [port] <host>\n"
		 " %s -V prints version info\n"
		 " %s -h prints more detailed help\n", progname, progname, progname);
}

/* end of check_ssh.c */
