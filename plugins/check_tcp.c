/*****************************************************************************
 *
 * Monitoring check_tcp plugin
 *
 * License: GPL
 * Copyright (c) 1999-2025 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_tcp plugin
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
 * $Id$
 *
 *****************************************************************************/

/* progname "check_tcp" changes depending on symlink called */
char *progname;
const char *copyright = "1999-2025";
const char *email = "devel@monitoring-plugins.org";

#include "./common.h"
#include "./netutils.h"
#include "./utils.h"
#include "./check_tcp.d/config.h"
#include "output.h"
#include "states.h"

#include <sys/types.h>
#include <ctype.h>
#include <sys/select.h>

ssize_t my_recv(char *buf, size_t len) {
#ifdef HAVE_SSL
	return np_net_ssl_read(buf, (int)len);
#else
	return read(socket_descriptor, buf, len);
#endif // HAVE_SSL
}

ssize_t my_send(char *buf, size_t len) {
#ifdef HAVE_SSL
	return np_net_ssl_write(buf, (int)len);
#else
	return write(socket_descriptor, buf, len);
#endif // HAVE_SSL
}

typedef struct {
	int errorcode;
	check_tcp_config config;
} check_tcp_config_wrapper;
static check_tcp_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/, check_tcp_config /*config*/);
void print_help(const char *service);
void print_usage(void);

int verbosity = 0;

static const int READ_TIMEOUT = 2;

const int MAXBUF = 1024;

const int DEFAULT_FTP_PORT = 21;
const int DEFAULT_POP_PORT = 110;
const int DEFAULT_SPOP_PORT = 995;
const int DEFAULT_SMTP_PORT = 25;
const int DEFAULT_SSMTP_PORT = 465;
const int DEFAULT_IMAP_PORT = 143;
const int DEFAULT_SIMAP_PORT = 993;
const int DEFAULT_XMPP_C2S_PORT = 5222;
const int DEFAULT_NNTP_PORT = 119;
const int DEFAULT_NNTPS_PORT = 563;
const int DEFAULT_CLAMD_PORT = 3310;

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* determine program- and service-name quickly */
	progname = strrchr(argv[0], '/');
	if (progname != NULL) {
		progname++;
	} else {
		progname = argv[0];
	}

	// Initialize config here with values from above,
	// might be changed by on disk config or cli commands
	check_tcp_config config = check_tcp_config_init();

	size_t prog_name_len = strlen(progname);
	const size_t prefix_length = strlen("check_");

	if (prog_name_len <= prefix_length) {
		die(STATE_UNKNOWN, _("Weird progname"));
	}

	if (!memcmp(progname, "check_", prefix_length)) {
		config.service = strdup(progname + prefix_length);
		if (config.service == NULL) {
			die(STATE_UNKNOWN, _("Allocation failed"));
		}

		for (size_t i = 0; i < prog_name_len - prefix_length; i++) {
			config.service[i] = toupper(config.service[i]);
		}
	}

	/* set up a reasonable buffer at first (will be realloc()'ed if
	 * user specifies other options) */
	config.server_expect = calloc(2, sizeof(char *));

	if (config.server_expect == NULL) {
		die(STATE_UNKNOWN, _("Allocation failed"));
	}

	/* determine defaults for this service's protocol */
	if (!strncmp(config.service, "UDP", strlen("UDP"))) {
		config.protocol = IPPROTO_UDP;
	} else if (!strncmp(config.service, "FTP", strlen("FTP"))) {
		config.server_expect[0] = "220";
		config.quit = "QUIT\r\n";
		config.server_port = DEFAULT_FTP_PORT;
	} else if (!strncmp(config.service, "POP", strlen("POP")) || !strncmp(config.service, "POP3", strlen("POP3"))) {
		config.server_expect[0] = "+OK";
		config.quit = "QUIT\r\n";
		config.server_port = DEFAULT_POP_PORT;
	} else if (!strncmp(config.service, "SMTP", strlen("SMTP"))) {
		config.server_expect[0] = "220";
		config.quit = "QUIT\r\n";
		config.server_port = DEFAULT_SMTP_PORT;
	} else if (!strncmp(config.service, "IMAP", strlen("IMAP"))) {
		config.server_expect[0] = "* OK";
		config.quit = "a1 LOGOUT\r\n";
		config.server_port = DEFAULT_IMAP_PORT;
	}
#ifdef HAVE_SSL
	else if (!strncmp(config.service, "SIMAP", strlen("SIMAP"))) {
		config.server_expect[0] = "* OK";
		config.quit = "a1 LOGOUT\r\n";
		config.use_tls = true;
		config.server_port = DEFAULT_SIMAP_PORT;
	} else if (!strncmp(config.service, "SPOP", strlen("SPOP"))) {
		config.server_expect[0] = "+OK";
		config.quit = "QUIT\r\n";
		config.use_tls = true;
		config.server_port = DEFAULT_SPOP_PORT;
	} else if (!strncmp(config.service, "SSMTP", strlen("SSMTP"))) {
		config.server_expect[0] = "220";
		config.quit = "QUIT\r\n";
		config.use_tls = true;
		config.server_port = DEFAULT_SSMTP_PORT;
	} else if (!strncmp(config.service, "JABBER", strlen("JABBER"))) {
		config.send = "<stream:stream to=\'host\' xmlns=\'jabber:client\' xmlns:stream=\'http://etherx.jabber.org/streams\'>\n";
		config.server_expect[0] = "<?xml version=\'1.0\'";
		config.quit = "</stream:stream>\n";
		config.hide_output = true;
		config.server_port = DEFAULT_XMPP_C2S_PORT;
	} else if (!strncmp(config.service, "NNTPS", strlen("NNTPS"))) {
		config.server_expect_count = 2;
		config.server_expect[0] = "200";
		config.server_expect[1] = "201";
		config.quit = "QUIT\r\n";
		config.use_tls = true;
		config.server_port = DEFAULT_NNTPS_PORT;
	}
#endif
	else if (!strncmp(config.service, "NNTP", strlen("NNTP"))) {
		config.server_expect_count = 2;
		char **tmp = realloc(config.server_expect, config.server_expect_count * sizeof(char *));
		if (tmp == NULL) {
			free(config.server_expect);
			die(STATE_UNKNOWN, _("Allocation failed"));
		}
		config.server_expect = tmp;

		config.server_expect[0] = strdup("200");
		config.server_expect[1] = strdup("201");
		config.quit = "QUIT\r\n";
		config.server_port = DEFAULT_NNTP_PORT;
	} else if (!strncmp(config.service, "CLAMD", strlen("CLAMD"))) {
		config.send = "PING";
		config.server_expect[0] = "PONG";
		config.quit = NULL;
		config.server_port = DEFAULT_CLAMD_PORT;
	}
	/* fallthrough check, so it's supposed to use reverse matching */
	else if (strcmp(config.service, "TCP")) {
		usage(_("CRITICAL - Generic check_tcp called with unknown service\n"));
	}

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_tcp_config_wrapper paw = process_arguments(argc, argv, config);
	if (paw.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	config = paw.config;

	if (verbosity > 0) {
		printf("Using service %s\n", config.service);
		printf("Port: %d\n", config.server_port);
	}

	if ((config.server_expect_count == 0) && config.server_expect[0]) {
		config.server_expect_count++;
	}

	if (config.protocol == IPPROTO_UDP && !(config.server_expect_count && config.send)) {
		usage(_("With UDP checks, a send/expect string must be specified."));
	}

	// Initialize check stuff before setting timers
	mp_check overall = mp_check_init();
	if (config.output_format_set) {
		mp_set_format(config.output_format);
	}

	/* set up the timer */
	signal(SIGALRM, socket_timeout_alarm_handler);
	alarm(socket_timeout);

	/* try to connect to the host at the given port number */
	struct timeval start_time;
	gettimeofday(&start_time, NULL);

	int socket_descriptor = 0;
	mp_subcheck inital_connect_result = mp_subcheck_init();

	// Try initial connection
	if (np_net_connect(config.server_address, config.server_port, &socket_descriptor, config.protocol) == STATE_CRITICAL) {
		// Early exit here, we got connection refused
		inital_connect_result = mp_set_subcheck_state(inital_connect_result, config.econn_refuse_state);
		xasprintf(&inital_connect_result.output, "Connection to %s on port %i was REFUSED", config.server_address, config.server_port);
		mp_add_subcheck_to_check(&overall, inital_connect_result);
		mp_exit(overall);
	} else {
		inital_connect_result = mp_set_subcheck_state(inital_connect_result, STATE_OK);
		xasprintf(&inital_connect_result.output, "Connection to %s on port %i was a SUCCESS", config.server_address, config.server_port);
		mp_add_subcheck_to_check(&overall, inital_connect_result);
	}

#ifdef HAVE_SSL
	if (config.use_tls) {
		mp_subcheck tls_connection_result = mp_subcheck_init();
		int result = np_net_ssl_init_with_hostname(socket_descriptor, (config.sni_specified ? config.sni : NULL));
		tls_connection_result = mp_set_subcheck_state(tls_connection_result, result);

		if (result == STATE_OK) {
			xasprintf(&tls_connection_result.output, "TLS connection succeeded");

			if (config.check_cert) {
				result = np_net_ssl_check_cert(config.days_till_exp_warn, config.days_till_exp_crit);

				mp_subcheck tls_certificate_lifetime_result = mp_subcheck_init();
				tls_certificate_lifetime_result = mp_set_subcheck_state(tls_certificate_lifetime_result, result);

				if (result == STATE_OK) {
					xasprintf(&tls_certificate_lifetime_result.output, "Certificate lifetime is within thresholds");
				} else if (result == STATE_WARNING) {
					xasprintf(&tls_certificate_lifetime_result.output, "Certificate lifetime is violating warning threshold (%i)",
							  config.days_till_exp_warn);
				} else if (result == STATE_CRITICAL) {
					xasprintf(&tls_certificate_lifetime_result.output, "Certificate lifetime is violating critical threshold (%i)",
							  config.days_till_exp_crit);
				} else {
					xasprintf(&tls_certificate_lifetime_result.output, "Certificate lifetime is somehow unknown");
				}

				mp_add_subcheck_to_subcheck(&tls_connection_result, tls_certificate_lifetime_result);
			}

			mp_add_subcheck_to_check(&overall, tls_connection_result);
		} else {
			xasprintf(&tls_connection_result.output, "TLS connection failed");
			mp_add_subcheck_to_check(&overall, tls_connection_result);

			if (socket_descriptor) {
				close(socket_descriptor);
			}
			np_net_ssl_cleanup();

			mp_exit(overall);
		}
	}
#endif /* HAVE_SSL */

	if (config.send != NULL) { /* Something to send? */
		my_send(config.send, strlen(config.send));
	}

	if (config.delay > 0) {
		start_time.tv_sec += config.delay;
		sleep(config.delay);
	}

	if (verbosity > 0) {
		if (config.send) {
			printf("Send string: %s\n", config.send);
		}
		if (config.quit) {
			printf("Quit string: %s\n", config.quit);
		}
		printf("server_expect_count: %d\n", (int)config.server_expect_count);
		for (size_t i = 0; i < config.server_expect_count; i++) {
			printf("\t%zd: %s\n", i, config.server_expect[i]);
		}
	}

	/* if(len) later on, we know we have a non-NULL response */
	ssize_t len = 0;
	char *status = NULL;
	int match = -1;
	mp_subcheck expected_data_result = mp_subcheck_init();

	if (config.server_expect_count) {
		ssize_t received = 0;
		char buffer[MAXBUF];

		/* watch for the expect string */
		while ((received = my_recv(buffer, sizeof(buffer))) > 0) {
			status = realloc(status, len + received + 1);

			if (status == NULL) {
				die(STATE_UNKNOWN, _("Allocation failed"));
			}

			memcpy(&status[len], buffer, received);
			len += received;
			status[len] = '\0';

			/* stop reading if user-forced */
			if (config.maxbytes && len >= config.maxbytes) {
				break;
			}

			if ((match = np_expect_match(status, config.server_expect, config.server_expect_count, config.match_flags)) != NP_MATCH_RETRY) {
				break;
			}

			fd_set rfds;
			FD_ZERO(&rfds);
			FD_SET(socket_descriptor, &rfds);

			/* some protocols wait for further input, so make sure we don't wait forever */
			struct timeval timeout;
			timeout.tv_sec = READ_TIMEOUT;
			timeout.tv_usec = 0;

			if (select(socket_descriptor + 1, &rfds, NULL, NULL, &timeout) <= 0) {
				break;
			}
		}

		if (match == NP_MATCH_RETRY) {
			match = NP_MATCH_FAILURE;
		}

		/* no data when expected, so return critical */
		if (len == 0) {
			xasprintf(&expected_data_result.output, "Received no data when some was expected");
			expected_data_result = mp_set_subcheck_state(expected_data_result, STATE_CRITICAL);
			mp_add_subcheck_to_check(&overall, expected_data_result);
			mp_exit(overall);
		}

		/* print raw output if we're debugging */
		if (verbosity > 0) {
			printf("received %d bytes from host\n#-raw-recv-------#\n%s\n#-raw-recv-------#\n", (int)len + 1, status);
		}
		/* strip whitespace from end of output */
		while (--len > 0 && isspace(status[len])) {
			status[len] = '\0';
		}
	}

	if (config.quit != NULL) {
		my_send(config.quit, strlen(config.quit));
	}

	if (socket_descriptor) {
		close(socket_descriptor);
	}
#ifdef HAVE_SSL
	np_net_ssl_cleanup();
#endif

	long microsec = deltime(start_time);
	double elapsed_time = (double)microsec / 1.0e6;

	mp_subcheck elapsed_time_result = mp_subcheck_init();

	mp_perfdata time_pd = perfdata_init();
	time_pd = mp_set_pd_value(time_pd, elapsed_time);
	time_pd.label = "time";
	time_pd.uom = "s";

	if (config.critical_time_set && elapsed_time > config.critical_time) {
		xasprintf(&elapsed_time_result.output, "Connection time %fs exceeded critical threshold (%f)", elapsed_time, config.critical_time);

		elapsed_time_result = mp_set_subcheck_state(elapsed_time_result, STATE_CRITICAL);
		time_pd.crit_present = true;
		mp_range crit_val = mp_range_init();

		crit_val.end = mp_create_pd_value(config.critical_time);
		crit_val.end_infinity = false;

		time_pd.crit = crit_val;
	} else if (config.warning_time_set && elapsed_time > config.warning_time) {
		xasprintf(&elapsed_time_result.output, "Connection time %fs exceeded warning threshold (%f)", elapsed_time, config.critical_time);

		elapsed_time_result = mp_set_subcheck_state(elapsed_time_result, STATE_WARNING);
		time_pd.warn_present = true;
		mp_range warn_val = mp_range_init();
		warn_val.end = mp_create_pd_value(config.critical_time);
		warn_val.end_infinity = false;

		time_pd.warn = warn_val;
	} else {
		elapsed_time_result = mp_set_subcheck_state(elapsed_time_result, STATE_OK);
		xasprintf(&elapsed_time_result.output, "Connection time %fs is within thresholds", elapsed_time);
	}

	mp_add_perfdata_to_subcheck(&elapsed_time_result, time_pd);
	mp_add_subcheck_to_check(&overall, elapsed_time_result);

	/* did we get the response we hoped? */
	if (match == NP_MATCH_FAILURE) {
		expected_data_result = mp_set_subcheck_state(expected_data_result, config.expect_mismatch_state);
		xasprintf(&expected_data_result.output, "Answer failed to match expectation");
		mp_add_subcheck_to_check(&overall, expected_data_result);
	}

	/* reset the alarm */
	alarm(0);

	mp_exit(overall);
}

/* process command-line arguments */
static check_tcp_config_wrapper process_arguments(int argc, char **argv, check_tcp_config config) {
	enum {
		SNI_OPTION = CHAR_MAX + 1,
		output_format_index,
	};

	static struct option longopts[] = {{"hostname", required_argument, 0, 'H'},
									   {"critical", required_argument, 0, 'c'},
									   {"warning", required_argument, 0, 'w'},
									   {"critical-codes", required_argument, 0, 'C'},
									   {"warning-codes", required_argument, 0, 'W'},
									   {"timeout", required_argument, 0, 't'},
									   {"protocol", required_argument, 0, 'P'}, /* FIXME: Unhandled */
									   {"port", required_argument, 0, 'p'},
									   {"escape", no_argument, 0, 'E'},
									   {"all", no_argument, 0, 'A'},
									   {"send", required_argument, 0, 's'},
									   {"expect", required_argument, 0, 'e'},
									   {"maxbytes", required_argument, 0, 'm'},
									   {"quit", required_argument, 0, 'q'},
									   {"jail", no_argument, 0, 'j'},
									   {"delay", required_argument, 0, 'd'},
									   {"refuse", required_argument, 0, 'r'},
									   {"mismatch", required_argument, 0, 'M'},
									   {"use-ipv4", no_argument, 0, '4'},
									   {"use-ipv6", no_argument, 0, '6'},
									   {"verbose", no_argument, 0, 'v'},
									   {"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'h'},
									   {"ssl", no_argument, 0, 'S'},
									   {"sni", required_argument, 0, SNI_OPTION},
									   {"certificate", required_argument, 0, 'D'},
									   {"output-format", required_argument, 0, output_format_index},
									   {0, 0, 0, 0}};

	if (argc < 2) {
		usage4(_("No arguments found"));
	}

	/* backwards compatibility */
	for (int i = 1; i < argc; i++) {
		if (strcmp("-to", argv[i]) == 0) {
			strcpy(argv[i], "-t");
		} else if (strcmp("-wt", argv[i]) == 0) {
			strcpy(argv[i], "-w");
		} else if (strcmp("-ct", argv[i]) == 0) {
			strcpy(argv[i], "-c");
		}
	}

	if (!is_option(argv[1])) {
		config.server_address = argv[1];
		argv[1] = argv[0];
		argv = &argv[1];
		argc--;
	}

	bool escape = false;

	while (true) {
		int option = 0;
		int option_index = getopt_long(argc, argv, "+hVv46EAH:s:e:q:m:c:w:t:p:C:W:d:Sr:jD:M:", longopts, &option);

		if (option_index == -1 || option_index == EOF || option_index == 1) {
			break;
		}

		switch (option_index) {
		case '?': /* print short usage statement if args not parsable */
			usage5();
		case 'h': /* help */
			print_help(config.service);
			exit(STATE_UNKNOWN);
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'v': /* verbose mode */
			verbosity++;
			config.match_flags |= NP_MATCH_VERBOSE;
			break;
		case '4': // Apparently unused TODO
			address_family = AF_INET;
			break;
		case '6': // Apparently unused TODO
#ifdef USE_IPV6
			address_family = AF_INET6;
#else
			usage4(_("IPv6 support not available"));
#endif
			break;
		case 'H': /* hostname */
			config.host_specified = true;
			config.server_address = optarg;
			break;
		case 'c': /* critical */
			config.critical_time = strtod(optarg, NULL);
			config.critical_time_set = true;
			break;
		case 'j': /* hide output */
			config.hide_output = true;
			break;
		case 'w': /* warning */
			config.warning_time = strtod(optarg, NULL);
			config.warning_time_set = true;
			break;
		case 't': /* timeout */
			if (!is_intpos(optarg)) {
				usage4(_("Timeout interval must be a positive integer"));
			} else {
				socket_timeout = atoi(optarg);
			}
			break;
		case 'p': /* port */
			if (!is_intpos(optarg)) {
				usage4(_("Port must be a positive integer"));
			} else {
				config.server_port = atoi(optarg);
			}
			break;
		case 'E':
			escape = true;
			break;
		case 's':
			if (escape) {
				config.send = np_escaped_string(optarg);
			} else {
				xasprintf(&config.send, "%s", optarg);
			}
			break;
		case 'e': /* expect string (may be repeated) */
			config.match_flags &= ~NP_MATCH_EXACT;
			if (config.server_expect_count == 0) {
				config.server_expect = malloc(sizeof(char *) * (++config.server_expect_count));
			} else {
				config.server_expect = realloc(config.server_expect, sizeof(char *) * (++config.server_expect_count));
			}

			if (config.server_expect == NULL) {
				die(STATE_UNKNOWN, _("Allocation failed"));
			}
			config.server_expect[config.server_expect_count - 1] = optarg;
			break;
		case 'm':
			if (!is_intpos(optarg)) {
				usage4(_("Maxbytes must be a positive integer"));
			} else {
				config.maxbytes = strtol(optarg, NULL, 0);
			}
			break;
		case 'q':
			if (escape) {
				config.quit = np_escaped_string(optarg);
			} else {
				xasprintf(&config.quit, "%s\r\n", optarg);
			}
			break;
		case 'r':
			if (!strncmp(optarg, "ok", 2)) {
				config.econn_refuse_state = STATE_OK;
			} else if (!strncmp(optarg, "warn", 4)) {
				config.econn_refuse_state = STATE_WARNING;
			} else if (!strncmp(optarg, "crit", 4)) {
				config.econn_refuse_state = STATE_CRITICAL;
			} else {
				usage4(_("Refuse must be one of ok, warn, crit"));
			}
			break;
		case 'M':
			if (!strncmp(optarg, "ok", 2)) {
				config.expect_mismatch_state = STATE_OK;
			} else if (!strncmp(optarg, "warn", 4)) {
				config.expect_mismatch_state = STATE_WARNING;
			} else if (!strncmp(optarg, "crit", 4)) {
				config.expect_mismatch_state = STATE_CRITICAL;
			} else {
				usage4(_("Mismatch must be one of ok, warn, crit"));
			}
			break;
		case 'd':
			if (is_intpos(optarg)) {
				config.delay = atoi(optarg);
			} else {
				usage4(_("Delay must be a positive integer"));
			}
			break;
		case 'D': /* Check SSL cert validity - days 'til certificate expiration */
#ifdef HAVE_SSL
#	ifdef USE_OPENSSL /* XXX */
		{
			char *temp;
			if ((temp = strchr(optarg, ',')) != NULL) {
				*temp = '\0';
				if (!is_intnonneg(optarg)) {
					usage2(_("Invalid certificate expiration period"), optarg);
				}
				config.days_till_exp_warn = atoi(optarg);
				*temp = ',';
				temp++;
				if (!is_intnonneg(temp)) {
					usage2(_("Invalid certificate expiration period"), temp);
				}
				config.days_till_exp_crit = atoi(temp);
			} else {
				config.days_till_exp_crit = 0;
				if (!is_intnonneg(optarg)) {
					usage2(_("Invalid certificate expiration period"), optarg);
				}
				config.days_till_exp_warn = atoi(optarg);
			}
			config.check_cert = true;
			config.use_tls = true;
		} break;
#	endif /* USE_OPENSSL */
#endif
			/* fallthrough if we don't have ssl */
		case 'S':
#ifdef HAVE_SSL
			config.use_tls = true;
#else
			die(STATE_UNKNOWN, _("Invalid option - SSL is not available"));
#endif
			break;
		case SNI_OPTION:
#ifdef HAVE_SSL
			config.use_tls = true;
			config.sni_specified = true;
			config.sni = optarg;
#else
			die(STATE_UNKNOWN, _("Invalid option - SSL is not available"));
#endif
			break;
		case 'A':
			config.match_flags |= NP_MATCH_ALL;
			break;
		case output_format_index: {
			parsed_output_format parser = mp_parse_output_format(optarg);
			if (!parser.parsing_success) {
				// TODO List all available formats here, maybe add anothoer usage function
				printf("Invalid output format: %s\n", optarg);
				exit(STATE_UNKNOWN);
			}

			config.output_format_set = true;
			config.output_format = parser.output_format;
			break;
		}
		}
	}

	int index = optind;
	if (!config.host_specified && index < argc) {
		config.server_address = strdup(argv[index++]);
	}

	if (config.server_address == NULL) {
		usage4(_("You must provide a server address"));
	} else if (config.server_address[0] != '/' && !is_host(config.server_address)) {
		die(STATE_CRITICAL, "%s %s - %s: %s\n", config.service, state_text(STATE_CRITICAL), _("Invalid hostname, address or socket"),
			config.server_address);
	}

	check_tcp_config_wrapper result = {
		.config = config,
		.errorcode = OK,
	};
	return result;
}

void print_help(const char *service) {
	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf(COPYRIGHT, copyright, email);

	printf(_("This plugin tests %s connections with the specified host (or unix socket).\n\n"), service);

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(UT_HOST_PORT, 'p', "none");

	printf(UT_IPv46);

	printf(" %s\n", "-E, --escape");
	printf("    %s\n", _("Can use \\n, \\r, \\t or \\\\ in send or quit string. Must come before send or quit option"));
	printf("    %s\n", _("Default: nothing added to send, \\r\\n added to end of quit"));
	printf(" %s\n", "-s, --send=STRING");
	printf("    %s\n", _("String to send to the server"));
	printf(" %s\n", "-e, --expect=STRING");
	printf("    %s %s\n", _("String to expect in server response"), _("(may be repeated)"));
	printf(" %s\n", "-A, --all");
	printf("    %s\n", _("All expect strings need to occur in server response. Default is any"));
	printf(" %s\n", "-q, --quit=STRING");
	printf("    %s\n", _("String to send server to initiate a clean close of the connection"));
	printf(" %s\n", "-r, --refuse=ok|warn|crit");
	printf("    %s\n", _("Accept TCP refusals with states ok, warn, crit (default: crit)"));
	printf(" %s\n", "-M, --mismatch=ok|warn|crit");
	printf("    %s\n", _("Accept expected string mismatches with states ok, warn, crit (default: warn)"));
	printf(" %s\n", "-j, --jail");
	printf("    %s\n", _("Hide output from TCP socket"));
	printf(" %s\n", "-m, --maxbytes=INTEGER");
	printf("    %s\n", _("Close connection once more than this number of bytes are received"));
	printf(" %s\n", "-d, --delay=INTEGER");
	printf("    %s\n", _("Seconds to wait between sending string and polling for response"));

#ifdef HAVE_SSL
	printf(" %s\n", "-D, --certificate=INTEGER[,INTEGER]");
	printf("    %s\n", _("Minimum number of days a certificate has to be valid."));
	printf("    %s\n", _("1st is #days for warning, 2nd is critical (if not specified - 0)."));
	printf(" %s\n", "-S, --ssl");
	printf("    %s\n", _("Use SSL for the connection."));
	printf(" %s\n", "--sni=STRING");
	printf("    %s\n", _("SSL server_name"));
#endif

	printf(UT_WARN_CRIT);

	printf(UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf(UT_OUTPUT_FORMAT);
	printf(UT_VERBOSE);

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s -H host -p port [-w <warning time>] [-c <critical time>] [-s <send string>]\n", progname);
	printf("[-e <expect string>] [-q <quit string>][-m <maximum bytes>] [-d <delay>]\n");
	printf("[-t <timeout seconds>] [-r <refuse state>] [-M <mismatch state>] [-v] [-4|-6] [-j]\n");
	printf("[-D <warn days cert expire>[,<crit days cert expire>]] [-S <use SSL>] [-E]\n");
}
