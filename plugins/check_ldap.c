/***************************************************************************** *
 * CHECK_LDAP.C
 *
 * Program: Ldap plugin for Nagios
 * License: GPL
 * Copyright (c) 1999 Didi Rieder (adrieder@sbox.tu-graz.ac.at)
 * 
 * Last Modified: $Date$
 *
 * Command line: check_ldap -h <host> -b <base_dn> -p <port> -w <warn_time> -w <crit_time>
 *
 * Description:
 *
 * This plugin is for testing a ldap server.
 *
 * Modifications:
 *
 * 08-25-1999 Ethan Galstad (nagios@nagios.org)
 *            Modified to use common plugin include file
 *
 *****************************************************************************/

#define PROGNAME "check_ldap"
#define REVISION "$Revision$"

#include "config.h"
#include "common.h"
#include "netutils.h"
#include "utils.h"

#include <lber.h>
#include <ldap.h>

#define UNKNOWN -1

int process_arguments (int, char **);
int call_getopt (int, char **);
int validate_arguments (void);
static void print_help (void);
static void print_usage (void);

char ld_defattr[] = "(objectclass=*)";
char *ld_attr = ld_defattr;
char *ld_host = NULL, *ld_base = NULL, *ld_passwd = NULL, *ld_binddn = NULL;
unsigned int ld_port = 389;
int warn_time = UNKNOWN, crit_time = UNKNOWN;

int
main (int argc, char *argv[])
{

	LDAP *ld;
	LDAPMessage *result;

	int t_diff;
	time_t time0, time1;

	if (process_arguments (argc, argv) == ERROR)
		usage ("check_ldap: could not parse arguments\n");

	/* initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm (socket_timeout);

	/* get the start time */
	time (&time0);

	/* initialize ldap */
	if (!(ld = ldap_open (ld_host, ld_port))) {
		/*ldap_perror(ld, "ldap_open"); */
		printf ("Could not connect to the server at port %i\n", ld_port);
		return STATE_CRITICAL;
	}

	/* bind to the ldap server */
	if (ldap_bind_s (ld, ld_binddn, ld_passwd, LDAP_AUTH_SIMPLE) !=
			LDAP_SUCCESS) {
		/*ldap_perror(ld, "ldap_bind"); */
		printf ("Could not bind to the ldap-server\n");
		return STATE_CRITICAL;
	}

	/* do a search of all objectclasses in the base dn */
	if (ldap_search_s (ld, ld_base, LDAP_SCOPE_BASE, ld_attr, NULL, 0, &result)
			!= LDAP_SUCCESS) {
		/*ldap_perror(ld, "ldap_search"); */
		printf ("Could not search/find objectclasses in %s\n", ld_base);
		return STATE_CRITICAL;
	}

	/* unbind from the ldap server */
	ldap_unbind (ld);

	/* reset the alarm handler */
	alarm (0);

	/* get the finish time */
	time (&time1);

	/* calcutate the elapsed time */
	t_diff = time1 - time0;

	/* check if warn_time or crit_time was exceeded */
	if ((t_diff >= warn_time) && (t_diff < crit_time)) {
		printf ("LDAP warning - %i seconds response time\n", t_diff);
		return STATE_WARNING;
	}
	if (t_diff >= crit_time) {
		printf ("LDAP critical - %i seconds response time\n", t_diff);
		return STATE_CRITICAL;
	}

	/* print out the result */
	printf ("LDAP ok - %i seconds response time\n", t_diff);

	return STATE_OK;
}

/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
	}

	c = 0;
	while (c += (call_getopt (argc - c, &argv[c]))) {
		if (argc <= c)
			break;
		if (ld_host[0] == 0) {
			strncpy (ld_host, argv[c], sizeof (ld_host) - 1);
			ld_host[sizeof (ld_host) - 1] = 0;
		}
	}

	return c;
}

int
call_getopt (int argc, char **argv)
{
	int c, i = 1;
#ifdef HAVE_GETOPT_H
	int option_index = 0;
	/* initialize the long option struct */
	static struct option long_options[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"timeout", required_argument, 0, 't'},
		{"host", required_argument, 0, 'H'},
		{"base", required_argument, 0, 'b'},
		{"attr", required_argument, 0, 'a'},
		{"bind", required_argument, 0, 'D'},
		{"pass", required_argument, 0, 'P'},
		{"port", required_argument, 0, 'p'},
		{"warn", required_argument, 0, 'w'},
		{"crit", required_argument, 0, 'c'},
		{0, 0, 0, 0}
	};
#endif

	for (c = 1; c < argc; c++)
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");

	while (1) {
#ifdef HAVE_GETOPT_H
		c =
			getopt_long (argc, argv, "+hVt:c:w:H:b:p:a:D:P:", long_options,
									 &option_index);
#else
		c = getopt (argc, argv, "+?hVt:c:w:H:b:p:a:D:P:");
#endif

		if (c == -1 || c == EOF)
			break;

		i++;
		switch (c) {
		case 't':
		case 'c':
		case 'w':
		case 'H':
		case 'b':
		case 'p':
		case 'a':
		case 'D':
		case 'P':
			i++;
		}

		switch (c) {
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (PROGNAME, REVISION);
			exit (STATE_OK);
		case 't':									/* timeout period */
			if (!is_intnonneg (optarg))
				usage2 ("timeout interval must be an integer", optarg);
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
			warn_time = atoi (optarg);
			break;
		case 'c':
			crit_time = atoi (optarg);
			break;
		default:
			usage ("check_ldap: could not parse arguments\n");
			break;
		}
	}
	return i;
}

int
validate_arguments ()
{
	if (ld_host[0] == 0 ||
			ld_base[0] == 0 ||
			ld_port == UNKNOWN || warn_time == UNKNOWN || crit_time == UNKNOWN) {
		return ERROR;
	}
	else {
		return OK;
	}
}



/* function print_help */
static void
print_help ()
{
	print_revision (PROGNAME, REVISION);
	printf
		("Copyright (c) 1999 Didi Rieder (adrieder@sbox.tu-graz.ac.at)\n"
		 "License: GPL\n" "\n");
	print_usage ();
	printf
		("\n"
		 "Options:\n"
		 "\t-H [--host] ... host\n"
		 "\t-a [--attr] ... ldap attribute to search (default: \"(objectclass=*)\"\n"
		 "\t-b [--base] ... ldap base (eg. ou=my unit, o=my org, c=at)\n"
		 "\t-D [--bind] ... ldap bind DN (if required)\n"
		 "\t-P [--pass] ... ldap password (if required)\n"
		 "\t-p [--port] ... ldap port (normaly 389)\n"
		 "\t-w [--warn] ... time in secs. - if the exceeds <warn> the STATE_WARNING will be returned\n"
		 "\t-c [--crit] ... time in secs. - if the exceeds <crit> the STATE_CRITICAL will be returned\n"
		 "\n");
}


static void
print_usage ()
{
	printf
		("Usage: %s -H <host> -b <base_dn> -p <port> [-a <attr>] [-D <binddn>]\n"
		 "         [-P <password>] [-w <warn_time>] [-c <crit_time>] [-t timeout]\n"
		 "(Note: all times are in seconds.)\n", PROGNAME);
}
