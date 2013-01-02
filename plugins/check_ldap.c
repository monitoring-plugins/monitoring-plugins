/*****************************************************************************
* 
* Nagios check_ldap plugin
* 
* License: GPL
* Copyright (c) 2000-2008 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_ldap plugin
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

/* progname may be check_ldaps */
char *progname = "check_ldap";
const char *copyright = "2000-2008";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"

#include <lber.h>
#define LDAP_DEPRECATED 1
#include <ldap.h>

enum {
	UNDEFINED = 0,
#ifdef HAVE_LDAP_SET_OPTION
	DEFAULT_PROTOCOL = 2,
#endif
	DEFAULT_PORT = 389
};

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

char ld_defattr[] = "(objectclass=*)";
char *ld_attr = ld_defattr;
char *ld_host = NULL;
char *ld_base = NULL;
char *ld_passwd = NULL;
char *ld_binddn = NULL;
int ld_port = -1;
#ifdef HAVE_LDAP_SET_OPTION
int ld_protocol = DEFAULT_PROTOCOL;
#endif
#ifndef LDAP_OPT_SUCCESS
# define LDAP_OPT_SUCCESS LDAP_SUCCESS
#endif
double warn_time = UNDEFINED;
double crit_time = UNDEFINED;
struct timeval tv;
int starttls = FALSE;
int ssl_on_connect = FALSE;
int verbose = 0;

/* for ldap tls */

char *SERVICE = "LDAP";

int
main (int argc, char *argv[])
{

	LDAP *ld;
	LDAPMessage *result;

	/* should be 	int result = STATE_UNKNOWN; */

	int status = STATE_UNKNOWN;
	long microsec;
	double elapsed_time;

	/* for ldap tls */

	int tls;
	int version=3;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	if (strstr(argv[0],"check_ldaps")) {
		xasprintf (&progname, "check_ldaps");
 	}

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	if (strstr(argv[0],"check_ldaps") && ! starttls && ! ssl_on_connect)
		starttls = TRUE;

	/* initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm (socket_timeout);

	/* get the start time */
	gettimeofday (&tv, NULL);

	/* initialize ldap */
#ifdef HAVE_LDAP_INIT
	if (!(ld = ldap_init (ld_host, ld_port))) {
		printf ("Could not connect to the server at port %i\n", ld_port);
		return STATE_CRITICAL;
	}
#else
	if (!(ld = ldap_open (ld_host, ld_port))) {
		if (verbose)
			ldap_perror(ld, "ldap_open");
		printf (_("Could not connect to the server at port %i\n"), ld_port);
		return STATE_CRITICAL;
	}
#endif /* HAVE_LDAP_INIT */

#ifdef HAVE_LDAP_SET_OPTION
	/* set ldap options */
	if (ldap_set_option (ld, LDAP_OPT_PROTOCOL_VERSION, &ld_protocol) !=
			LDAP_OPT_SUCCESS ) {
		printf(_("Could not set protocol version %d\n"), ld_protocol);
		return STATE_CRITICAL;
	}
#endif

	if (ld_port == LDAPS_PORT || ssl_on_connect) {
		xasprintf (&SERVICE, "LDAPS");
#if defined(HAVE_LDAP_SET_OPTION) && defined(LDAP_OPT_X_TLS)
		/* ldaps: set option tls */
		tls = LDAP_OPT_X_TLS_HARD;

		if (ldap_set_option (ld, LDAP_OPT_X_TLS, &tls) != LDAP_SUCCESS)
		{
			if (verbose)
				ldap_perror(ld, "ldaps_option");
			printf (_("Could not init TLS at port %i!\n"), ld_port);
			return STATE_CRITICAL;
		}
#else
		printf (_("TLS not supported by the libraries!\n"));
		return STATE_CRITICAL;
#endif /* LDAP_OPT_X_TLS */
	} else if (starttls) {
		xasprintf (&SERVICE, "LDAP-TLS");
#if defined(HAVE_LDAP_SET_OPTION) && defined(HAVE_LDAP_START_TLS_S)
		/* ldap with startTLS: set option version */
		if (ldap_get_option(ld,LDAP_OPT_PROTOCOL_VERSION, &version) == LDAP_OPT_SUCCESS )
		{
			if (version < LDAP_VERSION3)
			{
				version = LDAP_VERSION3;
				ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);
			}
		}
		/* call start_tls */
		if (ldap_start_tls_s(ld, NULL, NULL) != LDAP_SUCCESS)
		{
			if (verbose)
				ldap_perror(ld, "ldap_start_tls");
			printf (_("Could not init startTLS at port %i!\n"), ld_port);
			return STATE_CRITICAL;
		}
#else
		printf (_("startTLS not supported by the library, needs LDAPv3!\n"));
		return STATE_CRITICAL;
#endif /* HAVE_LDAP_START_TLS_S */
	}

	/* bind to the ldap server */
	if (ldap_bind_s (ld, ld_binddn, ld_passwd, LDAP_AUTH_SIMPLE) !=
			LDAP_SUCCESS) {
		if (verbose)
			ldap_perror(ld, "ldap_bind");
		printf (_("Could not bind to the LDAP server\n"));
		return STATE_CRITICAL;
	}

	/* do a search of all objectclasses in the base dn */
	if (ldap_search_s (ld, ld_base, LDAP_SCOPE_BASE, ld_attr, NULL, 0, &result)
			!= LDAP_SUCCESS) {
		if (verbose)
			ldap_perror(ld, "ldap_search");
		printf (_("Could not search/find objectclasses in %s\n"), ld_base);
		return STATE_CRITICAL;
	}

	/* unbind from the ldap server */
	ldap_unbind (ld);

	/* reset the alarm handler */
	alarm (0);

	/* calcutate the elapsed time and compare to thresholds */

	microsec = deltime (tv);
	elapsed_time = (double)microsec / 1.0e6;

	if (crit_time!=UNDEFINED && elapsed_time>crit_time)
		status = STATE_CRITICAL;
	else if (warn_time!=UNDEFINED && elapsed_time>warn_time)
		status = STATE_WARNING;
	else
		status = STATE_OK;

	/* print out the result */
	printf (_("LDAP %s - %.3f seconds response time|%s\n"),
	        state_text (status),
	        elapsed_time,
	        fperfdata ("time", elapsed_time, "s",
	                  (int)warn_time, warn_time,
	                  (int)crit_time, crit_time,
	                  TRUE, 0, FALSE, 0));

	return status;
}

/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option = 0;
	/* initialize the long option struct */
	static struct option longopts[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"timeout", required_argument, 0, 't'},
		{"hostname", required_argument, 0, 'H'},
		{"base", required_argument, 0, 'b'},
		{"attr", required_argument, 0, 'a'},
		{"bind", required_argument, 0, 'D'},
		{"pass", required_argument, 0, 'P'},
#ifdef HAVE_LDAP_SET_OPTION
		{"ver2", no_argument, 0, '2'},
		{"ver3", no_argument, 0, '3'},
#endif
		{"starttls", no_argument, 0, 'T'},
		{"ssl", no_argument, 0, 'S'},
		{"use-ipv4", no_argument, 0, '4'},
		{"use-ipv6", no_argument, 0, '6'},
		{"port", required_argument, 0, 'p'},
		{"warn", required_argument, 0, 'w'},
		{"crit", required_argument, 0, 'c'},
		{"verbose", no_argument, 0, 'v'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
	}

	while (1) {
		c = getopt_long (argc, argv, "hvV234TS6t:c:w:H:b:p:a:D:P:", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_OK);
		case 't':									/* timeout period */
			if (!is_intnonneg (optarg))
				usage2 (_("Timeout interval must be a positive integer"), optarg);
			else
				socket_timeout = atoi (optarg);
			break;
		case 'H':
			ld_host = optarg;
			break;
		case 'b':
			ld_base = optarg;
			break;
		case 'p':
			ld_port = atoi (optarg);
			break;
		case 'a':
			ld_attr = optarg;
			break;
		case 'D':
			ld_binddn = optarg;
			break;
		case 'P':
			ld_passwd = optarg;
			break;
		case 'w':
			warn_time = strtod (optarg, NULL);
			break;
		case 'c':
			crit_time = strtod (optarg, NULL);
			break;
#ifdef HAVE_LDAP_SET_OPTION
		case '2':
			ld_protocol = 2;
			break;
		case '3':
			ld_protocol = 3;
			break;
#endif
		case '4':
			address_family = AF_INET;
			break;
		case 'v':
			verbose++;
			break;
		case 'T':
			if (! ssl_on_connect)
				starttls = TRUE;
			else
				usage_va(_("%s cannot be combined with %s"), "-T/--starttls", "-S/--ssl");
			break;
		case 'S':
			if (! starttls) {
				ssl_on_connect = TRUE;
				if (ld_port == -1)
					ld_port = LDAPS_PORT;
			} else
				usage_va(_("%s cannot be combined with %s"), "-S/--ssl", "-T/--starttls");
			break;
		case '6':
#ifdef USE_IPV6
			address_family = AF_INET6;
#else
			usage (_("IPv6 support not available\n"));
#endif
			break;
		default:
			usage5 ();
		}
	}

	c = optind;
	if (ld_host == NULL && is_host(argv[c]))
		ld_host = strdup (argv[c++]);

	if (ld_base == NULL && argv[c])
		ld_base = strdup (argv[c++]);

	if (ld_port == -1)
		ld_port = DEFAULT_PORT;

	return validate_arguments ();
}


int
validate_arguments ()
{
	if (ld_host==NULL || strlen(ld_host)==0)
		usage4 (_("Please specify the host name\n"));

	if (ld_base==NULL)
		usage4 (_("Please specify the LDAP base\n"));

	return OK;
}


void
print_help (void)
{
	char *myport;
	xasprintf (&myport, "%d", DEFAULT_PORT);

	print_revision (progname, NP_VERSION);

	printf ("Copyright (c) 1999 Didi Rieder (adrieder@sbox.tu-graz.ac.at)\n");
	printf (COPYRIGHT, copyright, email);

	printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (UT_HOST_PORT, 'p', myport);

	printf (UT_IPv46);

	printf (" %s\n", "-a [--attr]");
  printf ("    %s\n", _("ldap attribute to search (default: \"(objectclass=*)\""));
  printf (" %s\n", "-b [--base]");
  printf ("    %s\n", _("ldap base (eg. ou=my unit, o=my org, c=at"));
  printf (" %s\n", "-D [--bind]");
  printf ("    %s\n", _("ldap bind DN (if required)"));
  printf (" %s\n", "-P [--pass]");
  printf ("    %s\n", _("ldap password (if required)"));
  printf (" %s\n", "-T [--starttls]");
  printf ("    %s\n", _("use starttls mechanism introduced in protocol version 3"));
  printf (" %s\n", "-S [--ssl]");
  printf ("    %s %i\n", _("use ldaps (ldap v2 ssl method). this also sets the default port to"), LDAPS_PORT);

#ifdef HAVE_LDAP_SET_OPTION
	printf (" %s\n", "-2 [--ver2]");
  printf ("    %s\n", _("use ldap protocol version 2"));
  printf (" %s\n", "-3 [--ver3]");
  printf ("    %s\n", _("use ldap protocol version 3"));
  printf ("    (%s %d)\n", _("default protocol version:"), DEFAULT_PROTOCOL);
#endif

	printf (UT_WARN_CRIT);

	printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf (UT_VERBOSE);

	printf ("\n");
	printf ("%s\n", _("Notes:"));
	printf (" %s\n", _("If this plugin is called via 'check_ldaps', method 'STARTTLS' will be"));
	printf (_(" implied (using default port %i) unless --port=636 is specified. In that case\n"), DEFAULT_PORT);
	printf (" %s\n", _("'SSL on connect' will be used no matter how the plugin was called."));
	printf (" %s\n", _("This detection is deprecated, please use 'check_ldap' with the '--starttls' or '--ssl' flags"));
	printf (" %s\n", _("to define the behaviour explicitly instead."));

	printf (UT_SUPPORT);
}

void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
	printf (" %s -H <host> -b <base_dn> [-p <port>] [-a <attr>] [-D <binddn>]",progname);
  printf ("\n       [-P <password>] [-w <warn_time>] [-c <crit_time>] [-t timeout]%s\n",
#ifdef HAVE_LDAP_SET_OPTION
			"\n       [-2|-3] [-4|-6]"
#else
			""
#endif
			);
}
