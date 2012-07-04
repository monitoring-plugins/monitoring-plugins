/*****************************************************************************
* 
* Nagios check_dbi plugin
* 
* License: GPL
* Copyright (c) 2011 Nagios Plugins Development Team
* Author: Sebastian 'tokkee' Harl <sh@teamix.net>
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
const char *copyright = "2011";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "utils.h"

#include "netutils.h"

#include "regex.h"

/* required for NAN */
#ifndef _ISOC99_SOURCE
#define _ISOC99_SOURCE
#endif

#include <assert.h>
#include <math.h>

#include <dbi/dbi.h>

#include <stdarg.h>

typedef enum {
	METRIC_CONN_TIME,
	METRIC_SERVER_VERSION,
	METRIC_QUERY_RESULT,
	METRIC_QUERY_TIME,
} np_dbi_metric_t;

typedef enum {
	TYPE_NUMERIC,
	TYPE_STRING,
} np_dbi_type_t;

typedef struct {
	char *key;
	char *value;
} driver_option_t;

char *host = NULL;
int verbose = 0;

char *warning_range = NULL;
char *critical_range = NULL;
thresholds *dbi_thresholds = NULL;

char *expect = NULL;

regex_t expect_re;
char *expect_re_str = NULL;
int expect_re_cflags = 0;

np_dbi_metric_t metric = METRIC_QUERY_RESULT;
np_dbi_type_t type = TYPE_NUMERIC;

char *np_dbi_driver = NULL;
driver_option_t *np_dbi_options = NULL;
int np_dbi_options_num = 0;
char *np_dbi_database = NULL;
char *np_dbi_query = NULL;

int process_arguments (int, char **);
int validate_arguments (void);
void print_usage (void);
void print_help (void);

double timediff (struct timeval, struct timeval);

void np_dbi_print_error (dbi_conn, char *, ...);

int do_query (dbi_conn, const char **, double *, double *);

int
main (int argc, char **argv)
{
	int status = STATE_UNKNOWN;

	dbi_driver driver;
	dbi_conn conn;

	unsigned int server_version;

	struct timeval start_timeval, end_timeval;
	double conn_time = 0.0;
	double query_time = 0.0;

	const char *query_val_str = NULL;
	double query_val = 0.0;

	int i;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* Set signal handling and alarm */
	if (signal (SIGALRM, timeout_alarm_handler) == SIG_ERR) {
		usage4 (_("Cannot catch SIGALRM"));
	}
	alarm (timeout_interval);

	if (verbose > 2)
		printf ("Initializing DBI\n");

	if (dbi_initialize (NULL) < 0) {
		printf ("UNKNOWN - failed to initialize DBI; possibly you don't have any drivers installed.\n");
		return STATE_UNKNOWN;
	}

	if (verbose)
		printf ("Opening DBI driver '%s'\n", np_dbi_driver);

	driver = dbi_driver_open (np_dbi_driver);
	if (! driver) {
		printf ("UNKNOWN - failed to open DBI driver '%s'; possibly it's not installed.\n",
				np_dbi_driver);

		printf ("Known drivers:\n");
		for (driver = dbi_driver_list (NULL); driver; driver = dbi_driver_list (driver)) {
			printf (" - %s\n", dbi_driver_get_name (driver));
		}
		return STATE_UNKNOWN;
	}

	/* make a connection to the database */
	gettimeofday (&start_timeval, NULL);

	conn = dbi_conn_open (driver);
	if (! conn) {
		printf ("UNKNOWN - failed top open connection object.\n");
		dbi_conn_close (conn);
		return STATE_UNKNOWN;
	}

	for (i = 0; i < np_dbi_options_num; ++i) {
		const char *opt;

		if (verbose > 1)
			printf ("Setting DBI driver option '%s' to '%s'\n",
					np_dbi_options[i].key, np_dbi_options[i].value);

		if (! dbi_conn_set_option (conn, np_dbi_options[i].key, np_dbi_options[i].value))
			continue;
		/* else: status != 0 */

		np_dbi_print_error (conn, "UNKNOWN - failed to set option '%s' to '%s'",
				np_dbi_options[i].key, np_dbi_options[i].value);
		printf ("Known driver options:\n");

		for (opt = dbi_conn_get_option_list (conn, NULL); opt;
				opt = dbi_conn_get_option_list (conn, opt)) {
			printf (" - %s\n", opt);
		}
		dbi_conn_close (conn);
		return STATE_UNKNOWN;
	}

	if (host) {
		if (verbose > 1)
			printf ("Setting DBI driver option 'host' to '%s'\n", host);
		dbi_conn_set_option (conn, "host", host);
	}

	if (verbose) {
		const char *dbname, *host;

		dbname = dbi_conn_get_option (conn, "dbname");
		host = dbi_conn_get_option (conn, "host");

		if (! dbname)
			dbname = "<unspecified>";
		if (! host)
			host = "<unspecified>";

		printf ("Connecting to database '%s' at host '%s'\n",
				dbname, host);
	}

	if (dbi_conn_connect (conn) < 0) {
		np_dbi_print_error (conn, "UNKOWN - failed to connect to database");
		return STATE_UNKNOWN;
	}

	gettimeofday (&end_timeval, NULL);
	conn_time = timediff (start_timeval, end_timeval);

	server_version = dbi_conn_get_engine_version (conn);
	if (verbose)
		printf ("Connected to server version %u\n", server_version);

	if (metric == METRIC_SERVER_VERSION)
		status = get_status (server_version, dbi_thresholds);

	if (verbose)
		printf ("Time elapsed: %f\n", conn_time);

	if (metric == METRIC_CONN_TIME)
		status = get_status (conn_time, dbi_thresholds);

	/* select a database */
	if (np_dbi_database) {
		if (verbose > 1)
			printf ("Selecting database '%s'\n", np_dbi_database);

		if (dbi_conn_select_db (conn, np_dbi_database)) {
			np_dbi_print_error (conn, "UNKOWN - failed to select database '%s'",
					np_dbi_database);
			return STATE_UNKNOWN;
		}
	}

	if (np_dbi_query) {
		/* execute query */
		status = do_query (conn, &query_val_str, &query_val, &query_time);
		if (status != STATE_OK)
			/* do_query prints an error message in this case */
			return status;

		if (metric == METRIC_QUERY_RESULT) {
			if (expect) {
				if ((! query_val_str) || strcmp (query_val_str, expect))
					status = STATE_CRITICAL;
				else
					status = STATE_OK;
			}
			else if (expect_re_str) {
				int err;

				err = regexec (&expect_re, query_val_str, 0, NULL, /* flags = */ 0);
				if (! err)
					status = STATE_OK;
				else if (err == REG_NOMATCH)
					status = STATE_CRITICAL;
				else {
					char errmsg[1024];
					regerror (err, &expect_re, errmsg, sizeof (errmsg));
					printf ("ERROR - failed to execute regular expression: %s\n",
							errmsg);
					status = STATE_CRITICAL;
				}
			}
			else
				status = get_status (query_val, dbi_thresholds);
		}
		else if (metric == METRIC_QUERY_TIME)
			status = get_status (query_time, dbi_thresholds);
	}

	if (verbose)
		printf("Closing connection\n");
	dbi_conn_close (conn);

	/* In case of METRIC_QUERY_RESULT, isnan(query_val) indicates an error
	 * which should have been reported and handled (abort) before
	 * ... unless we expected a string to be returned */
	assert ((metric != METRIC_QUERY_RESULT) || (! isnan (query_val))
			|| (type == TYPE_STRING));

	assert ((type != TYPE_STRING) || (expect || expect_re_str));

	printf ("%s - connection time: %fs", state_text (status), conn_time);
	if (np_dbi_query) {
		if (type == TYPE_STRING) {
			assert (expect || expect_re_str);
			printf (", '%s' returned '%s' in %fs", np_dbi_query,
					query_val_str ? query_val_str : "<nothing>", query_time);
			if (status != STATE_OK) {
				if (expect)
					printf (" (expected '%s')", expect);
				else if (expect_re_str)
					printf (" (expected regex /%s/%s)", expect_re_str,
							((expect_re_cflags & REG_ICASE) ? "i" : ""));
			}
		}
		else if (isnan (query_val))
			printf (", '%s' query execution time: %fs", np_dbi_query, query_time);
		else
			printf (", '%s' returned %f in %fs", np_dbi_query, query_val, query_time);
	}

	printf (" | conntime=%fs;%s;%s;0; server_version=%u;%s;%s;0;", conn_time,
			((metric == METRIC_CONN_TIME) && warning_range) ? warning_range : "",
			((metric == METRIC_CONN_TIME) && critical_range) ? critical_range : "",
			server_version,
			((metric == METRIC_SERVER_VERSION) && warning_range) ? warning_range : "",
			((metric == METRIC_SERVER_VERSION) && critical_range) ? critical_range : "");
	if (np_dbi_query) {
		if (! isnan (query_val)) /* this is also true when -e is used */
			printf (" query=%f;%s;%s;;", query_val,
					((metric == METRIC_QUERY_RESULT) && warning_range) ? warning_range : "",
					((metric == METRIC_QUERY_RESULT) && critical_range) ? critical_range : "");
		printf (" querytime=%fs;%s;%s;0;", query_time,
				((metric == METRIC_QUERY_TIME) && warning_range) ? warning_range : "",
				((metric == METRIC_QUERY_TIME) && critical_range) ? critical_range : "");
	}
	printf ("\n");
	return status;
}

/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option = 0;
	static struct option longopts[] = {
		STD_LONG_OPTS,

		{"expect", required_argument, 0, 'e'},
		{"regex", required_argument, 0, 'r'},
		{"regexi", required_argument, 0, 'R'},
		{"metric", required_argument, 0, 'm'},
		{"driver", required_argument, 0, 'd'},
		{"option", required_argument, 0, 'o'},
		{"query", required_argument, 0, 'q'},
		{"database", required_argument, 0, 'D'},
		{0, 0, 0, 0}
	};

	while (1) {
		c = getopt_long (argc, argv, "Vvht:c:w:e:r:R:m:H:d:o:q:D:",
				longopts, &option);

		if (c == EOF)
			break;

		switch (c) {
		case '?':     /* usage */
			usage5 ();
		case 'h':     /* help */
			print_help ();
			exit (STATE_OK);
		case 'V':     /* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_OK);

		case 'c':     /* critical range */
			critical_range = optarg;
			type = TYPE_NUMERIC;
			break;
		case 'w':     /* warning range */
			warning_range = optarg;
			type = TYPE_NUMERIC;
			break;
		case 'e':
			expect = optarg;
			type = TYPE_STRING;
			break;
		case 'R':
			expect_re_cflags = REG_ICASE;
			/* fall through */
		case 'r':
			{
				int err;

				expect_re_cflags |= REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
				expect_re_str = optarg;
				type = TYPE_STRING;

				err = regcomp (&expect_re, expect_re_str, expect_re_cflags);
				if (err) {
					char errmsg[1024];
					regerror (err, &expect_re, errmsg, sizeof (errmsg));
					printf ("ERROR - failed to compile regular expression: %s\n",
							errmsg);
					return ERROR;
				}
				break;
			}

		case 'm':
			if (! strcasecmp (optarg, "CONN_TIME"))
				metric = METRIC_CONN_TIME;
			else if (! strcasecmp (optarg, "SERVER_VERSION"))
				metric = METRIC_SERVER_VERSION;
			else if (! strcasecmp (optarg, "QUERY_RESULT"))
				metric = METRIC_QUERY_RESULT;
			else if (! strcasecmp (optarg, "QUERY_TIME"))
				metric = METRIC_QUERY_TIME;
			else
				usage2 (_("Invalid metric"), optarg);
			break;
		case 't':     /* timeout */
			if (!is_intnonneg (optarg))
				usage2 (_("Timeout interval must be a positive integer"), optarg);
			else
				timeout_interval = atoi (optarg);

		case 'H':     /* host */
			if (!is_host (optarg))
				usage2 (_("Invalid hostname/address"), optarg);
			else
				host = optarg;
			break;
		case 'v':
			verbose++;
			break;

		case 'd':
			np_dbi_driver = optarg;
			break;
		case 'o':
			{
				driver_option_t *new;

				char *k, *v;

				k = optarg;
				v = strchr (k, (int)'=');

				if (! v)
					usage2 (_("Option must be '<key>=<value>'"), optarg);

				*v = '\0';
				++v;

				new = realloc (np_dbi_options,
						(np_dbi_options_num + 1) * sizeof (*new));
				if (! new) {
					printf ("UNKOWN - failed to reallocate memory\n");
					exit (STATE_UNKNOWN);
				}

				np_dbi_options = new;
				new = np_dbi_options + np_dbi_options_num;
				++np_dbi_options_num;

				new->key = k;
				new->value = v;
			}
			break;
		case 'q':
			np_dbi_query = optarg;
			break;
		case 'D':
			np_dbi_database = optarg;
			break;
		}
	}

	set_thresholds (&dbi_thresholds, warning_range, critical_range);

	return validate_arguments ();
}

int
validate_arguments ()
{
	if (! np_dbi_driver)
		usage ("Must specify a DBI driver");

	if (((metric == METRIC_QUERY_RESULT) || (metric == METRIC_QUERY_TIME))
			&& (! np_dbi_query))
		usage ("Must specify a query to execute (metric == QUERY_RESULT)");

	if ((metric != METRIC_CONN_TIME)
			&& (metric != METRIC_SERVER_VERSION)
			&& (metric != METRIC_QUERY_RESULT)
			&& (metric != METRIC_QUERY_TIME))
		usage ("Invalid metric specified");

	if (expect && (warning_range || critical_range || expect_re_str))
		usage ("Do not mix -e and -w/-c/-r/-R");

	if (expect_re_str && (warning_range || critical_range || expect))
		usage ("Do not mix -r/-R and -w/-c/-e");

	if (expect && (metric != METRIC_QUERY_RESULT))
		usage ("Option -e requires metric QUERY_RESULT");

	if (expect_re_str && (metric != METRIC_QUERY_RESULT))
		usage ("Options -r/-R require metric QUERY_RESULT");

	return OK;
}

void
print_help (void)
{
	print_revision (progname, NP_VERSION);

	printf (COPYRIGHT, copyright, email);

	printf (_("This program connects to an (SQL) database using DBI and checks the\n"
			"specified metric against threshold levels. The default metric is\n"
			"the result of the specified query.\n"));

	printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);
/* include this conditionally to avoid 'zero-length printf format string'
 * compiler warnings */
#ifdef NP_EXTRA_OPTS
	printf (UT_EXTRA_OPTS);
#endif
	printf ("\n");

	printf (" %s\n", "-d, --driver=STRING");
	printf ("    %s\n", _("DBI driver to use"));
	printf (" %s\n", "-o, --option=STRING");
	printf ("    %s\n", _("DBI driver options"));
	printf (" %s\n", "-q, --query=STRING");
	printf ("    %s\n", _("query to execute"));
	printf ("\n");

	printf (UT_WARN_CRIT_RANGE);
	printf (" %s\n", "-e, --expect=STRING");
	printf ("    %s\n", _("String to expect as query result"));
	printf ("    %s\n", _("Do not mix with -w, -c, -r, or -R!"));
	printf (" %s\n", "-r, --regex=REGEX");
	printf ("    %s\n", _("Extended POSIX regular expression to check query result against"));
	printf ("    %s\n", _("Do not mix with -w, -c, -e, or -R!"));
	printf (" %s\n", "-R, --regexi=REGEX");
	printf ("    %s\n", _("Case-insensitive extended POSIX regex to check query result against"));
	printf ("    %s\n", _("Do not mix with -w, -c, -e, or -r!"));
	printf (" %s\n", "-m, --metric=METRIC");
	printf ("    %s\n", _("Metric to check thresholds against. Available metrics:"));
	printf ("    CONN_TIME    - %s\n", _("time used for setting up the database connection"));
	printf ("    QUERY_RESULT - %s\n", _("result (first column of first row) of the query"));
	printf ("    QUERY_TIME   - %s\n", _("time used to execute the query"));
	printf ("                   %s\n", _("(ignore the query result)"));
	printf ("\n");

	printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf (UT_VERBOSE);

	printf ("\n");
	printf (" %s\n", _("A DBI driver (-d option) is required. If the specified metric operates"));
	printf (" %s\n\n", _("on a query, one has to be specified (-q option)."));

	printf (" %s\n", _("This plugin connects to an (SQL) database using libdbi and, optionally,"));
	printf (" %s\n", _("executes the specified query. The first column of the first row of the"));
	printf (" %s\n", _("result will be parsed and, in QUERY_RESULT mode, compared with the"));
	printf (" %s\n", _("warning and critical ranges. The result from the query has to be numeric"));
	printf (" %s\n\n", _("(strings representing numbers are fine)."));

	printf (" %s\n", _("The number and type of required DBI driver options depends on the actual"));
	printf (" %s\n", _("driver. See its documentation at http://libdbi-drivers.sourceforge.net/"));
	printf (" %s\n\n", _("for details."));

	printf (" %s\n", _("Examples:"));
	printf ("  check_dbi -d pgsql -o username=postgres -m QUERY_RESULT \\\n");
	printf ("    -q 'SELECT COUNT(*) FROM pg_stat_activity' -w 5 -c 10\n");
	printf ("  Warning if more than five connections; critical if more than ten.\n\n");

	printf ("  check_dbi -d mysql -H localhost -o username=user -o password=secret \\\n");
	printf ("    -q 'SELECT COUNT(*) FROM logged_in_users -w 5:20 -c 0:50\n");
	printf ("  Warning if less than 5 or more than 20 users are logged in; critical\n");
	printf ("  if more than 50 users.\n\n");

	printf ("  check_dbi -d firebird -o username=user -o password=secret -o dbname=foo \\\n");
	printf ("    -m CONN_TIME -w 0.5 -c 2\n");
	printf ("  Warning if connecting to the database takes more than half of a second;\n");
	printf ("  critical if it takes more than 2 seconds.\n\n");

	printf ("  check_dbi -d mysql -H localhost -o username=user \\\n");
	printf ("    -q 'SELECT concat(@@version, \" \", @@version_comment)' \\\n");
	printf ("    -r '^5\\.[01].*MySQL Enterprise Server'\n");
	printf ("  Critical if the database server is not a MySQL enterprise server in either\n");
	printf ("  version 5.0.x or 5.1.x.\n\n");

	printf ("  check_dbi -d pgsql -u username=user -m SERVER_VERSION \\\n");
	printf ("    -w 090000:090099 -c 090000:090199\n");
	printf ("  Warn if the PostgreSQL server version is not 9.0.x; critical if the version\n");
	printf ("  is less than 9.x or higher than 9.1.x.\n");

	printf (UT_SUPPORT);
}

void
print_usage (void)
{
	printf ("%s\n", _("Usage:"));
	printf ("%s -d <DBI driver> [-o <DBI driver option> [...]] [-q <query>]\n", progname);
	printf (" [-H <host>] [-c <critical range>] [-w <warning range>] [-m <metric>]\n");
	printf (" [-e <string>] [-r|-R <regex>]\n");
}

#define CHECK_IGNORE_ERROR(s) \
	do { \
		if (metric != METRIC_QUERY_RESULT) \
			return (s); \
	} while (0)

const char *
get_field_str (dbi_conn conn, dbi_result res, unsigned short field_type)
{
	const char *str;

	if (field_type != DBI_TYPE_STRING) {
		printf ("CRITICAL - result value is not a string\n");
		return NULL;
	}

	str = dbi_result_get_string_idx (res, 1);
	if ((! str) || (strcmp (str, "ERROR") == 0)) {
		CHECK_IGNORE_ERROR (NULL);
		np_dbi_print_error (conn, "CRITICAL - failed to fetch string value");
		return NULL;
	}

	if ((verbose && (type == TYPE_STRING)) || (verbose > 2))
		printf ("Query returned string '%s'\n", str);
	return str;
}

double
get_field (dbi_conn conn, dbi_result res, unsigned short *field_type)
{
	double val = NAN;

	if (*field_type == DBI_TYPE_INTEGER) {
		val = (double)dbi_result_get_longlong_idx (res, 1);
	}
	else if (*field_type == DBI_TYPE_DECIMAL) {
		val = dbi_result_get_double_idx (res, 1);
	}
	else if (*field_type == DBI_TYPE_STRING) {
		const char *val_str;
		char *endptr = NULL;

		val_str = get_field_str (conn, res, *field_type);
		if (! val_str) {
			CHECK_IGNORE_ERROR (NAN);
			*field_type = DBI_TYPE_ERROR;
			return NAN;
		}

		val = strtod (val_str, &endptr);
		if (endptr == val_str) {
			CHECK_IGNORE_ERROR (NAN);
			printf ("CRITICAL - result value is not a numeric: %s\n", val_str);
			*field_type = DBI_TYPE_ERROR;
			return NAN;
		}
		else if ((endptr != NULL) && (*endptr != '\0')) {
			if (verbose)
				printf ("Garbage after value: %s\n", endptr);
		}
	}
	else {
		CHECK_IGNORE_ERROR (NAN);
		printf ("CRITICAL - cannot parse value of type %s (%i)\n",
				(*field_type == DBI_TYPE_BINARY)
					? "BINARY"
					: (*field_type == DBI_TYPE_DATETIME)
						? "DATETIME"
						: "<unknown>",
				*field_type);
		*field_type = DBI_TYPE_ERROR;
		return NAN;
	}
	return val;
}

double
get_query_result (dbi_conn conn, dbi_result res, const char **res_val_str, double *res_val)
{
	unsigned short field_type;
	double val = NAN;

	if (dbi_result_get_numrows (res) == DBI_ROW_ERROR) {
		CHECK_IGNORE_ERROR (STATE_OK);
		np_dbi_print_error (conn, "CRITICAL - failed to fetch rows");
		return STATE_CRITICAL;
	}

	if (dbi_result_get_numrows (res) < 1) {
		CHECK_IGNORE_ERROR (STATE_OK);
		printf ("WARNING - no rows returned\n");
		return STATE_WARNING;
	}

	if (dbi_result_get_numfields (res) == DBI_FIELD_ERROR) {
		CHECK_IGNORE_ERROR (STATE_OK);
		np_dbi_print_error (conn, "CRITICAL - failed to fetch fields");
		return STATE_CRITICAL;
	}

	if (dbi_result_get_numfields (res) < 1) {
		CHECK_IGNORE_ERROR (STATE_OK);
		printf ("WARNING - no fields returned\n");
		return STATE_WARNING;
	}

	if (dbi_result_first_row (res) != 1) {
		CHECK_IGNORE_ERROR (STATE_OK);
		np_dbi_print_error (conn, "CRITICAL - failed to fetch first row");
		return STATE_CRITICAL;
	}

	field_type = dbi_result_get_field_type_idx (res, 1);
	if (field_type != DBI_TYPE_ERROR) {
		if (type == TYPE_STRING)
			/* the value will be freed in dbi_result_free */
			*res_val_str = strdup (get_field_str (conn, res, field_type));
		else
			val = get_field (conn, res, &field_type);
	}

	*res_val = val;

	if (field_type == DBI_TYPE_ERROR) {
		CHECK_IGNORE_ERROR (STATE_OK);
		np_dbi_print_error (conn, "CRITICAL - failed to fetch data");
		return STATE_CRITICAL;
	}

	dbi_result_free (res);
	return STATE_OK;
}

#undef CHECK_IGNORE_ERROR

int
do_query (dbi_conn conn, const char **res_val_str, double *res_val, double *res_time)
{
	dbi_result res;

	struct timeval timeval_start, timeval_end;
	int status = STATE_OK;

	assert (np_dbi_query);

	if (verbose)
		printf ("Executing query '%s'\n", np_dbi_query);

	gettimeofday (&timeval_start, NULL);

	res = dbi_conn_query (conn, np_dbi_query);
	if (! res) {
		np_dbi_print_error (conn, "CRITICAL - failed to execute query '%s'", np_dbi_query);
		return STATE_CRITICAL;
	}

	status = get_query_result (conn, res, res_val_str, res_val);

	gettimeofday (&timeval_end, NULL);
	*res_time = timediff (timeval_start, timeval_end);

	if (verbose)
		printf ("Time elapsed: %f\n", *res_time);

	return status;
}

double
timediff (struct timeval start, struct timeval end)
{
	double diff;

	while (start.tv_usec > end.tv_usec) {
		--end.tv_sec;
		end.tv_usec += 1000000;
	}
	diff = (double)(end.tv_sec - start.tv_sec)
		+ (double)(end.tv_usec - start.tv_usec) / 1000000.0;
	return diff;
}

void
np_dbi_print_error (dbi_conn conn, char *fmt, ...)
{
	const char *errmsg = NULL;
	va_list ap;

	va_start (ap, fmt);

	dbi_conn_error (conn, &errmsg);
	vprintf (fmt, ap);
	printf (": %s\n", errmsg);

	va_end (ap);
}

