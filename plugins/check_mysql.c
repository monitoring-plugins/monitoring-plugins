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
const char *copyright = "1999-2002";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "utils.h"
#include "netutils.h"
#include <mysql/mysql.h>
#include <mysql/errmsg.h>

void
print_usage (void)
{
	printf (_("\
Usage: %s [-d database] [-H host] [-P port] [-u user] [-p password]\n"),
	        progname);
	printf (_(UT_HLP_VRS), progname, progname);
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
   Your clear-text password will be visible as a process table entry\n"));

	printf (_("\n\
There are no required arguments. By default, the local database with\n\
a server listening on MySQL standard port %d will be checked\n"), MYSQL_PORT);

	printf (_(UT_SUPPORT));
}

char *db_user = "";
char *db_host = "";
char *db_pass = "";
char *db = "";
unsigned int db_port = MYSQL_PORT;

int process_arguments (int, char **);
int validate_arguments (void);

int
main (int argc, char **argv)
{

	MYSQL mysql;
	char result[1024];

	if (process_arguments (argc, argv) != OK)
		usage (_("Invalid command arguments supplied\n"));

	/* initialize mysql  */
	mysql_init (&mysql);

	/* establish a connection to the server and error checking */
	if (!mysql_real_connect
			(&mysql, db_host, db_user, db_pass, db, db_port, NULL, 0)) {

		if (mysql_errno (&mysql) == CR_UNKNOWN_HOST) {
			printf ("%s\n", mysql_error (&mysql));
			return STATE_WARNING;

		}
		else if (mysql_errno (&mysql) == CR_VERSION_ERROR) {
			printf ("%s\n", mysql_error (&mysql));
			return STATE_WARNING;

		}
		else if (mysql_errno (&mysql) == CR_OUT_OF_MEMORY) {
			printf ("%s\n", mysql_error (&mysql));
			return STATE_WARNING;

		}
		else if (mysql_errno (&mysql) == CR_IPSOCK_ERROR) {
			printf ("%s\n", mysql_error (&mysql));
			return STATE_WARNING;

		}
		else if (mysql_errno (&mysql) == CR_SOCKET_CREATE_ERROR) {
			printf ("%s\n", mysql_error (&mysql));
			return STATE_WARNING;

		}
		else {
			printf ("%s\n", mysql_error (&mysql));
			return STATE_CRITICAL;
		}

	}

	/* get the server stats */
	sprintf (result, mysql_stat (&mysql));

	/* error checking once more */
	if (mysql_error (&mysql)) {

		if (mysql_errno (&mysql) == CR_SERVER_GONE_ERROR) {
			printf ("%s\n", mysql_error (&mysql));
			return STATE_CRITICAL;

		}
		else if (mysql_errno (&mysql) == CR_SERVER_LOST) {
			printf ("%s\n", mysql_error (&mysql));
			return STATE_CRITICAL;

		}
		else if (mysql_errno (&mysql) == CR_UNKNOWN_ERROR) {
			printf ("%s\n", mysql_error (&mysql));
			return STATE_UNKNOWN;
		}

	}

	/* close the connection */
	mysql_close (&mysql);

	/* print out the result of stats */
	printf ("%s\n", result);

	return STATE_OK;
}





/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option_index = 0;
	static struct option long_options[] = {
		{"hostname", required_argument, 0, 'H'},
		{"database", required_argument, 0, 'd'},
		{"username", required_argument, 0, 'u'},
		{"password", required_argument, 0, 'p'},
		{"port", required_argument, 0, 'P'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	if (argc < 1)
		return ERROR;

	while (1) {
		c = getopt_long (argc, argv, "hVP:p:u:d:H:", long_options, &option_index);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'H':									/* hostname */
			if (is_host (optarg)) {
				db_host = optarg;
			}
			else {
				usage (_("Invalid host name\n"));
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
		case 'V':									/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case '?':									/* help */
			usage (_("Invalid argument\n"));
		}
	}

	c = optind;

	while ( argc > c ) {

		if (strlen(db_host) == 0)
			if (is_host (argv[c])) {
				db_host = argv[c++];
			}
			else {
				usage ("Invalid host name");
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
	return OK;
}
