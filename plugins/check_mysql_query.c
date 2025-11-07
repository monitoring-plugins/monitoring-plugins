/*****************************************************************************
 *
 * Monitoring check_mysql_query plugin
 *
 * License: GPL
 * Copyright (c) 2006-2024 Monitoring Plugins Development Team
 * Original code from check_mysql, copyright 1999 Didi Rieder
 *
 * Description:
 *
 * This file contains the check_mysql_query plugin
 *
 * This plugin is for running arbitrary SQL and checking the results
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

#include "common.h"
#include "output.h"
#include "perfdata.h"
#include "states.h"
#include "thresholds.h"
#include "utils.h"
#include "utils_base.h"
#include "netutils.h"
#include "check_mysql_query.d/config.h"

#include <mysql.h>
#include <errmsg.h>

const char *progname = "check_mysql_query";
const char *copyright = "1999-2024";
const char *email = "devel@monitoring-plugins.org";

typedef struct {
	int errorcode;
	check_mysql_query_config config;
} check_mysql_query_config_wrapper;
static check_mysql_query_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static check_mysql_query_config_wrapper
	validate_arguments(check_mysql_query_config_wrapper /*config_wrapper*/);
static void print_help(void);
void print_usage(void);

static int verbose = 0;

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_mysql_query_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	const check_mysql_query_config config = tmp_config.config;

	MYSQL mysql;
	/* initialize mysql  */
	mysql_init(&mysql);

	if (config.opt_file != NULL) {
		mysql_options(&mysql, MYSQL_READ_DEFAULT_FILE, config.opt_file);
	}

	if (config.opt_group != NULL) {
		mysql_options(&mysql, MYSQL_READ_DEFAULT_GROUP, config.opt_group);
	} else {
		mysql_options(&mysql, MYSQL_READ_DEFAULT_GROUP, "client");
	}

	mp_check overall = mp_check_init();
	mp_subcheck sc_connect = mp_subcheck_init();

	/* establish a connection to the server and error checking */
	if (!mysql_real_connect(&mysql, config.db_host, config.db_user, config.db_pass, config.db,
							config.db_port, config.db_socket, 0)) {
		xasprintf(&sc_connect.output, "query failed: %s", mysql_error(&mysql));

		if (mysql_errno(&mysql) == CR_UNKNOWN_HOST) {
			sc_connect = mp_set_subcheck_state(sc_connect, STATE_WARNING);
		} else if (mysql_errno(&mysql) == CR_VERSION_ERROR) {
			sc_connect = mp_set_subcheck_state(sc_connect, STATE_WARNING);
		} else if (mysql_errno(&mysql) == CR_OUT_OF_MEMORY) {
			sc_connect = mp_set_subcheck_state(sc_connect, STATE_WARNING);
		} else if (mysql_errno(&mysql) == CR_IPSOCK_ERROR) {
			sc_connect = mp_set_subcheck_state(sc_connect, STATE_WARNING);
		} else if (mysql_errno(&mysql) == CR_SOCKET_CREATE_ERROR) {
			sc_connect = mp_set_subcheck_state(sc_connect, STATE_WARNING);
		} else {
			sc_connect = mp_set_subcheck_state(sc_connect, STATE_CRITICAL);
		}

		mp_add_subcheck_to_check(&overall, sc_connect);
		mp_exit(overall);
	}

	sc_connect = mp_set_subcheck_state(sc_connect, STATE_OK);
	xasprintf(&sc_connect.output, "query succeeded");
	mp_add_subcheck_to_check(&overall, sc_connect);

	if (mysql_query(&mysql, config.sql_query) != 0) {
		char *error = strdup(mysql_error(&mysql));
		mysql_close(&mysql);
		die(STATE_CRITICAL, "QUERY %s: %s - %s\n", _("CRITICAL"), _("Error with query"), error);
	}

	MYSQL_RES *res;
	/* store the result */
	if ((res = mysql_store_result(&mysql)) == NULL) {
		char *error = strdup(mysql_error(&mysql));
		mysql_close(&mysql);
		die(STATE_CRITICAL, "QUERY %s: Error with store_result - %s\n", _("CRITICAL"), error);
	}

	/* Check there is some data */
	if (mysql_num_rows(res) == 0) {
		mysql_close(&mysql);
		die(STATE_WARNING, "QUERY %s: %s\n", _("WARNING"), _("No rows returned"));
	}

	mp_subcheck sc_value = mp_subcheck_init();
	MYSQL_ROW row;
	/* fetch the first row */
	if ((row = mysql_fetch_row(res)) == NULL) {
		xasprintf(&sc_value.output, "fetch row error - %s", mysql_error(&mysql));
		mysql_free_result(res);
		mysql_close(&mysql);

		sc_value = mp_set_subcheck_state(sc_value, STATE_CRITICAL);
		mp_add_subcheck_to_check(&overall, sc_value);
		mp_exit(overall);
	}

	if (!is_numeric(row[0])) {
		xasprintf(&sc_value.output, "query result is not numeric");
		sc_value = mp_set_subcheck_state(sc_value, STATE_CRITICAL);
		mp_add_subcheck_to_check(&overall, sc_value);
		mp_exit(overall);
	}

	double value = strtod(row[0], NULL);

	/* free the result */
	mysql_free_result(res);

	/* close the connection */
	mysql_close(&mysql);

	if (verbose >= 3) {
		printf("mysql result: %f\n", value);
	}

	mp_perfdata pd_query_result = perfdata_init();
	pd_query_result = mp_set_pd_value(pd_query_result, value);
	pd_query_result = mp_pd_set_thresholds(pd_query_result, config.thresholds);
	pd_query_result.label = "result";
	mp_add_perfdata_to_subcheck(&sc_value, pd_query_result);

	sc_value = mp_set_subcheck_state(sc_value, mp_get_pd_status(pd_query_result));
	xasprintf(&sc_value.output, "'%s' returned '%f'", config.sql_query, value);

	mp_add_subcheck_to_check(&overall, sc_value);

	mp_exit(overall);
}

/* process command-line arguments */
check_mysql_query_config_wrapper process_arguments(int argc, char **argv) {
	static struct option longopts[] = {{"hostname", required_argument, 0, 'H'},
									   {"socket", required_argument, 0, 's'},
									   {"database", required_argument, 0, 'd'},
									   {"username", required_argument, 0, 'u'},
									   {"password", required_argument, 0, 'p'},
									   {"file", required_argument, 0, 'f'},
									   {"group", required_argument, 0, 'g'},
									   {"port", required_argument, 0, 'P'},
									   {"verbose", no_argument, 0, 'v'},
									   {"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'h'},
									   {"query", required_argument, 0, 'q'},
									   {"warning", required_argument, 0, 'w'},
									   {"critical", required_argument, 0, 'c'},
									   {0, 0, 0, 0}};

	check_mysql_query_config_wrapper result = {
		.errorcode = OK,
		.config = check_mysql_query_config_init(),
	};

	if (argc < 1) {
		result.errorcode = ERROR;
		return result;
	}

	while (true) {
		int option = 0;
		int option_char = getopt_long(argc, argv, "hvVP:p:u:d:H:s:q:w:c:f:g:", longopts, &option);

		if (option_char == -1 || option_char == EOF) {
			break;
		}

		switch (option_char) {
		case 'H': /* hostname */
			if (is_host(optarg)) {
				result.config.db_host = optarg;
			} else {
				usage2(_("Invalid hostname/address"), optarg);
			}
			break;
		case 's': /* socket */
			result.config.db_socket = optarg;
			break;
		case 'd': /* database */
			result.config.db = optarg;
			break;
		case 'u': /* username */
			result.config.db_user = optarg;
			break;
		case 'p': /* authentication information: password */
			result.config.db_pass = strdup(optarg);

			/* Delete the password from process list */
			while (*optarg != '\0') {
				*optarg = 'X';
				optarg++;
			}
			break;
		case 'f': /* client options file */
			result.config.opt_file = optarg;
			break;
		case 'g': /* client options group */
			result.config.opt_group = optarg;
			break;
		case 'P': /* critical time threshold */
			result.config.db_port = atoi(optarg);
			break;
		case 'v':
			verbose++;
			break;
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case 'q':
			xasprintf(&result.config.sql_query, "%s", optarg);
			break;
		case 'w': {
			mp_range_parsed tmp = mp_parse_range_string(optarg);
			if (tmp.error != MP_PARSING_SUCCES) {
				die(STATE_UNKNOWN, "failed to parse warning threshold");
			}
			result.config.thresholds = mp_thresholds_set_warn(result.config.thresholds, tmp.range);
		} break;
		case 'c': {
			mp_range_parsed tmp = mp_parse_range_string(optarg);
			if (tmp.error != MP_PARSING_SUCCES) {
				die(STATE_UNKNOWN, "failed to parse critical threshold");
			}
			result.config.thresholds = mp_thresholds_set_crit(result.config.thresholds, tmp.range);
		} break;
		case '?': /* help */
			usage5();
		}
	}

	return validate_arguments(result);
}

check_mysql_query_config_wrapper
validate_arguments(check_mysql_query_config_wrapper config_wrapper) {
	if (config_wrapper.config.sql_query == NULL) {
		usage("Must specify a SQL query to run");
	}

	if (config_wrapper.config.db_user == NULL) {
		config_wrapper.config.db_user = strdup("");
	}

	if (config_wrapper.config.db_host == NULL) {
		config_wrapper.config.db_host = strdup("");
	}

	if (config_wrapper.config.db == NULL) {
		config_wrapper.config.db = strdup("");
	}

	return config_wrapper;
}

void print_help(void) {
	char *myport;
	xasprintf(&myport, "%d", MYSQL_PORT);

	print_revision(progname, NP_VERSION);

	printf(_(COPYRIGHT), copyright, email);

	printf("%s\n", _("This program checks a query result against threshold levels"));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);
	printf(" -q, --query=STRING\n");
	printf("    %s\n", _("SQL query to run. Only first column in first row will be read"));
	printf(UT_WARN_CRIT_RANGE);
	printf(UT_HOST_PORT, 'P', myport);
	printf(" %s\n", "-s, --socket=STRING");
	printf("    %s\n", _("Use the specified socket (has no effect if -H is used)"));
	printf(" -d, --database=STRING\n");
	printf("    %s\n", _("Database to check"));
	printf(" %s\n", "-f, --file=STRING");
	printf("    %s\n", _("Read from the specified client options file"));
	printf(" %s\n", "-g, --group=STRING");
	printf("    %s\n", _("Use a client options group"));
	printf(" -u, --username=STRING\n");
	printf("    %s\n", _("Username to login with"));
	printf(" -p, --password=STRING\n");
	printf("    %s\n", _("Password to login with"));
	printf("    ==> %s <==\n", _("IMPORTANT: THIS FORM OF AUTHENTICATION IS NOT SECURE!!!"));
	printf("    %s\n", _("Your clear-text password could be visible as a process table entry"));

	printf("\n");
	printf(" %s\n", _("A query is required. The result from the query should be numeric."));
	printf(" %s\n", _("For extra security, create a user with minimal access."));

	printf("\n");
	printf("%s\n", _("Notes:"));
	printf(" %s\n", _("You must specify -p with an empty string to force an empty password,"));
	printf(" %s\n", _("overriding any my.cnf settings."));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf(" %s -q SQL_query [-w warn] [-c crit] [-H host] [-P port] [-s socket]\n", progname);
	printf("       [-d database] [-u user] [-p password] [-f optfile] [-g group]\n");
}
