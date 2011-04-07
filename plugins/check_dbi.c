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
* Runs an arbitrary SQL command and checks the result.
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

#include <dbi/dbi.h>

#include <stdarg.h>

typedef struct {
	char *key;
	char *value;
} driver_option_t;

char *host = NULL;
int verbose = 0;

char *warning_range = NULL;
char *critical_range = NULL;
thresholds *query_thresholds = NULL;

char *np_dbi_driver = NULL;
driver_option_t *np_dbi_options = NULL;
int np_dbi_options_num = 0;
char *np_dbi_database = NULL;
char *np_dbi_query = NULL;

int process_arguments (int, char **);
int validate_arguments (void);
void print_usage (void);
void print_help (void);

void np_dbi_print_error (dbi_conn, char *, ...);

int do_query (dbi_conn, double *);

int
main (int argc, char **argv)
{
	int status = STATE_UNKNOWN;

	dbi_driver driver;
	dbi_conn conn;

	struct timeval start_timeval, end_timeval;
	double elapsed_time;

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
		printf ("UNKNOWN - failed to initialize DBI.\n");
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
	while (start_timeval.tv_usec > end_timeval.tv_usec) {
		--end_timeval.tv_sec;
		end_timeval.tv_usec += 1000000;
	}
	elapsed_time = (double)(end_timeval.tv_sec - start_timeval.tv_sec)
		+ (double)(end_timeval.tv_usec - start_timeval.tv_usec) / 1000000.0;

	if (verbose)
		printf("Time elapsed: %f\n", elapsed_time);

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

	/* execute query */
	status = do_query (conn, &query_val);
	if (status != STATE_OK)
		/* do_query prints an error message in this case */
		return status;

	status = get_status (query_val, query_thresholds);

	if (verbose)
		printf("Closing connection\n");
	dbi_conn_close (conn);

	printf ("%s - connection time: %fs, '%s' returned %f",
			state_text (status), elapsed_time, np_dbi_query, query_val);
	printf (" | conntime=%fs;;;0 query=%f;%s;%s;0\n", elapsed_time, query_val,
			warning_range ? warning_range : "", critical_range ? critical_range : "");
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

		{"driver", required_argument, 0, 'd'},
		{"option", required_argument, 0, 'o'},
		{"query", required_argument, 0, 'q'},
		{"database", required_argument, 0, 'D'},
		{0, 0, 0, 0}
	};

	while (1) {
		c = getopt_long (argc, argv, "Vvht:c:w:H:d:o:q:D:",
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
			break;
		case 'w':     /* warning range */
			warning_range = optarg;
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

	set_thresholds (&query_thresholds, warning_range, critical_range);

	return validate_arguments ();
}

int
validate_arguments ()
{
	if (! np_dbi_driver)
		usage ("Must specify a DBI driver");

	if (! np_dbi_query)
		usage ("Must specify an SQL query to execute");

	return OK;
}

void
print_help (void)
{
	print_revision (progname, NP_VERSION);

	printf (COPYRIGHT, copyright, email);

	printf (_("This program checks a query result against threshold levels"));

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
	printf ("    %s\n", _("SQL query to execute"));
	printf ("\n");

	printf (UT_WARN_CRIT_RANGE);

	printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf (UT_VERBOSE);

	printf ("\n");
	printf (" %s\n", _("A DBI driver (-d option) and a query (-q option) are required."));
	printf (" %s\n", _("This plugin connects to an SQL database using libdbi and executes the"));
	printf (" %s\n", _("specified SQL query. The first column of the first row of the result"));
	printf (" %s\n", _("will be used as the check result and, if specified, compared with the"));
	printf (" %s\n", _("warning and critical ranges. The result from the query has to be numeric"));
	printf (" %s\n\n", _("(strings representing numbers are fine)."));

	printf (" %s\n", _("The number and type of required DBI driver options depends on the actual"));
	printf (" %s\n", _("driver. See its documentation at http://libdbi-drivers.sourceforge.net/"));
	printf (" %s\n", _("for details."));

	printf (UT_SUPPORT);
}

void
print_usage (void)
{
	printf ("%s\n", _("Usage:"));
	printf ("%s -d <DBI driver> [-o <DBI driver option> [...]] -q <SQL query>\n", progname);
	printf (" [-H <host>] [-c <critical value>] [-w <warning value>]\n");
}

double
get_field (dbi_conn conn, dbi_result res, unsigned short *field_type)
{
	double val = 0.0;

	if (*field_type == DBI_TYPE_INTEGER) {
		val = (double)dbi_result_get_longlong_idx (res, 1);
	}
	else if (*field_type == DBI_TYPE_DECIMAL) {
		val = dbi_result_get_double_idx (res, 1);
	}
	else if (*field_type == DBI_TYPE_STRING) {
		const char *val_str;
		char *endptr = NULL;

		val_str = dbi_result_get_string_idx (res, 1);
		if ((! val_str) || (strcmp (val_str, "ERROR") == 0)) {
			np_dbi_print_error (conn, "CRITICAL - failed to fetch string value");
			*field_type = DBI_TYPE_ERROR;
			return 0.0;
		}

		if (verbose > 2)
			printf ("Query returned string '%s'\n", val_str);

		val = strtod (val_str, &endptr);
		if (endptr == val_str) {
			printf ("CRITICAL - result value is not a numeric: %s\n", val_str);
			*field_type = DBI_TYPE_ERROR;
			return 0.0;
		}
		else if ((endptr != NULL) && (*endptr != '\0')) {
			if (verbose)
				printf ("Garbage after value: %s\n", endptr);
		}
	}
	else {
		printf ("CRITICAL - cannot parse value of type %s (%i)\n",
				(*field_type == DBI_TYPE_BINARY)
					? "BINARY"
					: (*field_type == DBI_TYPE_DATETIME)
						? "DATETIME"
						: "<unknown>",
				*field_type);
		*field_type = DBI_TYPE_ERROR;
		return 0.0;
	}
	return val;
}

int
do_query (dbi_conn conn, double *res_val)
{
	dbi_result res;

	unsigned short field_type;
	double val = 0.0;

	if (verbose)
		printf ("Executing query '%s'\n", np_dbi_query);

	res = dbi_conn_query (conn, np_dbi_query);
	if (! res) {
		np_dbi_print_error (conn, "CRITICAL - failed to execute query '%s'", np_dbi_query);
		return STATE_CRITICAL;
	}

	if (dbi_result_get_numrows (res) == DBI_ROW_ERROR) {
		np_dbi_print_error (conn, "CRITICAL - failed to fetch rows");
		return STATE_CRITICAL;
	}

	if (dbi_result_get_numrows (res) < 1) {
		printf ("WARNING - no rows returned\n");
		return STATE_WARNING;
	}

	if (dbi_result_get_numfields (res) == DBI_FIELD_ERROR) {
		np_dbi_print_error (conn, "CRITICAL - failed to fetch fields");
		return STATE_CRITICAL;
	}

	if (dbi_result_get_numfields (res) < 1) {
		printf ("WARNING - no fields returned\n");
		return STATE_WARNING;
	}

	if (dbi_result_first_row (res) != 1) {
		np_dbi_print_error (conn, "CRITICAL - failed to fetch first row");
		return STATE_CRITICAL;
	}

	field_type = dbi_result_get_field_type_idx (res, 1);
	if (field_type != DBI_TYPE_ERROR)
		val = get_field (conn, res, &field_type);

	if (field_type == DBI_TYPE_ERROR) {
		np_dbi_print_error (conn, "CRITICAL - failed to fetch data");
		return STATE_CRITICAL;
	}

	*res_val = val;

	dbi_result_free (res);
	return STATE_OK;
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

