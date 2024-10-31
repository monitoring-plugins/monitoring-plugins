/*****************************************************************************
*
* Monitoring check_ssh plugin
*
* License: GPL
* Copyright (c) 2000-2007 Monitoring Plugins Development Team
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
const char *email = "devel@monitoring-plugins.org";

#include "./common.h"
#include "./netutils.h"
#include "utils.h"

#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif

#define SSH_DFL_PORT    22
#define BUFF_SZ         256

int port = -1;
char *server_name = NULL;
char *remote_version = NULL;
char *remote_protocol = NULL;
bool verbose = false;

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

int ssh_connect (char *haddr, int hport, char *remote_version, char *remote_protocol);


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
	result = ssh_connect (server_name, port, remote_version, remote_protocol);

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
		{"remote-protocol", required_argument, 0, 'P'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++)
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");

	while (1) {
		c = getopt_long (argc, argv, "+Vhv46t:r:H:p:P:", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':									/* help */
			usage5 ();
		case 'V':									/* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_UNKNOWN);
		case 'h':									/* help */
			print_help ();
			exit (STATE_UNKNOWN);
		case 'v':									/* verbose */
			verbose = true;
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
		case 'P':									/* remote version */
			remote_protocol = optarg;
			break;
		case 'H':									/* host */
			if (!is_host (optarg))
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
ssh_connect (char *haddr, int hport, char *remote_version, char *remote_protocol)
{
	int sd;
	int result;
	int len = 0;
	ssize_t recv_ret = 0;
	char *version_control_string = NULL;
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

	char *output = (char *) calloc (BUFF_SZ + 1, sizeof(char));

	unsigned int iteration = 0;
	ssize_t byte_offset = 0;

	while ((version_control_string == NULL) && (recv_ret = recv(sd, output+byte_offset, BUFF_SZ - byte_offset, 0) > 0)) {

		if (strchr(output, '\n')) { /* we've got at least one full line, start parsing*/
			byte_offset = 0;

			char *index = NULL;
			while ((index = strchr(output+byte_offset, '\n')) != NULL) {
				/*Partition the buffer so that this line is a separate string,
				 * by replacing the newline with NUL*/
				output[(index - output)] = '\0';
				len = strlen(output + byte_offset);

				if ((len >= 4) && (strncmp (output+byte_offset, "SSH-", 4) == 0)) {
					/*if the string starts with SSH-, this _should_ be a valid version control string*/
						version_control_string = output+byte_offset;
						break;
				}

				/*the start of the next line (if one exists) will be after the current one (+ NUL)*/
				byte_offset += (len + 1);
			}

			if(version_control_string == NULL) {
				/* move unconsumed data to beginning of buffer, null rest */
				memmove((void *)output, (void *)output+byte_offset+1, BUFF_SZ - len+1);
				memset(output+byte_offset+1, 0, BUFF_SZ-byte_offset+1);

				/*start reading from end of current line chunk on next recv*/
				byte_offset = strlen(output);
			}
		} else {
			byte_offset += recv_ret;
		}
	}

	if (recv_ret < 0) {
		printf("SSH CRITICAL - %s", strerror(errno));
		exit(STATE_CRITICAL);
	}

	if (version_control_string == NULL) {
		printf("SSH CRITICAL - No version control string received");
		exit(STATE_CRITICAL);
	}
	/*
	 * "When the connection has been established, both sides MUST send an
	 * identification string.  This identification string MUST be
	 *
	 * SSH-protoversion-softwareversion SP comments CR LF"
	 *		- RFC 4253:4.2
	 */
	strip (version_control_string);
	if (verbose)
		printf ("%s\n", version_control_string);
	ssh_proto = version_control_string + 4;

	/*
	 * We assume the protoversion is of the form Major.Minor, although
	 * this is not _strictly_ required. See
	 *
	 * "Both the 'protoversion' and 'softwareversion' strings MUST consist of
	 * printable US-ASCII characters, with the exception of whitespace
	 * characters and the minus sign (-)"
	 *		- RFC 4253:4.2
	 * and,
	 *
	 * "As stated earlier, the 'protoversion' specified for this protocol is
	 * "2.0".  Earlier versions of this protocol have not been formally
	 * documented, but it is widely known that they use 'protoversion' of
	 * "1.x" (e.g., "1.5" or "1.3")."
	 *		- RFC 4253:5
	 */
	ssh_server = ssh_proto + strspn (ssh_proto, "0123456789.") + 1; /* (+1 for the '-' separating protoversion from softwareversion) */

	/* If there's a space in the version string, whatever's after the space is a comment
	 * (which is NOT part of the server name/version)*/
	char *tmp = strchr(ssh_server, ' ');
	if (tmp) {
		ssh_server[tmp - ssh_server] = '\0';
	}
	if (strlen(ssh_proto) == 0 || strlen(ssh_server) == 0) {
		printf(_("SSH CRITICAL - Invalid protocol version control string %s\n"), version_control_string);
		exit (STATE_CRITICAL);
	}
	ssh_proto[strspn (ssh_proto, "0123456789. ")] = 0;

	xasprintf (&buffer, "SSH-%s-check_ssh_%s\r\n", ssh_proto, rev_no);
	send (sd, buffer, strlen (buffer), MSG_DONTWAIT);
	if (verbose)
		printf ("%s\n", buffer);

	if (remote_version && strcmp(remote_version, ssh_server)) {
		printf
			(_("SSH CRITICAL - %s (protocol %s) version mismatch, expected '%s'\n"),
			 ssh_server, ssh_proto, remote_version);
		close(sd);
		exit (STATE_CRITICAL);
	}

	if (remote_protocol && strcmp(remote_protocol, ssh_proto)) {
		printf
			(_("SSH CRITICAL - %s (protocol %s) protocol version mismatch, expected '%s' | %s\n"),
			 ssh_server, ssh_proto, remote_protocol, fperfdata("time", elapsed_time, "s",
			 false, 0, false, 0, true, 0, true, (int)socket_timeout));
		close(sd);
		exit (STATE_CRITICAL);
	}
	elapsed_time = (double)deltime(tv) / 1.0e6;

	printf
		(_("SSH OK - %s (protocol %s) | %s\n"),
		 ssh_server, ssh_proto, fperfdata("time", elapsed_time, "s",
			 false, 0, false, 0, true, 0, true, (int)socket_timeout));
	close(sd);
	exit (STATE_OK);
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

	printf (UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf (" %s\n", "-r, --remote-version=STRING");
	printf ("    %s\n", _("Alert if string doesn't match expected server version (ex: OpenSSH_3.9p1)"));

	printf (" %s\n", "-P, --remote-protocol=STRING");
	printf ("    %s\n", _("Alert if protocol doesn't match expected protocol version (ex: 2.0)"));

	printf (UT_VERBOSE);

	printf (UT_SUPPORT);
}



void
print_usage (void)
{
	printf ("%s\n", _("Usage:"));
	printf ("%s  [-4|-6] [-t <timeout>] [-r <remote version>] [-p <port>] <host>\n", progname);
}

