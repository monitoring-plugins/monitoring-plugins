/***************************************************************************** *
 * CHECK_LDAP.C
 *
 * Program: Ldap plugin for Nagios
 * License: GPL
 * Copyright (c) 1999 Didi Rieder (adrieder@sbox.tu-graz.ac.at)
 * 
 * Last Modified: $Date$
 *
 * Command line: check_ldap -H <host> -b <base_dn> -p <port> -w <warn_time> -w <crit_time>
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

const char *progname = "check_ldap";
const char *revision = "$Revision$";

#include "config.h"
#include "common.h"
#include "netutils.h"
#include "utils.h"

#include <lber.h>
#include <ldap.h>

enum {
	UNDEFINED = -1,
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
char *ld_host = "";
char *ld_base = "";
char *ld_passwd = NULL;
char *ld_binddn = NULL;
unsigned int ld_port = DEFAULT_PORT;
#ifdef HAVE_LDAP_SET_OPTION
int ld_protocol = DEFAULT_PROTOCOL;
#endif
int warn_time = UNDEFINED;
int crit_time = UNDEFINED;

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

#ifdef HAVE_LDAP_SET_OPTION
	/* set ldap options */
	if (ldap_set_option (ld, LDAP_OPT_PROTOCOL_VERSION, &ld_protocol) !=
			LDAP_OPT_SUCCESS ) {
		printf("Could not set protocol version %d\n", ld_protocol);
		return STATE_CRITICAL;
	}
#endif
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

	/* calcutate the elapsed time and compare to thresholds */
	t_diff = time1 - time0;

	if (crit_time!=UNDEFINED && t_diff>=crit_time) {
		printf ("LDAP CRITICAL - %i seconds response time\n", t_diff);
		return STATE_CRITICAL;
	}

	if (warn_time!=UNDEFINED && t_diff>=warn_time) {
		printf ("LDAP WARNING - %i seconds response time\n", t_diff);
		return STATE_WARNING;
	}

	/* print out the result */
	printf ("LDAP OK - %i seconds response time\n", t_diff);

	return STATE_OK;
}

/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option_index = 0;
	/* initialize the long option struct */
	static struct option longopts[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"timeout", required_argument, 0, 't'},
		{"host", required_argument, 0, 'H'},
		{"base", required_argument, 0, 'b'},
		{"attr", required_argument, 0, 'a'},
		{"bind", required_argument, 0, 'D'},
		{"pass", required_argument, 0, 'P'},
#ifdef HAVE_LDAP_SET_OPTION
		{"ver2", no_argument, 0, '2'},
		{"ver3", no_argument, 0, '3'},
#endif
		{"use-ipv4", no_argument, 0, '4'},
		{"use-ipv6", no_argument, 0, '6'},
		{"port", required_argument, 0, 'p'},
		{"warn", required_argument, 0, 'w'},
		{"crit", required_argument, 0, 'c'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
	}

	while (1) {
		c = getopt_long (argc, argv, "hV2346t:c:w:H:b:p:a:D:P:", longopts, &option_index);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 't':									/* timeout period */
			if (!is_intnonneg (optarg))
				usage2 ("timeout interval must be a positive integer", optarg);
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
		case '6':
#ifdef USE_IPV6
			address_family = AF_INET6;
#else
			usage ("IPv6 support not available\n");
#endif
			break;
		default:
			usage ("check_ldap: could not parse unknown arguments\n");
			break;
		}
	}

	c = optind;
	if (strlen(ld_host) == 0 && is_host(argv[c])) {
		asprintf (&ld_host, "%s", argv[c++]);
	}
	if (strlen(ld_base) == 0 && argv[c]) {
		asprintf (&ld_base, "%s", argv[c++]);
	}

	return validate_arguments ();
}

int
validate_arguments ()
{
	if (strlen(ld_host) == 0)
		usage ("please specify the host name\n");

	if (strlen(ld_base) == 0)
		usage ("please specify the LDAP base\n");

	else
		return OK;

}



/* function print_help */
void
print_help ()
{
	print_revision (progname, revision);
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
		 "\t-p [--port] ... ldap port (default: %d)\n"
#ifdef HAVE_LDAP_SET_OPTION
		 "\t-2 [--ver2] ... use ldap protocol version 2\n"
		 "\t-3 [--ver3] ... use ldap protocol version 3\n"
		 "\t-4 [--use-ipv4] ... use IPv4 protocol\n"
		 "\t-6 [--use-ipv6] ... use IPv6 protocol\n"
		 "\t\t(default protocol version: %d)\n"
#endif
		 "\t-w [--warn] ... time in secs. - if the exceeds <warn> the STATE_WARNING will be returned\n"
		 "\t-c [--crit] ... time in secs. - if the exceeds <crit> the STATE_CRITICAL will be returned\n"
		 "\n", DEFAULT_PORT
#ifdef HAVE_LDAP_SET_OPTION
 		     , DEFAULT_PROTOCOL
#endif
		);
}


void
print_usage ()
{
	printf
		("Usage: %s -H <host> -b <base_dn> [-p <port>] [-a <attr>] [-D <binddn>]\n"
		 "         [-P <password>] [-w <warn_time>] [-c <crit_time>] [-t timeout]\n"
#ifdef HAVE_LDAP_SET_OPTION
		 "         [-2|-3] [-4|-6]\n"
#endif
		 "(Note: all times are in seconds.)\n", progname);
}
