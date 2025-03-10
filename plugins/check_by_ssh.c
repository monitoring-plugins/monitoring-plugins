/*****************************************************************************
 *
 * Monitoring check_by_ssh plugin
 *
 * License: GPL
 * Copyright (c) 2000-2024 Monitoring Plugins Development Team
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
const char *copyright = "2000-2024";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "utils.h"
#include "utils_cmd.h"
#include "check_by_ssh.d/config.h"
#include "states.h"

#ifndef NP_MAXARGS
#	define NP_MAXARGS 1024
#endif

typedef struct {
	int errorcode;
	check_by_ssh_config config;
} check_by_ssh_config_wrapper;
static check_by_ssh_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static check_by_ssh_config_wrapper validate_arguments(check_by_ssh_config_wrapper /*config_wrapper*/);

static command_construct comm_append(command_construct /*cmd*/, const char * /*str*/);
static void print_help(void);
void print_usage(void);

static bool verbose = false;

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_by_ssh_config_wrapper tmp_config = process_arguments(argc, argv);

	/* process arguments */
	if (tmp_config.errorcode == ERROR) {
		usage_va(_("Could not parse arguments"));
	}

	const check_by_ssh_config config = tmp_config.config;

	/* Set signal handling and alarm timeout */
	if (signal(SIGALRM, timeout_alarm_handler) == SIG_ERR) {
		usage_va(_("Cannot catch SIGALRM"));
	}
	alarm(timeout_interval);

	/* run the command */
	if (verbose) {
		printf("Command: %s\n", config.cmd.commargv[0]);
		for (int i = 1; i < config.cmd.commargc; i++) {
			printf("Argument %i: %s\n", i, config.cmd.commargv[i]);
		}
	}

	output chld_out;
	output chld_err;
	mp_state_enum result = cmd_run_array(config.cmd.commargv, &chld_out, &chld_err, 0);

	/* SSH returns 255 if connection attempt fails; include the first line of error output */
	if (result == 255 && config.unknown_timeout) {
		printf(_("SSH connection failed: %s\n"), chld_err.lines > 0 ? chld_err.line[0] : "(no error output)");
		return STATE_UNKNOWN;
	}

	if (verbose) {
		for (size_t i = 0; i < chld_out.lines; i++) {
			printf("stdout: %s\n", chld_out.line[i]);
		}
		for (size_t i = 0; i < chld_err.lines; i++) {
			printf("stderr: %s\n", chld_err.line[i]);
		}
	}

	size_t skip_stdout = 0;
	if (config.skip_stdout == -1) { /* --skip-stdout specified without argument */
		skip_stdout = chld_out.lines;
	} else {
		skip_stdout = config.skip_stdout;
	}

	size_t skip_stderr = 0;
	if (config.skip_stderr == -1) { /* --skip-stderr specified without argument */
		skip_stderr = chld_err.lines;
	} else {
		skip_stderr = config.skip_stderr;
	}

	/* UNKNOWN or worse if (non-skipped) output found on stderr */
	if (chld_err.lines > (size_t)skip_stderr) {
		printf(_("Remote command execution failed: %s\n"), chld_err.line[skip_stderr]);
		if (config.warn_on_stderr) {
			return max_state_alt(result, STATE_WARNING);
		}
		return max_state_alt(result, STATE_UNKNOWN);
	}

	/* this is simple if we're not supposed to be passive.
	 * Wrap up quickly and keep the tricks below */
	if (!config.passive) {
		if (chld_out.lines > (size_t)skip_stdout) {
			for (size_t i = skip_stdout; i < chld_out.lines; i++) {
				puts(chld_out.line[i]);
			}
		} else {
			printf(_("%s - check_by_ssh: Remote command '%s' returned status %d\n"), state_text(result), config.remotecmd, result);
		}
		return result; /* return error status from remote command */
	}

	/*
	 * Passive mode
	 */

	/* process output */
	FILE *file_pointer = NULL;
	if (!(file_pointer = fopen(config.outputfile, "a"))) {
		printf(_("SSH WARNING: could not open %s\n"), config.outputfile);
		exit(STATE_UNKNOWN);
	}

	time_t local_time = time(NULL);
	unsigned int commands = 0;
	char *status_text;
	int cresult;
	for (size_t i = skip_stdout; i < chld_out.lines; i++) {
		status_text = chld_out.line[i++];
		if (i == chld_out.lines || strstr(chld_out.line[i], "STATUS CODE: ") == NULL) {
			die(STATE_UNKNOWN, _("%s: Error parsing output\n"), progname);
		}

		if (config.service[commands] && status_text && sscanf(chld_out.line[i], "STATUS CODE: %d", &cresult) == 1) {
			fprintf(file_pointer, "[%d] PROCESS_SERVICE_CHECK_RESULT;%s;%s;%d;%s\n", (int)local_time, config.host_shortname,
					config.service[commands++], cresult, status_text);
		}
	}

	/* Multiple commands and passive checking should always return OK */
	exit(result);
}

/* process command-line arguments */
check_by_ssh_config_wrapper process_arguments(int argc, char **argv) {
	static struct option longopts[] = {{"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'h'},
									   {"verbose", no_argument, 0, 'v'},
									   {"fork", no_argument, 0, 'f'},
									   {"timeout", required_argument, 0, 't'},
									   {"unknown-timeout", no_argument, 0, 'U'},
									   {"host", required_argument, 0, 'H'}, /* backward compatibility */
									   {"hostname", required_argument, 0, 'H'},
									   {"port", required_argument, 0, 'p'},
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
									   {"warn-on-stderr", no_argument, 0, 'W'},
									   {"proto1", no_argument, 0, '1'},
									   {"proto2", no_argument, 0, '2'},
									   {"use-ipv4", no_argument, 0, '4'},
									   {"use-ipv6", no_argument, 0, '6'},
									   {"ssh-option", required_argument, 0, 'o'},
									   {"quiet", no_argument, 0, 'q'},
									   {"configfile", optional_argument, 0, 'F'},
									   {0, 0, 0, 0}};

	check_by_ssh_config_wrapper result = {
		.errorcode = OK,
		.config = check_by_ssh_config_init(),
	};

	if (argc < 2) {
		result.errorcode = ERROR;
		return result;
	}

	for (int index = 1; index < argc; index++) {
		if (strcmp("-to", argv[index]) == 0) {
			strcpy(argv[index], "-t");
		}
	}

	result.config.cmd = comm_append(result.config.cmd, SSH_COMMAND);

	int option = 0;
	while (true) {
		int opt_index = getopt_long(argc, argv, "Vvh1246fqt:UH:O:p:i:u:l:C:S::E::n:s:o:F:", longopts, &option);

		if (opt_index == -1 || opt_index == EOF) {
			break;
		}

		switch (opt_index) {
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case 'v': /* help */
			verbose = true;
			break;
		case 't': /* timeout period */
			if (!is_integer(optarg)) {
				usage_va(_("Timeout interval must be a positive integer"));
			} else {
				timeout_interval = atoi(optarg);
			}
			break;
		case 'U':
			result.config.unknown_timeout = true;
			break;
		case 'H': /* host */
			result.config.hostname = optarg;
			break;
		case 'p': /* port number */
			if (!is_integer(optarg)) {
				usage_va(_("Port must be a positive integer"));
			}
			result.config.cmd = comm_append(result.config.cmd, "-p");
			result.config.cmd = comm_append(result.config.cmd, optarg);
			break;
		case 'O': /* output file */
			result.config.outputfile = optarg;
			result.config.passive = true;
			break;
		case 's': /* description of service to check */ {
			char *p1;
			char *p2;

			p1 = optarg;
			result.config.service = realloc(result.config.service, (++result.config.number_of_services) * sizeof(char *));
			while ((p2 = index(p1, ':'))) {
				*p2 = '\0';
				result.config.service[result.config.number_of_services - 1] = p1;
				result.config.service = realloc(result.config.service, (++result.config.number_of_services) * sizeof(char *));
				p1 = p2 + 1;
			}
			result.config.service[result.config.number_of_services - 1] = p1;
			break;
		case 'n': /* short name of host in the monitoring configuration */
			result.config.host_shortname = optarg;
		} break;
		case 'u':
			result.config.cmd = comm_append(result.config.cmd, "-l");
			result.config.cmd = comm_append(result.config.cmd, optarg);
			break;
		case 'l': /* login name */
			result.config.cmd = comm_append(result.config.cmd, "-l");
			result.config.cmd = comm_append(result.config.cmd, optarg);
			break;
		case 'i': /* identity */
			result.config.cmd = comm_append(result.config.cmd, "-i");
			result.config.cmd = comm_append(result.config.cmd, optarg);
			break;

		case '1': /* Pass these switches directly to ssh */
			result.config.cmd = comm_append(result.config.cmd, "-1");
			break;
		case '2': /* 1 to force version 1, 2 to force version 2 */
			result.config.cmd = comm_append(result.config.cmd, "-2");
			break;
		case '4': /* -4 for IPv4 */
			result.config.cmd = comm_append(result.config.cmd, "-4");
			break;
		case '6': /* -6 for IPv6 */
			result.config.cmd = comm_append(result.config.cmd, "-6");
			break;
		case 'f': /* fork to background */
			result.config.cmd = comm_append(result.config.cmd, "-f");
			break;
		case 'C': /* Command for remote machine */
			result.config.commands++;
			if (result.config.commands > 1) {
				xasprintf(&result.config.remotecmd, "%s;echo STATUS CODE: $?;", result.config.remotecmd);
			}
			xasprintf(&result.config.remotecmd, "%s%s", result.config.remotecmd, optarg);
			break;
		case 'S': /* skip n (or all) lines on stdout */
			if (optarg == NULL) {
				result.config.skip_stdout = -1; /* skip all output on stdout */
			} else if (!is_integer(optarg)) {
				usage_va(_("skip-stdout argument must be an integer"));
			} else {
				result.config.skip_stdout = atoi(optarg);
			}
			break;
		case 'E': /* skip n (or all) lines on stderr */
			if (optarg == NULL) {
				result.config.skip_stderr = -1; /* skip all output on stderr */
			} else if (!is_integer(optarg)) {
				usage_va(_("skip-stderr argument must be an integer"));
			} else {
				result.config.skip_stderr = atoi(optarg);
			}
			break;
		case 'W': /* exit with warning if there is an output on stderr */
			result.config.warn_on_stderr = true;
			break;
		case 'o': /* Extra options for the ssh command */
			result.config.cmd = comm_append(result.config.cmd, "-o");
			result.config.cmd = comm_append(result.config.cmd, optarg);
			break;
		case 'q': /* Tell the ssh command to be quiet */
			result.config.cmd = comm_append(result.config.cmd, "-q");
			break;
		case 'F': /* ssh configfile */
			result.config.cmd = comm_append(result.config.cmd, "-F");
			result.config.cmd = comm_append(result.config.cmd, optarg);
			break;
		default: /* help */
			usage5();
		}
	}

	int c = optind;
	if (result.config.hostname == NULL) {
		if (c <= argc) {
			die(STATE_UNKNOWN, _("%s: You must provide a host name\n"), progname);
		}
		result.config.hostname = argv[c++];
	}

	if (strlen(result.config.remotecmd) == 0) {
		for (; c < argc; c++) {
			if (strlen(result.config.remotecmd) > 0) {
				xasprintf(&result.config.remotecmd, "%s %s", result.config.remotecmd, argv[c]);
			} else {
				xasprintf(&result.config.remotecmd, "%s", argv[c]);
			}
		}
	}

	if (result.config.commands > 1 || result.config.passive) {
		xasprintf(&result.config.remotecmd, "%s;echo STATUS CODE: $?;", result.config.remotecmd);
	}

	if (result.config.remotecmd == NULL || strlen(result.config.remotecmd) <= 1) {
		usage_va(_("No remotecmd"));
	}

	result.config.cmd = comm_append(result.config.cmd, result.config.hostname);
	result.config.cmd = comm_append(result.config.cmd, result.config.remotecmd);

	return validate_arguments(result);
}

command_construct comm_append(command_construct cmd, const char *str) {

	if (verbose) {
		for (int i = 0; i < cmd.commargc; i++) {
			printf("Current command: [%i] %s\n", i, cmd.commargv[i]);
		}

		printf("Appending: %s\n", str);
	}

	if (++cmd.commargc > NP_MAXARGS) {
		die(STATE_UNKNOWN, _("%s: Argument limit of %d exceeded\n"), progname, NP_MAXARGS);
	}

	if ((cmd.commargv = (char **)realloc(cmd.commargv, (cmd.commargc + 1) * sizeof(char *))) == NULL) {
		die(STATE_UNKNOWN, _("Can not (re)allocate 'commargv' buffer\n"));
	}

	cmd.commargv[cmd.commargc - 1] = strdup(str);
	cmd.commargv[cmd.commargc] = NULL;

	return cmd;
}

check_by_ssh_config_wrapper validate_arguments(check_by_ssh_config_wrapper config_wrapper) {
	if (config_wrapper.config.remotecmd == NULL || config_wrapper.config.hostname == NULL) {
		config_wrapper.errorcode = ERROR;
		return config_wrapper;
	}

	if (config_wrapper.config.passive && config_wrapper.config.commands != config_wrapper.config.number_of_services) {
		die(STATE_UNKNOWN, _("%s: In passive mode, you must provide a service name for each command.\n"), progname);
	}

	if (config_wrapper.config.passive && config_wrapper.config.host_shortname == NULL) {
		die(STATE_UNKNOWN, _("%s: In passive mode, you must provide the host short name from the monitoring configs.\n"), progname);
	}

	return config_wrapper;
}

void print_help(void) {
	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Karl DeBisschop <kdebisschop@users.sourceforge.net>\n");
	printf(COPYRIGHT, copyright, email);

	printf(_("This plugin uses SSH to execute commands on a remote host"));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);

	printf(UT_EXTRA_OPTS);

	printf(UT_HOST_PORT, 'p', "none");

	printf(UT_IPv46);

	printf(" %s\n", "-1, --proto1");
	printf("    %s\n", _("tell ssh to use Protocol 1 [optional]"));
	printf(" %s\n", "-2, --proto2");
	printf("    %s\n", _("tell ssh to use Protocol 2 [optional]"));
	printf(" %s\n", "-S, --skip-stdout[=n]");
	printf("    %s\n", _("Ignore all or (if specified) first n lines on STDOUT [optional]"));
	printf(" %s\n", "-E, --skip-stderr[=n]");
	printf("    %s\n", _("Ignore all or (if specified) first n lines on STDERR [optional]"));
	printf(" %s\n", "-W, --warn-on-stderr]");
	printf("    %s\n", _("Exit with an warning, if there is an output on STDERR"));
	printf(" %s\n", "-f");
	printf("    %s\n", _("tells ssh to fork rather than create a tty [optional]. This will always return OK if ssh is executed"));
	printf(" %s\n", "-C, --command='COMMAND STRING'");
	printf("    %s\n", _("command to execute on the remote machine"));
	printf(" %s\n", "-l, --logname=USERNAME");
	printf("    %s\n", _("SSH user name on remote host [optional]"));
	printf(" %s\n", "-i, --identity=KEYFILE");
	printf("    %s\n", _("identity of an authorized key [optional]"));
	printf(" %s\n", "-O, --output=FILE");
	printf("    %s\n", _("external command file for monitoring [optional]"));
	printf(" %s\n", "-s, --services=LIST");
	printf("    %s\n", _("list of monitoring service names, separated by ':' [optional]"));
	printf(" %s\n", "-n, --name=NAME");
	printf("    %s\n", _("short name of host in the monitoring configuration [optional]"));
	printf(" %s\n", "-o, --ssh-option=OPTION");
	printf("    %s\n", _("Call ssh with '-o OPTION' (may be used multiple times) [optional]"));
	printf(" %s\n", "-F, --configfile");
	printf("    %s\n", _("Tell ssh to use this configfile [optional]"));
	printf(" %s\n", "-q, --quiet");
	printf("    %s\n", _("Tell ssh to suppress warning and diagnostic messages [optional]"));
	printf(UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);
	printf(" %s\n", "-U, --unknown-timeout");
	printf("    %s\n", _("Make connection problems return UNKNOWN instead of CRITICAL"));
	printf(UT_VERBOSE);
	printf("\n");
	printf(" %s\n", _("The most common mode of use is to refer to a local identity file with"));
	printf(" %s\n", _("the '-i' option. In this mode, the identity pair should have a null"));
	printf(" %s\n", _("passphrase and the public key should be listed in the authorized_keys"));
	printf(" %s\n", _("file of the remote host. Usually the key will be restricted to running"));
	printf(" %s\n", _("only one command on the remote server. If the remote SSH server tracks"));
	printf(" %s\n", _("invocation arguments, the one remote program may be an agent that can"));
	printf(" %s\n", _("execute additional commands as proxy"));
	printf("\n");
	printf(" %s\n", _("To use passive mode, provide multiple '-C' options, and provide"));
	printf(" %s\n", _("all of -O, -s, and -n options (servicelist order must match '-C'options)"));
	printf("\n");
	printf("%s\n", _("Examples:"));
	printf(" %s\n", "$ check_by_ssh -H localhost -n lh -s c1:c2:c3 -C uptime -C uptime -C uptime -O /tmp/foo");
	printf(" %s\n", "$ cat /tmp/foo");
	printf(" %s\n", "[1080933700] PROCESS_SERVICE_CHECK_RESULT;flint;c1;0; up 2 days");
	printf(" %s\n", "[1080933700] PROCESS_SERVICE_CHECK_RESULT;flint;c2;0; up 2 days");
	printf(" %s\n", "[1080933700] PROCESS_SERVICE_CHECK_RESULT;flint;c3;0; up 2 days");

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf(" %s -H <host> -C <command> [-fqvU] [-1|-2] [-4|-6]\n"
		   "       [-S [lines]] [-E [lines]] [-W] [-t timeout] [-i identity]\n"
		   "       [-l user] [-n name] [-s servicelist] [-O outputfile]\n"
		   "       [-p port] [-o ssh-option] [-F configfile]\n",
		   progname);
}
