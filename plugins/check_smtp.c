/*****************************************************************************
 *
 * Monitoring check_smtp plugin
 *
 * License: GPL
 * Copyright (c) 2000-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_smtp plugin
 *
 * This plugin will attempt to open an SMTP connection with the host.
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

const char *progname = "check_smtp";
const char *copyright = "2000-2024";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "netutils.h"
#include "utils.h"
#include "base64.h"
#include "regex.h"
#include "output.h"
#include <string.h>
#include "perfdata.h"
#include "thresholds.h"

#include <ctype.h>
#include "check_smtp.d/config.h"
#include "../lib/states.h"

#define PROXY_PREFIX    "PROXY TCP4 0.0.0.0 0.0.0.0 25 25\r\n"
#define SMTP_HELO       "HELO "
#define SMTP_EHLO       "EHLO "
#define SMTP_LHLO       "LHLO "
#define SMTP_QUIT       "QUIT\r\n"
#define SMTP_STARTTLS   "STARTTLS\r\n"
#define SMTP_AUTH_LOGIN "AUTH LOGIN\r\n"

#define EHLO_SUPPORTS_STARTTLS 1

typedef struct {
	int errorcode;
	check_smtp_config config;
} check_smtp_config_wrapper;
static check_smtp_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);

int my_recv(check_smtp_config config, void *buf, int num, int socket_descriptor,
			bool ssl_established) {
#ifdef HAVE_SSL
	if ((config.use_starttls || config.use_ssl) && ssl_established) {
		return np_net_ssl_read(buf, num);
	}
	return (int)read(socket_descriptor, buf, (size_t)num);
#else /* ifndef HAVE_SSL */
	return read(socket_descriptor, buf, len)
#endif
}

int my_send(check_smtp_config config, void *buf, int num, int socket_descriptor,
			bool ssl_established) {
#ifdef HAVE_SSL
	if ((config.use_starttls || config.use_ssl) && ssl_established) {

		return np_net_ssl_write(buf, num);
	}
	return (int)send(socket_descriptor, buf, (size_t)num, 0);
#else /* ifndef HAVE_SSL */
	return send(socket_descriptor, buf, len, 0);
#endif
}

static void print_help(void);
void print_usage(void);
static char *smtp_quit(check_smtp_config /*config*/, char /*buffer*/[MAX_INPUT_BUFFER],
					   int /*socket_descriptor*/, bool /*ssl_established*/);
static int recvline(char * /*buf*/, size_t /*bufsize*/, check_smtp_config /*config*/,
					int /*socket_descriptor*/, bool /*ssl_established*/);
static int recvlines(check_smtp_config /*config*/, char * /*buf*/, size_t /*bufsize*/,
					 int /*socket_descriptor*/, bool /*ssl_established*/);
static int my_close(int /*socket_descriptor*/);

static int verbose = 0;

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_smtp_config_wrapper tmp_config = process_arguments(argc, argv);

	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	const check_smtp_config config = tmp_config.config;

	if (config.output_format_is_set) {
		mp_set_format(config.output_format);
	}

	/* If localhostname not set on command line, use gethostname to set */
	char *localhostname = config.localhostname;
	if (!localhostname) {
		localhostname = malloc(HOST_MAX_BYTES);
		if (!localhostname) {
			printf(_("malloc() failed!\n"));
			exit(STATE_CRITICAL);
		}
		if (gethostname(localhostname, HOST_MAX_BYTES)) {
			printf(_("gethostname() failed!\n"));
			exit(STATE_CRITICAL);
		}
	}

	char *helocmd = NULL;
	if (config.use_lhlo) {
		xasprintf(&helocmd, "%s%s%s", SMTP_LHLO, localhostname, "\r\n");
	} else if (config.use_ehlo) {
		xasprintf(&helocmd, "%s%s%s", SMTP_EHLO, localhostname, "\r\n");
	} else {
		xasprintf(&helocmd, "%s%s%s", SMTP_HELO, localhostname, "\r\n");
	}

	if (verbose) {
		printf("HELOCMD: %s", helocmd);
	}

	char *mail_command = strdup("MAIL ");
	char *cmd_str = NULL;
	/* initialize the MAIL command with optional FROM command  */
	xasprintf(&cmd_str, "%sFROM:<%s>%s", mail_command, config.from_arg, "\r\n");

	if (verbose && config.send_mail_from) {
		printf("FROM CMD: %s", cmd_str);
	}

	/* Catch pipe errors in read/write - sometimes occurs when writing QUIT */
	(void)signal(SIGPIPE, SIG_IGN);

	/* initialize alarm signal handling */
	(void)signal(SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	(void)alarm(socket_timeout);

	struct timeval start_time;
	/* start timer */
	gettimeofday(&start_time, NULL);

	mp_check overall = mp_check_init();
	int socket_descriptor = 0;
	/* try to connect to the host at the given port number */
	mp_state_enum tcp_connection_error =
		my_tcp_connect(config.server_address, config.server_port, &socket_descriptor);

	mp_subcheck sc_network_connection = mp_subcheck_init();
	if (tcp_connection_error != STATE_OK) {
		sc_network_connection = mp_set_subcheck_state(sc_network_connection, STATE_CRITICAL);
		xasprintf(&sc_network_connection.output, "network connection failed");
		mp_add_subcheck_to_check(&overall, sc_network_connection);
		mp_exit(overall);
	}

	sc_network_connection = mp_set_subcheck_state(sc_network_connection, STATE_OK);
	xasprintf(&sc_network_connection.output, "network connection succeeded");
	mp_add_subcheck_to_check(&overall, sc_network_connection);
	bool ssl_established = false;

	/* If requested, send PROXY header */
	if (config.use_proxy_prefix) {
		if (verbose) {
			printf("Sending header %s\n", PROXY_PREFIX);
		}
		my_send(config, PROXY_PREFIX, strlen(PROXY_PREFIX), socket_descriptor, ssl_established);
	}

	char buffer[MAX_INPUT_BUFFER];
#ifdef HAVE_SSL
	if (config.use_ssl) {
		mp_state_enum tls_connection_error = np_net_ssl_init_with_hostname(
			socket_descriptor, (config.use_sni ? config.server_address : NULL));
		mp_subcheck sc_tls_conn = mp_subcheck_init();
		if (tls_connection_error != STATE_OK) {
			sc_tls_conn = mp_set_subcheck_state(sc_tls_conn, STATE_CRITICAL);
			xasprintf(&sc_tls_conn.output, _("Cannot create TLS context"));
			mp_add_subcheck_to_check(&overall, sc_tls_conn);

			close(socket_descriptor);
			np_net_ssl_cleanup();

			mp_exit(overall);
		}

		sc_tls_conn = mp_set_subcheck_state(sc_tls_conn, STATE_OK);
		xasprintf(&sc_tls_conn.output, _("Created TLS context"));
		mp_add_subcheck_to_check(&overall, sc_tls_conn);

		ssl_established = true;
	}
#endif

	/* watch for the SMTP connection string and */
	/* return a WARNING status if we couldn't read any data */
	if (recvlines(config, buffer, MAX_INPUT_BUFFER, socket_descriptor, ssl_established) <= 0) {
		mp_subcheck sc_receiver = mp_subcheck_init();
		xasprintf(&sc_receiver.output, _("recv() failed"));
		sc_receiver = mp_set_subcheck_state(sc_receiver, STATE_WARNING);
		mp_add_subcheck_to_check(&overall, sc_receiver);
		mp_exit(overall);
	}

	char *server_response = NULL;
	/* save connect return (220 hostname ..) for later use */
	xasprintf(&server_response, "%s", buffer);

	/* send the HELO/EHLO command */
	my_send(config, helocmd, (int)strlen(helocmd), socket_descriptor, ssl_established);

	/* allow for response to helo command to reach us */
	if (recvlines(config, buffer, MAX_INPUT_BUFFER, socket_descriptor, ssl_established) <= 0) {
		mp_subcheck sc_receiver = mp_subcheck_init();
		xasprintf(&sc_receiver.output, _("recv() failed"));
		sc_receiver = mp_set_subcheck_state(sc_receiver, STATE_WARNING);
		mp_add_subcheck_to_check(&overall, sc_receiver);
		mp_exit(overall);
	}

	if (config.use_ehlo || config.use_lhlo) {
		if ((strstr(buffer, "250 STARTTLS") != NULL || strstr(buffer, "250-STARTTLS") != NULL) &&
			(config.use_starttls)) {
			smtp_quit(config, buffer, socket_descriptor, ssl_established);

			mp_subcheck sc_supports_starttls = mp_subcheck_init();
			xasprintf(&sc_supports_starttls.output, _("STARTTLS not supported by server"));
			sc_supports_starttls = mp_set_subcheck_state(sc_supports_starttls, STATE_WARNING);
			mp_add_subcheck_to_check(&overall, sc_supports_starttls);
			mp_exit(overall);
		}
	}

#ifdef HAVE_SSL
	if (config.use_starttls) {
		/* send the STARTTLS command */
		send(socket_descriptor, SMTP_STARTTLS, strlen(SMTP_STARTTLS), 0);

		recvlines(config, buffer, MAX_INPUT_BUFFER, socket_descriptor,
				  ssl_established); /* wait for it */
		if (!strstr(buffer, SMTP_EXPECT)) {
			mp_subcheck sc_starttls_init = mp_subcheck_init();
			xasprintf(&sc_starttls_init.output, _("Server does not support STARTTLS"));
			sc_starttls_init = mp_set_subcheck_state(sc_starttls_init, STATE_UNKNOWN);
			mp_add_subcheck_to_check(&overall, sc_starttls_init);

			smtp_quit(config, buffer, socket_descriptor, ssl_established);

			mp_exit(overall);
		}

		mp_state_enum starttls_init_error = np_net_ssl_init_with_hostname(
			socket_descriptor, (config.use_sni ? config.server_address : NULL));
		mp_subcheck sc_starttls_tls_setup = mp_subcheck_init();
		if (starttls_init_error != STATE_OK) {
			xasprintf(&sc_starttls_tls_setup.output, _("Cannot create SSL context"));
			sc_starttls_tls_setup = mp_set_subcheck_state(sc_starttls_tls_setup, STATE_CRITICAL);

			close(socket_descriptor);
			np_net_ssl_cleanup();

			mp_add_subcheck_to_check(&overall, sc_starttls_tls_setup);
			mp_exit(overall);
		}

		xasprintf(&sc_starttls_tls_setup.output, _("Succeeded to create SSL context"));
		sc_starttls_tls_setup = mp_set_subcheck_state(sc_starttls_tls_setup, STATE_OK);
		mp_add_subcheck_to_check(&overall, sc_starttls_tls_setup);

		ssl_established = true;

		/*
		 * Resend the EHLO command.
		 *
		 * RFC 3207 (4.2) says: ``The client MUST discard any knowledge
		 * obtained from the server, such as the list of SMTP service
		 * extensions, which was not obtained from the TLS negotiation
		 * itself.  The client SHOULD send an EHLO command as the first
		 * command after a successful TLS negotiation.''  For this
		 * reason, some MTAs will not allow an AUTH LOGIN command before
		 * we resent EHLO via TLS.
		 */
		if (my_send(config, helocmd, strlen(helocmd), socket_descriptor, ssl_established) <= 0) {
			my_close(socket_descriptor);

			mp_subcheck sc_resend_ehlo = mp_subcheck_init();
			xasprintf(&sc_resend_ehlo.output, _("Cannot send EHLO command via TLS"));
			sc_resend_ehlo = mp_set_subcheck_state(sc_resend_ehlo, STATE_UNKNOWN);
			mp_add_subcheck_to_check(&overall, sc_resend_ehlo);
			mp_exit(overall);
		}

		if (verbose) {
			printf(_("sent %s"), helocmd);
		}

		if (recvlines(config, buffer, MAX_INPUT_BUFFER, socket_descriptor, ssl_established) <= 0) {
			my_close(socket_descriptor);

			mp_subcheck sc_reread_ehlo = mp_subcheck_init();
			xasprintf(&sc_reread_ehlo.output, _("Cannot read EHLO response via TLS"));
			sc_reread_ehlo = mp_set_subcheck_state(sc_reread_ehlo, STATE_UNKNOWN);
			mp_add_subcheck_to_check(&overall, sc_reread_ehlo);
			mp_exit(overall);
		}

		if (verbose) {
			printf("%s", buffer);
		}

#	ifdef USE_OPENSSL
		if (config.check_cert) {
			mp_subcheck sc_cert_check = mp_subcheck_init();

			sc_cert_check = mp_set_subcheck_state(
				sc_cert_check,
				np_net_ssl_check_cert(config.days_till_exp_warn, config.days_till_exp_crit));

			smtp_quit(config, buffer, socket_descriptor, ssl_established);
			my_close(socket_descriptor);

			mp_add_subcheck_to_check(&overall, sc_cert_check);
			mp_exit(overall);
		}
#	endif /* USE_OPENSSL */
	}
#endif

	if (verbose) {
		printf("%s", buffer);
	}

	/* save buffer for later use */
	xasprintf(&server_response, "%s%s", server_response, buffer);
	/* strip the buffer of carriage returns */
	strip(server_response);

	/* make sure we find the droids we are looking for */
	if (!strstr(server_response, config.server_expect)) {
		mp_subcheck sc_expect_response = mp_subcheck_init();
		if (config.server_port == SMTP_PORT) {
			xasprintf(&sc_expect_response.output, _("Invalid SMTP response received from host: %s"),
					  server_response);
		} else {
			xasprintf(&sc_expect_response.output,
					  _("Invalid SMTP response received from host on port %d: %s"),
					  config.server_port, server_response);
		}
		sc_expect_response = mp_set_subcheck_state(sc_expect_response, STATE_WARNING);
		mp_add_subcheck_to_check(&overall, sc_expect_response);
		mp_exit(overall);
	}

	if (config.send_mail_from) {
		my_send(config, cmd_str, (int)strlen(cmd_str), socket_descriptor, ssl_established);
		if (recvlines(config, buffer, MAX_INPUT_BUFFER, socket_descriptor, ssl_established) >= 1 &&
			verbose) {
			printf("%s", buffer);
		}
	}

	int counter = 0;
	while (counter < config.ncommands) {
		xasprintf(&cmd_str, "%s%s", config.commands[counter], "\r\n");
		my_send(config, cmd_str, (int)strlen(cmd_str), socket_descriptor, ssl_established);
		if (recvlines(config, buffer, MAX_INPUT_BUFFER, socket_descriptor, ssl_established) >= 1 &&
			verbose) {
			printf("%s", buffer);
		}

		strip(buffer);

		if (counter < config.nresponses) {
			int cflags = REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
			regex_t preg;
			int errcode = regcomp(&preg, config.responses[counter], cflags);
			char errbuf[MAX_INPUT_BUFFER];
			if (errcode != 0) {
				regerror(errcode, &preg, errbuf, MAX_INPUT_BUFFER);

				mp_subcheck sc_failed_regex_compile = mp_subcheck_init();
				xasprintf(&sc_failed_regex_compile.output,
						  _("Could Not Compile Regular Expression"));
				sc_failed_regex_compile =
					mp_set_subcheck_state(sc_failed_regex_compile, STATE_UNKNOWN);
				mp_add_subcheck_to_check(&overall, sc_failed_regex_compile);
				mp_exit(overall);
			}

			regmatch_t pmatch[10];
			int eflags = 0;
			mp_subcheck sc_regex_match = mp_subcheck_init();
			int excode = regexec(&preg, buffer, 10, pmatch, eflags);
			if (excode == 0) {
				// successful match
				sc_regex_match = mp_set_subcheck_state(sc_regex_match, STATE_OK);
				xasprintf(&sc_regex_match.output, _("Regular expression matched"));
			} else if (excode == REG_NOMATCH) {
				sc_regex_match = mp_set_subcheck_state(sc_regex_match, STATE_WARNING);
				xasprintf(&sc_regex_match.output, _("Invalid response '%s' to command '%s'"),
						  buffer, config.commands[counter]);
			} else {
				regerror(excode, &preg, errbuf, MAX_INPUT_BUFFER);
				xasprintf(&sc_regex_match.output, _("regexec execute error: %s"), errbuf);
				sc_regex_match = mp_set_subcheck_state(sc_regex_match, STATE_UNKNOWN);
			}
			mp_add_subcheck_to_check(&overall, sc_regex_match);
		}
		counter++;
	}

	if (config.authtype != NULL) {
		if (strcmp(config.authtype, "LOGIN") == 0) {
			mp_subcheck sc_auth = mp_subcheck_init();
			sc_auth = mp_set_subcheck_default_state(sc_auth, STATE_OK);
			xasprintf(&sc_auth.output, "Authentication via LOGIN");

			do {
				/* send AUTH LOGIN */
				my_send(config, SMTP_AUTH_LOGIN, strlen(SMTP_AUTH_LOGIN), socket_descriptor,
						ssl_established);

				if (verbose) {
					printf(_("sent %s\n"), "AUTH LOGIN");
				}

				int ret1;
				if ((ret1 = recvlines(config, buffer, MAX_INPUT_BUFFER, socket_descriptor,
									  ssl_established)) <= 0) {
					xasprintf(&sc_auth.output, _("recv() failed after AUTH LOGIN"));
					sc_auth = mp_set_subcheck_state(sc_auth, STATE_WARNING);
					break;
				}

				if (verbose) {
					printf(_("received %s\n"), buffer);
				}

				if (strncmp(buffer, "334", 3) != 0) {
					sc_auth = mp_set_subcheck_state(sc_auth, STATE_CRITICAL);
					xasprintf(&sc_auth.output, _("invalid response received after AUTH LOGIN"));
					break;
				}

				/* encode authuser with base64 */
				char *abuf;
				base64_encode_alloc(config.authuser, strlen(config.authuser), &abuf);
				xasprintf(&abuf, "%s\r\n", abuf);
				my_send(config, abuf, (int)strlen(abuf), socket_descriptor, ssl_established);

				if (verbose) {
					printf(_("sent %s\n"), abuf);
				}

				int ret2;
				if ((ret2 = recvlines(config, buffer, MAX_INPUT_BUFFER, socket_descriptor,
									  ssl_established)) <= 0) {
					sc_auth = mp_set_subcheck_state(sc_auth, STATE_CRITICAL);
					xasprintf(&sc_auth.output, _("recv() failed after sending authuser"));
					break;
				}
				if (verbose) {
					printf(_("received %s\n"), buffer);
				}

				if (strncmp(buffer, "334", 3) != 0) {
					sc_auth = mp_set_subcheck_state(sc_auth, STATE_CRITICAL);
					xasprintf(&sc_auth.output, _("invalid response received after authuser"));
					break;
				}

				/* encode authpass with base64 */
				base64_encode_alloc(config.authpass, strlen(config.authpass), &abuf);
				xasprintf(&abuf, "%s\r\n", abuf);
				my_send(config, abuf, (int)strlen(abuf), socket_descriptor, ssl_established);

				if (verbose) {
					printf(_("sent %s\n"), abuf);
				}

				int ret3;
				if ((ret3 = recvlines(config, buffer, MAX_INPUT_BUFFER, socket_descriptor,
									  ssl_established)) <= 0) {
					sc_auth = mp_set_subcheck_state(sc_auth, STATE_CRITICAL);
					xasprintf(&sc_auth.output, _("recv() failed after sending authpass"));
					break;
				}

				if (verbose) {
					printf(_("received %s\n"), buffer);
				}

				if (strncmp(buffer, "235", 3) != 0) {
					sc_auth = mp_set_subcheck_state(sc_auth, STATE_CRITICAL);
					xasprintf(&sc_auth.output, _("invalid response received after authpass"));
					break;
				}
				break;
			} while (false);

			mp_add_subcheck_to_check(&overall, sc_auth);
		} else {
			// no other options implement, should not reach this
			exit(STATE_UNKNOWN);
		}
	}

	/* tell the server we're done */
	smtp_quit(config, buffer, socket_descriptor, ssl_established);

	/* finally close the connection */
	close(socket_descriptor);

	/* reset the alarm */
	alarm(0);

	long microsec = deltime(start_time);
	double elapsed_time = (double)microsec / 1.0e6;

	mp_perfdata pd_elapsed_time = perfdata_init();
	pd_elapsed_time.value = mp_create_pd_value(elapsed_time);
	pd_elapsed_time.label = "time";
	pd_elapsed_time = mp_pd_set_thresholds(pd_elapsed_time, config.time_thresholds);

	mp_subcheck sc_elapsed_time = mp_subcheck_init();
	sc_elapsed_time = mp_set_subcheck_state(sc_elapsed_time, mp_get_pd_status(pd_elapsed_time));
	mp_add_perfdata_to_subcheck(&sc_elapsed_time, pd_elapsed_time);

	xasprintf(&sc_elapsed_time.output, "Request took: %gs", elapsed_time);

	mp_add_subcheck_to_check(&overall, sc_elapsed_time);

	mp_exit(overall);
}

/* process command-line arguments */
check_smtp_config_wrapper process_arguments(int argc, char **argv) {
	enum {
		SNI_OPTION = CHAR_MAX + 1,
		output_format_index,
	};

	int option = 0;
	static struct option longopts[] = {{"hostname", required_argument, 0, 'H'},
									   {"expect", required_argument, 0, 'e'},
									   {"critical", required_argument, 0, 'c'},
									   {"warning", required_argument, 0, 'w'},
									   {"timeout", required_argument, 0, 't'},
									   {"port", required_argument, 0, 'p'},
									   {"from", required_argument, 0, 'f'},
									   {"fqdn", required_argument, 0, 'F'},
									   {"authtype", required_argument, 0, 'A'},
									   {"authuser", required_argument, 0, 'U'},
									   {"authpass", required_argument, 0, 'P'},
									   {"command", required_argument, 0, 'C'},
									   {"response", required_argument, 0, 'R'},
									   {"verbose", no_argument, 0, 'v'},
									   {"version", no_argument, 0, 'V'},
									   {"use-ipv4", no_argument, 0, '4'},
									   {"use-ipv6", no_argument, 0, '6'},
									   {"help", no_argument, 0, 'h'},
									   {"lmtp", no_argument, 0, 'L'},
									   {"ssl", no_argument, 0, 's'},
									   {"tls", no_argument, 0, 's'},
									   {"starttls", no_argument, 0, 'S'},
									   {"sni", no_argument, 0, SNI_OPTION},
									   {"certificate", required_argument, 0, 'D'},
									   {"ignore-quit-failure", no_argument, 0, 'q'},
									   {"proxy", no_argument, 0, 'r'},
									   {"output-format", required_argument, 0, output_format_index},
									   {0, 0, 0, 0}};

	check_smtp_config_wrapper result = {
		.config = check_smtp_config_init(),
		.errorcode = OK,
	};

	if (argc < 2) {
		result.errorcode = ERROR;
		return result;
	}

	for (int index = 1; index < argc; index++) {
		if (strcmp("-to", argv[index]) == 0) {
			strcpy(argv[index], "-t");
		} else if (strcmp("-wt", argv[index]) == 0) {
			strcpy(argv[index], "-w");
		} else if (strcmp("-ct", argv[index]) == 0) {
			strcpy(argv[index], "-c");
		}
	}

	int command_size = 0;
	int response_size = 0;
	bool implicit_tls = false;
	int server_port_option = 0;
	while (true) {
		int opt_index =
			getopt_long(argc, argv, "+hVv46Lrt:p:f:e:c:w:H:C:R:sSD:F:A:U:P:q", longopts, &option);

		if (opt_index == -1 || opt_index == EOF) {
			break;
		}

		switch (opt_index) {
		case 'H': /* hostname */
			if (is_host(optarg)) {
				result.config.server_address = optarg;
			} else {
				usage2(_("Invalid hostname/address"), optarg);
			}
			break;
		case 'p': /* port */
			if (is_intpos(optarg)) {
				server_port_option = atoi(optarg);
			} else {
				usage4(_("Port must be a positive integer"));
			}
			break;
		case 'F':
			/* localhostname */
			result.config.localhostname = strdup(optarg);
			break;
		case 'f': /* from argument */
			result.config.from_arg = optarg + strspn(optarg, "<");
			result.config.from_arg =
				strndup(result.config.from_arg, strcspn(result.config.from_arg, ">"));
			result.config.send_mail_from = true;
			break;
		case 'A':
			result.config.authtype = optarg;
			result.config.use_ehlo = true;
			break;
		case 'U':
			result.config.authuser = optarg;
			break;
		case 'P':
			result.config.authpass = optarg;
			break;
		case 'e': /* server expect string on 220  */
			result.config.server_expect = optarg;
			break;
		case 'C': /* commands  */
			if (result.config.ncommands >= command_size) {
				command_size += 8;
				result.config.commands =
					realloc(result.config.commands, sizeof(char *) * command_size);
				if (result.config.commands == NULL) {
					die(STATE_UNKNOWN, _("Could not realloc() units [%d]\n"),
						result.config.ncommands);
				}
			}
			result.config.commands[result.config.ncommands] = (char *)malloc(sizeof(char) * 255);
			strncpy(result.config.commands[result.config.ncommands], optarg, 255);
			result.config.ncommands++;
			break;
		case 'R': /* server responses */
			if (result.config.nresponses >= response_size) {
				response_size += 8;
				result.config.responses =
					realloc(result.config.responses, sizeof(char *) * response_size);
				if (result.config.responses == NULL) {
					die(STATE_UNKNOWN, _("Could not realloc() units [%d]\n"),
						result.config.nresponses);
				}
			}
			result.config.responses[result.config.nresponses] = (char *)malloc(sizeof(char) * 255);
			strncpy(result.config.responses[result.config.nresponses], optarg, 255);
			result.config.nresponses++;
			break;
		case 'c': /* critical time threshold */
			if (!is_nonnegative(optarg)) {
				usage4(_("Critical time must be a positive"));
			} else {
				mp_range_parsed tmp_crit = mp_parse_range_string(optarg);
				if (tmp_crit.error == MP_PARSING_SUCCES) {
					result.config.time_thresholds =
						mp_thresholds_set_crit(result.config.time_thresholds, tmp_crit.range);
				} else {
					usage2(_("Critical time invalid: %s"), optarg);
				}
			}
			break;
		case 'w': /* warning time threshold */
			if (!is_nonnegative(optarg)) {
				usage4(_("Warning time must be a positive"));
			} else {
				mp_range_parsed tmp_warn = mp_parse_range_string(optarg);
				if (tmp_warn.error == MP_PARSING_SUCCES) {
					result.config.time_thresholds =
						mp_thresholds_set_warn(result.config.time_thresholds, tmp_warn.range);
				} else {
					usage2(_("Warning time invalid: %s"), optarg);
				}
			}
			break;
		case 'v': /* verbose */
			verbose++;
			break;
		case 'q':
			result.config.ignore_send_quit_failure = true; /* ignore problem sending QUIT */
			break;
		case 't': /* timeout */
			if (is_intnonneg(optarg)) {
				socket_timeout = atoi(optarg);
			} else {
				usage4(_("Timeout interval must be a positive integer"));
			}
			break;
		case 'D': {
			/* Check SSL cert validity */
#ifdef USE_OPENSSL
			char *temp;
			if ((temp = strchr(optarg, ',')) != NULL) {
				*temp = '\0';
				if (!is_intnonneg(optarg)) {
					usage2("Invalid certificate expiration period", optarg);
				}
				result.config.days_till_exp_warn = atoi(optarg);
				*temp = ',';
				temp++;
				if (!is_intnonneg(temp)) {
					usage2(_("Invalid certificate expiration period"), temp);
				}
				result.config.days_till_exp_crit = atoi(temp);
			} else {
				result.config.days_till_exp_crit = 0;
				if (!is_intnonneg(optarg)) {
					usage2("Invalid certificate expiration period", optarg);
				}
				result.config.days_till_exp_warn = atoi(optarg);
			}
			result.config.check_cert = true;
			result.config.ignore_send_quit_failure = true;
#else
			usage(_("SSL support not available - install OpenSSL and recompile"));
#endif
			implicit_tls = true;
			// fallthrough
		case 's':
			/* ssl */
			result.config.use_ssl = true;
			result.config.server_port = SMTPS_PORT;
			break;
		case 'S':
			/* starttls */
			result.config.use_starttls = true;
			result.config.use_ehlo = true;
			break;
		}
		case SNI_OPTION:
#ifdef HAVE_SSL
			result.config.use_sni = true;
#else
			usage(_("SSL support not available - install OpenSSL and recompile"));
#endif
			break;
		case 'r':
			result.config.use_proxy_prefix = true;
			break;
		case 'L':
			result.config.use_lhlo = true;
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
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case '?': /* help */
			usage5();
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

	int c = optind;
	if (result.config.server_address == NULL) {
		if (argv[c]) {
			if (is_host(argv[c])) {
				result.config.server_address = argv[c];
			} else {
				usage2(_("Invalid hostname/address"), argv[c]);
			}
		} else {
			result.config.server_address = strdup("localhost");
		}
	}

	if (result.config.use_starttls && result.config.use_ssl) {
		if (implicit_tls) {
			result.config.use_ssl = false;
		} else {
			usage4(_("Set either -s/--ssl/--tls or -S/--starttls"));
		}
	}

	if (server_port_option != 0) {
		result.config.server_port = server_port_option;
	}

	if (result.config.authtype != NULL) {
		if (strcmp(result.config.authtype, "LOGIN") == 0) {
			if (result.config.authuser == NULL) {
				usage4(_("no authuser specified, "));
			}

			if (result.config.authpass == NULL) {
				usage4(_("no authpass specified, "));
			}
		} else {
			usage4(_("only authtype LOGIN is supported"));
		}
	}

	return result;
}

char *smtp_quit(check_smtp_config config, char buffer[MAX_INPUT_BUFFER], int socket_descriptor,
				bool ssl_established) {
	int sent_bytes =
		my_send(config, SMTP_QUIT, strlen(SMTP_QUIT), socket_descriptor, ssl_established);
	if (sent_bytes < 0) {
		if (config.ignore_send_quit_failure) {
			if (verbose) {
				printf(_("Connection closed by server before sending QUIT command\n"));
			}
			return buffer;
		}
		die(STATE_UNKNOWN, _("Connection closed by server before sending QUIT command\n"));
	}

	if (verbose) {
		printf(_("sent %s\n"), "QUIT");
	}

	/* read the response but don't care about problems */
	int bytes = recvlines(config, buffer, MAX_INPUT_BUFFER, socket_descriptor, ssl_established);
	if (verbose) {
		if (bytes < 0) {
			printf(_("recv() failed after QUIT."));
		} else if (bytes == 0) {
			printf(_("Connection reset by peer."));
		} else {
			buffer[bytes] = '\0';
			printf(_("received %s\n"), buffer);
		}
	}

	return buffer;
}

/*
 * Receive one line, copy it into buf and nul-terminate it.  Returns the
 * number of bytes written to buf (excluding the '\0') or 0 on EOF or <0 on
 * error.
 *
 * TODO: Reading one byte at a time is very inefficient.  Replace this by a
 * function which buffers the data, move that to netutils.c and change
 * check_smtp and other plugins to use that.  Also, remove (\r)\n.
 */
int recvline(char *buf, size_t bufsize, check_smtp_config config, int socket_descriptor,
			 bool ssl_established) {
	int result;
	int counter;

	for (counter = result = 0; counter < bufsize - 1; counter++) {
		if ((result = my_recv(config, &buf[counter], 1, socket_descriptor, ssl_established)) != 1) {
			break;
		}
		if (buf[counter] == '\n') {
			buf[++counter] = '\0';
			return counter;
		}
	}
	return (result == 1 || counter == 0) ? -2 : result; /* -2 if out of space */
}

/*
 * Receive one or more lines, copy them into buf and nul-terminate it.  Returns
 * the number of bytes written to buf (excluding the '\0') or 0 on EOF or <0 on
 * error.  Works for all protocols which format multiline replies as follows:
 *
 * ``The format for multiline replies requires that every line, except the last,
 * begin with the reply code, followed immediately by a hyphen, `-' (also known
 * as minus), followed by text.  The last line will begin with the reply code,
 * followed immediately by <SP>, optionally some text, and <CRLF>.  As noted
 * above, servers SHOULD send the <SP> if subsequent text is not sent, but
 * clients MUST be prepared for it to be omitted.'' (RFC 2821, 4.2.1)
 *
 * TODO: Move this to netutils.c.  Also, remove \r and possibly the final \n.
 */
int recvlines(check_smtp_config config, char *buf, size_t bufsize, int socket_descriptor,
			  bool ssl_established) {
	int result;
	int counter;

	for (counter = 0; /* forever */; counter += result) {
		if (!((result = recvline(buf + counter, bufsize - counter, config, socket_descriptor,
								 ssl_established)) > 3 &&
			  isdigit((int)buf[counter]) && isdigit((int)buf[counter + 1]) &&
			  isdigit((int)buf[counter + 2]) && buf[counter + 3] == '-')) {
			break;
		}
	}

	return (result <= 0) ? result : result + counter;
}

int my_close(int socket_descriptor) {
	int result;
	result = close(socket_descriptor);
#ifdef HAVE_SSL
	np_net_ssl_cleanup();
#endif
	return result;
}

void print_help(void) {
	char *myport;
	xasprintf(&myport, "%d", SMTP_PORT);

	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999-2001 Ethan Galstad <nagios@nagios.org>\n");
	printf(COPYRIGHT, copyright, email);

	printf("%s\n", _("This plugin will attempt to open an SMTP connection with the host."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(UT_HOST_PORT, 'p', myport);

	printf(UT_IPv46);

	printf(" %s\n", "-e, --expect=STRING");
	printf(_("    String to expect in first line of server response (default: '%s')\n"),
		   SMTP_EXPECT);
	printf(" %s\n", "-C, --command=STRING");
	printf("    %s\n", _("SMTP command (may be used repeatedly)"));
	printf(" %s\n", "-R, --response=STRING");
	printf("    %s\n", _("Expected response to command (may be used repeatedly)"));
	printf(" %s\n", "-f, --from=STRING");
	printf("    %s\n", _("FROM-address to include in MAIL command, required by Exchange 2000")),
		printf(" %s\n", "-F, --fqdn=STRING");
	printf("    %s\n", _("FQDN used for HELO"));
	printf(" %s\n", "-r, --proxy");
	printf("    %s\n", _("Use PROXY protocol prefix for the connection."));
#ifdef HAVE_SSL
	printf(" %s\n", "-D, --certificate=INTEGER[,INTEGER]");
	printf("    %s\n", _("Minimum number of days a certificate has to be valid."));
	printf(" %s\n", "-s, --ssl, --tls");
	printf("    %s\n", _("Use SSL/TLS for the connection."));
	printf(_("    Sets default port to %d.\n"), SMTPS_PORT);
	printf(" %s\n", "-S, --starttls");
	printf("    %s\n", _("Use STARTTLS for the connection."));
	printf(" %s\n", "--sni");
	printf("    %s\n", _("Enable SSL/TLS hostname extension support (SNI)"));
#endif

	printf(" %s\n", "-A, --authtype=STRING");
	printf("    %s\n", _("SMTP AUTH type to check (default none, only LOGIN supported)"));
	printf(" %s\n", "-U, --authuser=STRING");
	printf("    %s\n", _("SMTP AUTH username"));
	printf(" %s\n", "-P, --authpass=STRING");
	printf("    %s\n", _("SMTP AUTH password"));
	printf(" %s\n", "-L, --lmtp");
	printf("    %s\n", _("Send LHLO instead of HELO/EHLO"));
	printf(" %s\n", "-q, --ignore-quit-failure");
	printf("    %s\n", _("Ignore failure when sending QUIT command to server"));

	printf(UT_WARN_CRIT);

	printf(UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf(UT_VERBOSE);

	printf(UT_OUTPUT_FORMAT);

	printf("\n");
	printf("%s\n", _("Successful connects return STATE_OK, refusals and timeouts return"));
	printf("%s\n", _("STATE_CRITICAL, other errors return STATE_UNKNOWN.  Successful"));
	printf("%s\n", _("connects, but incorrect response messages from the host result in"));
	printf("%s\n", _("STATE_WARNING return values."));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s -H host [-p port] [-4|-6] [-e expect] [-C command] [-R response] [-f from addr]\n",
		   progname);
	printf("[-A authtype -U authuser -P authpass] [-w warn] [-c crit] [-t timeout] [-q]\n");
	printf("[-F fqdn] [-S] [-L] [-D warn days cert expire[,crit days cert expire]] [-r] [--sni] "
		   "[-v] \n");
}
