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

const char *progname = "check_smtp";
const char *revision = "$Revision$";
const char *copyright = "2000-2004";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"

enum {
	SMTP_PORT	= 25
};
const char *SMTP_EXPECT = "220";
const char *SMTP_HELO = "HELO ";
const char *SMTP_QUIT	= "QUIT\r\n";

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

#ifdef HAVE_REGEX_H
#include <regex.h>
char regex_expect[MAX_INPUT_BUFFER] = "";
regex_t preg;
regmatch_t pmatch[10];
char timestamp[10] = "";
char errbuf[MAX_INPUT_BUFFER];
int cflags = REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
int eflags = 0;
int errcode, excode;
#endif

int server_port = SMTP_PORT;
char *server_address = NULL;
char *server_expect = NULL;
int smtp_use_dummycmd = 0;
char *mail_command = NULL;
char *from_arg = NULL;
int ncommands=0;
int command_size=0;
int nresponses=0;
int response_size=0;
char **commands = NULL;
char **responses = NULL;
int warning_time = 0;
int check_warning_time = FALSE;
int critical_time = 0;
int check_critical_time = FALSE;
int verbose = 0;



int
main (int argc, char **argv)
{
	int sd;
	int n = 0;
	double elapsed_time;
	long microsec;
	int result = STATE_UNKNOWN;
	char buffer[MAX_INPUT_BUFFER];
	char *cmd_str = NULL;
	char *helocmd = NULL;
	struct timeval tv;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* initialize the HELO command with the localhostname */
#ifndef HOST_MAX_BYTES
#define HOST_MAX_BYTES 255
#endif
	helocmd = malloc (HOST_MAX_BYTES);
	gethostname(helocmd, HOST_MAX_BYTES);
	asprintf (&helocmd, "%s%s%s", SMTP_HELO, helocmd, "\r\n");

	/* initialize the MAIL command with optional FROM command  */
	asprintf (&cmd_str, "%sFROM: %s%s", mail_command, from_arg, "\r\n");

	if (verbose && smtp_use_dummycmd)
		printf ("FROM CMD: %s", cmd_str);
	
	/* initialize alarm signal handling */
	(void) signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	(void) alarm (socket_timeout);

	/* start timer */
	gettimeofday (&tv, NULL);

	/* try to connect to the host at the given port number */
	result = my_tcp_connect (server_address, server_port, &sd);

	if (result == STATE_OK) { /* we connected */

		/* watch for the SMTP connection string and */
		/* return a WARNING status if we couldn't read any data */
		if (recv (sd, buffer, MAX_INPUT_BUFFER - 1, 0) == -1) {
			printf (_("recv() failed\n"));
			result = STATE_WARNING;
		}
		else {
			if (verbose)
				printf ("%s", buffer);
			/* strip the buffer of carriage returns */
			strip (buffer);
			/* make sure we find the response we are looking for */
			if (!strstr (buffer, server_expect)) {
				if (server_port == SMTP_PORT)
					printf (_("Invalid SMTP response received from host\n"));
				else
					printf (_("Invalid SMTP response received from host on port %d\n"),
									server_port);
				result = STATE_WARNING;
			}
		}

		/* send the HELO command */
		send(sd, helocmd, strlen(helocmd), 0);

		/* allow for response to helo command to reach us */
		recv(sd, buffer, MAX_INPUT_BUFFER-1, 0);
				
		/* sendmail will syslog a "NOQUEUE" error if session does not attempt
		 * to do something useful. This can be prevented by giving a command
		 * even if syntax is illegal (MAIL requires a FROM:<...> argument)
		 *
		 * According to rfc821 you can include a null reversepath in the from command
		 * - but a log message is generated on the smtp server.
		 *
		 * You can disable sending mail_command with '--nocommand'
		 * Use the -f option to provide a FROM address
		 */
		if (smtp_use_dummycmd) {
			send(sd, cmd_str, strlen(cmd_str), 0);
			recv(sd, buffer, MAX_INPUT_BUFFER-1, 0);
			if (verbose) 
				printf("%s", buffer);
		}

		while (n < ncommands) {
			asprintf (&cmd_str, "%s%s", commands[n], "\r\n");
			send(sd, cmd_str, strlen(cmd_str), 0);
			recv(sd, buffer, MAX_INPUT_BUFFER-1, 0);
			if (verbose) 
				printf("%s", buffer);
			strip (buffer);
			if (n < nresponses) {
#ifdef HAVE_REGEX_H
				cflags |= REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
				//strncpy (regex_expect, responses[n], sizeof (regex_expect) - 1);
				//regex_expect[sizeof (regex_expect) - 1] = '\0';
				errcode = regcomp (&preg, responses[n], cflags);
				if (errcode != 0) {
					regerror (errcode, &preg, errbuf, MAX_INPUT_BUFFER);
					printf (_("Could Not Compile Regular Expression"));
					return ERROR;
				}
				excode = regexec (&preg, buffer, 10, pmatch, eflags);
				if (excode == 0) {
					result = STATE_OK;
				}
				else if (excode == REG_NOMATCH) {
					result = STATE_WARNING;
					printf (_("SMTP %s - Invalid response '%s' to command '%s'\n"), state_text (result), buffer, commands[n]);
				}
				else {
					regerror (excode, &preg, errbuf, MAX_INPUT_BUFFER);
					printf (_("Execute Error: %s\n"), errbuf);
					result = STATE_UNKNOWN;
				}
#else
				if (strstr(buffer, responses[n])!=buffer) {
					result = STATE_WARNING;
					printf (_("SMTP %s - Invalid response '%s' to command '%s'\n"), state_text (result), buffer, commands[n]);
				}
#endif
			}
			n++;
		}

		/* tell the server we're done */
		send (sd, SMTP_QUIT, strlen (SMTP_QUIT), 0);

		/* finally close the connection */
		close (sd);
	}

	/* reset the alarm */
	alarm (0);

	microsec = deltime (tv);
	elapsed_time = (double)microsec / 1.0e6;

	if (result == STATE_OK) {
		if (check_critical_time && elapsed_time > (double) critical_time)
			result = STATE_CRITICAL;
		else if (check_warning_time && elapsed_time > (double) warning_time)
			result = STATE_WARNING;
	}

	printf (_("SMTP %s - %.3f sec. response time%s%s|%s\n"),
	        state_text (result), elapsed_time,
          verbose?", ":"", verbose?buffer:"",
	        fperfdata ("time", elapsed_time, "s",
	                  (int)check_warning_time, warning_time,
	                  (int)check_critical_time, critical_time,
	                  TRUE, 0, FALSE, 0));

	return result;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option = 0;
	static struct option longopts[] = {
		{"hostname", required_argument, 0, 'H'},
		{"expect", required_argument, 0, 'e'},
		{"critical", required_argument, 0, 'c'},
		{"warning", required_argument, 0, 'w'},
		{"timeout", required_argument, 0, 't'},
		{"port", required_argument, 0, 'p'},
		{"from", required_argument, 0, 'f'},
		{"command", required_argument, 0, 'C'},
		{"response", required_argument, 0, 'R'},
		{"nocommand", required_argument, 0, 'n'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"use-ipv4", no_argument, 0, '4'},
		{"use-ipv6", no_argument, 0, '6'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
		else if (strcmp ("-wt", argv[c]) == 0)
			strcpy (argv[c], "-w");
		else if (strcmp ("-ct", argv[c]) == 0)
			strcpy (argv[c], "-c");
	}

	while (1) {
		c = getopt_long (argc, argv, "+hVv46t:p:f:e:c:w:H:C:R:",
		                 longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'H':									/* hostname */
			if (is_host (optarg)) {
				server_address = optarg;
			}
			else {
				usage2 (_("Invalid hostname/address"), optarg);
			}
			break;
		case 'p':									/* port */
			if (is_intpos (optarg))
				server_port = atoi (optarg);
			else
				usage4 (_("Port must be a positive integer"));
			break;
		case 'f':									/* from argument */
			from_arg = optarg;
			smtp_use_dummycmd = 1;
			break;
		case 'e':									/* server expect string on 220  */
			server_expect = optarg;
			break;
		case 'C':									/* commands  */
			if (ncommands >= command_size) {
				commands = realloc (commands, command_size+8);
				if (commands == NULL)
					die (STATE_UNKNOWN,
					     _("Could not realloc() units [%d]\n"), ncommands);
			}
			commands[ncommands] = optarg;
			ncommands++;
			break;
		case 'R':									/* server responses */
			if (nresponses >= response_size) {
				responses = realloc (responses, response_size+8);
				if (responses == NULL)
					die (STATE_UNKNOWN,
					     _("Could not realloc() units [%d]\n"), nresponses);
			}
			responses[nresponses] = optarg;
			nresponses++;
			break;
		case 'c':									/* critical time threshold */
			if (is_intnonneg (optarg)) {
				critical_time = atoi (optarg);
				check_critical_time = TRUE;
			}
			else {
				usage4 (_("Critical time must be a positive integer"));
			}
			break;
		case 'w':									/* warning time threshold */
			if (is_intnonneg (optarg)) {
				warning_time = atoi (optarg);
				check_warning_time = TRUE;
			}
			else {
				usage4 (_("Warning time must be a positive integer"));
			}
			break;
		case 'v':									/* verbose */
			verbose++;
			break;
		case 't':									/* timeout */
			if (is_intnonneg (optarg)) {
				socket_timeout = atoi (optarg);
			}
			else {
				usage4 (_("Time interval must be a positive integer"));
			}
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
		case 'V':									/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case '?':									/* help */
			printf (_("%s: Unknown argument: %s\n\n"), progname, optarg);
			print_usage ();
			exit (STATE_UNKNOWN);
		}
	}

	c = optind;
	if (server_address == NULL) {
		if (argv[c]) {
			if (is_host (argv[c]))
				server_address = argv[c];
			else
				usage2 (_("Invalid hostname/address"), argv[c]);
		}
		else {
			asprintf (&server_address, "127.0.0.1");
		}
	}

	if (server_expect == NULL)
		server_expect = strdup (SMTP_EXPECT);

	if (mail_command == NULL)
		mail_command = strdup("MAIL ");

	if (from_arg==NULL)
		from_arg = strdup(" ");

	return validate_arguments ();
}



int
validate_arguments (void)
{
	return OK;
}



void
print_help (void)
{
	char *myport;
	asprintf (&myport, "%d", SMTP_PORT);

	print_revision (progname, revision);

	printf ("Copyright (c) 1999-2001 Ethan Galstad <nagios@nagios.org>\n");
	printf (COPYRIGHT, copyright, email);

	printf(_("This plugin will attempt to open an SMTP connection with the host.\n\n"));

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_(UT_HOST_PORT), 'p', myport);

	printf (_(UT_IPv46));

	printf (_("\
 -e, --expect=STRING\n\
   String to expect in first line of server response (default: '%s')\n\
 -n, nocommand\n\
   Suppress SMTP command\n\
 -C, --command=STRING\n\
   SMTP command (may be used repeatedly)\n\
 -R, --command=STRING\n\
   Expected response to command (may be used repeatedly)\n\
 -f, --from=STRING\n\
   FROM-address to include in MAIL command, required by Exchange 2000\n"),
	        SMTP_EXPECT);

	printf (_(UT_WARN_CRIT));

	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);

	printf (_(UT_VERBOSE));

	printf(_("\n\
Successul connects return STATE_OK, refusals and timeouts return\n\
STATE_CRITICAL, other errors return STATE_UNKNOWN.  Successful\n\
connects, but incorrect reponse messages from the host result in\n\
STATE_WARNING return values.\n"));

	printf (_(UT_SUPPORT));
}



void
print_usage (void)
{
	printf ("\
Usage: %s -H host [-p port] [-e expect] [-C command] [-f from addr]\n\
                  [-w warn] [-c crit] [-t timeout] [-n] [-v] [-4|-6]\n", progname);
}
