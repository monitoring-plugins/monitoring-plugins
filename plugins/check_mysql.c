/******************************************************************************
*
* CHECK_MYSQL.C
*
* Program: Mysql plugin for Nagios
* License: GPL
* Copyright (c) 1999 Didi Rieder (adrieder@sbox.tu-graz.ac.at)
*  portions (c) 2000 Karl DeBisschop (kdebisschop@users.sourceforge.net)
* 
* $Id$
*
* Description:
*
* This plugin is for testing a mysql server.
******************************************************************************/

const char *progname = "check_mysql";
const char *revision = "$Revision$";
const char *copyright = "1999-2004";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#define SLAVERESULTSIZE 40

#include "common.h"
#include "utils.h"
#include "netutils.h"

#include <mysql/mysql.h>
#include <mysql/errmsg.h>

char *db_user = NULL;
char *db_host = NULL;
char *db_pass = NULL;
char *db = NULL;
unsigned int db_port = MYSQL_PORT;
int check_slave = 0;

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
	char slaveresult[SLAVERESULTSIZE];

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	if (process_arguments (argc, argv) != TRUE)
		usage4 (_("Could not parse arguments"));

	/* initialize mysql  */
	mysql_init (&mysql);

	/* establish a connection to the server and error checking */
	if (!mysql_real_connect(&mysql,db_host,db_user,db_pass,db,db_port,NULL,0)) {
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

	if(check_slave) {
		/* check the slave status */
		if (mysql_query (&mysql, "show slave status") != 0) {
			mysql_close (&mysql);
			die (STATE_CRITICAL, "slave query error: %s\n", mysql_error (&mysql));
		}

		/* store the result */
		if ( (res = mysql_store_result (&mysql)) == NULL) {
			mysql_close (&mysql);
			die (STATE_CRITICAL, "slave store_result error: %s\n", mysql_error (&mysql));
		}

		/* fetch the first row */
		if ( (row = mysql_fetch_row (res)) == NULL) {
			mysql_free_result (res);
			mysql_close (&mysql);
			die (STATE_CRITICAL, "slave fetch row error: %s\n", mysql_error (&mysql));
		}

		if (mysql_field_count (&mysql) == 12) {
			/* mysql 3.23.x */
			snprintf (slaveresult, SLAVERESULTSIZE, "Slave running: %s", row[6]);
			if (strcmp (row[6], "Yes") != 0) {
				mysql_free_result (res);
				mysql_close (&mysql);
				die (STATE_CRITICAL, "%s\n", slaveresult);
			}

		} else {
			/* mysql 4.x.x */
			snprintf (slaveresult, SLAVERESULTSIZE, "Slave IO: %s Slave SQL: %s", row[9], row[10]);
			if (strcmp (row[9], "Yes") != 0 || strcmp (row[10], "Yes") != 0) {
				mysql_free_result (res);
				mysql_close (&mysql);
				die (STATE_CRITICAL, "%s\n", slaveresult);
			}
		}

		/* free the result */
		mysql_free_result (res);
	}

	/* close the connection */
	mysql_close (&mysql);

	/* print out the result of stats */
	if (check_slave) {
		printf ("%s %s\n", result, slaveresult);
	} else {
		printf ("%s\n", result);
	}

	return STATE_OK;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option = 0;
	static struct option longopts[] = {
		{"hostname", required_argument, 0, 'H'},
		{"database", required_argument, 0, 'd'},
		{"username", required_argument, 0, 'u'},
		{"password", required_argument, 0, 'p'},
		{"port", required_argument, 0, 'P'},
		{"check-slave", no_argument, 0, 'S'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	if (argc < 1)
		return ERROR;

	while (1) {
		c = getopt_long (argc, argv, "hVSP:p:u:d:H:", longopts, &option);

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
		case 'd':									/* hostname */
			db = optarg;
			break;
		case 'u':									/* username */
			db_user = optarg;
			break;
		case 'p':									/* authentication information: password */
			db_pass = optarg;
			break;
		case 'P':									/* critical time threshold */
			db_port = atoi (optarg);
			break;
		case 'S':
			check_slave = 1;							/* check-slave */
			break;
		case 'V':									/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case '?':									/* help */
			printf (_("%s: Unknown argument: %s\n\n"), progname, optarg);
			print_usage ();
			exit (STATE_UNKNOWN);
		}
	}

	c = optind;

	while ( argc > c ) {

		if (strlen(db_host) == 0)
			if (is_host (argv[c])) {
				db_host = argv[c++];
			}
			else {
				usage2 (_("Invalid hostname/address"), optarg);
			}
		else if (strlen(db_user) == 0)
			db_user = argv[c++];
		else if (strlen(db_pass) == 0)
			db_pass = argv[c++];
		else if (strlen(db) == 0)
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

	if (db_host == NULL)
		db_host = strdup("");

	if (db_pass == NULL)
		db_pass == strdup("");

	if (db == NULL)
		db = strdup("");

	return OK;
}



void
print_help (void)
{
	char *myport;
	asprintf (&myport, "%d", MYSQL_PORT);

	print_revision (progname, revision);

	printf (_(COPYRIGHT), copyright, email);

	printf (_("This program tests connections to a mysql server\n"));

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_(UT_HOST_PORT), 'P', myport);

	printf (_("\
 -d, --database=STRING\n\
   Check database with indicated name\n\
 -u, --username=STRING\n\
   Connect using the indicated username\n\
 -p, --password=STRING\n\
   Use the indicated password to authenticate the connection\n\
   ==> IMPORTANT: THIS FORM OF AUTHENTICATION IS NOT SECURE!!! <==\n\
   Your clear-text password will be visible as a process table entry\n\
 -S, --check-slave\n\
   Check if the slave thread is running properly.\n"));

	printf (_("\n\
There are no required arguments. By default, the local database with\n\
a server listening on MySQL standard port %d will be checked\n"), MYSQL_PORT);

	printf (_(UT_SUPPORT));
}



void
print_usage (void)
{
	printf ("\
Usage: %s [-d database] [-H host] [-P port] [-u user] [-p password] [-S]\n",
	        progname);
	printf (UT_HLP_VRS, progname, progname);
}
