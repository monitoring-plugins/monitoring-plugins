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

const char *progname = "check_radius";
const char *revision = "$Revision$";
const char *copyright = "2000-2003";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "utils.h"
#include "netutils.h"
#include <radiusclient.h>

int process_arguments (int, char **);
void print_help (void);
void print_usage (void);

char *server = NULL;
char *username = NULL;
char *password = NULL;
char *nasid = NULL;
char *expect = NULL;
char *config_file = NULL;
unsigned short port = PW_AUTH_UDP_PORT;
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
<refname>&progname;</refname>
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
<para>Todo List</para>
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
	SEND_DATA data;
	int result = STATE_UNKNOWN;
	UINT4 client_id;
	char *str;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	if (process_arguments (argc, argv) != TRUE)
		usage4 (_("Could not parse arguments"));

	str = strdup ("dictionary");
	if ((config_file && rc_read_config (config_file)) ||
			rc_read_dictionary (rc_conf_str (str)))
		die (STATE_UNKNOWN, _("Config file error"));

	service = PW_AUTHENTICATE_ONLY;

	if (!(rc_avpair_add (&data.send_pairs, PW_SERVICE_TYPE, &service, 0) &&
				rc_avpair_add (&data.send_pairs, PW_USER_NAME, username, 0) &&
				rc_avpair_add (&data.send_pairs, PW_USER_PASSWORD, password, 0) &&
				(nasid==NULL || rc_avpair_add (&data.send_pairs, PW_NAS_IDENTIFIER, nasid, 0))))
		die (STATE_UNKNOWN, _("Out of Memory?"));

	/* 
	 * Fill in NAS-IP-Address 
	 */

	if ((client_id = rc_own_ipaddress ()) == 0)
		return (ERROR_RC);

	if (rc_avpair_add (&(data.send_pairs), PW_NAS_IP_ADDRESS, &client_id, 0) ==
			NULL) return (ERROR_RC);

	rc_buildreq (&data, PW_ACCESS_REQUEST, server, port, (int)timeout_interval,
	             retries);

	result = rc_send_server (&data, msg);
	rc_avpair_free (data.send_pairs);
	if (data.receive_pairs)
		rc_avpair_free (data.receive_pairs);

	if (result == TIMEOUT_RC)
		die (STATE_CRITICAL, _("Timeout"));
	if (result == ERROR_RC)
		die (STATE_CRITICAL, _("Auth Error"));
	if (result == BADRESP_RC)
		die (STATE_WARNING, _("Auth Failed"));
	if (expect && !strstr (msg, expect))
		die (STATE_WARNING, "%s", msg);
	if (result == OK_RC)
		die (STATE_OK, _("Auth OK"));
	return (0);
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option = 0;
	static struct option longopts[] = {
		{"hostname", required_argument, 0, 'H'},
		{"port", required_argument, 0, 'P'},
		{"username", required_argument, 0, 'u'},
		{"password", required_argument, 0, 'p'},
		{"nas-id", required_argument, 0, 'n'},
		{"filename", required_argument, 0, 'F'},
		{"expect", required_argument, 0, 'e'},
		{"retries", required_argument, 0, 'r'},
		{"timeout", required_argument, 0, 't'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	if (argc == 9) {
		config_file = argv[1];
		username = argv[2];
		password = argv[3];
		if (is_intpos (argv[4]))
			timeout_interval = atoi (argv[4]);
		else
			usage2 (_("Timeout interval must be a positive integer"), optarg);
		if (is_intpos (argv[5]))
			retries = atoi (argv[5]);
		else
			usage (_("Number of retries must be a positive integer"));
		server = argv[6];
		if (is_intpos (argv[7]))
			port = atoi (argv[7]);
		else
			usage (_("Port must be a positive integer"));
		expect = argv[8];
		return OK;
	}

	while (1) {
		c = getopt_long (argc, argv, "+hVvH:P:F:u:p:n:t:r:e:", longopts,
									 &option);

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case '?':									/* print short usage statement if args not parsable */
			printf (_("%s: Unknown argument: %s\n\n"), progname, optarg);
			print_usage ();
			exit (STATE_UNKNOWN);
		case 'h':									/* help */
			print_help ();
			exit (OK);
		case 'V':									/* version */
			print_revision (progname, revision);
			exit (OK);
		case 'v':									/* verbose mode */
			verbose = TRUE;
			break;
		case 'H':									/* hostname */
			if (is_host (optarg) == FALSE) {
				usage2 (_("Invalid hostname/address"), optarg);
			}
			server = optarg;
			break;
		case 'P':									/* port */
			if (is_intnonneg (optarg))
				port = atoi (optarg);
			else
				usage (_("Port must be a positive integer"));
			break;
		case 'u':									/* username */
			username = optarg;
			break;
		case 'p':									/* password */
			password = optarg;
			break;
		case 'n':									/* nas id */
			nasid = optarg;
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
				usage (_("Number of retries must be a positive integer"));
			break;
		case 't':									/* timeout */
			if (is_intpos (optarg))
				timeout_interval = atoi (optarg);
			else
				usage2 (_("Timeout interval must be a positive integer"), optarg);
			break;
		}
	}
	return OK;
}



void
print_help (void)
{
	char *myport;
	asprintf (&myport, "%d", PW_AUTH_UDP_PORT);

	print_revision (progname, revision);

	printf ("Copyright (c) 1999 Robert August Vincent II\n");
	printf (COPYRIGHT, copyright, email);

	printf(_("Tests to see if a radius server is accepting connections.\n\n"));

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_(UT_HOST_PORT), 'P', myport);

	printf (_("\
 -u, --username=STRING\n\
    The user to authenticate\n\
 -p, --password=STRING\n\
    Password for autentication (SECURITY RISK)\n\
 -n, --nas-id=STRING\n\
    NAS identifier\n\
 -F, --filename=STRING\n\
    Configuration file\n\
 -e, --expect=STRING\n\
    Response string to expect from the server\n\
 -r, --retries=INTEGER\n\
    Number of times to retry a failed connection\n"));

	printf (_(UT_TIMEOUT), timeout_interval);

	printf (_("\n\
This plugin tests a radius server to see if it is accepting connections.\n\
\n\
The server to test must be specified in the invocation, as well as a user\n\
name and password. A configuration file may also be present. The format of\n\
the configuration file is described in the radiusclient library sources.\n\n"));

	printf (_("\
The password option presents a substantial security issue because the\n\
password can be determined by careful watching of the command line in\n\
a process listing.  This risk is exacerbated because nagios will\n\
run the plugin at regular prdictable intervals.  Please be sure that\n\
the password used does not allow access to sensitive system resources,\n\
otherwise compormise could occur.\n"));

	printf (_(UT_SUPPORT));
}



void
print_usage (void)
{
	printf ("\
Usage: %s -H host -F config_file -u username -p password [-n nas-id] [-P port]\n\
          [-t timeout] [-r retries] [-e expect]\n", progname);
}
