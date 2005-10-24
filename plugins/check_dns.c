/******************************************************************************

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 LIMITATION: nslookup on Solaris 7 can return output over 2 lines, which will not 
 be picked up by this plugin
 
 $Id$

******************************************************************************/

const char *progname = "check_dns";
const char *revision = "$Revision$";
const char *copyright = "2000-2004";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "utils.h"
#include "netutils.h"
#include "runcmd.h"

int process_arguments (int, char **);
int validate_arguments (void);
int error_scan (char *);
void print_help (void);
void print_usage (void);

#define ADDRESS_LENGTH 256
char query_address[ADDRESS_LENGTH] = "";
char dns_server[ADDRESS_LENGTH] = "";
char ptr_server[ADDRESS_LENGTH] = "";
int verbose = FALSE;
char expected_address[ADDRESS_LENGTH] = "";
int match_expected_address = FALSE;
int expect_authority = FALSE;

int
main (int argc, char **argv)
{
	char *command_line = NULL;
	char input_buffer[MAX_INPUT_BUFFER];
	char *address = NULL;
	char *msg = NULL;
	char *temp_buffer = NULL;
	int non_authoritative = FALSE;
	int result = STATE_UNKNOWN;
	double elapsed_time;
	long microsec;
	struct timeval tv;
	int multi_address;
	int parse_address = FALSE; /* This flag scans for Address: but only after Name: */
	output chld_out, chld_err;
	size_t i;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* Set signal handling and alarm */
	if (signal (SIGALRM, popen_timeout_alarm_handler) == SIG_ERR) {
		usage_va(_("Cannot catch SIGALRM"));
	}

	if (process_arguments (argc, argv) == ERROR) {
		usage_va(_("Could not parse arguments"));
	}

	/* get the command to run */
	asprintf (&command_line, "%s %s %s", NSLOOKUP_COMMAND, query_address, dns_server);

	alarm (timeout_interval);
	gettimeofday (&tv, NULL);

	if (verbose)
		printf ("%s\n", command_line);

	/* run the command */
	if((np_runcmd(command_line, &chld_out, &chld_err, 0)) != 0) {
		msg = (char *)_("nslookup returned error status");
		result = STATE_WARNING;
	}

	/* scan stdout */
	for(i = 0; i < chld_out.lines; i++) {
		if (verbose)
			puts(chld_out.line[i]);

		if (strstr (chld_out.line[i], ".in-addr.arpa")) {
			if ((temp_buffer = strstr (chld_out.line[i], "name = ")))
				address = strdup (temp_buffer + 7);
			else {
				msg = (char *)_("Warning plugin error");
				result = STATE_WARNING;
			}
		}

		/* the server is responding, we just got the host name... */
		if (strstr (chld_out.line[i], "Name:"))
			parse_address = TRUE;
		else if (parse_address == TRUE && (strstr (chld_out.line[i], "Address:") ||
		         strstr (chld_out.line[i], "Addresses:"))) {
			temp_buffer = index (chld_out.line[i], ':');
			temp_buffer++;

			/* Strip leading spaces */
			for (; *temp_buffer != '\0' && *temp_buffer == ' '; temp_buffer++)
				/* NOOP */;
			
			strip(temp_buffer);
			if (temp_buffer==NULL || strlen(temp_buffer)==0) {
				die (STATE_CRITICAL,
				     _("DNS CRITICAL - '%s' returned empty host name string\n"),
				     NSLOOKUP_COMMAND);
			}

			if (address == NULL)
				address = strdup (temp_buffer);
			else
				asprintf(&address, "%s,%s", address, temp_buffer);
		}

		else if (strstr (chld_out.line[i], _("Non-authoritative answer:"))) {
			non_authoritative = TRUE;
		}

		result = error_scan (chld_out.line[i]);
		if (result != STATE_OK) {
			msg = strchr (chld_out.line[i], ':');
			if(msg) msg++;
			break;
		}
	}

	/* scan stderr */
	for(i = 0; i < chld_err.lines; i++) {
		if (verbose)
			puts(chld_err.line[i]);

		if (error_scan (chld_err.line[i]) != STATE_OK) {
			result = max_state (result, error_scan (chld_err.line[i]));
			msg = strchr(input_buffer, ':');
			if(msg) msg++;
		}
	}

	/* If we got here, we should have an address string,
	 * and we can segfault if we do not */
	if (address==NULL || strlen(address)==0)
		die (STATE_CRITICAL,
		     _("DNS CRITICAL - '%s' msg parsing exited with no address\n"),
		     NSLOOKUP_COMMAND);

	/* compare to expected address */
	if (result == STATE_OK && match_expected_address && strcmp(address, expected_address)) {
		result = STATE_CRITICAL;
		asprintf(&msg, _("expected %s but got %s"), expected_address, address);
	}

	/* check if authoritative */
	if (result == STATE_OK && expect_authority && non_authoritative) {
		result = STATE_CRITICAL;
		asprintf(&msg, _("server %s is not authoritative for %s"), dns_server, query_address);
	}

	microsec = deltime (tv);
	elapsed_time = (double)microsec / 1.0e6;

	if (result == STATE_OK) {
		if (strchr (address, ',') == NULL)
			multi_address = FALSE;
		else
			multi_address = TRUE;

		printf ("DNS %s: ", _("OK"));
		printf (ngettext("%.3f second response time", "%.3f seconds response time", elapsed_time), elapsed_time);
		printf (_(". %s returns %s"), query_address, address);
		printf ("|%s\n", fperfdata ("time", elapsed_time, "s", FALSE, 0, FALSE, 0, TRUE, 0, FALSE, 0));
	}
	else if (result == STATE_WARNING)
		printf (_("DNS WARNING - %s\n"),
		        !strcmp (msg, "") ? _(" Probably a non-existent host/domain") : msg);
	else if (result == STATE_CRITICAL)
		printf (_("DNS CRITICAL - %s\n"),
		        !strcmp (msg, "") ? _(" Probably a non-existent host/domain") : msg);
	else
		printf (_("DNS UNKNOW - %s\n"),
		        !strcmp (msg, "") ? _(" Probably a non-existent host/domain") : msg);

	return result;
}



int
error_scan (char *input_buffer)
{

	/* the DNS lookup timed out */
	if (strstr (input_buffer, _("Note: nslookup is deprecated and may be removed from future releases.")) ||
	    strstr (input_buffer, _("Consider using the `dig' or `host' programs instead.  Run nslookup with")) ||
	    strstr (input_buffer, _("the `-sil[ent]' option to prevent this message from appearing.")))
		return STATE_OK;

	/* DNS server is not running... */
	else if (strstr (input_buffer, "No response from server"))
		die (STATE_CRITICAL, _("No response from DNS %s\n"), dns_server);

	/* Host name is valid, but server doesn't have records... */
	else if (strstr (input_buffer, "No records"))
		die (STATE_CRITICAL, _("DNS %s has no records\n"), dns_server);

	/* Connection was refused */
	else if (strstr (input_buffer, "Connection refused") ||
		 strstr (input_buffer, "Couldn't find server") ||
	         strstr (input_buffer, "Refused") ||
	         (strstr (input_buffer, "** server can't find") &&
	          strstr (input_buffer, ": REFUSED")))
		die (STATE_CRITICAL, _("Connection to DNS %s was refused\n"), dns_server);

	/* Query refused (usually by an ACL in the namserver) */ 
	else if (strstr (input_buffer, "Query refused"))
		die (STATE_CRITICAL, _("Query was refused by DNS server at %s\n"), dns_server);

	/* No information (e.g. nameserver IP has two PTR records) */
	else if (strstr (input_buffer, "No information"))
		die (STATE_CRITICAL, _("No information returned by DNS server at %s\n"), dns_server);

	/* Host or domain name does not exist */
	else if (strstr (input_buffer, "Non-existent") ||
	         strstr (input_buffer, "** server can't find") ||
		 strstr (input_buffer,"NXDOMAIN"))
		die (STATE_CRITICAL, _("Domain %s was not found by the server\n"), query_address);

	/* Network is unreachable */
	else if (strstr (input_buffer, "Network is unreachable"))
		die (STATE_CRITICAL, _("Network is unreachable\n"));

	/* Internal server failure */
	else if (strstr (input_buffer, "Server failure"))
		die (STATE_CRITICAL, _("DNS failure for %s\n"), dns_server);

	/* Request error or the DNS lookup timed out */
	else if (strstr (input_buffer, "Format error") ||
	         strstr (input_buffer, "Timed out"))
		return STATE_WARNING;

	return STATE_OK;

}


/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int opt_index = 0;
	static struct option long_opts[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"verbose", no_argument, 0, 'v'},
		{"timeout", required_argument, 0, 't'},
		{"hostname", required_argument, 0, 'H'},
		{"server", required_argument, 0, 's'},
		{"reverse-server", required_argument, 0, 'r'},
		{"expected-address", required_argument, 0, 'a'},
		{"expect-authority", no_argument, 0, 'A'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++)
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");

	while (1) {
		c = getopt_long (argc, argv, "hVvAt:H:s:r:a:", long_opts, &opt_index);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'h': /* help */
			print_help ();
			exit (STATE_OK);
		case 'V': /* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 'v': /* version */
			verbose = TRUE;
			break;
		case 't': /* timeout period */
			timeout_interval = atoi (optarg);
			break;
		case 'H': /* hostname */
			if (strlen (optarg) >= ADDRESS_LENGTH)
				die (STATE_UNKNOWN, _("Input buffer overflow\n"));
			strcpy (query_address, optarg);
			break;
		case 's': /* server name */
			/* TODO: this host_or_die check is probably unnecessary.
			 * Better to confirm nslookup response matches */
			host_or_die(optarg);
			if (strlen (optarg) >= ADDRESS_LENGTH)
				die (STATE_UNKNOWN, _("Input buffer overflow\n"));
			strcpy (dns_server, optarg);
			break;
		case 'r': /* reverse server name */
			/* TODO: Is this host_or_die necessary? */
			host_or_die(optarg);
			if (strlen (optarg) >= ADDRESS_LENGTH)
				die (STATE_UNKNOWN, _("Input buffer overflow\n"));
			strcpy (ptr_server, optarg);
			break;
		case 'a': /* expected address */
			if (strlen (optarg) >= ADDRESS_LENGTH)
				die (STATE_UNKNOWN, _("Input buffer overflow\n"));
			strcpy (expected_address, optarg);
			match_expected_address = TRUE;
			break;
		case 'A': /* expect authority */
			expect_authority = TRUE;
			break;
		default: /* args not parsable */
			usage_va(_("Unknown argument - %s"), optarg);
		}
	}

	c = optind;
	if (strlen(query_address)==0 && c<argc) {
		if (strlen(argv[c])>=ADDRESS_LENGTH)
			die (STATE_UNKNOWN, _("Input buffer overflow\n"));
		strcpy (query_address, argv[c++]);
	}

	if (strlen(dns_server)==0 && c<argc) {
		/* TODO: See -s option */
		host_or_die(argv[c]);
		if (strlen(argv[c]) >= ADDRESS_LENGTH)
			die (STATE_UNKNOWN, _("Input buffer overflow\n"));
		strcpy (dns_server, argv[c++]);
	}

	return validate_arguments ();
}


int
validate_arguments ()
{
	if (query_address[0] == 0)
		return ERROR;

	return OK;
}


void
print_help (void)
{
	print_revision (progname, revision);

	printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf (COPYRIGHT, copyright, email);

	printf (_("\
This plugin uses the nslookup program to obtain the IP address\n\
for the given host/domain query.  A optional DNS server to use may\n\
be specified.  If no DNS server is specified, the default server(s)\n\
specified in /etc/resolv.conf will be used.\n\n"));

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_("\
-H, --hostname=HOST\n\
   The name or address you want to query\n\
-s, --server=HOST\n\
   Optional DNS server you want to use for the lookup\n\
-a, --expected-address=IP-ADDRESS\n\
   Optional IP address you expect the DNS server to return\n\
-A, --expect-authority\n\
   Optionally expect the DNS server to be authoritative for the lookup\n"));

	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);

	printf (_(UT_SUPPORT));
}


void
print_usage (void)
{
	printf ("\
Usage: %s -H host [-s server] [-a expected-address] [-A] [-t timeout]\n", progname);
}
