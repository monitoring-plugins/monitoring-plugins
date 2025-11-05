/*****************************************************************************
 *
 * Monitoring check_pgsql plugin
 *
 * License: GPL
 * Copyright (c) 1999-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_pgsql plugin
 *
 * Test whether a PostgreSQL Database is accepting connections.
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
#include "common.h"
#include "utils.h"
#include "utils_cmd.h"
#include "check_pgsql.d/config.h"
#include "thresholds.h"
#include "netutils.h"
#include <libpq-fe.h>
#include <pg_config_manual.h>

const char *progname = "check_pgsql";
const char *copyright = "1999-2024";
const char *email = "devel@monitoring-plugins.org";

#define DEFAULT_HOST "127.0.0.1"

/* return the PSQL server version as a 3-tuple */
#define PSQL_SERVER_VERSION3(server_version)                                                       \
	((server_version) / 10000),                                                                    \
		(((server_version) / 100) - (int)(((server_version) / 10000) * 100)),                      \
		(server_version) - (int)(((server_version) / 100) * 100)
/* return true if the given host is a UNIX domain socket */
#define PSQL_IS_UNIX_DOMAIN_SOCKET(host) ((NULL == (host)) || ('\0' == *(host)) || ('/' == *(host)))
/* return a 3-tuple identifying a host/port independent of the socket type */
#define PSQL_SOCKET3(host, port)                                                                   \
	((NULL == (host)) || ('\0' == *(host))) ? DEFAULT_PGSOCKET_DIR : host,                         \
		PSQL_IS_UNIX_DOMAIN_SOCKET(host) ? "/.s.PGSQL." : ":", port

typedef struct {
	int errorcode;
	check_pgsql_config config;
} check_pgsql_config_wrapper;
static check_pgsql_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);

static void print_help(void);
static bool is_pg_logname(char * /*username*/);

typedef enum {
	QUERY_OK,
	ERROR_WITH_QUERY,     // critical
	NO_ROWS_RETURNED,     // warning
	NO_COLUMNS_RETURNED,  // warning
	NO_DATA_RETURNED,     // critica/
	RESULT_IS_NOT_NUMERIC // critical
} do_query_errorcode;

typedef struct {
	do_query_errorcode error_code;
	double numerical_result;
} do_query_wrapper;
static do_query_wrapper do_query(PGconn * /*conn*/, char * /*query*/);
void print_usage(void);

static int verbose = 0;

/******************************************************************************

The (pseudo?)literate programming XML is contained within \@\@\- <XML> \-\@\@
tags in the comments. With in the tags, the XML is assembled sequentially.
You can define entities in tags. You also have all the #defines available as
entities.

Please note that all tags must be lowercase to use the DocBook XML DTD.

@@-<article>

<sect1>
<title>Quick Reference</title>
<!-- The refentry forms a manpage -->
<refentry>
<refmeta>
<manvolnum>5<manvolnum>
</refmeta>
<refnamdiv>
<refname>&progname;</refname>
<refpurpose>&SUMMARY;</refpurpose>
</refnamdiv>
</refentry>
</sect1>

<sect1>
<title>FAQ</title>
</sect1>

<sect1>
<title>Theory, Installation, and Operation</title>

<sect2>
<title>General Description</title>
<para>
&DESCRIPTION;
</para>
</sect2>

<sect2>
<title>Future Enhancements</title>
<para>ToDo List</para>
</sect2>


<sect2>
<title>Functions</title>
-@@
******************************************************************************/

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_pgsql_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	const check_pgsql_config config = tmp_config.config;

	if (config.output_format_is_set) {
		mp_set_format(config.output_format);
	}

	/* Set signal handling and alarm */
	if (signal(SIGALRM, timeout_alarm_handler) == SIG_ERR) {
		usage4(_("Cannot catch SIGALRM"));
	}
	alarm(timeout_interval);

	char *conninfo = NULL;
	if (config.pgparams) {
		asprintf(&conninfo, "%s ", config.pgparams);
	}

	asprintf(&conninfo, "%sdbname = '%s'", conninfo ? conninfo : "", config.dbName);
	if (config.pghost) {
		asprintf(&conninfo, "%s host = '%s'", conninfo, config.pghost);
	}
	if (config.pgport) {
		asprintf(&conninfo, "%s port = '%s'", conninfo, config.pgport);
	}
	if (config.pgoptions) {
		asprintf(&conninfo, "%s options = '%s'", conninfo, config.pgoptions);
	}
	/* if (pgtty) -- ignored by PQconnectdb */
	if (config.pguser) {
		asprintf(&conninfo, "%s user = '%s'", conninfo, config.pguser);
	}

	if (verbose) { /* do not include password (see right below) in output */
		printf("Connecting to PostgreSQL using conninfo: %s%s\n", conninfo,
			   config.pgpasswd ? " password = <hidden>" : "");
	}

	if (config.pgpasswd) {
		asprintf(&conninfo, "%s password = '%s'", conninfo, config.pgpasswd);
	}

	/* make a connection to the database */
	struct timeval start_timeval;
	gettimeofday(&start_timeval, NULL);
	PGconn *conn = PQconnectdb(conninfo);
	struct timeval end_timeval;
	gettimeofday(&end_timeval, NULL);

	while (start_timeval.tv_usec > end_timeval.tv_usec) {
		--end_timeval.tv_sec;
		end_timeval.tv_usec += 1000000;
	}
	double elapsed_time = (double)(end_timeval.tv_sec - start_timeval.tv_sec) +
						  ((double)(end_timeval.tv_usec - start_timeval.tv_usec) / 1000000.0);

	if (verbose) {
		printf("Time elapsed: %f\n", elapsed_time);
	}

	/* check to see that the backend connection was successfully made */
	if (verbose) {
		printf("Verifying connection\n");
	}

	mp_check overall = mp_check_init();

	mp_subcheck sc_connection = mp_subcheck_init();

	if (PQstatus(conn) == CONNECTION_BAD) {
		sc_connection = mp_set_subcheck_state(sc_connection, STATE_CRITICAL);
		xasprintf(&sc_connection.output, "no connection to '%s' (%s)", config.dbName,
				  PQerrorMessage(conn));
		PQfinish(conn);
		mp_add_subcheck_to_check(&overall, sc_connection);
		mp_exit(overall);
	} else {
		sc_connection = mp_set_subcheck_state(sc_connection, STATE_OK);
		xasprintf(&sc_connection.output, "connected to '%s'", config.dbName);
		mp_add_subcheck_to_check(&overall, sc_connection);
	}

	mp_subcheck sc_connection_time = mp_subcheck_init();
	sc_connection_time = mp_set_subcheck_default_state(sc_connection_time, STATE_UNKNOWN);

	xasprintf(&sc_connection_time.output, "connection time: %.10g", elapsed_time);

	mp_perfdata pd_connection_time = perfdata_init();
	pd_connection_time.label = "time";
	pd_connection_time.uom = "s";
	pd_connection_time = mp_set_pd_value(pd_connection_time, elapsed_time);
	pd_connection_time = mp_pd_set_thresholds(pd_connection_time, config.time_thresholds);

	mp_add_perfdata_to_subcheck(&sc_connection_time, pd_connection_time);
	sc_connection_time =
		mp_set_subcheck_state(sc_connection_time, mp_get_pd_status(pd_connection_time));

	mp_add_subcheck_to_check(&overall, sc_connection_time);

	if (verbose) {
		char *server_host = PQhost(conn);
		int server_version = PQserverVersion(conn);

		printf("Successfully connected to database %s (user %s) "
			   "at server %s%s%s (server version: %d.%d.%d, "
			   "protocol version: %d, pid: %d)\n",
			   PQdb(conn), PQuser(conn), PSQL_SOCKET3(server_host, PQport(conn)),
			   PSQL_SERVER_VERSION3(server_version), PQprotocolVersion(conn), PQbackendPID(conn));
	}

	if (config.pgquery) {
		mp_subcheck sc_query = mp_subcheck_init();
		sc_query = mp_set_subcheck_default_state(sc_query, STATE_UNKNOWN);
		if (config.pgqueryname) {
			xasprintf(&sc_query.output, "query '%s'", config.pgqueryname);
		} else {
			xasprintf(&sc_query.output, "query '%s'", config.pgquery);
		}

		do_query_wrapper query_result = do_query(conn, config.pgquery);

		switch (query_result.error_code) {
		case QUERY_OK: {
			// Query was succesful and there is a numerical result
			sc_query = mp_set_subcheck_state(sc_query, STATE_OK);
			xasprintf(&sc_query.output, "%s succeeded", sc_query.output);

			mp_perfdata pd_query = perfdata_init();
			pd_query = mp_set_pd_value(pd_query, query_result.numerical_result);
			pd_query = mp_pd_set_thresholds(pd_query, config.qthresholds);
			pd_query.label = "query";

			mp_subcheck sc_query_compare = mp_subcheck_init();
			mp_state_enum query_compare_state = mp_get_pd_status(pd_query);

			sc_query_compare = mp_set_subcheck_state(sc_query_compare, query_compare_state);
			mp_add_perfdata_to_subcheck(&sc_query_compare, pd_query);

			if (query_compare_state == STATE_OK) {
				xasprintf(&sc_query_compare.output, "query result '%f' is withing thresholds",
						  query_result.numerical_result);
			} else {
				xasprintf(&sc_query_compare.output, "query result '%f' is violating thresholds",
						  query_result.numerical_result);
			}
			mp_add_subcheck_to_check(&overall, sc_query_compare);

		} break;
		case ERROR_WITH_QUERY:
			xasprintf(&sc_query.output, "%s - Error with query: %s", sc_query.output,
					  PQerrorMessage(conn));
			sc_query = mp_set_subcheck_state(sc_query, STATE_CRITICAL);
			break;
		case NO_ROWS_RETURNED:
			xasprintf(&sc_query.output, "%s - no rows were returned by the query", sc_query.output);
			sc_query = mp_set_subcheck_state(sc_query, STATE_WARNING);
			break;
		case NO_COLUMNS_RETURNED:
			xasprintf(&sc_query.output, "%s - no columns were returned by the query",
					  sc_query.output);
			sc_query = mp_set_subcheck_state(sc_query, STATE_WARNING);
			break;
		case NO_DATA_RETURNED:
			xasprintf(&sc_query.output, "%s - no data was returned by the query", sc_query.output);
			sc_query = mp_set_subcheck_state(sc_query, STATE_WARNING);
			break;
		case RESULT_IS_NOT_NUMERIC:
			xasprintf(&sc_query.output, "%s - result of the query is not numeric", sc_query.output);
			sc_query = mp_set_subcheck_state(sc_query, STATE_CRITICAL);
			break;
		};

		mp_add_subcheck_to_check(&overall, sc_query);
	}

	if (verbose) {
		printf("Closing connection\n");
	}
	PQfinish(conn);

	mp_exit(overall);
}

/* process command-line arguments */
static check_pgsql_config_wrapper process_arguments(int argc, char **argv) {

	enum {
		OPTID_QUERYNAME = CHAR_MAX + 1,
		output_format_index,
	};

	static struct option longopts[] = {{"help", no_argument, 0, 'h'},
									   {"version", no_argument, 0, 'V'},
									   {"timeout", required_argument, 0, 't'},
									   {"critical", required_argument, 0, 'c'},
									   {"warning", required_argument, 0, 'w'},
									   {"hostname", required_argument, 0, 'H'},
									   {"logname", required_argument, 0, 'l'},
									   {"password", required_argument, 0, 'p'},
									   {"authorization", required_argument, 0, 'a'},
									   {"port", required_argument, 0, 'P'},
									   {"database", required_argument, 0, 'd'},
									   {"option", required_argument, 0, 'o'},
									   {"query", required_argument, 0, 'q'},
									   {"queryname", required_argument, 0, OPTID_QUERYNAME},
									   {"query_critical", required_argument, 0, 'C'},
									   {"query_warning", required_argument, 0, 'W'},
									   {"verbose", no_argument, 0, 'v'},
									   {"output-format", required_argument, 0, output_format_index},
									   {0, 0, 0, 0}};

	check_pgsql_config_wrapper result = {
		.errorcode = OK,
		.config = check_pgsql_config_init(),
	};

	while (true) {
		int option = 0;
		int option_char =
			getopt_long(argc, argv, "hVt:c:w:H:P:d:l:p:a:o:q:C:W:v", longopts, &option);

		if (option_char == EOF) {
			break;
		}

		switch (option_char) {
		case output_format_index: {
			parsed_output_format parser = mp_parse_output_format(optarg);
			if (!parser.parsing_success) {
				printf("Invalid output format: %s\n", optarg);
				exit(STATE_UNKNOWN);
			}

			result.config.output_format_is_set = true;
			result.config.output_format = parser.output_format;
			break;
		}
		case '?': /* usage */
			usage5();
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 't': /* timeout period */
			if (!is_integer(optarg)) {
				usage2(_("Timeout interval must be a positive integer"), optarg);
			} else {
				timeout_interval = atoi(optarg);
			}
			break;
		case 'c': /* critical time threshold */ {
			mp_range_parsed tmp = mp_parse_range_string(optarg);
			if (tmp.error != MP_PARSING_SUCCES) {
				die(STATE_UNKNOWN, "failed to parse critical time threshold");
			}
			result.config.time_thresholds =
				mp_thresholds_set_crit(result.config.time_thresholds, tmp.range);
		} break;
		case 'w': /* warning time threshold */ {
			mp_range_parsed tmp = mp_parse_range_string(optarg);
			if (tmp.error != MP_PARSING_SUCCES) {
				die(STATE_UNKNOWN, "failed to parse warning time threshold");
			}
			result.config.time_thresholds =
				mp_thresholds_set_warn(result.config.time_thresholds, tmp.range);
		} break;
		case 'C': /* critical query threshold */ {
			mp_range_parsed tmp = mp_parse_range_string(optarg);
			if (tmp.error != MP_PARSING_SUCCES) {
				die(STATE_UNKNOWN, "failed to parse critical query threshold");
			}

			result.config.qthresholds =
				mp_thresholds_set_crit(result.config.qthresholds, tmp.range);

		} break;
		case 'W': /* warning query threshold */ {
			mp_range_parsed tmp = mp_parse_range_string(optarg);
			if (tmp.error != MP_PARSING_SUCCES) {
				die(STATE_UNKNOWN, "failed to parse warning query threshold");
			}
			result.config.qthresholds =
				mp_thresholds_set_warn(result.config.qthresholds, tmp.range);
		} break;
		case 'H': /* host */
			if ((*optarg != '/') && (!is_host(optarg))) {
				usage2(_("Invalid hostname/address"), optarg);
			} else {
				result.config.pghost = optarg;
			}
			break;
		case 'P': /* port */
			if (!is_integer(optarg)) {
				usage2(_("Port must be a positive integer"), optarg);
			} else {
				result.config.pgport = optarg;
			}
			break;
		case 'd': /* database name */
			if (strlen(optarg) >= NAMEDATALEN) {
				usage2(_("Database name exceeds the maximum length"), optarg);
			}
			snprintf(result.config.dbName, NAMEDATALEN, "%s", optarg);
			break;
		case 'l': /* login name */
			if (!is_pg_logname(optarg)) {
				usage2(_("User name is not valid"), optarg);
			} else {
				result.config.pguser = optarg;
			}
			break;
		case 'p': /* authentication password */
		case 'a':
			result.config.pgpasswd = optarg;
			break;
		case 'o':
			if (result.config.pgparams) {
				asprintf(&result.config.pgparams, "%s %s", result.config.pgparams, optarg);
			} else {
				asprintf(&result.config.pgparams, "%s", optarg);
			}
			break;
		case 'q':
			result.config.pgquery = optarg;
			break;
		case OPTID_QUERYNAME:
			result.config.pgqueryname = optarg;
			break;
		case 'v':
			verbose++;
			break;
		}
	}

	return result;
}

/**

the tango program should eventually create an entity here based on the
function prototype

@@-
<sect3>
<title>is_pg_logname</title>

<para>&PROTO_is_pg_logname;</para>

<para>Given a username, this function returns true if the string is a
valid PostgreSQL username, and returns false if it is not. Valid PostgreSQL
usernames are less than &NAMEDATALEN; characters long and consist of
letters, numbers, dashes, and underscores, plus possibly some other
characters.</para>

<para>Currently this function only checks string length. Additional checks
should be added.</para>

</sect3>
-@@
******************************************************************************/

static bool is_pg_logname(char *username) {
	if (strlen(username) > NAMEDATALEN - 1) {
		return (false);
	}
	return (true);
}

/******************************************************************************
@@-
</sect2>
</sect1>
</article>
-@@
******************************************************************************/

void print_help(void) {
	char *myport;

	xasprintf(&myport, "%d", 5432);

	print_revision(progname, NP_VERSION);

	printf(COPYRIGHT, copyright, email);

	printf(_("Test whether a PostgreSQL Database is accepting connections."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(UT_HOST_PORT, 'P', myport);

	printf(" %s\n", "-d, --database=STRING");
	printf("    %s", _("Database to check "));
	printf(_("(default: %s)\n"), DEFAULT_DB);
	printf(" %s\n", "-l, --logname = STRING");
	printf("    %s\n", _("Login name of user"));
	printf(" %s\n", "-p, --password = STRING");
	printf("    %s\n", _("Password (BIG SECURITY ISSUE)"));
	printf(" %s\n", "-o, --option = STRING");
	printf("    %s\n", _("Connection parameters (keyword = value), see below"));

	printf(UT_WARN_CRIT);

	printf(UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf(" %s\n", "-q, --query=STRING");
	printf("    %s\n", _("SQL query to run. Only first column in first row will be read"));
	printf(" %s\n", "--queryname=STRING");
	printf("    %s\n", _("A name for the query, this string is used instead of the query"));
	printf("    %s\n", _("in the long output of the plugin"));
	printf(" %s\n", "-W, --query_warning=RANGE");
	printf("    %s\n", _("SQL query value to result in warning status (double)"));
	printf(" %s\n", "-C, --query_critical=RANGE");
	printf("    %s\n", _("SQL query value to result in critical status (double)"));

	printf(UT_VERBOSE);
	printf(UT_OUTPUT_FORMAT);

	printf("\n");
	printf(" %s\n", _("All parameters are optional."));
	printf(" %s\n", _("This plugin tests a PostgreSQL DBMS to determine whether it is active and"));
	printf(" %s\n", _("accepting queries. In its current operation, it simply connects to the"));
	printf(" %s\n", _("specified database, and then disconnects. If no database is specified, it"));
	printf(" %s\n", _("connects to the template1 database, which is present in every functioning"));
	printf(" %s\n\n", _("PostgreSQL DBMS."));

	printf(" %s\n", _("If a query is specified using the -q option, it will be executed after"));
	printf(" %s\n", _("connecting to the server. The result from the query has to be numeric."));
	printf(" %s\n",
		   _("Multiple SQL commands, separated by semicolon, are allowed but the result "));
	printf(" %s\n", _("of the last command is taken into account only. The value of the first"));
	printf(" %s\n",
		   _("column in the first row is used as the check result. If a second column is"));
	printf(" %s\n", _("present in the result set, this is added to the plugin output with a"));
	printf(" %s\n",
		   _("prefix of \"Extra Info:\". This information can be displayed in the system"));
	printf(" %s\n\n", _("executing the plugin."));

	printf(" %s\n", _("See the chapter \"Monitoring Database Activity\" of the PostgreSQL manual"));
	printf(" %s\n\n",
		   _("for details about how to access internal statistics of the database server."));

	printf(" %s\n",
		   _("For a list of available connection parameters which may be used with the -o"));
	printf(" %s\n",
		   _("command line option, see the documentation for PQconnectdb() in the chapter"));
	printf(" %s\n", _("\"libpq - C Library\" of the PostgreSQL manual. For example, this may be"));
	printf(" %s\n",
		   _("used to specify a service name in pg_service.conf to be used for additional"));
	printf(" %s\n", _("connection parameters: -o 'service=<name>' or to specify the SSL mode:"));
	printf(" %s\n\n", _("-o 'sslmode=require'."));

	printf(" %s\n", _("The plugin will connect to a local postmaster if no host is specified. To"));
	printf(" %s\n",
		   _("connect to a remote host, be sure that the remote postmaster accepts TCP/IP"));
	printf(" %s\n\n", _("connections (start the postmaster with the -i option)."));

	printf(" %s\n",
		   _("Typically, the monitoring user (unless the --logname option is used) should be"));
	printf(" %s\n",
		   _("able to connect to the database without a password. The plugin can also send"));
	printf(" %s\n", _("a password, but no effort is made to obscure or encrypt the password."));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s [-H <host>] [-P <port>] [-c <critical time>] [-w <warning time>]\n", progname);
	printf(" [-t <timeout>] [-d <database>] [-l <logname>] [-p <password>]\n"
		   "[-q <query>] [-C <critical query range>] [-W <warning query range>]\n");
}

static do_query_wrapper do_query(PGconn *conn, char *query) {
	if (verbose) {
		printf("Executing SQL query \"%s\".\n", query);
	}
	PGresult *res = PQexec(conn, query);

	do_query_wrapper result = {
		.error_code = QUERY_OK,
	};

	if (PGRES_TUPLES_OK != PQresultStatus(res)) {
		// TODO
		// printf(_("QUERY %s - %s: %s.\n"), _("CRITICAL"), _("Error with query"),
		// PQerrorMessage(conn));
		result.error_code = ERROR_WITH_QUERY;
		return result;
	}

	if (PQntuples(res) < 1) {
		// TODO
		// printf("QUERY %s - %s.\n", _("WARNING"), _("No rows returned"));
		result.error_code = NO_ROWS_RETURNED;
		return result;
	}

	if (PQnfields(res) < 1) {
		// TODO
		// printf("QUERY %s - %s.\n", _("WARNING"), _("No columns returned"));
		result.error_code = NO_COLUMNS_RETURNED;
		return result;
	}

	char *val_str = PQgetvalue(res, 0, 0);
	if (!val_str) {
		// TODO
		// printf("QUERY %s - %s.\n", _("CRITICAL"), _("No data returned"));
		result.error_code = NO_DATA_RETURNED;
		return result;
	}

	char *endptr = NULL;
	double value = strtod(val_str, &endptr);
	if (verbose) {
		printf("Query result: %f\n", value);
	}

	if (endptr == val_str) {
		// TODO
		// printf("QUERY %s - %s: %s\n", _("CRITICAL"), _("Is not a numeric"), val_str);
		result.error_code = RESULT_IS_NOT_NUMERIC;
		return result;
	}

	if ((endptr != NULL) && (*endptr != '\0')) {
		if (verbose) {
			printf("Garbage after value: %s.\n", endptr);
		}
	}

	result.numerical_result = value;

	return result;
}
