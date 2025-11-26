/*****************************************************************************
 *
 * Monitoring check_mysql plugin
 *
 * License: GPL
 * Copyright (c) 1999 Didi Rieder (adrieder@sbox.tu-graz.ac.at)
 * Copyright (c) 2000 Karl DeBisschop (kdebisschop@users.sourceforge.net)
 * Copyright (c) 1999-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_mysql plugin
 *
 * This program tests connections to a mysql server
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
#include "check_mysql.d/config.h"

#include <mysql.h>
#include <mysqld_error.h>
#include <errmsg.h>

const char *progname = "check_mysql";
const char *copyright = "1999-2024";
const char *email = "devel@monitoring-plugins.org";

static int verbose = 0;

#define REPLICA_RESULTSIZE 96

#define LENGTH_METRIC_UNIT 6
static const char *metric_unit[LENGTH_METRIC_UNIT] = {
	"Open_files",        "Open_tables",    "Qcache_free_memory", "Qcache_queries_in_cache",
	"Threads_connected", "Threads_running"};

#define LENGTH_METRIC_COUNTER 9
static const char *metric_counter[LENGTH_METRIC_COUNTER] = {"Connections",
															"Qcache_hits",
															"Qcache_inserts",
															"Qcache_lowmem_prunes",
															"Qcache_not_cached",
															"Queries",
															"Questions",
															"Table_locks_waited",
															"Uptime"};

#define MYSQLDUMP_THREADS_QUERY                                                                    \
	"SELECT COUNT(1) mysqldumpThreads FROM information_schema.processlist WHERE info LIKE "        \
	"'SELECT /*!40001 SQL_NO_CACHE */%'"

typedef struct {
	int errorcode;
	check_mysql_config config;
} check_mysql_config_wrapper;
static check_mysql_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static check_mysql_config_wrapper validate_arguments(check_mysql_config_wrapper /*config_wrapper*/);
static void print_help(void);
void print_usage(void);

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_mysql_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	const check_mysql_config config = tmp_config.config;

	if (config.output_format_is_set) {
		mp_set_format(config.output_format);
	}

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

	if (config.ssl) {
		mysql_ssl_set(&mysql, config.key, config.cert, config.ca_cert, config.ca_dir,
					  config.ciphers);
	}

	mp_check overall = mp_check_init();

	mp_subcheck sc_connection = mp_subcheck_init();
	/* establish a connection to the server and check for errors */
	if (!mysql_real_connect(&mysql, config.db_host, config.db_user, config.db_pass, config.db,
							config.db_port, config.db_socket, 0)) {
		/* Depending on internally-selected auth plugin MySQL might return */
		/* ER_ACCESS_DENIED_NO_PASSWORD_ERROR or ER_ACCESS_DENIED_ERROR. */
		/* Semantically these errors are the same. */
		if (config.ignore_auth && (mysql_errno(&mysql) == ER_ACCESS_DENIED_ERROR ||
								   mysql_errno(&mysql) == ER_ACCESS_DENIED_NO_PASSWORD_ERROR)) {
			xasprintf(&sc_connection.output, "Version: %s (protocol %d)",
					  mysql_get_server_info(&mysql), mysql_get_proto_info(&mysql));
			sc_connection = mp_set_subcheck_state(sc_connection, STATE_OK);

			mysql_close(&mysql);
		} else {
			if (mysql_errno(&mysql) == CR_UNKNOWN_HOST) {
				sc_connection = mp_set_subcheck_state(sc_connection, STATE_WARNING);
				xasprintf(&sc_connection.output, "%s", mysql_error(&mysql));
			} else if (mysql_errno(&mysql) == CR_VERSION_ERROR) {
				sc_connection = mp_set_subcheck_state(sc_connection, STATE_WARNING);
				xasprintf(&sc_connection.output, "%s", mysql_error(&mysql));
			} else if (mysql_errno(&mysql) == CR_OUT_OF_MEMORY) {
				sc_connection = mp_set_subcheck_state(sc_connection, STATE_WARNING);
				xasprintf(&sc_connection.output, "%s", mysql_error(&mysql));
			} else if (mysql_errno(&mysql) == CR_IPSOCK_ERROR) {
				sc_connection = mp_set_subcheck_state(sc_connection, STATE_WARNING);
				xasprintf(&sc_connection.output, "%s", mysql_error(&mysql));
			} else if (mysql_errno(&mysql) == CR_SOCKET_CREATE_ERROR) {
				sc_connection = mp_set_subcheck_state(sc_connection, STATE_WARNING);
				xasprintf(&sc_connection.output, "%s", mysql_error(&mysql));
			} else {
				sc_connection = mp_set_subcheck_state(sc_connection, STATE_CRITICAL);
				xasprintf(&sc_connection.output, "%s", mysql_error(&mysql));
			}
		}

		mp_add_subcheck_to_check(&overall, sc_connection);
		mp_exit(overall);
	} else {
		// successful connection
		sc_connection = mp_set_subcheck_state(sc_connection, STATE_OK);
		xasprintf(&sc_connection.output, "Version: %s (protocol %d)", mysql_get_server_info(&mysql),
				  mysql_get_proto_info(&mysql));
		mp_add_subcheck_to_check(&overall, sc_connection);
	}

	/* get the server stats */
	char *mysql_stats = strdup(mysql_stat(&mysql));

	mp_subcheck sc_stats = mp_subcheck_init();
	sc_stats = mp_set_subcheck_default_state(sc_stats, STATE_OK);

	/* error checking once more */
	if (mysql_errno(&mysql) != 0) {
		if ((mysql_errno(&mysql) == CR_SERVER_GONE_ERROR) ||
			(mysql_errno(&mysql) == CR_SERVER_LOST) || (mysql_errno(&mysql) == CR_UNKNOWN_ERROR)) {
			sc_stats = mp_set_subcheck_state(sc_stats, STATE_CRITICAL);
			xasprintf(&sc_stats.output, "Retrieving stats failed: %s", mysql_error(&mysql));
		} else {
			// not sure which error modes occur here, but mysql_error indicates an error
			sc_stats = mp_set_subcheck_state(sc_stats, STATE_WARNING);
			xasprintf(&sc_stats.output, "retrieving stats caused an error: %s",
					  mysql_error(&mysql));
		}

		mp_add_subcheck_to_check(&overall, sc_stats);
		mp_exit(overall);
	} else {
		xasprintf(&sc_stats.output, "retrieved stats: %s", mysql_stats);
		sc_stats = mp_set_subcheck_state(sc_stats, STATE_OK);
		mp_add_subcheck_to_check(&overall, sc_stats);
	}

	MYSQL_RES *res;
	MYSQL_ROW row;
	mp_subcheck sc_query = mp_subcheck_init();
	/* try to fetch some perf data */
	if (mysql_query(&mysql, "show global status") == 0) {
		if ((res = mysql_store_result(&mysql)) == NULL) {
			xasprintf(&sc_connection.output, "query failed - status store_result error: %s",
					  mysql_error(&mysql));
			mysql_close(&mysql);

			sc_query = mp_set_subcheck_state(sc_query, STATE_CRITICAL);
			mp_add_subcheck_to_check(&overall, sc_query);
			mp_exit(overall);
		}

		while ((row = mysql_fetch_row(res)) != NULL) {
			for (int i = 0; i < LENGTH_METRIC_UNIT; i++) {
				if (strcmp(row[0], metric_unit[i]) == 0) {
					mp_perfdata pd_mysql_stat = perfdata_init();
					pd_mysql_stat.label = (char *)metric_unit[i];
					pd_mysql_stat.value = mp_create_pd_value(atol(row[1]));
					mp_add_perfdata_to_subcheck(&sc_stats, pd_mysql_stat);
					continue;
				}
			}

			for (int i = 0; i < LENGTH_METRIC_COUNTER; i++) {
				if (strcmp(row[0], metric_counter[i]) == 0) {
					mp_perfdata pd_mysql_stat = perfdata_init();
					pd_mysql_stat.label = (char *)metric_counter[i];
					pd_mysql_stat.value = mp_create_pd_value(atol(row[1]));
					pd_mysql_stat.uom = "c";
					mp_add_perfdata_to_subcheck(&sc_stats, pd_mysql_stat);
					continue;
				}
			}
		}
	} else {
		// Query failed!
		xasprintf(&sc_connection.output, "query failed");
		sc_query = mp_set_subcheck_state(sc_query, STATE_CRITICAL);
		mp_add_subcheck_to_check(&overall, sc_query);
		mp_exit(overall);
	}

	if (config.check_replica) {
		// Detect which version we are, on older version
		// "show slave status" should work, on newer ones
		// "show replica status"
		// But first we have to find out whether this is
		// MySQL or MariaDB since the version numbering scheme
		// is different
		bool use_deprecated_slave_status = false;
		const char *server_version = mysql_get_server_info(&mysql);
		unsigned long server_verion_int = mysql_get_server_version(&mysql);
		unsigned long major_version = server_verion_int / 10000;
		unsigned long minor_version = (server_verion_int % 10000) / 100;
		unsigned long patch_version = (server_verion_int % 100);

		if (verbose) {
			printf("Found MariaDB/MySQL: %s, main version: %lu, minor version: %lu, patch version: "
				   "%lu\n",
				   server_version, major_version, minor_version, patch_version);
		}

		if (strstr(server_version, "MariaDB") != NULL) {
			// Looks like MariaDB, new commands should be available after 10.5.1
			if (major_version < 10) {
				use_deprecated_slave_status = true;
			} else if (major_version == 10) {
				if (minor_version < 5) {
					use_deprecated_slave_status = true;
				} else if (minor_version == 5 && patch_version < 1) {
					use_deprecated_slave_status = true;
				}
			}
		} else {
			// Looks like MySQL or at least not like MariaDB
			if (major_version < 8) {
				use_deprecated_slave_status = true;
			} else if (major_version == 10 && minor_version < 4) {
				use_deprecated_slave_status = true;
			}
		}

		char *replica_query = NULL;
		if (use_deprecated_slave_status) {
			replica_query = "show slave status";
		} else {
			replica_query = "show replica status";
		}

		mp_subcheck sc_replica = mp_subcheck_init();

		/* check the replica status */
		if (mysql_query(&mysql, replica_query) != 0) {
			xasprintf(&sc_replica.output, "replica query error: %s", mysql_error(&mysql));
			mysql_close(&mysql);

			sc_replica = mp_set_subcheck_state(sc_replica, STATE_CRITICAL);
			mp_add_subcheck_to_check(&overall, sc_replica);
			mp_exit(overall);
		}

		/* store the result */
		if ((res = mysql_store_result(&mysql)) == NULL) {
			xasprintf(&sc_replica.output, "replica store_result error: %s", mysql_error(&mysql));
			mysql_close(&mysql);

			sc_replica = mp_set_subcheck_state(sc_replica, STATE_CRITICAL);
			mp_add_subcheck_to_check(&overall, sc_replica);
			mp_exit(overall);
		}

		/* Check there is some data */
		if (mysql_num_rows(res) == 0) {
			mysql_close(&mysql);

			xasprintf(&sc_replica.output, "no replicas defined");
			sc_replica = mp_set_subcheck_state(sc_replica, STATE_WARNING);
			mp_add_subcheck_to_check(&overall, sc_replica);
			mp_exit(overall);
		}

		/* fetch the first row */
		if ((row = mysql_fetch_row(res)) == NULL) {
			xasprintf(&sc_replica.output, "replica fetch row error: %s", mysql_error(&mysql));
			mysql_free_result(res);
			mysql_close(&mysql);

			sc_replica = mp_set_subcheck_state(sc_replica, STATE_CRITICAL);
			mp_add_subcheck_to_check(&overall, sc_replica);
			mp_exit(overall);
		}

		if (mysql_field_count(&mysql) == 12) {
			/* mysql 3.23.x */
			xasprintf(&sc_replica.output, "Replica running: %s", row[6]);
			if (strcmp(row[6], "Yes") != 0) {
				mysql_free_result(res);
				mysql_close(&mysql);

				sc_replica = mp_set_subcheck_state(sc_replica, STATE_CRITICAL);
				mp_add_subcheck_to_check(&overall, sc_replica);
				mp_exit(overall);
			}
		} else {
			/* mysql 4.x.x and mysql 5.x.x */
			int replica_io_field = -1;
			int replica_sql_field = -1;
			int seconds_behind_field = -1;
			int num_fields;
			MYSQL_FIELD *fields;
			num_fields = mysql_num_fields(res);
			fields = mysql_fetch_fields(res);
			for (int i = 0; i < num_fields; i++) {
				if (use_deprecated_slave_status) {
					if (strcmp(fields[i].name, "Slave_IO_Running") == 0) {
						replica_io_field = i;
						continue;
					}
					if (strcmp(fields[i].name, "Slave_SQL_Running") == 0) {
						replica_sql_field = i;
						continue;
					}
					if (strcmp(fields[i].name, "Seconds_Behind_Master") == 0) {
						seconds_behind_field = i;
						continue;
					}
				} else {
					if (strcmp(fields[i].name, "Replica_IO_Running") == 0) {
						replica_io_field = i;
						continue;
					}
					if (strcmp(fields[i].name, "Replica_SQL_Running") == 0) {
						replica_sql_field = i;
						continue;
					}
					if (strcmp(fields[i].name, "Seconds_Behind_Source") == 0) {
						seconds_behind_field = i;
						continue;
					}
				}
			}

			/* Check if replica status is available */
			if ((replica_io_field < 0) || (replica_sql_field < 0) || (num_fields == 0)) {
				mysql_free_result(res);
				mysql_close(&mysql);

				xasprintf(&sc_replica.output, "Replica status unavailable");
				sc_replica = mp_set_subcheck_state(sc_replica, STATE_CRITICAL);
				mp_add_subcheck_to_check(&overall, sc_replica);
				mp_exit(overall);
			}

			/* Save replica status in replica_result */
			xasprintf(&sc_replica.output,
					  "Replica IO: %s Replica SQL: %s Seconds Behind Master: %s",
					  row[replica_io_field], row[replica_sql_field],
					  seconds_behind_field != -1 ? row[seconds_behind_field] : "Unknown");

			/* Raise critical error if SQL THREAD or IO THREAD are stopped, but only if there are no
			 * mysqldump threads running */
			if (strcmp(row[replica_io_field], "Yes") != 0 ||
				strcmp(row[replica_sql_field], "Yes") != 0) {
				MYSQL_RES *res_mysqldump;
				MYSQL_ROW row_mysqldump;
				unsigned int mysqldump_threads = 0;

				if (mysql_query(&mysql, MYSQLDUMP_THREADS_QUERY) == 0) {
					/* store the result */
					if ((res_mysqldump = mysql_store_result(&mysql)) != NULL) {
						if (mysql_num_rows(res_mysqldump) == 1) {
							if ((row_mysqldump = mysql_fetch_row(res_mysqldump)) != NULL) {
								mysqldump_threads = atoi(row_mysqldump[0]);
							}
						}
						/* free the result */
						mysql_free_result(res_mysqldump);
					}
					mysql_close(&mysql);
				}

				if (mysqldump_threads == 0) {
					sc_replica = mp_set_subcheck_state(sc_replica, STATE_CRITICAL);
					mp_add_subcheck_to_check(&overall, sc_replica);
					mp_exit(overall);
				} else {
					xasprintf(&sc_replica.output, "%s %s", sc_replica.output,
							  " Mysqldump: in progress");
				}
			}

			if (verbose >= 3) {
				if (seconds_behind_field == -1) {
					printf("seconds_behind_field not found\n");
				} else {
					printf("seconds_behind_field(index %d)=%s\n", seconds_behind_field,
						   row[seconds_behind_field]);
				}
			}

			/* Check Seconds Behind against threshold */
			if ((seconds_behind_field != -1) && (row[seconds_behind_field] != NULL &&
												 strcmp(row[seconds_behind_field], "NULL") != 0)) {
				mp_perfdata pd_seconds_behind = perfdata_init();
				pd_seconds_behind.label = "seconds behind master";
				pd_seconds_behind.value = mp_create_pd_value(atof(row[seconds_behind_field]));
				pd_seconds_behind =
					mp_pd_set_thresholds(pd_seconds_behind, config.replica_thresholds);
				pd_seconds_behind.uom = "s";
				mp_add_perfdata_to_subcheck(&sc_replica, pd_seconds_behind);

				mp_state_enum status = mp_get_pd_status(pd_seconds_behind);

				sc_replica = mp_set_subcheck_state(sc_replica, status);

				if (status != STATE_OK) {
					xasprintf(&sc_replica.output, "slow replica - %s", sc_replica.output);
					mp_add_subcheck_to_check(&overall, sc_replica);
					mp_exit(overall);
				}
			}
		}

		/* free the result */
		mysql_free_result(res);
	}

	/* close the connection */
	mysql_close(&mysql);

	mp_exit(overall);
}

/* process command-line arguments */
check_mysql_config_wrapper process_arguments(int argc, char **argv) {

	enum {
		CHECK_REPLICA_OPT = CHAR_MAX + 1,
		output_format_index,
	};

	static struct option longopts[] = {{"hostname", required_argument, 0, 'H'},
									   {"socket", required_argument, 0, 's'},
									   {"database", required_argument, 0, 'd'},
									   {"username", required_argument, 0, 'u'},
									   {"password", required_argument, 0, 'p'},
									   {"file", required_argument, 0, 'f'},
									   {"group", required_argument, 0, 'g'},
									   {"port", required_argument, 0, 'P'},
									   {"critical", required_argument, 0, 'c'},
									   {"warning", required_argument, 0, 'w'},
									   {"check-slave", no_argument, 0, 'S'},
									   {"check-replica", no_argument, 0, CHECK_REPLICA_OPT},
									   {"ignore-auth", no_argument, 0, 'n'},
									   {"verbose", no_argument, 0, 'v'},
									   {"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'h'},
									   {"ssl", no_argument, 0, 'l'},
									   {"ca-cert", optional_argument, 0, 'C'},
									   {"key", required_argument, 0, 'k'},
									   {"cert", required_argument, 0, 'a'},
									   {"ca-dir", required_argument, 0, 'D'},
									   {"ciphers", required_argument, 0, 'L'},
									   {"output-format", required_argument, 0, output_format_index},
									   {0, 0, 0, 0}};

	check_mysql_config_wrapper result = {
		.errorcode = OK,
		.config = check_mysql_config_init(),
	};

	if (argc < 1) {
		result.errorcode = ERROR;
		return result;
	}

	int option = 0;
	while (true) {
		int option_index =
			getopt_long(argc, argv, "hlvVnSP:p:u:d:H:s:c:w:a:k:C:D:L:f:g:", longopts, &option);

		if (option_index == -1 || option_index == EOF) {
			break;
		}

		switch (option_index) {
		case 'H': /* hostname */
			if (is_host(optarg)) {
				result.config.db_host = optarg;
			} else if (*optarg == '/') {
				result.config.db_socket = optarg;
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
		case 'l':
			result.config.ssl = true;
			break;
		case 'C':
			result.config.ca_cert = optarg;
			break;
		case 'a':
			result.config.cert = optarg;
			break;
		case 'k':
			result.config.key = optarg;
			break;
		case 'D':
			result.config.ca_dir = optarg;
			break;
		case 'L':
			result.config.ciphers = optarg;
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
		case 'S':
		case CHECK_REPLICA_OPT:
			result.config.check_replica = true; /* check-slave */
			break;
		case 'n':
			result.config.ignore_auth = true; /* ignore-auth */
			break;
		case 'w': {
			mp_range_parsed tmp = mp_parse_range_string(optarg);
			if (tmp.error != MP_PARSING_SUCCES) {
				die(STATE_UNKNOWN, "failed to parse warning time threshold");
			}
			result.config.replica_thresholds =
				mp_thresholds_set_warn(result.config.replica_thresholds, tmp.range);
		} break;
		case 'c': {
			mp_range_parsed tmp = mp_parse_range_string(optarg);
			if (tmp.error != MP_PARSING_SUCCES) {
				die(STATE_UNKNOWN, "failed to parse critical time threshold");
			}
			result.config.replica_thresholds =
				mp_thresholds_set_crit(result.config.replica_thresholds, tmp.range);
		} break;
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case 'v':
			verbose++;
			break;
		case '?': /* help */
			usage5();
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
		}
	}

	int index = optind;

	while (argc > index) {
		if (result.config.db_host == NULL) {
			if (is_host(argv[index])) {
				result.config.db_host = argv[index++];
			} else {
				usage2(_("Invalid hostname/address"), argv[index]);
			}
		} else if (result.config.db_user == NULL) {
			result.config.db_user = argv[index++];
		} else if (result.config.db_pass == NULL) {
			result.config.db_pass = argv[index++];
		} else if (result.config.db == NULL) {
			result.config.db = argv[index++];
		} else if (is_intnonneg(argv[index])) {
			result.config.db_port = atoi(argv[index++]);
		} else {
			break;
		}
	}

	return validate_arguments(result);
}

check_mysql_config_wrapper validate_arguments(check_mysql_config_wrapper config_wrapper) {
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

	printf("%s\n", _("This program tests connections to a MySQL server"));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(UT_HOST_PORT, 'P', myport);
	printf(" %s\n", "-n, --ignore-auth");
	printf("    %s\n", _("Ignore authentication failure and check for mysql connectivity only"));

	printf(" %s\n", "-s, --socket=STRING");
	printf("    %s\n", _("Use the specified socket (has no effect if -H is used)"));

	printf(" %s\n", "-d, --database=STRING");
	printf("    %s\n", _("Check database with indicated name"));
	printf(" %s\n", "-f, --file=STRING");
	printf("    %s\n", _("Read from the specified client options file"));
	printf(" %s\n", "-g, --group=STRING");
	printf("    %s\n", _("Use a client options group"));
	printf(" %s\n", "-u, --username=STRING");
	printf("    %s\n", _("Connect using the indicated username"));
	printf(" %s\n", "-p, --password=STRING");
	printf("    %s\n", _("Use the indicated password to authenticate the connection"));
	printf("    ==> %s <==\n", _("IMPORTANT: THIS FORM OF AUTHENTICATION IS NOT SECURE!!!"));
	printf("    %s\n", _("Your clear-text password could be visible as a process table entry"));
	printf(" %s\n", "-S, --check-slave");
	printf("    %s\n", _("Check if the slave thread is running properly. This option is deprecated "
						 "in favour of check-replica, which does the same"));
	printf(" %s\n", "--check-replica");
	printf("    %s\n", _("Check if the replica thread is running properly."));
	printf(" %s\n", "-w, --warning");
	printf("    %s\n",
		   _("Exit with WARNING status if replica server is more than INTEGER seconds"));
	printf("    %s\n", _("behind master"));
	printf(" %s\n", "-c, --critical");
	printf("    %s\n",
		   _("Exit with CRITICAL status if replica server is more then INTEGER seconds"));
	printf("    %s\n", _("behind master"));
	printf(" %s\n", "-l, --ssl");
	printf("    %s\n", _("Use ssl encryption"));
	printf(" %s\n", "-C, --ca-cert=STRING");
	printf("    %s\n", _("Path to CA signing the cert"));
	printf(" %s\n", "-a, --cert=STRING");
	printf("    %s\n", _("Path to SSL certificate"));
	printf(" %s\n", "-k, --key=STRING");
	printf("    %s\n", _("Path to private SSL key"));
	printf(" %s\n", "-D, --ca-dir=STRING");
	printf("    %s\n", _("Path to CA directory"));
	printf(" %s\n", "-L, --ciphers=STRING");
	printf("    %s\n", _("List of valid SSL ciphers"));

	printf(UT_OUTPUT_FORMAT);

	printf("\n");
	printf(" %s\n",
		   _("There are no required arguments. By default, the local database is checked"));
	printf(" %s\n", _("using the default unix socket. You can force TCP on localhost by using an"));
	printf(" %s\n", _("IP address or FQDN ('localhost' will use the socket as well)."));

	printf("\n");
	printf("%s\n", _("Notes:"));
	printf(" %s\n", _("You must specify -p with an empty string to force an empty password,"));
	printf(" %s\n", _("overriding any my.cnf settings."));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf(" %s [-d database] [-H host] [-P port] [-s socket]\n", progname);
	printf("       [-u user] [-p password] [-S] [-l] [-a cert] [-k key]\n");
	printf("       [-C ca-cert] [-D ca-dir] [-L ciphers] [-f optfile] [-g group]\n");
}
