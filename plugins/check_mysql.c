/*****************************************************************************
* 
* Nagios check_mysql plugin
* 
* License: GPL
* Copyright (c) 1999 Didi Rieder (adrieder@sbox.tu-graz.ac.at)
* Copyright (c) 2000 Karl DeBisschop (kdebisschop@users.sourceforge.net)
* Copyright (c) 1999-2011 Nagios Plugins Development Team
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
const char *copyright = "1999-2011";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#define SLAVERESULTSIZE 70

#include "common.h"
#include "utils.h"
#include "utils_base.h"
#include "netutils.h"

#include <mysql.h>
#include <errmsg.h>

char *db_user = NULL;
char *db_host = NULL;
char *db_socket = NULL;
char *db_pass = NULL;
char *db = NULL;
char *ca_cert = NULL;
char *ca_dir = NULL;
char *cert = NULL;
char *key = NULL;
char *ciphers = NULL;
bool ssl = false;
char *opt_file = NULL;
char *opt_group = NULL;
unsigned int db_port = MYSQL_PORT;
int check_slave = 0, warn_sec = 0, crit_sec = 0;
int verbose = 0;

static double warning_time = 0;
static double critical_time = 0;

#define LENGTH_METRIC_UNIT 6
static const char *metric_unit[LENGTH_METRIC_UNIT] = {
	"Open_files",
	"Open_tables",
	"Qcache_free_memory",
	"Qcache_queries_in_cache",
	"Threads_connected",
	"Threads_running"
};

#define LENGTH_METRIC_COUNTER 9
static const char *metric_counter[LENGTH_METRIC_COUNTER] = {
	"Connections",
	"Qcache_hits",
	"Qcache_inserts",
	"Qcache_lowmem_prunes",
	"Qcache_not_cached",
	"Queries",
	"Questions",
	"Table_locks_waited",
	"Uptime"
};

thresholds *my_threshold = NULL;

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

int
main (int argc, char **argv)
{

	MYSQL mysql;
	MYSQL_RES *res;
	MYSQL_ROW row;

	/* should be status */

	char *result = NULL;
	char *error = NULL;
	char slaveresult[SLAVERESULTSIZE];
	char* perf;

        perf = strdup ("");

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* initialize mysql  */
	mysql_init (&mysql);
	
	if (opt_file != NULL)
		mysql_options(&mysql,MYSQL_READ_DEFAULT_FILE,opt_file);

	if (opt_group != NULL)
		mysql_options(&mysql,MYSQL_READ_DEFAULT_GROUP,opt_group);
	else
		mysql_options(&mysql,MYSQL_READ_DEFAULT_GROUP,"client");

	if (ssl)
		mysql_ssl_set(&mysql,key,cert,ca_cert,ca_dir,ciphers);
	/* establish a connection to the server and error checking */
	if (!mysql_real_connect(&mysql,db_host,db_user,db_pass,db,db_port,db_socket,0)) {
		if (mysql_errno (&mysql) == CR_UNKNOWN_HOST)
			die (STATE_WARNING, "%s\n", mysql_error (&mysql));
		else if (mysql_errno (&mysql) == CR_VERSION_ERROR)
			die (STATE_WARNING, "%s\n", mysql_error (&mysql));
		else if (mysql_errno (&mysql) == CR_OUT_OF_MEMORY)
			die (STATE_WARNING, "%s\n", mysql_error (&mysql));
		else if (mysql_errno (&mysql) == CR_IPSOCK_ERROR)
			die (STATE_WARNING, "%s\n", mysql_error (&mysql));
		else if (mysql_errno (&mysql) == CR_SOCKET_CREATE_ERROR)
			die (STATE_WARNING, "%s\n", mysql_error (&mysql));
		else
			die (STATE_CRITICAL, "%s\n", mysql_error (&mysql));
	}

	/* get the server stats */
	result = strdup (mysql_stat (&mysql));

	/* error checking once more */
	if (mysql_error (&mysql)) {
		if (mysql_errno (&mysql) == CR_SERVER_GONE_ERROR)
			die (STATE_CRITICAL, "%s\n", mysql_error (&mysql));
		else if (mysql_errno (&mysql) == CR_SERVER_LOST)
			die (STATE_CRITICAL, "%s\n", mysql_error (&mysql));
		else if (mysql_errno (&mysql) == CR_UNKNOWN_ERROR)
			die (STATE_CRITICAL, "%s\n", mysql_error (&mysql));
	}

	/* try to fetch some perf data */
	if (mysql_query (&mysql, "show global status") == 0) {
		if ( (res = mysql_store_result (&mysql)) == NULL) {
			error = strdup(mysql_error(&mysql));
			mysql_close (&mysql);
			die (STATE_CRITICAL, _("status store_result error: %s\n"), error);
		}

		while ( (row = mysql_fetch_row (res)) != NULL) {
			int i;

			for(i = 0; i < LENGTH_METRIC_UNIT; i++) {
				if (strcmp(row[0], metric_unit[i]) == 0) {
					xasprintf(&perf, "%s%s ", perf, perfdata(metric_unit[i],
						atol(row[1]), "", FALSE, 0, FALSE, 0, FALSE, 0, FALSE, 0));
					continue;
				}
			}
			for(i = 0; i < LENGTH_METRIC_COUNTER; i++) {
				if (strcmp(row[0], metric_counter[i]) == 0) {
					xasprintf(&perf, "%s%s ", perf, perfdata(metric_counter[i],
						atol(row[1]), "c", FALSE, 0, FALSE, 0, FALSE, 0, FALSE, 0));
					continue;
				}
			}
		}
		/* remove trailing space */
                if (strlen(perf) > 0)
                    perf[strlen(perf) - 1] = '\0';
	}

	if(check_slave) {
		/* check the slave status */
		if (mysql_query (&mysql, "show slave status") != 0) {
			error = strdup(mysql_error(&mysql));
			mysql_close (&mysql);
			die (STATE_CRITICAL, _("slave query error: %s\n"), error);
		}

		/* store the result */
		if ( (res = mysql_store_result (&mysql)) == NULL) {
			error = strdup(mysql_error(&mysql));
			mysql_close (&mysql);
			die (STATE_CRITICAL, _("slave store_result error: %s\n"), error);
		}

		/* Check there is some data */
		if (mysql_num_rows(res) == 0) {
			mysql_close(&mysql);
			die (STATE_WARNING, "%s\n", _("No slaves defined"));
		}

		/* fetch the first row */
		if ( (row = mysql_fetch_row (res)) == NULL) {
			error = strdup(mysql_error(&mysql));
			mysql_free_result (res);
			mysql_close (&mysql);
			die (STATE_CRITICAL, _("slave fetch row error: %s\n"), error);
		}

		if (mysql_field_count (&mysql) == 12) {
			/* mysql 3.23.x */
			snprintf (slaveresult, SLAVERESULTSIZE, _("Slave running: %s"), row[6]);
			if (strcmp (row[6], "Yes") != 0) {
				mysql_free_result (res);
				mysql_close (&mysql);
				die (STATE_CRITICAL, "%s\n", slaveresult);
			}

		} else {
			/* mysql 4.x.x and mysql 5.x.x */
			int slave_io_field = -1 , slave_sql_field = -1, seconds_behind_field = -1, i, num_fields;
			MYSQL_FIELD* fields;

			num_fields = mysql_num_fields(res);
			fields = mysql_fetch_fields(res);
			for(i = 0; i < num_fields; i++) {
				if (strcmp(fields[i].name, "Slave_IO_Running") == 0) {
					slave_io_field = i;
					continue;
				}
				if (strcmp(fields[i].name, "Slave_SQL_Running") == 0) {
					slave_sql_field = i;
					continue;
				}
				if (strcmp(fields[i].name, "Seconds_Behind_Master") == 0) {
					seconds_behind_field = i;
					continue;
				}
			}

			/* Check if slave status is available */
			if ((slave_io_field < 0) || (slave_sql_field < 0) || (num_fields == 0)) {
				mysql_free_result (res);
				mysql_close (&mysql);
				die (STATE_CRITICAL, "Slave status unavailable\n");
			}

			/* Save slave status in slaveresult */
			snprintf (slaveresult, SLAVERESULTSIZE, "Slave IO: %s Slave SQL: %s Seconds Behind Master: %s", row[slave_io_field], row[slave_sql_field], seconds_behind_field!=-1?row[seconds_behind_field]:"Unknown");

			/* Raise critical error if SQL THREAD or IO THREAD are stopped */
			if (strcmp (row[slave_io_field], "Yes") != 0 || strcmp (row[slave_sql_field], "Yes") != 0) {
				mysql_free_result (res);
				mysql_close (&mysql);
				die (STATE_CRITICAL, "%s\n", slaveresult);
			}

			if (verbose >=3) {
				if (seconds_behind_field == -1) {
					printf("seconds_behind_field not found\n");
				} else {
					printf ("seconds_behind_field(index %d)=%s\n", seconds_behind_field, row[seconds_behind_field]);
				}
			}

			/* Check Seconds Behind against threshold */
			if ((seconds_behind_field != -1) && (strcmp (row[seconds_behind_field], "NULL") != 0)) {
				double value = atof(row[seconds_behind_field]);
				int status;

				status = get_status(value, my_threshold);

				xasprintf (&perf, "%s %s", perf, fperfdata ("seconds behind master", value, "s",
        	                        TRUE, (double) warning_time,
                	                TRUE, (double) critical_time,
                        	        FALSE, 0,
                                	FALSE, 0));

				if (status == STATE_WARNING) {
					printf("SLOW_SLAVE %s: %s|%s\n", _("WARNING"), slaveresult, perf);
					exit(STATE_WARNING);
				} else if (status == STATE_CRITICAL) {
					printf("SLOW_SLAVE %s: %s|%s\n", _("CRITICAL"), slaveresult, perf);
					exit(STATE_CRITICAL);
				}
			}
		}

		/* free the result */
		mysql_free_result (res);
	}

	/* close the connection */
	mysql_close (&mysql);

	/* print out the result of stats */
	if (check_slave) {
		printf ("%s %s|%s\n", result, slaveresult, perf);
	} else {
		printf ("%s|%s\n", result, perf);
	}

	return STATE_OK;
}


/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;
	char *warning = NULL;
	char *critical = NULL;

	int option = 0;
	static struct option longopts[] = {
		{"hostname", required_argument, 0, 'H'},
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
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{"ssl", no_argument, 0, 'l'},
		{"ca-cert", optional_argument, 0, 'C'},
		{"key", required_argument,0,'k'},
		{"cert", required_argument,0,'a'},
		{"ca-dir", required_argument, 0, 'D'},
		{"ciphers", required_argument, 0, 'L'},
		{0, 0, 0, 0}
	};

	if (argc < 1)
		return ERROR;

	while (1) {
		c = getopt_long (argc, argv, "hlvVSP:p:u:d:H:s:c:w:a:k:C:D:L:f:g:", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'H':									/* hostname */
			if (is_host (optarg)) {
				db_host = optarg;
			}
			else {
				usage2 (_("Invalid hostname/address"), optarg);
			}
			break;
		case 's':									/* socket */
			db_socket = optarg;
			break;
		case 'd':									/* database */
			db = optarg;
			break;
		case 'l':
			ssl = true;
			break;
		case 'C':
			ca_cert = optarg;
			break;
		case 'a':
			cert = optarg;
			break;
		case 'k':
			key = optarg;
			break;
		case 'D':
			ca_dir = optarg;
			break;
		case 'L':
			ciphers = optarg;
			break;
		case 'u':									/* username */
			db_user = optarg;
			break;
		case 'p':									/* authentication information: password */
			db_pass = strdup(optarg);

			/* Delete the password from process list */
			while (*optarg != '\0') {
				*optarg = 'X';
				optarg++;
			}
			break;
		case 'f':									/* client options file */
			opt_file = optarg;
			break;
		case 'g':									/* client options group */
			opt_group = optarg;
			break;
		case 'P':									/* critical time threshold */
			db_port = atoi (optarg);
			break;
		case 'S':
			check_slave = 1;							/* check-slave */
			break;
		case 'w':
			warning = optarg;
			warning_time = strtod (warning, NULL);
			break;
		case 'c':
			critical = optarg;
			critical_time = strtod (critical, NULL);
			break;
		case 'V':									/* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'v':
			verbose++;
			break;
		case '?':									/* help */
			usage5 ();
		}
	}

	c = optind;

	set_thresholds(&my_threshold, warning, critical);

	while ( argc > c ) {

		if (db_host == NULL)
			if (is_host (argv[c])) {
				db_host = argv[c++];
			}
			else {
				usage2 (_("Invalid hostname/address"), argv[c]);
			}
		else if (db_user == NULL)
			db_user = argv[c++];
		else if (db_pass == NULL)
			db_pass = argv[c++];
		else if (db == NULL)
			db = argv[c++];
		else if (is_intnonneg (argv[c]))
			db_port = atoi (argv[c++]);
		else
			break;
	}

	return validate_arguments ();
}


int
validate_arguments (void)
{
	if (db_user == NULL)
		db_user = strdup("");

	if (opt_file == NULL)
		opt_file = strdup("");

	if (opt_group == NULL)
		opt_group = strdup("");

	if (db_host == NULL)
		db_host = strdup("");

	if (db == NULL)
		db = strdup("");

	return OK;
}


void
print_help (void)
{
	char *myport;
	xasprintf (&myport, "%d", MYSQL_PORT);

	print_revision (progname, NP_VERSION);

	printf (_(COPYRIGHT), copyright, email);

	printf ("%s\n", _("This program tests connections to a MySQL server"));

  printf ("\n\n");

	print_usage ();

  printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

  printf (UT_HOST_PORT, 'P', myport);
  printf (" %s\n", "-s, --socket=STRING");
  printf ("    %s\n", _("Use the specified socket (has no effect if -H is used)"));

  printf (" %s\n", "-d, --database=STRING");
  printf ("    %s\n", _("Check database with indicated name"));
  printf (" %s\n", "-f, --file=STRING");
  printf ("    %s\n", _("Read from the specified client options file"));
  printf (" %s\n", "-g, --group=STRING");
  printf ("    %s\n", _("Use a client options group"));
  printf (" %s\n", "-u, --username=STRING");
  printf ("    %s\n", _("Connect using the indicated username"));
  printf (" %s\n", "-p, --password=STRING");
  printf ("    %s\n", _("Use the indicated password to authenticate the connection"));
  printf ("    ==> %s <==\n", _("IMPORTANT: THIS FORM OF AUTHENTICATION IS NOT SECURE!!!"));
  printf ("    %s\n", _("Your clear-text password could be visible as a process table entry"));
  printf (" %s\n", "-S, --check-slave");
  printf ("    %s\n", _("Check if the slave thread is running properly."));
  printf (" %s\n", "-w, --warning");
  printf ("    %s\n", _("Exit with WARNING status if slave server is more than INTEGER seconds"));
  printf ("    %s\n", _("behind master"));
  printf (" %s\n", "-c, --critical");
  printf ("    %s\n", _("Exit with CRITICAL status if slave server is more then INTEGER seconds"));
  printf ("    %s\n", _("behind master"));
  printf (" %s\n", "-l, --ssl");
  printf ("    %s\n", _("Use ssl encryptation"));
  printf (" %s\n", "-C, --ca-cert=STRING");
  printf ("    %s\n", _("Path to CA signing the cert"));
  printf (" %s\n", "-a, --cert=STRING");
  printf ("    %s\n", _("Path to SSL certificate"));
  printf (" %s\n", "-k, --key=STRING");
  printf ("    %s\n", _("Path to private SSL key"));
  printf (" %s\n", "-D, --ca-dir=STRING");
  printf ("    %s\n", _("Path to CA directory"));
  printf (" %s\n", "-L, --ciphers=STRING");
  printf ("    %s\n", _("List of valid SSL ciphers"));


  printf ("\n");
  printf (" %s\n", _("There are no required arguments. By default, the local database is checked"));
  printf (" %s\n", _("using the default unix socket. You can force TCP on localhost by using an"));
  printf (" %s\n", _("IP address or FQDN ('localhost' will use the socket as well)."));

	printf ("\n");
	printf ("%s\n", _("Notes:"));
	printf (" %s\n", _("You must specify -p with an empty string to force an empty password,"));
	printf (" %s\n", _("overriding any my.cnf settings."));

	printf (UT_SUPPORT);
}


void
print_usage (void)
{
	printf ("%s\n", _("Usage:"));
  printf (" %s [-d database] [-H host] [-P port] [-s socket]\n",progname);
  printf ("       [-u user] [-p password] [-S] [-l] [-a cert] [-k key]\n");
  printf ("       [-C ca-cert] [-D ca-dir] [-L ciphers] [-f optfile] [-g group]\n");
}
