/*****************************************************************************
* 
* Nagios check_radius plugin
* 
* License: GPL
* Copyright (c) 1999-2008 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_radius plugin
* 
* Tests to see if a radius server is accepting connections.
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

const char *progname = "check_radius";
const char *copyright = "2000-2008";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "utils.h"
#include "netutils.h"

#ifdef HAVE_LIBRADIUSCLIENT_NG
#include <radiusclient-ng.h>
rc_handle *rch = NULL;
#else
#include <radiusclient.h>
#endif

int process_arguments (int, char **);
void print_help (void);
void print_usage (void);

/* libradiusclient(-ng) wrapper functions */
#ifdef HAVE_LIBRADIUSCLIENT_NG
#define my_rc_conf_str(a) rc_conf_str(rch,a)
#define my_rc_send_server(a,b) rc_send_server(rch,a,b)
#define my_rc_buildreq(a,b,c,d,e,f) rc_buildreq(rch,a,b,c,d,e,f)
#define my_rc_own_ipaddress() rc_own_ipaddress(rch)
#define my_rc_avpair_add(a,b,c,d) rc_avpair_add(rch,a,b,c,-1,d)
#define my_rc_read_dictionary(a) rc_read_dictionary(rch, a)
#else
#define my_rc_conf_str(a) rc_conf_str(a)
#define my_rc_send_server(a,b) rc_send_server(a, b)
#define my_rc_buildreq(a,b,c,d,e,f) rc_buildreq(a,b,c,d,e,f)
#define my_rc_own_ipaddress() rc_own_ipaddress()
#define my_rc_avpair_add(a,b,c,d) rc_avpair_add(a, b, c, d)
#define my_rc_read_dictionary(a) rc_read_dictionary(a)
#endif

/* REJECT_RC is only defined in some version of radiusclient. It has
 * been reported from radiusclient-ng 0.5.6 on FreeBSD 7.2-RELEASE */
#ifndef REJECT_RC
#define REJECT_RC BADRESP_RC
#endif

int my_rc_read_config(char *);

char *server = NULL;
char *username = NULL;
char *password = NULL;
char *nasid = NULL;
char *nasipaddress = NULL;
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

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	str = strdup ("dictionary");
	if ((config_file && my_rc_read_config (config_file)) ||
			my_rc_read_dictionary (my_rc_conf_str (str)))
		die (STATE_UNKNOWN, _("Config file error"));

	service = PW_AUTHENTICATE_ONLY;

	memset (&data, 0, sizeof(data));
	if (!(my_rc_avpair_add (&data.send_pairs, PW_SERVICE_TYPE, &service, 0) &&
				my_rc_avpair_add (&data.send_pairs, PW_USER_NAME, username, 0) &&
				my_rc_avpair_add (&data.send_pairs, PW_USER_PASSWORD, password, 0)
				))
		die (STATE_UNKNOWN, _("Out of Memory?"));

	if (nasid != NULL) {
		if (!(my_rc_avpair_add (&data.send_pairs, PW_NAS_IDENTIFIER, nasid, 0)))
			die (STATE_UNKNOWN, _("Invalid NAS-Identifier"));
	}

	if (nasipaddress != NULL) {
		if (rc_good_ipaddr (nasipaddress))
			die (STATE_UNKNOWN, _("Invalid NAS-IP-Address"));
		if ((client_id = rc_get_ipaddr(nasipaddress)) == 0)
			die (STATE_UNKNOWN, _("Invalid NAS-IP-Address"));
	} else {
		if ((client_id = my_rc_own_ipaddress ()) == 0)
			die (STATE_UNKNOWN, _("Can't find local IP for NAS-IP-Address"));
	}
	if (my_rc_avpair_add (&(data.send_pairs), PW_NAS_IP_ADDRESS, &client_id, 0) == NULL)
		die (STATE_UNKNOWN, _("Invalid NAS-IP-Address"));

	my_rc_buildreq (&data, PW_ACCESS_REQUEST, server, port, (int)timeout_interval,
	             retries);

	result = my_rc_send_server (&data, msg);
	rc_avpair_free (data.send_pairs);
	if (data.receive_pairs)
		rc_avpair_free (data.receive_pairs);

	if (result == TIMEOUT_RC)
		die (STATE_CRITICAL, _("Timeout"));
	if (result == ERROR_RC)
		die (STATE_CRITICAL, _("Auth Error"));
	if (result == REJECT_RC)
		die (STATE_WARNING, _("Auth Failed"));
	if (result == BADRESP_RC)
		die (STATE_WARNING, _("Bad Response"));
	if (expect && !strstr (msg, expect))
		die (STATE_WARNING, "%s", msg);
	if (result == OK_RC)
		die (STATE_OK, _("Auth OK"));
	(void)snprintf(msg, sizeof(msg), _("Unexpected result code %d"), result);
	die (STATE_UNKNOWN, "%s", msg);
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
		{"nas-ip-address", required_argument, 0, 'N'},
		{"filename", required_argument, 0, 'F'},
		{"expect", required_argument, 0, 'e'},
		{"retries", required_argument, 0, 'r'},
		{"timeout", required_argument, 0, 't'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	while (1) {
		c = getopt_long (argc, argv, "+hVvH:P:F:u:p:n:N:t:r:e:", longopts,
									 &option);

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case '?':									/* print short usage statement if args not parsable */
			usage5 ();
		case 'h':									/* help */
			print_help ();
			exit (OK);
		case 'V':									/* version */
			print_revision (progname, NP_VERSION);
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
				usage4 (_("Port must be a positive integer"));
			break;
		case 'u':									/* username */
			username = optarg;
			break;
		case 'p':									/* password */
			password = strdup(optarg);

			/* Delete the password from process list */
			while (*optarg != '\0') {
				*optarg = 'X';
				optarg++;
			}
			break;
		case 'n':									/* nas id */
			nasid = optarg;
			break;
		case 'N':									/* nas ip address */
			nasipaddress = optarg;
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
				usage4 (_("Number of retries must be a positive integer"));
			break;
		case 't':									/* timeout */
			if (is_intpos (optarg))
				timeout_interval = atoi (optarg);
			else
				usage2 (_("Timeout interval must be a positive integer"), optarg);
			break;
		}
	}

	if (server == NULL)
		usage4 (_("Hostname was not supplied"));
	if (username == NULL)
		usage4 (_("User not specified"));
	if (password == NULL)
		usage4 (_("Password not specified"));
	if (config_file == NULL)
		usage4 (_("Configuration file not specified"));

	return OK;
}



void
print_help (void)
{
	char *myport;
	xasprintf (&myport, "%d", PW_AUTH_UDP_PORT);

	print_revision (progname, NP_VERSION);

	printf ("Copyright (c) 1999 Robert August Vincent II\n");
	printf (COPYRIGHT, copyright, email);

	printf("%s\n", _("Tests to see if a RADIUS server is accepting connections."));

  printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (UT_HOST_PORT, 'P', myport);

	printf (" %s\n", "-u, --username=STRING");
  printf ("    %s\n", _("The user to authenticate"));
  printf (" %s\n", "-p, --password=STRING");
  printf ("    %s\n", _("Password for autentication (SECURITY RISK)"));
  printf (" %s\n", "-n, --nas-id=STRING");
  printf ("    %s\n", _("NAS identifier"));
  printf (" %s\n", "-N, --nas-ip-address=STRING");
  printf ("    %s\n", _("NAS IP Address"));
  printf (" %s\n", "-F, --filename=STRING");
  printf ("    %s\n", _("Configuration file"));
  printf (" %s\n", "-e, --expect=STRING");
  printf ("    %s\n", _("Response string to expect from the server"));
  printf (" %s\n", "-r, --retries=INTEGER");
  printf ("    %s\n", _("Number of times to retry a failed connection"));

	printf (UT_TIMEOUT, timeout_interval);

  printf ("\n");
  printf ("%s\n", _("This plugin tests a RADIUS server to see if it is accepting connections."));
  printf ("%s\n", _("The server to test must be specified in the invocation, as well as a user"));
  printf ("%s\n", _("name and password. A configuration file may also be present. The format of"));
  printf ("%s\n", _("the configuration file is described in the radiusclient library sources."));
	printf ("%s\n", _("The password option presents a substantial security issue because the"));
  printf ("%s\n", _("password can possibly be determined by careful watching of the command line"));
  printf ("%s\n", _("in a process listing. This risk is exacerbated because nagios will"));
  printf ("%s\n", _("run the plugin at regular predictable intervals. Please be sure that"));
  printf ("%s\n", _("the password used does not allow access to sensitive system resources."));

	printf (UT_SUPPORT);
}



void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
	printf ("%s -H host -F config_file -u username -p password\n\
			[-P port] [-t timeout] [-r retries] [-e expect]\n\
			[-n nas-id] [-N nas-ip-addr]\n", progname);
}



int my_rc_read_config(char * a)
{
#ifdef HAVE_LIBRADIUSCLIENT_NG
	rch = rc_read_config(a);
	return (rch == NULL) ? 1 : 0;
#else
	return rc_read_config(a);
#endif
}
