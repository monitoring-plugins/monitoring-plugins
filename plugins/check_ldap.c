/*****************************************************************************
 *
 * Monitoring check_ldap plugin
 *
 * License: GPL
 * Copyright (c) 2000-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_ldap plugin
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

/* progname may be check_ldaps */
char *progname = "check_ldap";
const char *copyright = "2000-2024";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "netutils.h"
#include "utils.h"
#include "check_ldap.d/config.h"

#include "states.h"
#include <lber.h>
#define LDAP_DEPRECATED 1
#include <ldap.h>

enum {
	DEFAULT_PORT = 389
};

typedef struct {
	int errorcode;
	check_ldap_config config;
} check_ldap_config_wrapper;
static check_ldap_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static check_ldap_config_wrapper validate_arguments(check_ldap_config_wrapper /*config_wrapper*/);

static void print_help(void);
void print_usage(void);

#ifndef LDAP_OPT_SUCCESS
#	define LDAP_OPT_SUCCESS LDAP_SUCCESS
#endif
static int verbose = 0;

int main(int argc, char *argv[]) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	if (strstr(argv[0], "check_ldaps")) {
		xasprintf(&progname, "check_ldaps");
	}

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_ldap_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	const check_ldap_config config = tmp_config.config;

	/* initialize alarm signal handling */
	signal(SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm(socket_timeout);

	/* get the start time */
	struct timeval start_time;
	gettimeofday(&start_time, NULL);

	LDAP *ldap_connection;
	/* initialize ldap */
#ifdef HAVE_LDAP_INIT
	if (!(ldap_connection = ldap_init(config.ld_host, config.ld_port))) {
		printf("Could not connect to the server at port %i\n", config.ld_port);
		return STATE_CRITICAL;
	}
#else
	if (!(ld = ldap_open(config.ld_host, config.ld_port))) {
		if (verbose) {
			ldap_perror(ldap_connection, "ldap_open");
		}
		printf(_("Could not connect to the server at port %i\n"), config.ld_port);
		return STATE_CRITICAL;
	}
#endif /* HAVE_LDAP_INIT */

#ifdef HAVE_LDAP_SET_OPTION
	/* set ldap options */
	if (ldap_set_option(ldap_connection, LDAP_OPT_PROTOCOL_VERSION, &config.ld_protocol) !=
		LDAP_OPT_SUCCESS) {
		printf(_("Could not set protocol version %d\n"), config.ld_protocol);
		return STATE_CRITICAL;
	}
#endif

	int version = 3;
	int tls;
	if (config.ld_port == LDAPS_PORT || config.ssl_on_connect) {
#if defined(HAVE_LDAP_SET_OPTION) && defined(LDAP_OPT_X_TLS)
		/* ldaps: set option tls */
		tls = LDAP_OPT_X_TLS_HARD;

		if (ldap_set_option(ldap_connection, LDAP_OPT_X_TLS, &tls) != LDAP_SUCCESS) {
			if (verbose) {
				ldap_perror(ldap_connection, "ldaps_option");
			}
			printf(_("Could not init TLS at port %i!\n"), config.ld_port);
			return STATE_CRITICAL;
		}
#else
		printf(_("TLS not supported by the libraries!\n"));
		return STATE_CRITICAL;
#endif /* LDAP_OPT_X_TLS */
	} else if (config.starttls) {
#if defined(HAVE_LDAP_SET_OPTION) && defined(HAVE_LDAP_START_TLS_S)
		/* ldap with startTLS: set option version */
		if (ldap_get_option(ldap_connection, LDAP_OPT_PROTOCOL_VERSION, &version) ==
			LDAP_OPT_SUCCESS) {
			if (version < LDAP_VERSION3) {
				version = LDAP_VERSION3;
				ldap_set_option(ldap_connection, LDAP_OPT_PROTOCOL_VERSION, &version);
			}
		}
		/* call start_tls */
		if (ldap_start_tls_s(ldap_connection, NULL, NULL) != LDAP_SUCCESS) {
			if (verbose) {
				ldap_perror(ldap_connection, "ldap_start_tls");
			}
			printf(_("Could not init startTLS at port %i!\n"), config.ld_port);
			return STATE_CRITICAL;
		}
#else
		printf(_("startTLS not supported by the library, needs LDAPv3!\n"));
		return STATE_CRITICAL;
#endif /* HAVE_LDAP_START_TLS_S */
	}

	/* bind to the ldap server */
	if (ldap_bind_s(ldap_connection, config.ld_binddn, config.ld_passwd, LDAP_AUTH_SIMPLE) !=
		LDAP_SUCCESS) {
		if (verbose) {
			ldap_perror(ldap_connection, "ldap_bind");
		}
		printf(_("Could not bind to the LDAP server\n"));
		return STATE_CRITICAL;
	}

	LDAPMessage *result;
	int num_entries = 0;
	/* do a search of all objectclasses in the base dn */
	if (ldap_search_s(ldap_connection, config.ld_base,
					  (config.crit_entries != NULL || config.warn_entries != NULL)
						  ? LDAP_SCOPE_SUBTREE
						  : LDAP_SCOPE_BASE,
					  config.ld_attr, NULL, 0, &result) != LDAP_SUCCESS) {
		if (verbose) {
			ldap_perror(ldap_connection, "ldap_search");
		}
		printf(_("Could not search/find objectclasses in %s\n"), config.ld_base);
		return STATE_CRITICAL;
	}

	if (config.crit_entries != NULL || config.warn_entries != NULL) {
		num_entries = ldap_count_entries(ldap_connection, result);
	}

	/* unbind from the ldap server */
	ldap_unbind(ldap_connection);

	/* reset the alarm handler */
	alarm(0);

	/* calculate the elapsed time and compare to thresholds */

	long microsec = deltime(start_time);
	double elapsed_time = (double)microsec / 1.0e6;
	mp_state_enum status = STATE_UNKNOWN;
	if (config.crit_time_set && elapsed_time > config.crit_time) {
		status = STATE_CRITICAL;
	} else if (config.warn_time_set && elapsed_time > config.warn_time) {
		status = STATE_WARNING;
	} else {
		status = STATE_OK;
	}

	if (config.entries_thresholds != NULL) {
		if (verbose) {
			printf("entries found: %d\n", num_entries);
			print_thresholds("entry thresholds", config.entries_thresholds);
		}
		mp_state_enum status_entries = get_status(num_entries, config.entries_thresholds);
		if (status_entries == STATE_CRITICAL) {
			status = STATE_CRITICAL;
		} else if (status != STATE_CRITICAL) {
			status = status_entries;
		}
	}

	/* print out the result */
	if (config.crit_entries != NULL || config.warn_entries != NULL) {
		printf(_("LDAP %s - found %d entries in %.3f seconds|%s %s\n"), state_text(status),
			   num_entries, elapsed_time,
			   fperfdata("time", elapsed_time, "s", config.warn_time_set, config.warn_time,
						 config.crit_time_set, config.crit_time, true, 0, false, 0),
			   sperfdata("entries", (double)num_entries, "", config.warn_entries,
						 config.crit_entries, true, 0.0, false, 0.0));
	} else {
		printf(_("LDAP %s - %.3f seconds response time|%s\n"), state_text(status), elapsed_time,
			   fperfdata("time", elapsed_time, "s", config.warn_time_set, config.warn_time,
						 config.crit_time_set, config.crit_time, true, 0, false, 0));
	}

	exit(status);
}

/* process command-line arguments */
check_ldap_config_wrapper process_arguments(int argc, char **argv) {
	/* initialize the long option struct */
	static struct option longopts[] = {{"help", no_argument, 0, 'h'},
									   {"version", no_argument, 0, 'V'},
									   {"timeout", required_argument, 0, 't'},
									   {"hostname", required_argument, 0, 'H'},
									   {"base", required_argument, 0, 'b'},
									   {"attr", required_argument, 0, 'a'},
									   {"bind", required_argument, 0, 'D'},
									   {"pass", required_argument, 0, 'P'},
#ifdef HAVE_LDAP_SET_OPTION
									   {"ver2", no_argument, 0, '2'},
									   {"ver3", no_argument, 0, '3'},
#endif
									   {"starttls", no_argument, 0, 'T'},
									   {"ssl", no_argument, 0, 'S'},
									   {"use-ipv4", no_argument, 0, '4'},
									   {"use-ipv6", no_argument, 0, '6'},
									   {"port", required_argument, 0, 'p'},
									   {"warn", required_argument, 0, 'w'},
									   {"crit", required_argument, 0, 'c'},
									   {"warn-entries", required_argument, 0, 'W'},
									   {"crit-entries", required_argument, 0, 'C'},
									   {"verbose", no_argument, 0, 'v'},
									   {0, 0, 0, 0}};

	check_ldap_config_wrapper result = {
		.errorcode = OK,
		.config = check_ldap_config_init(),
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

	int option = 0;
	while (true) {
		int option_index =
			getopt_long(argc, argv, "hvV234TS6t:c:w:H:b:p:a:D:P:C:W:", longopts, &option);

		if (option_index == -1 || option_index == EOF) {
			break;
		}

		switch (option_index) {
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 't': /* timeout period */
			if (!is_intnonneg(optarg)) {
				usage2(_("Timeout interval must be a positive integer"), optarg);
			} else {
				socket_timeout = atoi(optarg);
			}
			break;
		case 'H':
			result.config.ld_host = optarg;
			break;
		case 'b':
			result.config.ld_base = optarg;
			break;
		case 'p':
			result.config.ld_port = atoi(optarg);
			break;
		case 'a':
			result.config.ld_attr = optarg;
			break;
		case 'D':
			result.config.ld_binddn = optarg;
			break;
		case 'P':
			result.config.ld_passwd = optarg;
			break;
		case 'w':
			result.config.warn_time_set = true;
			result.config.warn_time = strtod(optarg, NULL);
			break;
		case 'c':
			result.config.crit_time_set = true;
			result.config.crit_time = strtod(optarg, NULL);
			break;
		case 'W':
			result.config.warn_entries = optarg;
			break;
		case 'C':
			result.config.crit_entries = optarg;
			break;
#ifdef HAVE_LDAP_SET_OPTION
		case '2':
			result.config.ld_protocol = 2;
			break;
		case '3':
			result.config.ld_protocol = 3;
			break;
#endif // HAVE_LDAP_SET_OPTION
		case '4':
			address_family = AF_INET;
			break;
		case 'v':
			verbose++;
			break;
		case 'T':
			if (!result.config.ssl_on_connect) {
				result.config.starttls = true;
			} else {
				usage_va(_("%s cannot be combined with %s"), "-T/--starttls", "-S/--ssl");
			}
			break;
		case 'S':
			if (!result.config.starttls) {
				result.config.ssl_on_connect = true;
				if (result.config.ld_port == -1) {
					result.config.ld_port = LDAPS_PORT;
				}
			} else {
				usage_va(_("%s cannot be combined with %s"), "-S/--ssl", "-T/--starttls");
			}
			break;
		case '6':
#ifdef USE_IPV6
			address_family = AF_INET6;
#else
			usage(_("IPv6 support not available\n"));
#endif
			break;
		default:
			usage5();
		}
	}

	int index = optind;
	if ((result.config.ld_host == NULL) && is_host(argv[index])) {
		result.config.ld_host = strdup(argv[index++]);
	}

	if ((result.config.ld_base == NULL) && argv[index]) {
		result.config.ld_base = strdup(argv[index++]);
	}

	if (result.config.ld_port == -1) {
		result.config.ld_port = DEFAULT_PORT;
	}

	if (strstr(argv[0], "check_ldaps") && !result.config.starttls &&
		!result.config.ssl_on_connect) {
		result.config.starttls = true;
	}

	return validate_arguments(result);
}

check_ldap_config_wrapper validate_arguments(check_ldap_config_wrapper config_wrapper) {
	if (config_wrapper.config.ld_host == NULL || strlen(config_wrapper.config.ld_host) == 0) {
		usage4(_("Please specify the host name\n"));
	}

	if (config_wrapper.config.ld_base == NULL) {
		usage4(_("Please specify the LDAP base\n"));
	}

	if (config_wrapper.config.crit_entries != NULL || config_wrapper.config.warn_entries != NULL) {
		set_thresholds(&config_wrapper.config.entries_thresholds,
					   config_wrapper.config.warn_entries, config_wrapper.config.crit_entries);
	}

	if (config_wrapper.config.ld_passwd == NULL) {
		config_wrapper.config.ld_passwd = getenv("LDAP_PASSWORD");
	}

	return config_wrapper;
}

void print_help(void) {
	char *myport;
	xasprintf(&myport, "%d", DEFAULT_PORT);

	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Didi Rieder (adrieder@sbox.tu-graz.ac.at)\n");
	printf(COPYRIGHT, copyright, email);

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(UT_HOST_PORT, 'p', myport);

	printf(UT_IPv46);

	printf(" %s\n", "-a [--attr]");
	printf("    %s\n", _("ldap attribute to search (default: \"(objectclass=*)\""));
	printf(" %s\n", "-b [--base]");
	printf("    %s\n", _("ldap base (eg. ou=my unit, o=my org, c=at"));
	printf(" %s\n", "-D [--bind]");
	printf("    %s\n", _("ldap bind DN (if required)"));
	printf(" %s\n", "-P [--pass]");
	printf("    %s\n", _("ldap password (if required, or set the password through environment "
						 "variable 'LDAP_PASSWORD')"));
	printf(" %s\n", "-T [--starttls]");
	printf("    %s\n", _("use starttls mechanism introduced in protocol version 3"));
	printf(" %s\n", "-S [--ssl]");
	printf("    %s %i\n", _("use ldaps (ldap v2 ssl method). this also sets the default port to"),
		   LDAPS_PORT);

#ifdef HAVE_LDAP_SET_OPTION
	printf(" %s\n", "-2 [--ver2]");
	printf("    %s\n", _("use ldap protocol version 2"));
	printf(" %s\n", "-3 [--ver3]");
	printf("    %s\n", _("use ldap protocol version 3"));
	printf("    (%s %d)\n", _("default protocol version:"), DEFAULT_PROTOCOL);
#endif

	printf(UT_WARN_CRIT);

	printf(" %s\n", "-W [--warn-entries]");
	printf("    %s\n", _("Number of found entries to result in warning status"));
	printf(" %s\n", "-C [--crit-entries]");
	printf("    %s\n", _("Number of found entries to result in critical status"));

	printf(UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf(UT_VERBOSE);

	printf("\n");
	printf("%s\n", _("Notes:"));
	printf(" %s\n", _("If this plugin is called via 'check_ldaps', method 'STARTTLS' will be"));
	printf(_(" implied (using default port %i) unless --port=636 is specified. In that case\n"),
		   DEFAULT_PORT);
	printf(" %s\n", _("'SSL on connect' will be used no matter how the plugin was called."));
	printf(" %s\n", _("This detection is deprecated, please use 'check_ldap' with the '--starttls' "
					  "or '--ssl' flags"));
	printf(" %s\n", _("to define the behaviour explicitly instead."));
	printf(" %s\n", _("The parameters --warn-entries and --crit-entries are optional."));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf(" %s -H <host> -b <base_dn> [-p <port>] [-a <attr>] [-D <binddn>]", progname);
	printf("\n       [-P <password>] [-w <warn_time>] [-c <crit_time>] [-t timeout]%s\n",
#ifdef HAVE_LDAP_SET_OPTION
		   "\n       [-2|-3] [-4|-6]"
#else
		   ""
#endif
	);
}
