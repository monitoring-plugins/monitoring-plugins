/******************************************************************************
 *
 * Program: radius server check plugin for Nagios
 * License: GPL
 *
 * License Information:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id$
 *
 *****************************************************************************/

#define PROGNAME "check_radius"
#define REVISION "$Revision$"
#define COPYRIGHT "1999-2001"
#define AUTHORS "Robert August Vincent II/Karl DeBisschop"
#define EMAIL "kdebisschop@users.sourceforge.net"
#define SUMMARY "Tests to see if a radius server is accepting connections.\n"

#define OPTIONS "\
-H host -F config_file -u username -p password\'\
              [-P port] [-t timeout] [-r retries] [-e expect]"

#define LONGOPTIONS "\
 -H, --hostname=HOST\n\
    Host name argument for servers using host headers (use numeric\n\
    address if possible to bypass DNS lookup).\n\
 -P, --port=INTEGER\n\
    Port number (default: %d)\n\
 -u, --username=STRING\n\
    The user to authenticate\n\
 -p, --password=STRING\n\
    Password for autentication (SECURITY RISK)\n\
 -F, --filename=STRING\n\
    Configuration file\n\
 -e, --expect=STRING\n\
    Response string to expect from the server\n\
 -r, --retries=INTEGER\n\
    Number of times to retry a failed connection\n\
 -t, --timeout=INTEGER\n\
    Seconds before connection times out (default: %d)\n\
 -v\n\
    Show details for command-line debugging (do not use with nagios server)\n\
 -h, --help\n\
    Print detailed help screen\n\
 -V, --version\n\
    Print version information\n"

#define DESCRIPTION "\
The password option presents a substantial security issue because the 
password can be determined by careful watching of the command line in
a process listing.  This risk is exacerbated because nagios will
run the plugin at regular prdictable intervals.  Please be sure that
the password used does not allow access to sensitive system resources,
otherwise compormise could occur.\n"

#include "config.h"
#include "common.h"
#include "utils.h"
#include <radiusclient.h>

int process_arguments (int, char **);
void print_usage (void);
void print_help (void);

char *server = NULL;
int port = PW_AUTH_UDP_PORT;
char *username = NULL;
char *password = NULL;
char *expect = NULL;
char *config_file = NULL;
int retries = 1;
int verbose = FALSE;

ENV *env = NULL;

/******************************************************************************

The (psuedo?)literate programming XML is contained within \@\@\- <XML> \-\@\@
tags in the comments. With in the tags, the XML is assembled sequentially.
You can define entities in tags. You also have all the #defines available as
entities.

Please note that all tags must be lowercase to use the DocBook XML DTD.

@@-<article>

<sect1>
<title>Quick Reference</title>
<!-- The refentry forms a manpage -->
<refentry>
<refmeta>
<manvolnum>5<manvolnum>
</refmeta>
<refnamdiv>
<refname>&PROGNAME;</refname>
<refpurpose>&SUMMARY;</refpurpose>
</refnamdiv>
</refentry>
</sect1>

<sect1>
<title>FAQ</title>
</sect1>

<sect1>
<title>Theory, Installation, and Operation</title>

<sect2>
<title>General Description</title>
<para>
&DESCRIPTION;
</para>
</sect2>

<sect2>
<title>Future Enhancements</title>
<para>ToDo List</para>
<itemizedlist>
<listitem>Add option to get password from a secured file rather than the command line</listitem>
</itemizedlist>
</sect2>


<sect2>
<title>Functions</title>
-@@
******************************************************************************/

int
main (int argc, char **argv)
{
	UINT4 service;
	char msg[BUFFER_LEN];
	SEND_DATA data = { 0 };
	int result;
	UINT4 client_id;

	if (process_arguments (argc, argv) == ERROR)
		usage ("Could not parse arguments\n");

	if ((config_file && rc_read_config (config_file)) ||
			rc_read_dictionary (rc_conf_str ("dictionary")))
		terminate (STATE_UNKNOWN, "Config file error");

	service = PW_AUTHENTICATE_ONLY;

	if (!(rc_avpair_add (&data.send_pairs, PW_SERVICE_TYPE, &service, 0) &&
				rc_avpair_add (&data.send_pairs, PW_USER_NAME, username, 0) &&
				rc_avpair_add (&data.send_pairs, PW_USER_PASSWORD, password, 0)))
		terminate (STATE_UNKNOWN, "Out of Memory?");

	/* 
	 * Fill in NAS-IP-Address 
	 */

	if ((client_id = rc_own_ipaddress ()) == 0)
		return (ERROR_RC);

	if (rc_avpair_add (&(data.send_pairs), PW_NAS_IP_ADDRESS, &client_id, 0) ==
			NULL) return (ERROR_RC);

	rc_buildreq (&data, PW_ACCESS_REQUEST, server, port, timeout_interval,
							 retries);

	result = rc_send_server (&data, msg);
	rc_avpair_free (data.send_pairs);
	if (data.receive_pairs)
		rc_avpair_free (data.receive_pairs);

	if (result == TIMEOUT_RC)
		terminate (STATE_CRITICAL, "Timeout");
	if (result == ERROR_RC)
		terminate (STATE_CRITICAL, "Auth Error");
	if (result == BADRESP_RC)
		terminate (STATE_WARNING, "Auth Failed");
	if (expect && !strstr (msg, expect))
		terminate (STATE_WARNING, msg);
	if (result == OK_RC)
		terminate (STATE_OK, "Auth OK");
	return (0);
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] = {
		{"hostname", required_argument, 0, 'H'},
		{"port", required_argument, 0, 'P'},
		{"username", required_argument, 0, 'u'},
		{"password", required_argument, 0, 'p'},
		{"filename", required_argument, 0, 'F'},
		{"expect", required_argument, 0, 'e'},
		{"retries", required_argument, 0, 'r'},
		{"timeout", required_argument, 0, 't'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};
#endif

	if (argc < 2)
		return ERROR;

	if (argc == 9) {
		config_file = argv[1];
		username = argv[2];
		password = argv[3];
		if (is_intpos (argv[4]))
			timeout_interval = atoi (argv[4]);
		else
			usage ("Timeout interval must be a positive integer");
		if (is_intpos (argv[5]))
			retries = atoi (argv[5]);
		else
			usage ("Number of retries must be a positive integer");
		server = argv[6];
		if (is_intpos (argv[7]))
			port = atoi (argv[7]);
		else
			usage ("Server port must be a positive integer");
		expect = argv[8];
		return OK;
	}

	while (1) {
#ifdef HAVE_GETOPT_H
		c =
			getopt_long (argc, argv, "+hVvH:P:F:u:p:t:r:e:", long_options,
									 &option_index);
#else
		c = getopt (argc, argv, "+hVvH:P:F:u:p:t:r:e:");
#endif

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case '?':									/* print short usage statement if args not parsable */
			printf ("%s: Unknown argument: %s\n\n", my_basename (argv[0]), optarg);
			print_usage ();
			exit (STATE_UNKNOWN);
		case 'h':									/* help */
			print_help ();
			exit (OK);
		case 'V':									/* version */
			print_revision (my_basename (argv[0]), "$Revision$");
			exit (OK);
		case 'v':									/* verbose mode */
			verbose = TRUE;
			break;
		case 'H':									/* hostname */
			if (is_host (optarg) == FALSE) {
				printf ("Invalid host name/address\n\n");
				print_usage ();
				exit (STATE_UNKNOWN);
			}
			server = optarg;
			break;
		case 'P':									/* port */
			if (is_intnonneg (optarg))
				port = atoi (optarg);
			else
				usage ("Server port must be a positive integer");
			break;
		case 'u':									/* username */
			username = optarg;
			break;
		case 'p':									/* password */
			password = optarg;
			break;
		case 'F':									/* configuration file */
			config_file = optarg;
			break;
		case 'e':									/* expect */
			expect = optarg;
			break;
		case 'r':									/* retries */
			if (is_intpos (optarg))
				retries = atoi (optarg);
			else
				usage ("Number of retries must be a positive integer");
			break;
		case 't':									/* timeout */
			if (is_intpos (optarg))
				timeout_interval = atoi (optarg);
			else
				usage ("Timeout interval must be a positive integer");
			break;
		}
	}
	return OK;
}



void
print_help (void)
{
	print_revision (PROGNAME, REVISION);
	printf
		("Copyright (c) %s %s <%s>\n\n%s\n",
		 COPYRIGHT, AUTHORS, EMAIL, SUMMARY);
	print_usage ();
	printf
		("\nOptions:\n" LONGOPTIONS "\n" DESCRIPTION "\n", 
		 port, timeout_interval);
	support ();
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
