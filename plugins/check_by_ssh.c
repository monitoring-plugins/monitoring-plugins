/******************************************************************************

 The Nagios Plugins are free software; you can redistribute them
 and/or modify them under the terms of the GNU General Public
 License as published by the Free Software Foundation; either
 version 2 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 *****************************************************************************/
 
const char *progname = "check_by_ssh";
const char *revision = "$Revision$";
const char *copyright = "2000-2003";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"
#include "popen.h"

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

int commands = 0;
int services = 0;
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

	char input_buffer[MAX_INPUT_BUFFER];
	char *result_text;
	char *status_text;
	char *output;
	char *eol = NULL;
	int cresult;
	int result = STATE_UNKNOWN;
	time_t local_time;
	FILE *fp = NULL;

	remotecmd = strdup ("");
	comm = strdup (SSH_COMMAND);
	result_text = strdup ("");

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* process arguments */
	if (process_arguments (argc, argv) == ERROR)
		usage (_("Could not parse arguments\n"));


	/* Set signal handling and alarm timeout */
	if (signal (SIGALRM, popen_timeout_alarm_handler) == SIG_ERR) {
		printf (_("Cannot catch SIGALRM"));
		return STATE_UNKNOWN;
	}
	alarm (timeout_interval);


	/* run the command */

	if (verbose)
		printf ("%s\n", comm);

	child_process = spopen (comm);

	if (child_process == NULL) {
		printf (_("Unable to open pipe: %s"), comm);
		return STATE_UNKNOWN;
	}


	/* open STDERR  for spopen */
	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL) {
		printf (_("Could not open stderr for %s\n"), SSH_COMMAND);
	}


	/* build up results from remote command in result_text */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process))
		asprintf (&result_text, "%s%s", result_text, input_buffer);


	/* WARNING if output found on stderr */
	if (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr)) {
		printf ("%s\n", input_buffer);
		while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process))
			printf ("%s\n", input_buffer);
		return STATE_WARNING;
	}
	(void) fclose (child_stderr);


	/* close the pipe */
	result = spclose (child_process);


	/* process output */
	if (passive) {

		if (!(fp = fopen (outputfile, "a"))) {
			printf (_("SSH WARNING: could not open %s\n"), outputfile);
			exit (STATE_UNKNOWN);
		}

		local_time = time (NULL);
		commands = 0;
		while (result_text && strlen(result_text) > 0) {
			status_text = strstr (result_text, "STATUS CODE: ");
			if (status_text == NULL) {
				printf ("%s", result_text);
				return result;
			}
			asprintf (&output, "%s", result_text);
			result_text = strnl (status_text);
			eol = strpbrk (output, "\r\n");
			if (eol != NULL)
				eol[0] = 0;
			if (service[commands] && status_text
					&& sscanf (status_text, "STATUS CODE: %d", &cresult) == 1) {
				fprintf (fp, _("[%d] PROCESS_SERVICE_CHECK_RESULT;%s;%s;%d;%s\n"),
								 (int) local_time, host_shortname, service[commands++], cresult,
								 output);
			}
		}

	}


	/* print the first line from the remote command */
	else {
 		eol = strpbrk (result_text, "\r\n");
 		if (eol)
 			eol[0] = 0;
 		printf ("%s\n", result_text);
	}


	/* return error status from remote command */	
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
		{"proto1", no_argument, 0, '1'},
		{"proto2", no_argument, 0, '2'},
		{"use-ipv4", no_argument, 0, '4'},
		{"use-ipv6", no_argument, 0, '6'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++)
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");

	while (1) {
		c = getopt_long (argc, argv, "Vvh1246ft:H:O:p:i:u:l:C:n:s:", longopts,
									 &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':									/* help */
			print_usage ();
			exit (STATE_UNKNOWN);
		case 'V':									/* version */
			print_revision (progname, "$Revision$");
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'v':									/* help */
			verbose = TRUE;
			break;
		case 't':									/* timeout period */
			if (!is_integer (optarg))
				usage2 (_("timeout interval must be an integer"), optarg);
			else
				timeout_interval = atoi (optarg);
			break;
		case 'H':									/* host */
			if (!is_host (optarg))
				usage2 (_("invalid host name"), optarg);
			hostname = optarg;
			break;
		case 'p': /* port number */
			if (!is_integer (optarg))
				usage2 (_("port must be an integer"), optarg);
			asprintf (&comm,"%s -p %s", comm, optarg);
			break;
		case 'O':									/* output file */
			outputfile = optarg;
			passive = TRUE;
			break;
		case 's':									/* description of service to check */
			service = realloc (service, (++services) * sizeof(char *));
			p1 = optarg;
			while ((p2 = index (p1, ':'))) {
				*p2 = '\0';
				asprintf (&service[services-1], "%s", p1);
				service = realloc (service, (++services) * sizeof(char *));
				p1 = p2 + 1;
			}
			asprintf (&service[services-1], "%s", p1);
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
		}
	}

	c = optind;
	if (hostname == NULL) {
		if (c <= argc) {
			die (STATE_UNKNOWN, _("%s: You must provide a host name\n"), progname);
		} else if (!is_host (argv[c]))
			die (STATE_UNKNOWN, _("%s: Invalid host name %s\n"), progname, argv[c]);
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
		usage (_("No remotecmd\n"));

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

	printf (_("Copyright (c) 1999 Karl DeBisschop <kdebisschop@users.sourceforge.net>\n"));
	printf (_(COPYRIGHT), copyright, email);

	printf (_("This plugin uses SSH to execute commands on a remote host\n\n"));

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_(UT_HOST_PORT), 'p', "none");

	printf (_(UT_IPv46));

	printf (_("\
 -1, --proto1\n\
    tell ssh to use Protocol 1\n\
 -2, --proto2\n\
    tell ssh to use Protocol 2\n\
 -f\n\
    tells ssh to fork rather than create a tty\n"));

	printf (_("\
 -C, --command='COMMAND STRING'\n\
    command to execute on the remote machine\n\
 -l, --logname=USERNAME\n\
    SSH user name on remote host [optional]\n\
 -i, --identity=KEYFILE\n\
    identity of an authorized key [optional]\n\
 -O, --output=FILE\n\
    external command file for nagios [optional]\n\
 -s, --services=LIST\n\
    list of nagios service names, separated by ':' [optional]\n\
 -n, --name=NAME\n\
    short name of host in nagios configuration [optional]\n"));

	printf (_(UT_WARN_CRIT));

	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);

	printf (_("\n\
The most common mode of use is to refer to a local identity file with\n\
the '-i' option. In this mode, the identity pair should have a null\n\
passphrase and the public key should be listed in the authorized_keys\n\
file of the remote host. Usually the key will be restricted to running\n\
only one command on the remote server. If the remote SSH server tracks\n\
invocation agruments, the one remote program may be an agent that can\n\
execute additional commands as proxy\n"));

	printf (_("\n\
To use passive mode, provide multiple '-C' options, and provide\n\
all of -O, -s, and -n options (servicelist order must match '-C'\n\
options)\n"));

	printf (_(UT_SUPPORT));
}





void
print_usage (void)
{
	printf (_("\n\
Usage: %s [-f46] [-t timeout] [-i identity] [-l user] -H <host> \n\
  -C <command> [-n name] [-s servicelist] [-O outputfile] [-p port]\n"),
	        progname);
	printf (_(UT_HLP_VRS), progname, progname);
}
