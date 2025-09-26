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

const char *progname = "check_mysql";
const char *copyright = "1999-2024";
const char *email = "devel@monitoring-plugins.org";

#define REPLICA_RESULTSIZE 96

#include "common.h"
#include "utils.h"
#include "utils_base.h"
#include "netutils.h"
#include "check_mysql.d/config.h"

#include <mysql.h>
#include <mysqld_error.h>
#include <errmsg.h>

static int verbose = 0;

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
	/* establish a connection to the server and error checking */
	if (!mysql_real_connect(&mysql, config.db_host, config.db_user, config.db_pass, config.db,
							config.db_port, config.db_socket, 0)) {
		/* Depending on internally-selected auth plugin MySQL might return */
		/* ER_ACCESS_DENIED_NO_PASSWORD_ERROR or ER_ACCESS_DENIED_ERROR. */
		/* Semantically these errors are the same. */
		if (config.ignore_auth && (mysql_errno(&mysql) == ER_ACCESS_DENIED_ERROR ||
								   mysql_errno(&mysql) == ER_ACCESS_DENIED_NO_PASSWORD_ERROR)) {
			printf("MySQL OK - Version: %s (protocol %d)\n", mysql_get_server_info(&mysql),
				   mysql_get_proto_info(&mysql));
			mysql_close(&mysql);
			return STATE_OK;
		}

		if (mysql_errno(&mysql) == CR_UNKNOWN_HOST) {
			die(STATE_WARNING, "%s\n", mysql_error(&mysql));
		} else if (mysql_errno(&mysql) == CR_VERSION_ERROR) {
			die(STATE_WARNING, "%s\n", mysql_error(&mysql));
		} else if (mysql_errno(&mysql) == CR_OUT_OF_MEMORY) {
			die(STATE_WARNING, "%s\n", mysql_error(&mysql));
		} else if (mysql_errno(&mysql) == CR_IPSOCK_ERROR) {
			die(STATE_WARNING, "%s\n", mysql_error(&mysql));
		} else if (mysql_errno(&mysql) == CR_SOCKET_CREATE_ERROR) {
			die(STATE_WARNING, "%s\n", mysql_error(&mysql));
		} else {
			die(STATE_CRITICAL, "%s\n", mysql_error(&mysql));
		}
	}

	/* get the server stats */
	char *result = strdup(mysql_stat(&mysql));

	/* error checking once more */
	if (mysql_error(&mysql)) {
		if (mysql_errno(&mysql) == CR_SERVER_GONE_ERROR) {
			die(STATE_CRITICAL, "%s\n", mysql_error(&mysql));
		} else if (mysql_errno(&mysql) == CR_SERVER_LOST) {
			die(STATE_CRITICAL, "%s\n", mysql_error(&mysql));
		} else if (mysql_errno(&mysql) == CR_UNKNOWN_ERROR) {
			die(STATE_CRITICAL, "%s\n", mysql_error(&mysql));
		}
	}

	char *perf = strdup("");
	char *error = NULL;
	MYSQL_RES *res;
	MYSQL_ROW row;
	/* try to fetch some perf data */
	if (mysql_query(&mysql, "show global status") == 0) {
		if ((res = mysql_store_result(&mysql)) == NULL) {
			error = strdup(mysql_error(&mysql));
			mysql_close(&mysql);
			die(STATE_CRITICAL, _("status store_result error: %s\n"), error);
		}

		while ((row = mysql_fetch_row(res)) != NULL) {
			for (int i = 0; i < LENGTH_METRIC_UNIT; i++) {
				if (strcmp(row[0], metric_unit[i]) == 0) {
					xasprintf(&perf, "%s%s ", perf,
							  perfdata(metric_unit[i], atol(row[1]), "", false, 0, false, 0, false,
									   0, false, 0));
					continue;
				}
			}
			for (int i = 0; i < LENGTH_METRIC_COUNTER; i++) {
				if (strcmp(row[0], metric_counter[i]) == 0) {
					xasprintf(&perf, "%s%s ", perf,
							  perfdata(metric_counter[i], atol(row[1]), "c", false, 0, false, 0,
									   false, 0, false, 0));
					continue;
				}
			}
		}
		/* remove trailing space */
		if (strlen(perf) > 0) {
			perf[strlen(perf) - 1] = '\0';
		}
	}

	char replica_result[REPLICA_RESULTSIZE] = {0};
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
			printf("Found MariaDB: %s, main version: %lu, minor version: %lu, patch version: %lu\n",
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

		/* check the replica status */
		if (mysql_query(&mysql, replica_query) != 0) {
			error = strdup(mysql_error(&mysql));
			mysql_close(&mysql);
			die(STATE_CRITICAL, _("replica query error: %s\n"), error);
		}

		/* store the result */
		if ((res = mysql_store_result(&mysql)) == NULL) {
			error = strdup(mysql_error(&mysql));
			mysql_close(&mysql);
			die(STATE_CRITICAL, _("replica store_result error: %s\n"), error);
		}

		/* Check there is some data */
		if (mysql_num_rows(res) == 0) {
			mysql_close(&mysql);
			die(STATE_WARNING, "%s\n", _("No replicas defined"));
		}

		/* fetch the first row */
		if ((row = mysql_fetch_row(res)) == NULL) {
			error = strdup(mysql_error(&mysql));
			mysql_free_result(res);
			mysql_close(&mysql);
			die(STATE_CRITICAL, _("replica fetch row error: %s\n"), error);
		}

		if (mysql_field_count(&mysql) == 12) {
			/* mysql 3.23.x */
			snprintf(replica_result, REPLICA_RESULTSIZE, _("Replica running: %s"), row[6]);
			if (strcmp(row[6], "Yes") != 0) {
				mysql_free_result(res);
				mysql_close(&mysql);
				die(STATE_CRITICAL, "%s\n", replica_result);
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
			}

			/* Check if replica status is available */
			if ((replica_io_field < 0) || (replica_sql_field < 0) || (num_fields == 0)) {
				mysql_free_result(res);
				mysql_close(&mysql);
				die(STATE_CRITICAL, "Replica status unavailable\n");
			}

			/* Save replica status in replica_result */
			snprintf(replica_result, REPLICA_RESULTSIZE,
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
					die(STATE_CRITICAL, "%s\n", replica_result);
				} else {
					strncat(replica_result, " Mysqldump: in progress", REPLICA_RESULTSIZE - 1);
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
				double value = atof(row[seconds_behind_field]);
				int status;

				status = get_status(value, config.my_threshold);

				xasprintf(&perf, "%s %s", perf,
						  fperfdata("seconds behind master", value, "s", true,
									(double)config.warning_time, true, (double)config.critical_time,
									false, 0, false, 0));

				if (status == STATE_WARNING) {
					printf("SLOW_REPLICA %s: %s|%s\n", _("WARNING"), replica_result, perf);
					exit(STATE_WARNING);
				} else if (status == STATE_CRITICAL) {
					printf("SLOW_REPLICA %s: %s|%s\n", _("CRITICAL"), replica_result, perf);
					exit(STATE_CRITICAL);
				}
			}
		}

		/* free the result */
		mysql_free_result(res);
	}

	/* close the connection */
	mysql_close(&mysql);

	/* print out the result of stats */
	if (config.check_replica) {
		printf("%s %s|%s\n", result, replica_result, perf);
	} else {
		printf("%s|%s\n", result, perf);
	}

	return STATE_OK;
}

#define CHECK_REPLICA_OPT CHAR_MAX + 1

/* process command-line arguments */
check_mysql_config_wrapper process_arguments(int argc, char **argv) {
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
									   {0, 0, 0, 0}};

	check_mysql_config_wrapper result = {
		.errorcode = OK,
		.config = check_mysql_config_init(),
	};

	if (argc < 1) {
		result.errorcode = ERROR;
		return result;
	}

	char *warning = NULL;
	char *critical = NULL;

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
		case 'w':
			warning = optarg;
			result.config.warning_time = strtod(warning, NULL);
			break;
		case 'c':
			critical = optarg;
			result.config.critical_time = strtod(critical, NULL);
			break;
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
		}
	}

	int index = optind;

	set_thresholds(&result.config.my_threshold, warning, critical);

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
