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

#define PROGNAME "check_mysql"

#include "common.h"
#include "utils.h"

#include <mysql/mysql.h>
#include <mysql/errmsg.h>

char *db_user = NULL;
char *db_host = NULL;
char *db_pass = NULL;
char *db = NULL;
unsigned int db_port = MYSQL_PORT;

int process_arguments (int, char **);
int call_getopt (int, char **);
int validate_arguments (void);
int check_disk (int usp, int free_disk);
void print_help (void);
void print_usage (void);

int
main (int argc, char **argv)
{

	MYSQL mysql;
	char result[1024];

	if (process_arguments (argc, argv) != OK)
		usage ("Invalid command arguments supplied\n");

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

	if (argc < 1)
		return ERROR;

	c = 0;
	while ((c += (call_getopt (argc - c, &argv[c]))) < argc) {

		if (is_option (argv[c]))
			continue;

		if (db_host == NULL)
			if (is_host (argv[c])) {
				db_host = argv[c];
			}
			else {
				usage ("Invalid host name");
			}
		else if (db_user == NULL)
			db_user = argv[c];
		else if (db_pass == NULL)
			db_pass = argv[c];
		else if (db == NULL)
			db = argv[c];
		else if (is_intnonneg (argv[c]))
			db_port = atoi (argv[c]);
	}

	if (db_host == NULL)
		db_host = strscpy (db_host, "127.0.0.1");

	return validate_arguments ();
}






int
call_getopt (int argc, char **argv)
{
	int c, i = 0;

#ifdef HAVE_GETOPT_H
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
#endif

	while (1) {
#ifdef HAVE_GETOPT_H
		c =
			getopt_long (argc, argv, "+hVP:p:u:d:H:", long_options, &option_index);
#else
		c = getopt (argc, argv, "+?hVP:p:u:d:H:");
#endif

		i++;

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case 'P':
		case 'p':
		case 'u':
		case 'd':
		case 'H':
			i++;
		}

		switch (c) {
		case 'H':									/* hostname */
			if (is_host (optarg)) {
				db_host = optarg;
			}
			else {
				usage ("Invalid host name\n");
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
			print_revision (my_basename (argv[0]), "$Revision$");
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case '?':									/* help */
			usage ("Invalid argument\n");
		}
	}
	return i;
}





int
validate_arguments (void)
{
	return OK;
}





void
print_help (void)
{
	print_revision (PROGNAME, "$Revision$");
	printf
		("Copyright (c) 2000 Didi Rieder/Karl DeBisschop\n\n"
		 "This plugin is for testing a mysql server.\n");
	print_usage ();
	printf
		("\nThere are no required arguments. By default, the local database with\n"
		 "a server listening on MySQL standard port %d will be checked\n\n"
		 "Options:\n"
		 " -d, --database=STRING\n"
		 "   Check database with indicated name\n"
		 " -H, --hostname=STRING or IPADDRESS\n"
		 "   Check server on the indicated host\n"
		 " -P, --port=INTEGER\n"
		 "   Make connection on the indicated port\n"
		 " -u, --username=STRING\n"
		 "   Connect using the indicated username\n"
		 " -p, --password=STRING\n"
		 "   Use the indicated password to authenticate the connection\n"
		 "   ==> IMPORTANT: THIS FORM OF AUTHENTICATION IS NOT SECURE!!! <==\n"
		 "   Your clear-text password will be visible as a process table entry\n"
		 " -h, --help\n"
		 "    Print detailed help screen\n"
		 " -V, --version\n" "    Print version information\n\n", MYSQL_PORT);
	support ();
}





void
print_usage (void)
{
	printf
		("Usage: %s [-d database] [-H host] [-P port] [-u user] [-p password]\n"
		 "       %s --help\n"
		 "       %s --version\n", PROGNAME, PROGNAME, PROGNAME);
}
