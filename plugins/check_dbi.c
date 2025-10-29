/*****************************************************************************
 *
 * Monitoring check_dbi plugin
 *
 * License: GPL
 * Copyright (c) 2011-2024 Monitoring Plugins Development Team
 * Original Author: Sebastian 'tokkee' Harl <sh@teamix.net>
 *
 * Description:
 *
 * This file contains the check_dbi plugin
 *
 * Runs an arbitrary (SQL) command and checks the result.
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

const char *progname = "check_dbi";
const char *copyright = "2011-2024";
const char *email = "devel@monitoring-plugins.org";

#include "../lib/monitoringplug.h"
#include "perfdata.h"
#include "output.h"
#include "states.h"
#include "check_dbi.d/config.h"
#include "common.h"
#include "utils.h"
#include "utils_cmd.h"

#include "netutils.h"

#include "regex.h"

/* required for NAN */
#ifndef _ISOC99_SOURCE
#	define _ISOC99_SOURCE
#endif

#include <assert.h>
#include <math.h>

#include <dbi/dbi.h>

#include <stdarg.h>

static int verbose = 0;

typedef struct {
	int errorcode;
	check_dbi_config config;
} check_dbi_config_wrapper;

static check_dbi_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static check_dbi_config_wrapper validate_arguments(check_dbi_config_wrapper /*config_wrapper*/);
void print_usage(void);
static void print_help(void);

static double timediff(struct timeval /*start*/, struct timeval /*end*/);

static void np_dbi_print_error(dbi_conn /*conn*/, char * /*fmt*/, ...);

typedef struct {
	char *result_string;
	double result_number;
	double query_duration;
	int error_code;
	const char *error_string;
	mp_state_enum query_processing_status;
} do_query_result;
static do_query_result do_query(dbi_conn conn, mp_dbi_metric metric, mp_dbi_type type, char *query);

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_dbi_config_wrapper tmp = process_arguments(argc, argv);

	if (tmp.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	const check_dbi_config config = tmp.config;

	if (config.output_format_is_set) {
		mp_set_format(config.output_format);
	}

	/* Set signal handling and alarm */
	if (signal(SIGALRM, timeout_alarm_handler) == SIG_ERR) {
		usage4(_("Cannot catch SIGALRM"));
	}
	alarm(timeout_interval);

	if (verbose > 2) {
		printf("Initializing DBI\n");
	}

	dbi_inst instance_p = NULL;
	if (dbi_initialize_r(NULL, &instance_p) < 0) {
		printf("failed to initialize DBI; possibly you don't have any drivers installed.\n");
		exit(STATE_UNKNOWN);
	}

	// Try to prevent libdbi from printing stuff on stderr
	// Who thought that would be a good idea anyway?
	dbi_set_verbosity_r(0, instance_p);

	if (instance_p == NULL) {
		printf("failed to initialize DBI.\n");
		exit(STATE_UNKNOWN);
	}

	if (verbose) {
		printf("Opening DBI driver '%s'\n", config.dbi_driver);
	}

	dbi_driver driver = dbi_driver_open_r(config.dbi_driver, instance_p);
	if (!driver) {
		printf("failed to open DBI driver '%s'; possibly it's not installed.\n", config.dbi_driver);

		printf("Known drivers:\n");
		for (driver = dbi_driver_list_r(NULL, instance_p); driver;
			 driver = dbi_driver_list_r(driver, instance_p)) {
			printf(" - %s\n", dbi_driver_get_name(driver));
		}
		exit(STATE_UNKNOWN);
	}

	/* make a connection to the database */
	struct timeval start_timeval;
	gettimeofday(&start_timeval, NULL);

	dbi_conn conn = dbi_conn_open(driver);
	if (!conn) {
		printf("UNKNOWN - failed top open connection object.\n");
		dbi_conn_close(conn);
		exit(STATE_UNKNOWN);
	}

	for (size_t i = 0; i < config.dbi_options_num; ++i) {
		const char *opt;

		if (verbose > 1) {
			printf("Setting DBI driver option '%s' to '%s'\n", config.dbi_options[i].key,
				   config.dbi_options[i].value);
		}

		if (!dbi_conn_set_option(conn, config.dbi_options[i].key, config.dbi_options[i].value)) {
			continue;
		}

		// Failing to set option
		np_dbi_print_error(conn, "failed to set option '%s' to '%s'", config.dbi_options[i].key,
						   config.dbi_options[i].value);
		printf("Known driver options:\n");

		for (opt = dbi_conn_get_option_list(conn, NULL); opt;
			 opt = dbi_conn_get_option_list(conn, opt)) {
			printf(" - %s\n", opt);
		}
		dbi_conn_close(conn);
		exit(STATE_UNKNOWN);
	}

	if (config.host) {
		if (verbose > 1) {
			printf("Setting DBI driver option 'host' to '%s'\n", config.host);
		}
		dbi_conn_set_option(conn, "host", config.host);
	}

	if (verbose) {
		const char *dbname;
		const char *host;

		dbname = dbi_conn_get_option(conn, "dbname");
		host = dbi_conn_get_option(conn, "host");

		if (!dbname) {
			dbname = "<unspecified>";
		}
		if (!host) {
			host = "<unspecified>";
		}

		printf("Connecting to database '%s' at host '%s'\n", dbname, host);
	}

	if (dbi_conn_connect(conn) < 0) {
		np_dbi_print_error(conn, "failed to connect to database");
		exit(STATE_UNKNOWN);
	}

	struct timeval end_timeval;
	gettimeofday(&end_timeval, NULL);
	double conn_time = timediff(start_timeval, end_timeval);
	if (verbose) {
		printf("Time elapsed: %f\n", conn_time);
	}

	mp_check overall = mp_check_init();

	mp_subcheck sc_connection_time = mp_subcheck_init();
	sc_connection_time = mp_set_subcheck_default_state(sc_connection_time, STATE_OK);
	xasprintf(&sc_connection_time.output, "Connection time: %f", conn_time);

	mp_perfdata pd_conn_duration = perfdata_init();
	pd_conn_duration.label = "conntime";
	pd_conn_duration = mp_set_pd_value(pd_conn_duration, conn_time);

	if (config.metric == METRIC_CONN_TIME) {
		// TODO set pd thresholds
		mp_state_enum status = get_status(conn_time, config.dbi_thresholds);
		sc_connection_time = mp_set_subcheck_state(sc_connection_time, status);
		if (status != STATE_OK) {
			xasprintf(&sc_connection_time.output, "%s violates thresholds",
					  sc_connection_time.output);
		}
	}

	mp_add_perfdata_to_subcheck(&sc_connection_time, pd_conn_duration);
	mp_add_subcheck_to_check(&overall, sc_connection_time);

	unsigned int server_version = dbi_conn_get_engine_version(conn);
	if (verbose) {
		printf("Connected to server version %u\n", server_version);
	}

	mp_subcheck sc_server_version = mp_subcheck_init();
	sc_server_version = mp_set_subcheck_default_state(sc_server_version, STATE_OK);
	xasprintf(&sc_server_version.output, "Connected to server version %u", server_version);

	if (config.metric == METRIC_SERVER_VERSION) {
		mp_state_enum status = get_status(server_version, config.dbi_thresholds);

		sc_server_version = mp_set_subcheck_state(sc_server_version, status);

		if (status != STATE_OK) {
			xasprintf(&sc_server_version.output, "%s violates thresholds",
					  sc_server_version.output);
		}
	};
	mp_add_subcheck_to_check(&overall, sc_server_version);

	/* select a database */
	if (config.dbi_database) {
		if (verbose > 1) {
			printf("Selecting database '%s'\n", config.dbi_database);
		}

		mp_subcheck sc_select_db = mp_subcheck_init();
		sc_select_db = mp_set_subcheck_default_state(sc_select_db, STATE_OK);

		if (dbi_conn_select_db(conn, config.dbi_database)) {
			np_dbi_print_error(conn, "UNKNOWN - failed to select database '%s'",
							   config.dbi_database);
			exit(STATE_UNKNOWN);
		} else {
			mp_add_subcheck_to_check(&overall, sc_select_db);
		}
	}

	// Do a query (if configured)
	if (config.dbi_query) {
		mp_subcheck sc_query = mp_subcheck_init();
		sc_query = mp_set_subcheck_default_state(sc_query, STATE_UNKNOWN);

		/* execute query */
		do_query_result query_res = do_query(conn, config.metric, config.type, config.dbi_query);

		if (query_res.error_code != 0) {
			xasprintf(&sc_query.output, "Query failed: %s", query_res.error_string);
			sc_query = mp_set_subcheck_state(sc_query, STATE_CRITICAL);
		} else if (query_res.query_processing_status != STATE_OK) {
			if (query_res.error_string) {
				xasprintf(&sc_query.output, "Failed to process query: %s", query_res.error_string);
			} else {
				xasprintf(&sc_query.output, "Failed to process query");
			}
			sc_query = mp_set_subcheck_state(sc_query, query_res.query_processing_status);
		} else {
			// query succeeded in general
			xasprintf(&sc_query.output, "Query '%s' succeeded", config.dbi_query);

			// that's a OK by default now
			sc_query = mp_set_subcheck_default_state(sc_query, STATE_OK);

			// query duration first
			mp_perfdata pd_query_duration = perfdata_init();
			pd_query_duration = mp_set_pd_value(pd_query_duration, query_res.query_duration);
			pd_query_duration.label = "querytime";
			if (config.metric == METRIC_QUERY_TIME) {
				// TODO set thresholds
			}

			mp_add_perfdata_to_subcheck(&sc_query, pd_query_duration);

			if (config.metric == METRIC_QUERY_RESULT) {
				if (config.expect) {
					if ((!query_res.result_string) ||
						strcmp(query_res.result_string, config.expect)) {
						xasprintf(&sc_query.output, "Found string '%s' in query result",
								  config.expect);
						sc_query = mp_set_subcheck_state(sc_query, STATE_CRITICAL);
					} else {
						xasprintf(&sc_query.output, "Did not find string '%s' in query result",
								  config.expect);
						sc_query = mp_set_subcheck_state(sc_query, STATE_OK);
					}
				} else if (config.expect_re_str) {
					int comp_err;
					regex_t expect_re = {};
					comp_err = regcomp(&expect_re, config.expect_re_str, config.expect_re_cflags);
					if (comp_err != 0) {
						// TODO error, failed to compile regex
						// TODO move this to config sanitatisation
						printf("Failed to compile regex from string '%s'", config.expect_re_str);
						exit(STATE_UNKNOWN);
					}

					int err =
						regexec(&expect_re, query_res.result_string, 0, NULL, /* flags = */ 0);
					if (!err) {
						sc_query = mp_set_subcheck_state(sc_query, STATE_OK);
						xasprintf(&sc_query.output, "Found regular expression '%s' in query result",
								  config.expect_re_str);
					} else if (err == REG_NOMATCH) {
						sc_query = mp_set_subcheck_state(sc_query, STATE_CRITICAL);
						xasprintf(&sc_query.output,
								  "Did not find regular expression '%s' in query result",
								  config.expect_re_str);
					} else {
						char errmsg[1024];
						regerror(err, &expect_re, errmsg, sizeof(errmsg));
						xasprintf(&sc_query.output,
								  "ERROR - failed to execute regular expression: %s\n", errmsg);
						sc_query = mp_set_subcheck_state(sc_query, STATE_CRITICAL);
					}
				} else {
					// no string matching
					if (isnan(query_res.result_number)) {
						// The query result is not a number, but no string checking was configured
						// so we expected a number
						// this is a CRITICAL
						xasprintf(&sc_query.output, "Query '%s' result is not numeric",
								  config.dbi_query);
						sc_query = mp_set_subcheck_state(sc_query, STATE_CRITICAL);

					} else {
						mp_state_enum query_numerical_result =
							get_status(query_res.result_number, config.dbi_thresholds);
						sc_query = mp_set_subcheck_state(sc_query, query_numerical_result);

						mp_perfdata pd_query_val = perfdata_init();
						pd_query_val = mp_set_pd_value(pd_query_val, query_res.result_number);
						pd_query_val.label = "query";
						mp_add_perfdata_to_subcheck(&sc_query, pd_query_val);

						// TODO set pd thresholds
						// if (config.dbi_thresholds->warning) {
						// pd_query_val.warn= config.dbi_thresholds->warning
						// } else {
						// }

						if (query_numerical_result == STATE_OK) {
							xasprintf(&sc_query.output,
									  "Query result '%f' is within given thresholds",
									  query_res.result_number);
						} else {
							xasprintf(&sc_query.output,
									  "Query result '%f' violates the given thresholds",
									  query_res.result_number);
						}
					}
				}
			} else if (config.metric == METRIC_QUERY_TIME) {
				mp_state_enum query_time_status =
					get_status(query_res.query_duration, config.dbi_thresholds);
				mp_set_subcheck_state(sc_query, query_time_status);

				if (query_time_status == STATE_OK) {
					xasprintf(&sc_query.output, "Query duration '%f' is within given thresholds",
							  query_res.query_duration);
				} else {
					xasprintf(&sc_query.output, "Query duration '%f' violates the given thresholds",
							  query_res.query_duration);
				}
			} else {
				/* In case of METRIC_QUERY_RESULT, isnan(query_val) indicates an error
				 * which should have been reported and handled (abort) before
				 * ... unless we expected a string to be returned */
				assert((!isnan(query_res.result_number)) || (config.type == TYPE_STRING));
			}
		}

		mp_add_subcheck_to_check(&overall, sc_query);
	}

	if (verbose) {
		printf("Closing connection\n");
	}
	dbi_conn_close(conn);

	mp_exit(overall);
}

/* process command-line arguments */
check_dbi_config_wrapper process_arguments(int argc, char **argv) {
	enum {
		output_format_index = CHAR_MAX + 1,
	};

	int option = 0;
	static struct option longopts[] = {STD_LONG_OPTS,
									   {"expect", required_argument, 0, 'e'},
									   {"regex", required_argument, 0, 'r'},
									   {"regexi", required_argument, 0, 'R'},
									   {"metric", required_argument, 0, 'm'},
									   {"driver", required_argument, 0, 'd'},
									   {"option", required_argument, 0, 'o'},
									   {"query", required_argument, 0, 'q'},
									   {"database", required_argument, 0, 'D'},
									   {"output-format", required_argument, 0, output_format_index},
									   {0, 0, 0, 0}};

	check_dbi_config_wrapper result = {
		.config = check_dbi_config_init(),
		.errorcode = OK,
	};
	int option_char;
	while (true) {
		option_char = getopt_long(argc, argv, "Vvht:c:w:e:r:R:m:H:d:o:q:D:", longopts, &option);

		if (option_char == EOF) {
			break;
		}

		switch (option_char) {
		case '?': /* usage */
			usage5();
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);

		case 'c': /* critical range */
			result.config.critical_range = optarg;
			result.config.type = TYPE_NUMERIC;
			break;
		case 'w': /* warning range */
			result.config.warning_range = optarg;
			result.config.type = TYPE_NUMERIC;
			break;
		case 'e':
			result.config.expect = optarg;
			result.config.type = TYPE_STRING;
			break;
		case 'R':
			result.config.expect_re_cflags = REG_ICASE;
			/* fall through */
		case 'r': {
			int err;

			result.config.expect_re_cflags |= REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
			result.config.expect_re_str = optarg;
			result.config.type = TYPE_STRING;

			regex_t expect_re = {};
			err = regcomp(&expect_re, result.config.expect_re_str, result.config.expect_re_cflags);
			if (err) {
				char errmsg[1024];
				regerror(err, &expect_re, errmsg, sizeof(errmsg));
				printf("ERROR - failed to compile regular expression: %s\n", errmsg);

				result.errorcode = ERROR;
				return result;
			}
			break;
		}
		case 'm':
			if (!strcasecmp(optarg, "CONN_TIME")) {
				result.config.metric = METRIC_CONN_TIME;
			} else if (!strcasecmp(optarg, "SERVER_VERSION")) {
				result.config.metric = METRIC_SERVER_VERSION;
			} else if (!strcasecmp(optarg, "QUERY_RESULT")) {
				result.config.metric = METRIC_QUERY_RESULT;
			} else if (!strcasecmp(optarg, "QUERY_TIME")) {
				result.config.metric = METRIC_QUERY_TIME;
			} else {
				usage2(_("Invalid metric"), optarg);
			}
			break;
		case 't': /* timeout */
			if (!is_intnonneg(optarg)) {
				usage2(_("Timeout interval must be a positive integer"), optarg);
			} else {
				timeout_interval = atoi(optarg);
			}
			break;
		case 'H': /* host */
			if (!is_host(optarg)) {
				usage2(_("Invalid hostname/address"), optarg);
			} else {
				result.config.host = optarg;
			}
			break;
		case 'v':
			verbose++;
			break;
		case 'd':
			result.config.dbi_driver = optarg;
			break;
		case 'o': {
			driver_option_t *new = NULL;

			char *key = optarg;
			char *value = strchr(key, '=');

			if (!value) {
				usage2(_("Option must be '<key>=<value>'"), optarg);
			}

			*value = '\0';
			++value;

			new = realloc(result.config.dbi_options,
						  (result.config.dbi_options_num + 1) * sizeof(*new));
			if (!new) {
				printf("UNKNOWN - failed to reallocate memory\n");
				exit(STATE_UNKNOWN);
			}

			result.config.dbi_options = new;
			new = result.config.dbi_options + result.config.dbi_options_num;
			result.config.dbi_options_num++;

			new->key = key;
			new->value = value;
		} break;
		case 'q':
			result.config.dbi_query = optarg;
			break;
		case 'D':
			result.config.dbi_database = optarg;
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

	set_thresholds(&result.config.dbi_thresholds, result.config.warning_range,
				   result.config.critical_range);

	return validate_arguments(result);
}

check_dbi_config_wrapper validate_arguments(check_dbi_config_wrapper config_wrapper) {
	if (!config_wrapper.config.dbi_driver) {
		usage("Must specify a DBI driver");
	}

	if (((config_wrapper.config.metric == METRIC_QUERY_RESULT) ||
		 (config_wrapper.config.metric == METRIC_QUERY_TIME)) &&
		(!config_wrapper.config.dbi_query)) {
		usage("Must specify a query to execute (metric == QUERY_RESULT)");
	}

	if ((config_wrapper.config.metric != METRIC_CONN_TIME) &&
		(config_wrapper.config.metric != METRIC_SERVER_VERSION) &&
		(config_wrapper.config.metric != METRIC_QUERY_RESULT) &&
		(config_wrapper.config.metric != METRIC_QUERY_TIME)) {
		usage("Invalid metric specified");
	}

	if (config_wrapper.config.expect &&
		(config_wrapper.config.warning_range || config_wrapper.config.critical_range ||
		 config_wrapper.config.expect_re_str)) {
		usage("Do not mix -e and -w/-c/-r/-R");
	}

	if (config_wrapper.config.expect_re_str &&
		(config_wrapper.config.warning_range || config_wrapper.config.critical_range ||
		 config_wrapper.config.expect)) {
		usage("Do not mix -r/-R and -w/-c/-e");
	}

	if (config_wrapper.config.expect && (config_wrapper.config.metric != METRIC_QUERY_RESULT)) {
		usage("Option -e requires metric QUERY_RESULT");
	}

	if (config_wrapper.config.expect_re_str &&
		(config_wrapper.config.metric != METRIC_QUERY_RESULT)) {
		usage("Options -r/-R require metric QUERY_RESULT");
	}

	if (config_wrapper.config.type == TYPE_STRING) {
		assert(config_wrapper.config.expect || config_wrapper.config.expect_re_str);
	}

	config_wrapper.errorcode = OK;
	return config_wrapper;
}

void print_help(void) {
	print_revision(progname, NP_VERSION);

	printf(COPYRIGHT, copyright, email);

	printf(_("This program connects to an (SQL) database using DBI and checks the\n"
			 "specified metric against threshold levels. The default metric is\n"
			 "the result of the specified query.\n"));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
/* include this conditionally to avoid 'zero-length printf format string'
 * compiler warnings */
#ifdef NP_EXTRA_OPTS
	printf(UT_EXTRA_OPTS);
#endif
	printf("\n");

	printf(" %s\n", "-d, --driver=STRING");
	printf("    %s\n", _("DBI driver to use"));
	printf(" %s\n", "-o, --option=STRING");
	printf("    %s\n", _("DBI driver options"));
	printf(" %s\n", "-q, --query=STRING");
	printf("    %s\n", _("query to execute"));
	printf(" %s\n", "-H STRING");
	printf("    %s\n", _("target database host"));
	printf("\n");

	printf(UT_WARN_CRIT_RANGE);
	printf(" %s\n", "-e, --expect=STRING");
	printf("    %s\n", _("String to expect as query result"));
	printf("    %s\n", _("Do not mix with -w, -c, -r, or -R!"));
	printf(" %s\n", "-r, --regex=REGEX");
	printf("    %s\n", _("Extended POSIX regular expression to check query result against"));
	printf("    %s\n", _("Do not mix with -w, -c, -e, or -R!"));
	printf(" %s\n", "-R, --regexi=REGEX");
	printf("    %s\n", _("Case-insensitive extended POSIX regex to check query result against"));
	printf("    %s\n", _("Do not mix with -w, -c, -e, or -r!"));
	printf(" %s\n", "-m, --metric=METRIC");
	printf("    %s\n", _("Metric to check thresholds against. Available metrics:"));
	printf("    CONN_TIME    - %s\n", _("time used for setting up the database connection"));
	printf("    QUERY_RESULT - %s\n", _("result (first column of first row) of the query"));
	printf("    QUERY_TIME   - %s\n", _("time used to execute the query"));
	printf("                   %s\n", _("(ignore the query result)"));
	printf("\n");

	printf(UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf(UT_VERBOSE);

	printf("\n");
	printf(" %s\n", _("A DBI driver (-d option) is required. If the specified metric operates"));
	printf(" %s\n\n", _("on a query, one has to be specified (-q option)."));

	printf(" %s\n", _("This plugin connects to an (SQL) database using libdbi and, optionally,"));
	printf(" %s\n", _("executes the specified query. The first column of the first row of the"));
	printf(" %s\n", _("result will be parsed and, in QUERY_RESULT mode, compared with the"));
	printf(" %s\n", _("warning and critical ranges. The result from the query has to be numeric"));
	printf(" %s\n\n", _("(strings representing numbers are fine)."));

	printf(" %s\n", _("The number and type of required DBI driver options depends on the actual"));
	printf(" %s\n", _("driver. See its documentation at http://libdbi-drivers.sourceforge.net/"));
	printf(" %s\n\n", _("for details."));

	printf(" %s\n", _("Examples:"));
	printf("  check_dbi -d pgsql -o username=postgres -m QUERY_RESULT \\\n");
	printf("    -q 'SELECT COUNT(*) FROM pg_stat_activity' -w 5 -c 10\n");
	printf("  Warning if more than five connections; critical if more than ten.\n\n");

	printf("  check_dbi -d mysql -H localhost -o username=user -o password=secret \\\n");
	printf("    -q 'SELECT COUNT(*) FROM logged_in_users -w 5:20 -c 0:50\n");
	printf("  Warning if less than 5 or more than 20 users are logged in; critical\n");
	printf("  if more than 50 users.\n\n");

	printf("  check_dbi -d firebird -o username=user -o password=secret -o dbname=foo \\\n");
	printf("    -m CONN_TIME -w 0.5 -c 2\n");
	printf("  Warning if connecting to the database takes more than half of a second;\n");
	printf("  critical if it takes more than 2 seconds.\n\n");

	printf("  check_dbi -d mysql -H localhost -o username=user \\\n");
	printf("    -q 'SELECT concat(@@version, \" \", @@version_comment)' \\\n");
	printf("    -r '^5\\.[01].*MySQL Enterprise Server'\n");
	printf("  Critical if the database server is not a MySQL enterprise server in either\n");
	printf("  version 5.0.x or 5.1.x.\n\n");

	printf("  check_dbi -d pgsql -u username=user -m SERVER_VERSION \\\n");
	printf("    -w 090000:090099 -c 090000:090199\n");
	printf("  Warn if the PostgreSQL server version is not 9.0.x; critical if the version\n");
	printf("  is less than 9.x or higher than 9.1.x.\n");

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s -d <DBI driver> [-o <DBI driver option> [...]] [-q <query>]\n", progname);
	printf(" [-H <host>] [-c <critical range>] [-w <warning range>] [-m <metric>]\n");
	printf(" [-e <string>] [-r|-R <regex>]\n");
}

const char *get_field_str(dbi_result res, mp_dbi_metric metric, mp_dbi_type type) {
	const char *str = dbi_result_get_string_idx(res, 1);
	if ((!str) || (strcmp(str, "ERROR") == 0)) {
		if (metric != METRIC_QUERY_RESULT) {
			return NULL;
		}
		return NULL;
	}

	if ((verbose && (type == TYPE_STRING)) || (verbose > 2)) {
		printf("Query returned string '%s'\n", str);
	}
	return str;
}

typedef struct {
	double value;
	int error_code;
	int dbi_error_code; // not sure if useful
} get_field_wrapper;
get_field_wrapper get_field(dbi_result res, mp_dbi_metric metric, mp_dbi_type type) {

	unsigned short field_type = dbi_result_get_field_type_idx(res, 1);
	get_field_wrapper result = {
		.value = NAN,
		.error_code = OK,
	};

	if (field_type == DBI_TYPE_INTEGER) {
		result.value = (double)dbi_result_get_longlong_idx(res, 1);
	} else if (field_type == DBI_TYPE_DECIMAL) {
		result.value = dbi_result_get_double_idx(res, 1);
	} else if (field_type == DBI_TYPE_STRING) {
		const char *val_str;
		char *endptr = NULL;

		val_str = get_field_str(res, metric, type);
		if (!val_str) {
			result.error_code = ERROR;
			field_type = DBI_TYPE_ERROR;
			return result;
		}

		result.value = strtod(val_str, &endptr);
		if (endptr == val_str) {
			if (metric != METRIC_QUERY_RESULT) {
				result.error_code = ERROR;
				return result;
			}

			if (verbose) {
				printf("CRITICAL - result value is not a numeric: %s\n", val_str);
			}

			field_type = DBI_TYPE_ERROR;
			result.error_code = ERROR;
			return result;
		}

		if ((endptr != NULL) && (*endptr != '\0')) {
			if (verbose) {
				printf("Garbage after value: %s\n", endptr);
			}
		}
	} else {
		if (metric != METRIC_QUERY_RESULT) {
			result.error_code = ERROR;
			return result;
		}
		// printf("CRITICAL - cannot parse value of type %s (%i)\n",
		// (*field_type == DBI_TYPE_BINARY)     ? "BINARY"
		// : (*field_type == DBI_TYPE_DATETIME) ? "DATETIME"
		// : "<unknown>",
		// *field_type);
		field_type = DBI_TYPE_ERROR;
		result.error_code = ERROR;
	}
	return result;
}

static do_query_result do_query(dbi_conn conn, mp_dbi_metric metric, mp_dbi_type type,
								char *query) {
	assert(query);

	if (verbose) {
		printf("Executing query '%s'\n", query);
	}

	do_query_result result = {
		.query_duration = 0,
		.result_string = NULL,
		.result_number = 0,
		.error_code = 0,
		.query_processing_status = STATE_UNKNOWN,
	};

	struct timeval timeval_start;
	gettimeofday(&timeval_start, NULL);

	dbi_result res = dbi_conn_query(conn, query);
	if (!res) {
		dbi_conn_error(conn, &result.error_string);
		result.error_code = 1;
		return result;
	}

	struct timeval timeval_end;
	gettimeofday(&timeval_end, NULL);
	result.query_duration = timediff(timeval_start, timeval_end);

	if (verbose) {
		printf("Query duration: %f\n", result.query_duration);
	}

	// Default state is OK, all error will be set explicitly
	mp_state_enum query_processing_state = STATE_OK;
	{

		if (dbi_result_get_numrows(res) == DBI_ROW_ERROR) {
			if (metric != METRIC_QUERY_RESULT) {
				query_processing_state = STATE_OK;
			} else {
				dbi_conn_error(conn, &result.error_string);
				query_processing_state = STATE_CRITICAL;
			}
		} else if (dbi_result_get_numrows(res) < 1) {
			if (metric != METRIC_QUERY_RESULT) {
				query_processing_state = STATE_OK;
			} else {
				result.error_string = "no rows returned";
				// printf("WARNING - no rows returned\n");
				query_processing_state = STATE_WARNING;
			}
		} else if (dbi_result_get_numfields(res) == DBI_FIELD_ERROR) {
			if (metric != METRIC_QUERY_RESULT) {
				query_processing_state = STATE_OK;
			} else {
				dbi_conn_error(conn, &result.error_string);
				// np_dbi_print_error(conn, "CRITICAL - failed to fetch fields");
				query_processing_state = STATE_CRITICAL;
			}
		} else if (dbi_result_get_numfields(res) < 1) {
			if (metric != METRIC_QUERY_RESULT) {
				query_processing_state = STATE_OK;
			} else {
				result.error_string = "no fields returned";
				// printf("WARNING - no fields returned\n");
				query_processing_state = STATE_WARNING;
			}
		} else if (dbi_result_first_row(res) != 1) {
			if (metric != METRIC_QUERY_RESULT) {
				query_processing_state = STATE_OK;
			} else {
				dbi_conn_error(conn, &result.error_string);
				// np_dbi_print_error(conn, "CRITICAL - failed to fetch first row");
				query_processing_state = STATE_CRITICAL;
			}
		} else {
			unsigned short field_type = dbi_result_get_field_type_idx(res, 1);
			if (field_type != DBI_TYPE_ERROR) {
				if (type == TYPE_STRING) {
					result.result_string = strdup(get_field_str(res, metric, type));
				} else {
					get_field_wrapper gfw = get_field(res, metric, type);
					result.result_number = gfw.value;
				}
			} else {
				// Error when retrieving the field, that is OK if the Query result is not of
				// interest
				if (metric != METRIC_QUERY_RESULT) {
					query_processing_state = STATE_OK;
				} else {
					dbi_conn_error(conn, &result.error_string);
					// np_dbi_print_error(conn, "CRITICAL - failed to fetch data");
					query_processing_state = STATE_CRITICAL;
				}
			}
		}
	}
	dbi_result_free(res);

	result.query_processing_status = query_processing_state;

	return result;
}

static double timediff(struct timeval start, struct timeval end) {
	double diff;

	while (start.tv_usec > end.tv_usec) {
		--end.tv_sec;
		end.tv_usec += 1000000;
	}
	diff = (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_usec - start.tv_usec) / 1000000.0;
	return diff;
}

static void np_dbi_print_error(dbi_conn conn, char *fmt, ...) {
	const char *errmsg = NULL;
	va_list ap;

	va_start(ap, fmt);

	dbi_conn_error(conn, &errmsg);
	vprintf(fmt, ap);
	printf(": %s\n", errmsg);

	va_end(ap);
}
