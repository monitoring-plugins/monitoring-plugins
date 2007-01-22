/******************************************************************************
*
* Nagios check_by_ssh plugin
*
* License: GPL
* Copyright (c) 1999-2006 nagios-plugins team
*
* Last Modified: $Date$
*
* Description:
*
* This file contains the check_by_ssh plugin
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
******************************************************************************/
 
const char *progname = "check_by_ssh";
const char *revision = "$Revision$";
const char *copyright = "2000-2006";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"
#include "runcmd.h"

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

int commands = 0;
int services = 0;
int skip = 0;
char *remotecmd = NULL;
char *comm = NULL;
char *hostname = NULL;
char *outputfile = NULL;
char *host_shortname = NULL;
char **service;
int passive = FALSE;
int verbose = FALSE;

int
main (int argc, char **argv)
{

	char *status_text;
	int cresult;
	int result = STATE_UNKNOWN;
	int i;
	time_t local_time;
	FILE *fp = NULL;
	struct output chld_out, chld_err;

	remotecmd = "";
	comm = strdup (SSH_COMMAND);

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* process arguments */
	if (process_arguments (argc, argv) == ERROR)
		usage_va(_("Could not parse arguments"));

	/* Set signal handling and alarm timeout */
	if (signal (SIGALRM, popen_timeout_alarm_handler) == SIG_ERR) {
		usage_va(_("Cannot catch SIGALRM"));
	}
	alarm (timeout_interval);

	/* run the command */
	if (verbose)
		printf ("%s\n", comm);

	result = np_runcmd(comm, &chld_out, &chld_err, 0);
	/* UNKNOWN if output found on stderr */
	if(chld_err.buflen) {
		printf(_("Remote command execution failed: %s\n"),
			   chld_err.buflen ? chld_err.buf : _("Unknown error"));
		return STATE_UNKNOWN;
	}

	/* this is simple if we're not supposed to be passive.
	 * Wrap up quickly and keep the tricks below */
	if(!passive) {
		printf ("%s\n", skip < chld_out.lines ? chld_out.line[skip] : chld_out.buf);
		return result; 	/* return error status from remote command */
	}


	/*
	 * Passive mode
	 */

	/* process output */
	if (!(fp = fopen (outputfile, "a"))) {
		printf (_("SSH WARNING: could not open %s\n"), outputfile);
		exit (STATE_UNKNOWN);
	}

	local_time = time (NULL);
	commands = 0;
	for(i = skip; chld_out.line[i]; i++) {
		status_text = strstr (chld_out.line[i], "STATUS CODE: ");
		if (status_text == NULL) {
			printf ("%s", chld_out.line[i]);
			return result;
		}
		if (service[commands] && status_text
			&& sscanf (status_text, "STATUS CODE: %d", &cresult) == 1)
		{
			fprintf (fp, "[%d] PROCESS_SERVICE_CHECK_RESULT;%s;%s;%d;%s\n",
			         (int) local_time, host_shortname, service[commands++],
			         cresult, chld_out.line[i]);
		}
	}
	
	/* force an OK state */
	return result;
}

/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;
	char *p1, *p2;

	int option = 0;
	static struct option longopts[] = {
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{"verbose", no_argument, 0, 'v'},
		{"fork", no_argument, 0, 'f'},
		{"timeout", required_argument, 0, 't'},
		{"host", required_argument, 0, 'H'},
		{"port", required_argument,0,'p'},
		{"output", required_argument, 0, 'O'},
		{"name", required_argument, 0, 'n'},
		{"services", required_argument, 0, 's'},
		{"identity", required_argument, 0, 'i'},
		{"user", required_argument, 0, 'u'},
		{"logname", required_argument, 0, 'l'},
		{"command", required_argument, 0, 'C'},
		{"skip", required_argument, 0, 'S'},
		{"proto1", no_argument, 0, '1'},
		{"proto2", no_argument, 0, '2'},
		{"use-ipv4", no_argument, 0, '4'},
		{"use-ipv6", no_argument, 0, '6'},
		{"ssh-option", required_argument, 0, 'o'},
		{"quiet", no_argument, 0, 'q'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++)
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");

	while (1) {
		c = getopt_long (argc, argv, "Vvh1246fqt:H:O:p:i:u:l:C:S:n:s:o:", longopts,
		                 &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'V':									/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'v':									/* help */
			verbose = TRUE;
			break;
		case 't':									/* timeout period */
			if (!is_integer (optarg))
				usage_va(_("Timeout interval must be a positive integer"));
			else
				timeout_interval = atoi (optarg);
			break;
		case 'H':									/* host */
			host_or_die(optarg);
			hostname = optarg;
			break;
		case 'p': /* port number */
			if (!is_integer (optarg))
				usage_va(_("Port must be a positive integer"));
			asprintf (&comm,"%s -p %s", comm, optarg);
			break;
		case 'O':									/* output file */
			outputfile = optarg;
			passive = TRUE;
			break;
		case 's':									/* description of service to check */
			p1 = optarg;
			service = realloc (service, (++services) * sizeof(char *));
			while ((p2 = index (p1, ':'))) {
				*p2 = '\0';
				service[services - 1] = p1;
				service = realloc (service, (++services) * sizeof(char *));
				p1 = p2 + 1;
			}
			service[services - 1] = p1;
			break;
		case 'n':									/* short name of host in nagios configuration */
			host_shortname = optarg;
			break;

		case 'u':
			c = 'l';
		case 'l':									/* login name */
		case 'i':									/* identity */
			asprintf (&comm, "%s -%c %s", comm, c, optarg);
			break;

		case '1':									/* Pass these switches directly to ssh */
		case '2':									/* 1 to force version 1, 2 to force version 2 */
		case '4':									/* -4 for IPv4 */
		case '6': 								/* -6 for IPv6 */
		case 'f':									/* fork to background */
			asprintf (&comm, "%s -%c", comm, c);
			break;
		case 'C':									/* Command for remote machine */
			commands++;
			if (commands > 1)
				asprintf (&remotecmd, "%s;echo STATUS CODE: $?;", remotecmd);
			asprintf (&remotecmd, "%s%s", remotecmd, optarg);
			break;
		case 'S':									/* Skip n lines in the output to ignore system banner */
			if (!is_integer (optarg))
				usage_va(_("skip lines must be an integer"));
			else
				skip = atoi (optarg);
			break;
		case 'o':									/* Extra options for the ssh command */
			asprintf (&comm, "%s -%c '%s'", comm, c, optarg);
			break;
		case 'q':									/* Tell the ssh command to be quiet */
			asprintf (&comm, "%s -%c", comm, c);
			break;
		default:									/* help */
			usage_va(_("Unknown argument - %s"), optarg);
		}
	}

	c = optind;
	if (hostname == NULL) {
		if (c <= argc) {
			die (STATE_UNKNOWN, _("%s: You must provide a host name\n"), progname);
		}
		host_or_die(argv[c]);
		hostname = argv[c++];
	}

	if (strlen(remotecmd) == 0) {
		for (; c < argc; c++)
			if (strlen(remotecmd) > 0)
				asprintf (&remotecmd, "%s %s", remotecmd, argv[c]);
			else
				asprintf (&remotecmd, "%s", argv[c]);
	}

	if (commands > 1)
		asprintf (&remotecmd, "%s;echo STATUS CODE: $?;", remotecmd);

	if (remotecmd == NULL || strlen (remotecmd) <= 1)
		usage_va(_("No remotecmd"));

	asprintf (&comm, "%s %s '%s'", comm, hostname, remotecmd);

	return validate_arguments ();
}



int
validate_arguments (void)
{
	if (remotecmd == NULL || hostname == NULL)
		return ERROR;

	if (passive && commands != services)
		die (STATE_UNKNOWN, _("%s: In passive mode, you must provide a service name for each command.\n"), progname);

	if (passive && host_shortname == NULL)
		die (STATE_UNKNOWN, _("%s: In passive mode, you must provide the host short name from the nagios configs.\n"), progname);

	return OK;
}


void
print_help (void)
{
	print_revision (progname, revision);

	printf ("Copyright (c) 1999 Karl DeBisschop <kdebisschop@users.sourceforge.net>\n");
	printf (COPYRIGHT, copyright, email);

	printf (_("This plugin uses SSH to execute commands on a remote host"));

  printf ("\n\n");
  
	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_(UT_HOST_PORT), 'p', "none");

	printf (_(UT_IPv46));

  printf (" %s\n", "-1, --proto1");
  printf ("    %s\n", _("tell ssh to use Protocol 1"));
  printf (" %s\n", "-2, --proto2");
  printf ("    %s\n", _("tell ssh to use Protocol 2"));
  printf (" %s\n", "-S, --skiplines=n");
  printf ("    %s\n", _("Ignore first n lines on STDERR (to suppress a logon banner)"));
  printf (" %s\n", "-f");
  printf ("    %s\n", _("tells ssh to fork rather than create a tty"));
  printf (" %s\n","-C, --command='COMMAND STRING'");
  printf ("    %s\n", _("command to execute on the remote machine"));
  printf (" %s\n","-l, --logname=USERNAME");
  printf ("    %s\n", _("SSH user name on remote host [optional]"));
  printf (" %s\n","-i, --identity=KEYFILE");
  printf ("    %s\n", _("identity of an authorized key [optional]"));
  printf (" %s\n","-O, --output=FILE");
  printf ("    %s\n", _("external command file for nagios [optional]"));
  printf (" %s\n","-s, --services=LIST");
  printf ("    %s\n", _("list of nagios service names, separated by ':' [optional]"));
  printf (" %s\n","-n, --name=NAME");
  printf ("    %s\n", _("short name of host in nagios configuration [optional]"));
  printf (" %s\n","-o, --ssh-option=OPTION");
  printf ("    %s\n", _("Call ssh with '-o OPTION' (may be used multiple times) [optional]"));
  printf (" %s\n","-q, --quiet");
  printf ("    %s\n", _("Tell ssh to suppress warning and diagnostic messages [optional]"));
	printf (_(UT_WARN_CRIT));
	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);
  printf (" %s\n", _("The most common mode of use is to refer to a local identity file with"));
  printf (" %s\n", _("the '-i' option. In this mode, the identity pair should have a null"));
  printf (" %s\n", _("passphrase and the public key should be listed in the authorized_keys"));
  printf (" %s\n", _("file of the remote host. Usually the key will be restricted to running"));
  printf (" %s\n", _("only one command on the remote server. If the remote SSH server tracks"));
  printf (" %s\n", _("invocation arguments, the one remote program may be an agent that can"));
  printf (" %s\n", _("execute additional commands as proxy"));
  printf (" %s\n", _("To use passive mode, provide multiple '-C' options, and provide"));
  printf (" %s\n", _("all of -O, -s, and -n options (servicelist order must match '-C'options)"));
  printf ("\n");
  printf ("%s\n", _("Examples:"));
  printf (" %s\n", "$ check_by_ssh -H localhost -n lh -s c1:c2:c3 -C uptime -C uptime -C uptime -O /tmp/foo");
  printf (" %s\n", "$ cat /tmp/foo");
  printf (" %s\n", "[1080933700] PROCESS_SERVICE_CHECK_RESULT;flint;c1;0; up 2 days");
  printf (" %s\n", "[1080933700] PROCESS_SERVICE_CHECK_RESULT;flint;c2;0; up 2 days");
  printf (" %s\n", "[1080933700] PROCESS_SERVICE_CHECK_RESULT;flint;c3;0; up 2 days");
	printf (_(UT_SUPPORT));
}



void
print_usage (void)
{
	printf (_("Usage:"));
  printf(" %s [-fq46] [-t timeout] [-i identity] [-l user] -H <host> -C <command>",progname);
  printf(" [-n name] [-s servicelist] [-O outputfile] [-p port] [-o ssh-option]\n");
}
