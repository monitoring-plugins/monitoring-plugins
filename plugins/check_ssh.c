/*****************************************************************************
 *
 * Monitoring check_ssh plugin
 *
 * License: GPL
 * Copyright (c) 2000-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_ssh plugin
 *
 * Try to connect to an SSH server at specified server and port
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

#include "output.h"
#include "perfdata.h"
#include "states.h"
const char *progname = "check_ssh";
const char *copyright = "2000-2024";
const char *email = "devel@monitoring-plugins.org";

#include "./common.h"
#include "./netutils.h"
#include "utils.h"
#include "./check_ssh.d/config.h"

#ifndef MSG_DONTWAIT
#	define MSG_DONTWAIT 0
#endif

#define BUFF_SZ 256

static bool verbose = false;

typedef struct process_arguments_wrapper {
	int errorcode;
	check_ssh_config config;
} process_arguments_wrapper;

static process_arguments_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static void print_help(void);
void print_usage(void);

static int ssh_connect(mp_check *overall, char *haddr, int hport, char *remote_version,
					   char *remote_protocol);

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	process_arguments_wrapper tmp_config = process_arguments(argc, argv);

	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	check_ssh_config config = tmp_config.config;

	mp_check overall = mp_check_init();
	if (config.output_format_is_set) {
		mp_set_format(config.output_format);
	}

	/* initialize alarm signal handling */
	signal(SIGALRM, socket_timeout_alarm_handler);
	alarm(socket_timeout);

	/* ssh_connect exits if error is found */
	ssh_connect(&overall, config.server_name, config.port, config.remote_version,
				config.remote_protocol);

	alarm(0);

	mp_exit(overall);
}

#define output_format_index CHAR_MAX + 1

/* process command-line arguments */
process_arguments_wrapper process_arguments(int argc, char **argv) {
	static struct option longopts[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"host", required_argument, 0, 'H'}, /* backward compatibility */
		{"hostname", required_argument, 0, 'H'},
		{"port", required_argument, 0, 'p'},
		{"use-ipv4", no_argument, 0, '4'},
		{"use-ipv6", no_argument, 0, '6'},
		{"timeout", required_argument, 0, 't'},
		{"verbose", no_argument, 0, 'v'},
		{"remote-version", required_argument, 0, 'r'},
		{"remote-protocol", required_argument, 0, 'P'},
		{"output-format", required_argument, 0, output_format_index},
		{0, 0, 0, 0}};

	process_arguments_wrapper result = {
		.config = check_ssh_config_init(),
		.errorcode = OK,
	};

	if (argc < 2) {
		result.errorcode = ERROR;
		return result;
	}

	for (int i = 1; i < argc; i++) {
		if (strcmp("-to", argv[i]) == 0) {
			strcpy(argv[i], "-t");
		}
	}

	int option_char;
	while (true) {
		int option = 0;
		option_char = getopt_long(argc, argv, "+Vhv46t:r:H:p:P:", longopts, &option);

		if (option_char == -1 || option_char == EOF) {
			break;
		}

		switch (option_char) {
		case '?': /* help */
			usage5();
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case 'v': /* verbose */
			verbose = true;
			break;
		case 't': /* timeout period */
			if (!is_intpos(optarg)) {
				usage2(_("Timeout interval must be a positive integer"), optarg);
			} else {
				socket_timeout = (unsigned int)atoi(optarg);
			}
			break;
		case '4':
			address_family = AF_INET;
			break;
		case '6':
#ifdef USE_IPV6
			address_family = AF_INET6;
#else
			usage4(_("IPv6 support not available"));
#endif
			break;
		case 'r': /* remote version */
			result.config.remote_version = optarg;
			break;
		case 'P': /* remote version */
			result.config.remote_protocol = optarg;
			break;
		case 'H': /* host */
			if (!is_host(optarg)) {
				usage2(_("Invalid hostname/address"), optarg);
			}
			result.config.server_name = optarg;
			break;
		case 'p': /* port */
			if (is_intpos(optarg)) {
				result.config.port = atoi(optarg);
			} else {
				usage2(_("Port number must be a positive integer"), optarg);
			}
			break;
		case output_format_index: {
			parsed_output_format parser = mp_parse_output_format(optarg);
			if (!parser.parsing_success) {
				// TODO List all available formats here, maybe add anothoer usage function
				printf("Invalid output format: %s\n", optarg);
				exit(STATE_UNKNOWN);
			}

			result.config.output_format_is_set = true;
			result.config.output_format = parser.output_format;
			break;
		}
		}
	}

	option_char = optind;
	if (result.config.server_name == NULL && option_char < argc) {
		if (is_host(argv[option_char])) {
			result.config.server_name = argv[option_char++];
		}
	}

	if (result.config.port == -1 && option_char < argc) {
		if (is_intpos(argv[option_char])) {
			result.config.port = atoi(argv[option_char++]);
		} else {
			print_usage();
			exit(STATE_UNKNOWN);
		}
	}

	if (result.config.server_name == NULL) {
		result.errorcode = ERROR;
		return result;
	}

	return result;
}

/************************************************************************
 *
 * Try to connect to SSH server at specified server and port
 *
 *-----------------------------------------------------------------------*/

int ssh_connect(mp_check *overall, char *haddr, int hport, char *desired_remote_version,
				char *desired_remote_protocol) {
	struct timeval tv;
	gettimeofday(&tv, NULL);

	int socket;
	int result = my_tcp_connect(haddr, hport, &socket);

	mp_subcheck connection_sc = mp_subcheck_init();
	if (result != STATE_OK) {
		connection_sc = mp_set_subcheck_state(connection_sc, STATE_CRITICAL);
		xasprintf(&connection_sc.output,
				  "Failed to establish TCP connection to Host %s and Port %d", haddr, hport);
		mp_add_subcheck_to_check(overall, connection_sc);
		return result;
	}

	char *output = (char *)calloc(BUFF_SZ + 1, sizeof(char));
	char *buffer = NULL;
	ssize_t recv_ret = 0;
	char *version_control_string = NULL;
	size_t byte_offset = 0;
	while ((version_control_string == NULL) &&
		   (recv_ret = recv(socket, output + byte_offset, (unsigned long)(BUFF_SZ - byte_offset),
							0) > 0)) {

		if (strchr(output, '\n')) { /* we've got at least one full line, start parsing*/
			byte_offset = 0;

			char *index = NULL;
			while ((index = strchr(output + byte_offset, '\n')) != NULL) {
				/*Partition the buffer so that this line is a separate string,
				 * by replacing the newline with NUL*/
				output[(index - output)] = '\0';
				size_t len = strlen(output + byte_offset);

				if ((len >= 4) && (strncmp(output + byte_offset, "SSH-", 4) == 0)) {
					/*if the string starts with SSH-, this _should_ be a valid version control
					 * string*/
					version_control_string = output + byte_offset;
					break;
				}

				/*the start of the next line (if one exists) will be after the current one (+ NUL)*/
				byte_offset += (len + 1);
			}

			if (version_control_string == NULL) {
				/* move unconsumed data to beginning of buffer */
				memmove((void *)output, (void *)(output + byte_offset), BUFF_SZ - byte_offset);

				/*start reading from end of current line chunk on next recv*/
				byte_offset = strlen(output);

				/* NUL the rest of the buffer */
				memset(output + byte_offset, 0, BUFF_SZ - byte_offset);
			}
		} else {
			byte_offset += (size_t)recv_ret;
		}
	}

	if (recv_ret < 0) {
		connection_sc = mp_set_subcheck_state(connection_sc, STATE_CRITICAL);
		xasprintf(&connection_sc.output, "%s - %s", "SSH CRITICAL - ", strerror(errno));
		mp_add_subcheck_to_check(overall, connection_sc);
		return OK;
	}

	if (version_control_string == NULL) {
		connection_sc = mp_set_subcheck_state(connection_sc, STATE_CRITICAL);
		xasprintf(&connection_sc.output, "%s", "SSH CRITICAL - No version control string received");
		mp_add_subcheck_to_check(overall, connection_sc);
		return OK;
	}

	connection_sc = mp_set_subcheck_state(connection_sc, STATE_OK);
	xasprintf(&connection_sc.output, "%s", "Initial connection succeeded");
	mp_add_subcheck_to_check(overall, connection_sc);

	/*
	 * "When the connection has been established, both sides MUST send an
	 * identification string.  This identification string MUST be
	 *
	 * SSH-protoversion-softwareversion SP comments CR LF"
	 *		- RFC 4253:4.2
	 */
	strip(version_control_string);
	if (verbose) {
		printf("%s\n", version_control_string);
	}

	char *ssh_proto = version_control_string + 4;

	/*
	 * We assume the protoversion is of the form Major.Minor, although
	 * this is not _strictly_ required. See
	 *
	 * "Both the 'protoversion' and 'softwareversion' strings MUST consist of
	 * printable US-ASCII characters, with the exception of whitespace
	 * characters and the minus sign (-)"
	 *		- RFC 4253:4.2
	 * and,
	 *
	 * "As stated earlier, the 'protoversion' specified for this protocol is
	 * "2.0".  Earlier versions of this protocol have not been formally
	 * documented, but it is widely known that they use 'protoversion' of
	 * "1.x" (e.g., "1.5" or "1.3")."
	 *		- RFC 4253:5
	 */
	char *ssh_server = ssh_proto + strspn(ssh_proto, "0123456789.") +
					   1; /* (+1 for the '-' separating protoversion from softwareversion) */

	/* If there's a space in the version string, whatever's after the space is a comment
	 * (which is NOT part of the server name/version)*/
	char *tmp = strchr(ssh_server, ' ');
	if (tmp) {
		ssh_server[tmp - ssh_server] = '\0';
	}

	mp_subcheck protocol_validity_sc = mp_subcheck_init();
	if (strlen(ssh_proto) == 0 || strlen(ssh_server) == 0) {
		protocol_validity_sc = mp_set_subcheck_state(protocol_validity_sc, STATE_CRITICAL);
		xasprintf(&protocol_validity_sc.output, "Invalid protocol version control string %s",
				  version_control_string);
		mp_add_subcheck_to_check(overall, protocol_validity_sc);
		return OK;
	}

	protocol_validity_sc = mp_set_subcheck_state(protocol_validity_sc, STATE_OK);
	xasprintf(&protocol_validity_sc.output, "Valid protocol version control string %s",
			  version_control_string);
	mp_add_subcheck_to_check(overall, protocol_validity_sc);

	ssh_proto[strspn(ssh_proto, "0123456789. ")] = 0;

	static char *rev_no = VERSION;
	xasprintf(&buffer, "SSH-%s-check_ssh_%s\r\n", ssh_proto, rev_no);
	send(socket, buffer, strlen(buffer), MSG_DONTWAIT);
	if (verbose) {
		printf("%s\n", buffer);
	}

	if (desired_remote_version && strcmp(desired_remote_version, ssh_server)) {
		mp_subcheck remote_version_sc = mp_subcheck_init();
		remote_version_sc = mp_set_subcheck_state(remote_version_sc, STATE_CRITICAL);
		xasprintf(&remote_version_sc.output, _("%s (protocol %s) version mismatch, expected '%s'"),
				  ssh_server, ssh_proto, desired_remote_version);
		close(socket);
		mp_add_subcheck_to_check(overall, remote_version_sc);
		return OK;
	}

	double elapsed_time = (double)deltime(tv) / 1.0e6;
	mp_perfdata time_pd = perfdata_init();
	time_pd.value = mp_create_pd_value(elapsed_time);
	time_pd.label = "time";
	time_pd.max_present = true;
	time_pd.max = mp_create_pd_value(socket_timeout);

	mp_subcheck protocol_version_sc = mp_subcheck_init();
	mp_add_perfdata_to_subcheck(&protocol_version_sc, time_pd);

	if (desired_remote_protocol && strcmp(desired_remote_protocol, ssh_proto)) {
		protocol_version_sc = mp_set_subcheck_state(protocol_version_sc, STATE_CRITICAL);
		xasprintf(&protocol_version_sc.output,
				  _("%s (protocol %s) protocol version mismatch, expected '%s'"), ssh_server,
				  ssh_proto, desired_remote_protocol);
	} else {
		protocol_version_sc = mp_set_subcheck_state(protocol_version_sc, STATE_OK);
		xasprintf(&protocol_version_sc.output, "SSH server version: %s (protocol version: %s)",
				  ssh_server, ssh_proto);
	}

	mp_add_subcheck_to_check(overall, protocol_version_sc);
	close(socket);
	return OK;
}

void print_help(void) {
	char *myport;
	xasprintf(&myport, "%d", default_ssh_port);

	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Remi Paulmier <remi@sinfomic.fr>\n");
	printf(COPYRIGHT, copyright, email);

	printf("%s\n", _("Try to connect to an SSH server at specified server and port"));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(UT_HOST_PORT, 'p', myport);

	printf(UT_IPv46);

	printf(UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf(" %s\n", "-r, --remote-version=STRING");
	printf("    %s\n",
		   _("Alert if string doesn't match expected server version (ex: OpenSSH_3.9p1)"));

	printf(" %s\n", "-P, --remote-protocol=STRING");
	printf("    %s\n", _("Alert if protocol doesn't match expected protocol version (ex: 2.0)"));
	printf(UT_OUTPUT_FORMAT);

	printf(UT_VERBOSE);

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s  [-4|-6] [-t <timeout>] [-r <remote version>] [-p <port>] --hostname <host>\n",
		   progname);
}
