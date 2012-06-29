/*****************************************************************************
* 
* Nagios check_by_ssh plugin
* 
* License: GPL
* Copyright (c) 2000-2008 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_by_ssh plugin
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

const char *progname = "check_by_ssh";
const char *copyright = "2000-2008";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "utils.h"
#include "netutils.h"
#include "utils_cmd.h"

#ifndef NP_MAXARGS
#define NP_MAXARGS 1024
#endif

int process_arguments (int, char **);
int validate_arguments (void);
void comm_append (const char *);
void print_help (void);
void print_usage (void);

unsigned int commands = 0;
unsigned int services = 0;
int skip_stdout = 0;
int skip_stderr = 0;
char *remotecmd = NULL;
char **commargv = NULL;
int commargc = 0;
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
	output chld_out, chld_err;

	remotecmd = "";
	comm_append(SSH_COMMAND);

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	/* process arguments */
	if (process_arguments (argc, argv) == ERROR)
		usage_va(_("Could not parse arguments"));

	/* Set signal handling and alarm timeout */
	if (signal (SIGALRM, timeout_alarm_handler) == SIG_ERR) {
		usage_va(_("Cannot catch SIGALRM"));
	}
	alarm (timeout_interval);

	/* run the command */
	if (verbose) {
		printf ("Command: %s\n", commargv[0]);
		for (i=1; i<commargc; i++)
			printf ("Argument %i: %s\n", i, commargv[i]);
	}

	result = cmd_run_array (commargv, &chld_out, &chld_err, 0);

	if (skip_stdout == -1) /* --skip-stdout specified without argument */
		skip_stdout = chld_out.lines;
	if (skip_stderr == -1) /* --skip-stderr specified without argument */
		skip_stderr = chld_err.lines;

	/* UNKNOWN or worse if (non-skipped) output found on stderr */
	if(chld_err.lines > skip_stderr) {
		printf (_("Remote command execution failed: %s\n"),
		        chld_err.line[skip_stderr]);
		return max_state_alt(result, STATE_UNKNOWN);
	}

	/* this is simple if we're not supposed to be passive.
	 * Wrap up quickly and keep the tricks below */
	if(!passive) {
		if (chld_out.lines > skip_stdout)
			for (i = skip_stdout; i < chld_out.lines; i++)
				puts (chld_out.line[i]);
		else
			printf (_("%s - check_by_ssh: Remote command '%s' returned status %d\n"),
			        state_text(result), remotecmd, result);
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
	for(i = skip_stdout; i < chld_out.lines; i++) {
		status_text = chld_out.line[i++];
		if (i == chld_out.lines || strstr (chld_out.line[i], "STATUS CODE: ") == NULL)
			die (STATE_UNKNOWN, _("%s: Error parsing output\n"), progname);

		if (service[commands] && status_text
			&& sscanf (chld_out.line[i], "STATUS CODE: %d", &cresult) == 1)
		{
			fprintf (fp, "[%d] PROCESS_SERVICE_CHECK_RESULT;%s;%s;%d;%s\n",
			         (int) local_time, host_shortname, service[commands++],
			         cresult, status_text);
		}
	}
	
	/* Multiple commands and passive checking should always return OK */
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
		{"skip", optional_argument, 0, 'S'}, /* backwards compatibility */
		{"skip-stdout", optional_argument, 0, 'S'},
		{"skip-stderr", optional_argument, 0, 'E'},
		{"proto1", no_argument, 0, '1'},
		{"proto2", no_argument, 0, '2'},
		{"use-ipv4", no_argument, 0, '4'},
		{"use-ipv6", no_argument, 0, '6'},
		{"ssh-option", required_argument, 0, 'o'},
		{"quiet", no_argument, 0, 'q'},
		{"configfile", optional_argument, 0, 'F'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++)
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");

	while (1) {
		c = getopt_long (argc, argv, "Vvh1246fqt:H:O:p:i:u:l:C:S::E::n:s:o:F:", longopts,
		                 &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'V':									/* version */
			print_revision (progname, NP_VERSION);
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
			comm_append("-p");
			comm_append(optarg);
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
			comm_append("-l");
			comm_append(optarg);
			break;
		case 'l':									/* login name */
			comm_append("-l");
			comm_append(optarg);
			break;
		case 'i':									/* identity */
			comm_append("-i");
			comm_append(optarg);
			break;

		case '1':									/* Pass these switches directly to ssh */
			comm_append("-1");
			break;
		case '2':									/* 1 to force version 1, 2 to force version 2 */
			comm_append("-2");
			break;
		case '4':									/* -4 for IPv4 */
			comm_append("-4");
			break;
		case '6': 								/* -6 for IPv6 */
			comm_append("-6");
			break;
		case 'f':									/* fork to background */
			comm_append("-f");
			break;
		case 'C':									/* Command for remote machine */
			commands++;
			if (commands > 1)
				xasprintf (&remotecmd, "%s;echo STATUS CODE: $?;", remotecmd);
			xasprintf (&remotecmd, "%s%s", remotecmd, optarg);
			break;
		case 'S':									/* skip n (or all) lines on stdout */
			if (optarg == NULL)
				skip_stdout = -1; /* skip all output on stdout */
			else if (!is_integer (optarg))
				usage_va(_("skip-stdout argument must be an integer"));
			else
				skip_stdout = atoi (optarg);
			break;
		case 'E':									/* skip n (or all) lines on stderr */
			if (optarg == NULL)
				skip_stderr = -1; /* skip all output on stderr */
			else if (!is_integer (optarg))
				usage_va(_("skip-stderr argument must be an integer"));
			else
				skip_stderr = atoi (optarg);
			break;
		case 'o':									/* Extra options for the ssh command */
			comm_append("-o");
			comm_append(optarg);
			break;
		case 'q':									/* Tell the ssh command to be quiet */
			comm_append("-q");
			break;
		case 'F': 									/* ssh configfile */
			comm_append("-F");
			comm_append(optarg);
			break;
		default:									/* help */
			usage5();
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
				xasprintf (&remotecmd, "%s %s", remotecmd, argv[c]);
			else
				xasprintf (&remotecmd, "%s", argv[c]);
	}

	if (commands > 1 || passive)
		xasprintf (&remotecmd, "%s;echo STATUS CODE: $?;", remotecmd);

	if (remotecmd == NULL || strlen (remotecmd) <= 1)
		usage_va(_("No remotecmd"));

	comm_append(hostname);
	comm_append(remotecmd);

	return validate_arguments ();
}


void
comm_append (const char *str)
{

	if (++commargc > NP_MAXARGS)
		die(STATE_UNKNOWN, _("%s: Argument limit of %d exceeded\n"), progname, NP_MAXARGS);

	if ((commargv = (char **)realloc(commargv, (commargc+1) * sizeof(char *))) == NULL)
		die(STATE_UNKNOWN, _("Can not (re)allocate 'commargv' buffer\n"));

	commargv[commargc-1] = strdup(str);
	commargv[commargc] = NULL;

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
	print_revision (progname, NP_VERSION);

	printf ("Copyright (c) 1999 Karl DeBisschop <kdebisschop@users.sourceforge.net>\n");
	printf (COPYRIGHT, copyright, email);

	printf (_("This plugin uses SSH to execute commands on a remote host"));

  printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);

	printf (UT_EXTRA_OPTS);

	printf (UT_HOST_PORT, 'p', "none");

	printf (UT_IPv46);

  printf (" %s\n", "-1, --proto1");
  printf ("    %s\n", _("tell ssh to use Protocol 1 [optional]"));
  printf (" %s\n", "-2, --proto2");
  printf ("    %s\n", _("tell ssh to use Protocol 2 [optional]"));
  printf (" %s\n", "-S, --skip-stdout[=n]");
  printf ("    %s\n", _("Ignore all or (if specified) first n lines on STDOUT [optional]"));
  printf (" %s\n", "-E, --skip-stderr[=n]");
  printf ("    %s\n", _("Ignore all or (if specified) first n lines on STDERR [optional]"));
  printf (" %s\n", "-f");
  printf ("    %s\n", _("tells ssh to fork rather than create a tty [optional]. This will always return OK if ssh is executed"));
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
  printf (" %s\n","-F, --configfile");
  printf ("    %s\n", _("Tell ssh to use this configfile [optional]"));
  printf (" %s\n","-q, --quiet");
  printf ("    %s\n", _("Tell ssh to suppress warning and diagnostic messages [optional]"));
	printf (UT_WARN_CRIT);
	printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);
	printf (UT_VERBOSE);
	printf("\n");
  printf (" %s\n", _("The most common mode of use is to refer to a local identity file with"));
  printf (" %s\n", _("the '-i' option. In this mode, the identity pair should have a null"));
  printf (" %s\n", _("passphrase and the public key should be listed in the authorized_keys"));
  printf (" %s\n", _("file of the remote host. Usually the key will be restricted to running"));
  printf (" %s\n", _("only one command on the remote server. If the remote SSH server tracks"));
  printf (" %s\n", _("invocation arguments, the one remote program may be an agent that can"));
  printf (" %s\n", _("execute additional commands as proxy"));
  printf("\n");
  printf (" %s\n", _("To use passive mode, provide multiple '-C' options, and provide"));
  printf (" %s\n", _("all of -O, -s, and -n options (servicelist order must match '-C'options)"));
  printf ("\n");
  printf ("%s\n", _("Examples:"));
  printf (" %s\n", "$ check_by_ssh -H localhost -n lh -s c1:c2:c3 -C uptime -C uptime -C uptime -O /tmp/foo");
  printf (" %s\n", "$ cat /tmp/foo");
  printf (" %s\n", "[1080933700] PROCESS_SERVICE_CHECK_RESULT;flint;c1;0; up 2 days");
  printf (" %s\n", "[1080933700] PROCESS_SERVICE_CHECK_RESULT;flint;c2;0; up 2 days");
  printf (" %s\n", "[1080933700] PROCESS_SERVICE_CHECK_RESULT;flint;c3;0; up 2 days");

	printf(UT_SUPPORT);
}



void
print_usage (void)
{
	printf ("%s\n", _("Usage:"));
	printf (" %s -H <host> -C <command> [-fqv] [-1|-2] [-4|-6]\n"
	        "       [-S [lines]] [-E [lines]] [-t timeout] [-i identity]\n"
	        "       [-l user] [-n name] [-s servicelist] [-O outputfile]\n"
	        "       [-p port] [-o ssh-option] [-F configfile]\n",
	        progname);
}
