/******************************************************************************
 *
 * Program: PostgreSQL plugin for Nagios
 * License: GPL
 *
 * License Information:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id$
 *
 *****************************************************************************/

const char *progname = "check_pgsql";
#define REVISION "$Revision$"
#define COPYRIGHT "1999-2001"
#define AUTHOR "Karl DeBisschop"
#define EMAIL "kdebisschop@users.sourceforge.net"
#define SUMMARY "Tests to see if a PostgreSQL DBMS is accepting connections.\n"

#define OPTIONS "[-c critical_time] [-w warning_time] [-t timeout] [-H host]\n\
             [-P port] [-d database] [-l logname] [-p password]"

#define LONGOPTIONS "\
  -c, --critical=INTEGER\n\
    Exit STATE_CRITICAL if connection time exceeds threshold (default: %d)\n\
  -w, --warning=INTEGER\n\
    Exit STATE_WARNING if connection time exceeds threshold (default: %d)\n\
  -t, --timeout=INTEGER\n\
    Terminate test if timeout limit is exceeded (default: %d)\n\
  -H, --hostname=STRING\n\
    Name or numeric IP address of machine running backend\n\
  -P, --port=INTEGER\n\
    Port running backend (default: %d)\n\
  -d, --database=STRING\n\
    Database to check (default: %s)\n\
  -l, --logname = STRING\n\
    Login name of user\n\
  -p, --password = STRING\n\
    Password (BIG SECURITY ISSUE)\n"

#define DESCRIPTION "All parameters are optional.\n\
\n\
This plugin tests a PostgreSQL DBMS to determine whether it is active and\n\
accepting queries. In its current operation, it simply connects to the\n\
specified database, and then disconnects. If no database is specified, it\n\
connects to the template1 database, which is present in every functioning \n\
PostgreSQL DBMS.\n\
\n\
The plugin will connect to a local postmaster if no host is specified. To\n\
connect to a remote host, be sure that the remote postmaster accepts TCP/IP\n\
connections (start the postmaster with the -i option).\n\
\n\
Typically, the nagios user (unless the --logname option is used) should be\n\
able to connect to the database without a password. The plugin can also send\n\
a password, but no effort is made to obsure or encrypt the password.\n"

#define DEFAULT_DB "template1"
#define DEFAULT_HOST "127.0.0.1"
enum {
	DEFAULT_PORT = 5432,
	DEFAULT_WARN = 2,
	DEFAULT_CRIT = 8,
	DEFAULT_TIMEOUT = 30
};

#include "config.h"
#include "common.h"
#include "utils.h"
#include <netdb.h>
#include <libpq-fe.h>

int process_arguments (int, char **);
int validate_arguments (void);
void print_usage (void);
void print_help (void);
int is_pg_dbname (char *);
int is_pg_logname (char *);

char *pghost = NULL;						/* host name of the backend server */
char *pgport = NULL;						/* port of the backend server */
int default_port = DEFAULT_PORT;
char *pgoptions = NULL;
char *pgtty = NULL;
char dbName[NAMEDATALEN] = DEFAULT_DB;
char *pguser = NULL;
char *pgpasswd = NULL;
int twarn = DEFAULT_WARN;
int tcrit = DEFAULT_CRIT;

PGconn *conn;
/*PGresult   *res;*/


/******************************************************************************

The (psuedo?)literate programming XML is contained within \@\@\- <XML> \-\@\@
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
<itemizedlist>
<listitem>Add option to get password from a secured file rather than the command line</listitem>
<listitem>Add option to specify the query to execute</listitem>
</itemizedlist>
</sect2>


<sect2>
<title>Functions</title>
-@@
******************************************************************************/

int
main (int argc, char **argv)
{
	int elapsed_time;

	/* begin, by setting the parameters for a backend connection if the
	 * parameters are null, then the system will try to use reasonable
	 * defaults by looking up environment variables or, failing that,
	 * using hardwired constants */

	pgoptions = NULL;  /* special options to start up the backend server */
	pgtty = NULL;      /* debugging tty for the backend server */

	if (process_arguments (argc, argv) == ERROR)
		usage ("Could not parse arguments");

	/* Set signal handling and alarm */
	if (signal (SIGALRM, timeout_alarm_handler) == SIG_ERR) {
		printf ("Cannot catch SIGALRM");
		return STATE_UNKNOWN;
	}
	alarm (timeout_interval);

	/* make a connection to the database */
	time (&start_time);
	conn =
		PQsetdbLogin (pghost, pgport, pgoptions, pgtty, dbName, pguser, pgpasswd);
	time (&end_time);
	elapsed_time = (int) (end_time - start_time);

	/* check to see that the backend connection was successfully made */
	if (PQstatus (conn) == CONNECTION_BAD) {
		printf ("PGSQL: CRITICAL - no connection to '%s' (%s).\n", dbName,
						PQerrorMessage (conn));
		PQfinish (conn);
		return STATE_CRITICAL;
	}
	else if (elapsed_time > tcrit) {
		PQfinish (conn);
		printf ("PGSQL: CRITICAL - database %s (%d sec.)\n", dbName,
						elapsed_time);
		return STATE_CRITICAL;
	}
	else if (elapsed_time > twarn) {
		PQfinish (conn);
		printf ("PGSQL: WARNING - database %s (%d sec.)\n", dbName, elapsed_time);
		return STATE_WARNING;
	}
	else {
		PQfinish (conn);
		printf ("PGSQL: ok - database %s (%d sec.)\n", dbName, elapsed_time);
		return STATE_OK;
	}
}




void
print_help (void)
{
	print_revision (progname, REVISION);
	printf
		("Copyright (c) %s %s <%s>\n\n%s\n",
		 COPYRIGHT, AUTHOR, EMAIL, SUMMARY);
	print_usage ();
	printf
		("\nOptions:\n" LONGOPTIONS "\n" DESCRIPTION "\n", 
		 DEFAULT_WARN, DEFAULT_CRIT, DEFAULT_TIMEOUT, DEFAULT_PORT, DEFAULT_DB);
	support ();
}

void
print_usage (void)
{
	printf ("Usage:\n" " %s %s\n"
					" %s (-h | --help) for detailed help\n"
					" %s (-V | --version) for version information\n",
					progname, OPTIONS, progname, progname);
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] = {
		{"help", no_argument, 0, 'h'},
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
		{0, 0, 0, 0}
	};
#endif

	while (1) {
#ifdef HAVE_GETOPT_H
		c = getopt_long (argc, argv, "hVt:c:w:H:P:d:l:p:a:",
		                 long_options, &option_index);
#else
		c = getopt (argc, argv, "hVt:c:w:H:P:d:l:p:a:");
#endif
		if (c == EOF)
			break;

		switch (c) {
		case '?':     /* usage */
			usage3 ("Unknown argument", optopt);
		case 'h':     /* help */
			print_help ();
			exit (STATE_OK);
		case 'V':     /* version */
			print_revision (progname, REVISION);
			exit (STATE_OK);
		case 't':     /* timeout period */
			if (!is_integer (optarg))
				usage2 ("Timeout Interval must be an integer", optarg);
			timeout_interval = atoi (optarg);
			break;
		case 'c':     /* critical time threshold */
			if (!is_integer (optarg))
				usage2 ("Invalid critical threshold", optarg);
			tcrit = atoi (optarg);
			break;
		case 'w':     /* warning time threshold */
			if (!is_integer (optarg))
				usage2 ("Invalid critical threshold", optarg);
			twarn = atoi (optarg);
			break;
		case 'H':     /* host */
			if (!is_host (optarg))
				usage2 ("You gave an invalid host name", optarg);
			pghost = optarg;
			break;
		case 'P':     /* port */
			if (!is_integer (optarg))
				usage2 ("Port must be an integer", optarg);
			pgport = optarg;
			break;
		case 'd':     /* database name */
			if (!is_pg_dbname (optarg))
				usage2 ("Database name is not valid", optarg);
			strncpy (dbName, optarg, NAMEDATALEN - 1);
			dbName[NAMEDATALEN - 1] = 0;
			break;
		case 'l':     /* login name */
			if (!is_pg_logname (optarg))
				usage2 ("user name is not valid", optarg);
			pguser = optarg;
			break;
		case 'p':     /* authentication password */
		case 'a':
			pgpasswd = optarg;
			break;
		}
	}

	return validate_arguments ();
}


/******************************************************************************

@@-
<sect3>
<title>validate_arguments</title>

<para>&PROTO_validate_arguments;</para>

<para>Given a database name, this function returns TRUE if the string
is a valid PostgreSQL database name, and returns false if it is
not.</para>

<para>Valid PostgreSQL database names are less than &NAMEDATALEN;
characters long and consist of letters, numbers, and underscores. The
first character cannot be a number, however.</para>

</sect3>
-@@
******************************************************************************/

int
validate_arguments ()
{
	return OK;
}



/******************************************************************************

@@-
<sect3>
<title>is_pg_dbname</title>

<para>&PROTO_is_pg_dbname;</para>

<para>Given a database name, this function returns TRUE if the string
is a valid PostgreSQL database name, and returns false if it is
not.</para>

<para>Valid PostgreSQL database names are less than &NAMEDATALEN;
characters long and consist of letters, numbers, and underscores. The
first character cannot be a number, however.</para>

</sect3>
-@@
******************************************************************************/

int
is_pg_dbname (char *dbname)
{
	char txt[NAMEDATALEN];
	char tmp[NAMEDATALEN];
	if (strlen (dbname) > NAMEDATALEN - 1)
		return (FALSE);
	strncpy (txt, dbname, NAMEDATALEN - 1);
	txt[NAMEDATALEN - 1] = 0;
	if (sscanf (txt, "%[_a-zA-Z]%[^_a-zA-Z0-9]", tmp, tmp) == 1)
		return (TRUE);
	if (sscanf (txt, "%[_a-zA-Z]%[_a-zA-Z0-9]%[^_a-zA-Z0-9]", tmp, tmp, tmp) ==
			2) return (TRUE);
	return (FALSE);
}

/**

the tango program should eventually create an entity here based on the 
function prototype

@@-
<sect3>
<title>is_pg_logname</title>

<para>&PROTO_is_pg_logname;</para>

<para>Given a username, this function returns TRUE if the string is a
valid PostgreSQL username, and returns false if it is not. Valid PostgreSQL
usernames are less than &NAMEDATALEN; characters long and consist of
letters, numbers, dashes, and underscores, plus possibly some other
characters.</para>

<para>Currently this function only checks string length. Additional checks
should be added.</para>

</sect3>
-@@
******************************************************************************/

int
is_pg_logname (char *username)
{
	if (strlen (username) > NAMEDATALEN - 1)
		return (FALSE);
	return (TRUE);
}

/******************************************************************************
@@-
</sect2> 
</sect1>
</article>
-@@
******************************************************************************/
