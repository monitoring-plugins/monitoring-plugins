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

static mp_state_enum do_query(dbi_conn /*conn*/, const char ** /*res_val_str*/, double * /*res_val*/, double * /*res_time*/, mp_dbi_metric /*metric*/,
					mp_dbi_type /*type*/, char * /*np_dbi_query*/);

int main(int argc, char **argv) {
	int status = STATE_UNKNOWN;

	dbi_driver driver;
	dbi_conn conn;

	unsigned int server_version;

	struct timeval start_timeval;
	struct timeval end_timeval;
	double conn_time = 0.0;
	double query_time = 0.0;

	const char *query_val_str = NULL;
	double query_val = 0.0;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_dbi_config_wrapper tmp = process_arguments(argc, argv);

	if (tmp.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	check_dbi_config config = tmp.config;

	/* Set signal handling and alarm */
	if (signal(SIGALRM, timeout_alarm_handler) == SIG_ERR) {
		usage4(_("Cannot catch SIGALRM"));
	}
	alarm(timeout_interval);

	if (verbose > 2) {
		printf("Initializing DBI\n");
	}

	dbi_inst *instance_p = {0};

	if (dbi_initialize_r(NULL, instance_p) < 0) {
		printf("UNKNOWN - failed to initialize DBI; possibly you don't have any drivers installed.\n");
		return STATE_UNKNOWN;
	}

	if (instance_p == NULL) {
		printf("UNKNOWN - failed to initialize DBI.\n");
		return STATE_UNKNOWN;
	}

	if (verbose) {
		printf("Opening DBI driver '%s'\n", config.dbi_driver);
	}

	driver = dbi_driver_open_r(config.dbi_driver, instance_p);
	if (!driver) {
		printf("UNKNOWN - failed to open DBI driver '%s'; possibly it's not installed.\n", config.dbi_driver);

		printf("Known drivers:\n");
		for (driver = dbi_driver_list_r(NULL, instance_p); driver; driver = dbi_driver_list_r(driver, instance_p)) {
			printf(" - %s\n", dbi_driver_get_name(driver));
		}
		return STATE_UNKNOWN;
	}

	/* make a connection to the database */
	gettimeofday(&start_timeval, NULL);

	conn = dbi_conn_open(driver);
	if (!conn) {
		printf("UNKNOWN - failed top open connection object.\n");
		dbi_conn_close(conn);
		return STATE_UNKNOWN;
	}

	for (size_t i = 0; i < config.dbi_options_num; ++i) {
		const char *opt;

		if (verbose > 1) {
			printf("Setting DBI driver option '%s' to '%s'\n", config.dbi_options[i].key, config.dbi_options[i].value);
		}

		if (!dbi_conn_set_option(conn, config.dbi_options[i].key, config.dbi_options[i].value)) {
			continue;
		}
		/* else: status != 0 */

		np_dbi_print_error(conn, "UNKNOWN - failed to set option '%s' to '%s'", config.dbi_options[i].key, config.dbi_options[i].value);
		printf("Known driver options:\n");

		for (opt = dbi_conn_get_option_list(conn, NULL); opt; opt = dbi_conn_get_option_list(conn, opt)) {
			printf(" - %s\n", opt);
		}
		dbi_conn_close(conn);
		return STATE_UNKNOWN;
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
		np_dbi_print_error(conn, "UNKNOWN - failed to connect to database");
		return STATE_UNKNOWN;
	}

	gettimeofday(&end_timeval, NULL);
	conn_time = timediff(start_timeval, end_timeval);

	server_version = dbi_conn_get_engine_version(conn);
	if (verbose) {
		printf("Connected to server version %u\n", server_version);
	}

	if (config.metric == METRIC_SERVER_VERSION) {
		status = get_status(server_version, config.dbi_thresholds);
	}

	if (verbose) {
		printf("Time elapsed: %f\n", conn_time);
	}

	if (config.metric == METRIC_CONN_TIME) {
		status = get_status(conn_time, config.dbi_thresholds);
	}

	/* select a database */
	if (config.dbi_database) {
		if (verbose > 1) {
			printf("Selecting database '%s'\n", config.dbi_database);
		}

		if (dbi_conn_select_db(conn, config.dbi_database)) {
			np_dbi_print_error(conn, "UNKNOWN - failed to select database '%s'", config.dbi_database);
			return STATE_UNKNOWN;
		}
	}

	if (config.dbi_query) {
		/* execute query */
		status = do_query(conn, &query_val_str, &query_val, &query_time, config.metric, config.type, config.dbi_query);
		if (status != STATE_OK) {
			/* do_query prints an error message in this case */
			return status;
		}

		if (config.metric == METRIC_QUERY_RESULT) {
			if (config.expect) {
				if ((!query_val_str) || strcmp(query_val_str, config.expect)) {
					status = STATE_CRITICAL;
				} else {
					status = STATE_OK;
				}
			} else if (config.expect_re_str) {
				int err;

				regex_t expect_re = {};
				err = regexec(&expect_re, query_val_str, 0, NULL, /* flags = */ 0);
				if (!err) {
					status = STATE_OK;
				} else if (err == REG_NOMATCH) {
					status = STATE_CRITICAL;
				} else {
					char errmsg[1024];
					regerror(err, &expect_re, errmsg, sizeof(errmsg));
					printf("ERROR - failed to execute regular expression: %s\n", errmsg);
					status = STATE_CRITICAL;
				}
			} else {
				status = get_status(query_val, config.dbi_thresholds);
			}
		} else if (config.metric == METRIC_QUERY_TIME) {
			status = get_status(query_time, config.dbi_thresholds);
		}
	}

	if (verbose) {
		printf("Closing connection\n");
	}
	dbi_conn_close(conn);

	/* In case of METRIC_QUERY_RESULT, isnan(query_val) indicates an error
	 * which should have been reported and handled (abort) before
	 * ... unless we expected a string to be returned */
	assert((config.metric != METRIC_QUERY_RESULT) || (!isnan(query_val)) || (config.type == TYPE_STRING));

	assert((config.type != TYPE_STRING) || (config.expect || config.expect_re_str));

	printf("%s - connection time: %fs", state_text(status), conn_time);
	if (config.dbi_query) {
		if (config.type == TYPE_STRING) {
			assert(config.expect || config.expect_re_str);
			printf(", '%s' returned '%s' in %fs", config.dbi_query, query_val_str ? query_val_str : "<nothing>", query_time);
			if (status != STATE_OK) {
				if (config.expect) {
					printf(" (expected '%s')", config.expect);
				} else if (config.expect_re_str) {
					printf(" (expected regex /%s/%s)", config.expect_re_str, ((config.expect_re_cflags & REG_ICASE) ? "i" : ""));
				}
			}
		} else if (isnan(query_val)) {
			printf(", '%s' query execution time: %fs", config.dbi_query, query_time);
		} else {
			printf(", '%s' returned %f in %fs", config.dbi_query, query_val, query_time);
		}
	}

	printf(" | conntime=%fs;%s;%s;0; server_version=%u;%s;%s;0;", conn_time,
		   ((config.metric == METRIC_CONN_TIME) && config.warning_range) ? config.warning_range : "",
		   ((config.metric == METRIC_CONN_TIME) && config.critical_range) ? config.critical_range : "", server_version,
		   ((config.metric == METRIC_SERVER_VERSION) && config.warning_range) ? config.warning_range : "",
		   ((config.metric == METRIC_SERVER_VERSION) && config.critical_range) ? config.critical_range : "");
	if (config.dbi_query) {
		if (!isnan(query_val)) { /* this is also true when -e is used */
			printf(" query=%f;%s;%s;;", query_val, ((config.metric == METRIC_QUERY_RESULT) && config.warning_range) ? config.warning_range : "",
				   ((config.metric == METRIC_QUERY_RESULT) && config.critical_range) ? config.critical_range : "");
		}
		printf(" querytime=%fs;%s;%s;0;", query_time, ((config.metric == METRIC_QUERY_TIME) && config.warning_range) ? config.warning_range : "",
			   ((config.metric == METRIC_QUERY_TIME) && config.critical_range) ? config.critical_range : "");
	}
	printf("\n");
	return status;
}

/* process command-line arguments */
check_dbi_config_wrapper process_arguments(int argc, char **argv) {

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

			new = realloc(result.config.dbi_options, (result.config.dbi_options_num + 1) * sizeof(*new));
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
		}
	}

	set_thresholds(&result.config.dbi_thresholds, result.config.warning_range, result.config.critical_range);

	return validate_arguments(result);
}

check_dbi_config_wrapper validate_arguments(check_dbi_config_wrapper config_wrapper) {
	if (!config_wrapper.config.dbi_driver) {
		usage("Must specify a DBI driver");
	}

	if (((config_wrapper.config.metric == METRIC_QUERY_RESULT) || (config_wrapper.config.metric == METRIC_QUERY_TIME)) &&
		(!config_wrapper.config.dbi_query)) {
		usage("Must specify a query to execute (metric == QUERY_RESULT)");
	}

	if ((config_wrapper.config.metric != METRIC_CONN_TIME) && (config_wrapper.config.metric != METRIC_SERVER_VERSION) &&
		(config_wrapper.config.metric != METRIC_QUERY_RESULT) && (config_wrapper.config.metric != METRIC_QUERY_TIME)) {
		usage("Invalid metric specified");
	}

	if (config_wrapper.config.expect && (config_wrapper.config.warning_range || config_wrapper.config.critical_range || config_wrapper.config.expect_re_str)) {
		usage("Do not mix -e and -w/-c/-r/-R");
	}

	if (config_wrapper.config.expect_re_str && (config_wrapper.config.warning_range || config_wrapper.config.critical_range || config_wrapper.config.expect)) {
		usage("Do not mix -r/-R and -w/-c/-e");
	}

	if (config_wrapper.config.expect && (config_wrapper.config.metric != METRIC_QUERY_RESULT)) {
		usage("Option -e requires metric QUERY_RESULT");
	}

	if (config_wrapper.config.expect_re_str && (config_wrapper.config.metric != METRIC_QUERY_RESULT)) {
		usage("Options -r/-R require metric QUERY_RESULT");
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

const char *get_field_str(dbi_conn conn, dbi_result res, unsigned short field_type, mp_dbi_metric metric, mp_dbi_type type) {
	const char *str;

	if (field_type != DBI_TYPE_STRING) {
		printf("CRITICAL - result value is not a string\n");
		return NULL;
	}

	str = dbi_result_get_string_idx(res, 1);
	if ((!str) || (strcmp(str, "ERROR") == 0)) {
		if (metric != METRIC_QUERY_RESULT) {
			return NULL;
		}
		np_dbi_print_error(conn, "CRITICAL - failed to fetch string value");
		return NULL;
	}

	if ((verbose && (type == TYPE_STRING)) || (verbose > 2)) {
		printf("Query returned string '%s'\n", str);
	}
	return str;
}

double get_field(dbi_conn conn, dbi_result res, unsigned short *field_type, mp_dbi_metric metric, mp_dbi_type type) {
	double val = NAN;

	if (*field_type == DBI_TYPE_INTEGER) {
		val = (double)dbi_result_get_longlong_idx(res, 1);
	} else if (*field_type == DBI_TYPE_DECIMAL) {
		val = dbi_result_get_double_idx(res, 1);
	} else if (*field_type == DBI_TYPE_STRING) {
		const char *val_str;
		char *endptr = NULL;

		val_str = get_field_str(conn, res, *field_type, metric, type);
		if (!val_str) {
			if (metric != METRIC_QUERY_RESULT) {
				return NAN;
			}
			*field_type = DBI_TYPE_ERROR;
			return NAN;
		}

		val = strtod(val_str, &endptr);
		if (endptr == val_str) {
			if (metric != METRIC_QUERY_RESULT) {
				return NAN;
			}
			printf("CRITICAL - result value is not a numeric: %s\n", val_str);
			*field_type = DBI_TYPE_ERROR;
			return NAN;
		}
		if ((endptr != NULL) && (*endptr != '\0')) {
			if (verbose) {
				printf("Garbage after value: %s\n", endptr);
			}
		}
	} else {
		if (metric != METRIC_QUERY_RESULT) {
			return NAN;
		}
		printf("CRITICAL - cannot parse value of type %s (%i)\n",
			   (*field_type == DBI_TYPE_BINARY)     ? "BINARY"
			   : (*field_type == DBI_TYPE_DATETIME) ? "DATETIME"
													: "<unknown>",
			   *field_type);
		*field_type = DBI_TYPE_ERROR;
		return NAN;
	}
	return val;
}

mp_state_enum get_query_result(dbi_conn conn, dbi_result res, const char **res_val_str, double *res_val, mp_dbi_metric metric, mp_dbi_type type) {
	unsigned short field_type;
	double val = NAN;

	if (dbi_result_get_numrows(res) == DBI_ROW_ERROR) {
		if (metric != METRIC_QUERY_RESULT) {
			return STATE_OK;
		}
		np_dbi_print_error(conn, "CRITICAL - failed to fetch rows");
		return STATE_CRITICAL;
	}

	if (dbi_result_get_numrows(res) < 1) {
		if (metric != METRIC_QUERY_RESULT) {
			return STATE_OK;
		}
		printf("WARNING - no rows returned\n");
		return STATE_WARNING;
	}

	if (dbi_result_get_numfields(res) == DBI_FIELD_ERROR) {
		if (metric != METRIC_QUERY_RESULT) {
			return STATE_OK;
		}
		np_dbi_print_error(conn, "CRITICAL - failed to fetch fields");
		return STATE_CRITICAL;
	}

	if (dbi_result_get_numfields(res) < 1) {
		if (metric != METRIC_QUERY_RESULT) {
			return STATE_OK;
		}
		printf("WARNING - no fields returned\n");
		return STATE_WARNING;
	}

	if (dbi_result_first_row(res) != 1) {
		if (metric != METRIC_QUERY_RESULT) {
			return STATE_OK;
		}
		np_dbi_print_error(conn, "CRITICAL - failed to fetch first row");
		return STATE_CRITICAL;
	}

	field_type = dbi_result_get_field_type_idx(res, 1);
	if (field_type != DBI_TYPE_ERROR) {
		if (type == TYPE_STRING) {
			/* the value will be freed in dbi_result_free */
			*res_val_str = strdup(get_field_str(conn, res, field_type, metric, type));
		} else {
			val = get_field(conn, res, &field_type, metric, type);
		}
	}

	*res_val = val;

	if (field_type == DBI_TYPE_ERROR) {
		if (metric != METRIC_QUERY_RESULT) {
			return STATE_OK;
		}
		np_dbi_print_error(conn, "CRITICAL - failed to fetch data");
		return STATE_CRITICAL;
	}

	dbi_result_free(res);
	return STATE_OK;
}

mp_state_enum do_query(dbi_conn conn, const char **res_val_str, double *res_val, double *res_time, mp_dbi_metric metric, mp_dbi_type type,
			 char *np_dbi_query) {
	dbi_result res;

	struct timeval timeval_start;
	struct timeval timeval_end;
	mp_state_enum status = STATE_OK;

	assert(np_dbi_query);

	if (verbose) {
		printf("Executing query '%s'\n", np_dbi_query);
	}

	gettimeofday(&timeval_start, NULL);

	res = dbi_conn_query(conn, np_dbi_query);
	if (!res) {
		np_dbi_print_error(conn, "CRITICAL - failed to execute query '%s'", np_dbi_query);
		return STATE_CRITICAL;
	}

	status = get_query_result(conn, res, res_val_str, res_val, metric, type);

	gettimeofday(&timeval_end, NULL);
	*res_time = timediff(timeval_start, timeval_end);

	if (verbose) {
		printf("Time elapsed: %f\n", *res_time);
	}

	return status;
}

double timediff(struct timeval start, struct timeval end) {
	double diff;

	while (start.tv_usec > end.tv_usec) {
		--end.tv_sec;
		end.tv_usec += 1000000;
	}
	diff = (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_usec - start.tv_usec) / 1000000.0;
	return diff;
}

void np_dbi_print_error(dbi_conn conn, char *fmt, ...) {
	const char *errmsg = NULL;
	va_list ap;

	va_start(ap, fmt);

	dbi_conn_error(conn, &errmsg);
	vprintf(fmt, ap);
	printf(": %s\n", errmsg);

	va_end(ap);
}
