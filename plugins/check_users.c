/*****************************************************************************
 *
 * Monitoring check_users plugin
 *
 * License: GPL
 * Copyright (c) 2000-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_users plugin
 *
 * This plugin checks the number of users currently logged in on the local
 * system and generates an error if the number exceeds the thresholds
 * specified.
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

const char *progname = "check_users";
const char *copyright = "2000-2024";
const char *email = "devel@monitoring-plugins.org";

#include "check_users.d/users.h"
#include "output.h"
#include "perfdata.h"
#include "states.h"
#include "utils_base.h"
#include "./common.h"
#include "./utils.h"
#include "check_users.d/config.h"
#include "thresholds.h"

#if HAVE_WTSAPI32_H
#	include <windows.h>
#	include <wtsapi32.h>
#	undef ERROR
#	define ERROR -1
#elif HAVE_UTMPX_H
#	include <utmpx.h>
#else
#	include "popen.h"
#endif

#ifdef HAVE_LIBSYSTEMD
#	include <systemd/sd-daemon.h>
#	include <systemd/sd-login.h>
#endif

typedef struct process_argument_wrapper {
	int errorcode;
	check_users_config config;
} check_users_config_wrapper;
check_users_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);

void print_help(void);
void print_usage(void);

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_users_config_wrapper tmp_config = process_arguments(argc, argv);

	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	check_users_config config = tmp_config.config;

#ifdef _WIN32
#	if HAVE_WTSAPI32_H
	get_num_of_users_wrapper user_wrapper = get_num_of_users_windows();
#	else
#		error Did not find WTSAPI32
#	endif // HAVE_WTSAPI32_H
#else
#	ifdef HAVE_LIBSYSTEMD
	get_num_of_users_wrapper user_wrapper = get_num_of_users_systemd();
#	elif HAVE_UTMPX_H
	get_num_of_users_wrapper user_wrapper = get_num_of_users_utmp();
#	else  // !HAVE_LIBSYSTEMD && !HAVE_UTMPX_H
	get_num_of_users_wrapper user_wrapper = get_num_of_users_who_command();
#	endif // HAVE_LIBSYSTEMD
#endif     // _WIN32

	mp_check overall = mp_check_init();
	if (config.output_format_is_set) {
		mp_set_format(config.output_format);
	}
	mp_subcheck sc_users = mp_subcheck_init();

	if (user_wrapper.errorcode != 0) {
		sc_users = mp_set_subcheck_state(sc_users, STATE_UNKNOWN);
		sc_users.output = "Failed to retrieve number of users";
		mp_add_subcheck_to_check(&overall, sc_users);
		mp_exit(overall);
	}
	/* check the user count against warning and critical thresholds */

	mp_perfdata users_pd = {
		.label = "users",
		.value = mp_create_pd_value(user_wrapper.users),
	};

	users_pd = mp_pd_set_thresholds(users_pd, config.thresholds);
	mp_add_perfdata_to_subcheck(&sc_users, users_pd);

	int tmp_status = mp_get_pd_status(users_pd);
	sc_users = mp_set_subcheck_state(sc_users, tmp_status);

	switch (tmp_status) {
	case STATE_WARNING:
		xasprintf(&sc_users.output, "%d users currently logged in. This violates the warning threshold", user_wrapper.users);
		break;
	case STATE_CRITICAL:
		xasprintf(&sc_users.output, "%d users currently logged in. This violates the critical threshold", user_wrapper.users);
		break;
	default:
		xasprintf(&sc_users.output, "%d users currently logged in", user_wrapper.users);
	}

	mp_add_subcheck_to_check(&overall, sc_users);
	mp_exit(overall);
}

#define output_format_index CHAR_MAX + 1

/* process command-line arguments */
check_users_config_wrapper process_arguments(int argc, char **argv) {
	static struct option longopts[] = {{"critical", required_argument, 0, 'c'},
									   {"warning", required_argument, 0, 'w'},
									   {"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'h'},
									   {"output-format", required_argument, 0, output_format_index},
									   {0, 0, 0, 0}};

	if (argc < 2) {
		usage(progname);
	}

	char *warning_range = NULL;
	char *critical_range = NULL;
	check_users_config_wrapper result = {
		.config = check_users_config_init(),
		.errorcode = OK,
	};

	while (true) {
		int counter = getopt_long(argc, argv, "+hVvc:w:", longopts, NULL);

		if (counter == -1 || counter == EOF || counter == 1) {
			break;
		}

		switch (counter) {
		case '?': /* print short usage statement if args not parsable */
			usage5();
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'c': /* critical */
			critical_range = optarg;
			break;
		case 'w': /* warning */
			warning_range = optarg;
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

	int option_char = optind;

	if (warning_range == NULL && argc > option_char) {
		warning_range = argv[option_char++];
	}

	if (critical_range == NULL && argc > option_char) {
		critical_range = argv[option_char++];
	}

	// TODO add proper verification for ranges here!
	mp_range_parsed tmp;
	if (warning_range) {
		tmp = mp_parse_range_string(warning_range);
	} else {
		printf("Warning threshold missing\n");
		print_usage();
		exit(STATE_UNKNOWN);
		}

	if (tmp.error == MP_PARSING_SUCCES) {
		result.config.thresholds.warning = tmp.range;
		result.config.thresholds.warning_is_set = true;
	} else {
		printf("Failed to parse warning range: %s", warning_range);
		exit(STATE_UNKNOWN);
	}

	if (critical_range) {
		tmp = mp_parse_range_string(critical_range);
	} else {
		printf("Critical threshold missing\n");
		print_usage();
		exit(STATE_UNKNOWN);
	}

	if (tmp.error == MP_PARSING_SUCCES) {
		result.config.thresholds.critical = tmp.range;
		result.config.thresholds.critical_is_set = true;
	} else {
		printf("Failed to parse critical range: %s", critical_range);
		exit(STATE_UNKNOWN);
	}

	return result;
}

void print_help(void) {
	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Ethan Galstad\n");
	printf(COPYRIGHT, copyright, email);

	printf("%s\n", _("This plugin checks the number of users currently logged in on the local"));
	printf("%s\n", _("system and generates an error if the number exceeds the thresholds specified."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(" %s\n", "-w, --warning=RANGE_EXPRESSION");
	printf("    %s\n", _("Set WARNING status if number of logged in users violates RANGE_EXPRESSION"));
	printf(" %s\n", "-c, --critical=RANGE_EXPRESSION");
	printf("    %s\n", _("Set CRITICAL status if number of logged in users violates RANGE_EXPRESSION"));
	printf(UT_OUTPUT_FORMAT);

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s -w <users> -c <users>\n", progname);
}
