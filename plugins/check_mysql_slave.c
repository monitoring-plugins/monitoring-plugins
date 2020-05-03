/*****************************************************************************
*
* Monitoring check_mysql plugin
*
* License: GPL
* Copyright (c) 1999 Didi Rieder (adrieder@sbox.tu-graz.ac.at)
* Copyright (c) 2000 Karl DeBisschop (kdebisschop@users.sourceforge.net)
* Copyright (c) 1999-2011 Monitoring Plugins Development Team
* Copyright (c) 2016 Zalora South East Asia Pte. Ltd
*
* Description:
*
* This file contains the check_mysql_slave plugin
*
* This program tests MySQL/MariaDB slaves
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

const char *progname = "check_mysql_slave";
const char *copyright = "1999-2016";
const char *email = "devel@monitoring-plugins.org";

#define SLAVERESULTSIZE 256

#include "common.h"
#include "utils.h"
#include "utils_base.h"
#include "netutils.h"

#include <mysql.h>
#include <mysqld_error.h>
#include <errmsg.h>

char *db_user = NULL;
char *db_host = NULL;
char *db_socket = NULL;
char *db_pass = NULL;
char *ca_cert = NULL;
char *ca_dir = NULL;
char *cert = NULL;
char *key = NULL;
char *ciphers = NULL;
bool ssl = false;
char *opt_file = NULL;
char *opt_group = NULL;
unsigned int db_port = 0;
int warn_sec = 0, crit_sec = 0;
char *connection_name = NULL;
char *query;

static double warning_time = 0;
static double critical_time = 0;

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
  char *perf;
  perf = strdup ("");

  char *error = NULL;
  char slaveresult[SLAVERESULTSIZE];

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  /* Parse extra opts if any */
  argv = np_extra_opts (&argc, argv, progname);

  if (process_arguments (argc, argv) == ERROR)
    usage4 (_("Could not parse arguments"));

  /* initialize mysql  */
  mysql_init (&mysql);

  if (opt_file != NULL)
    mysql_options (&mysql, MYSQL_READ_DEFAULT_FILE, opt_file);

  if (opt_group != NULL)
    mysql_options (&mysql, MYSQL_READ_DEFAULT_GROUP, opt_group);
  else
    mysql_options (&mysql, MYSQL_READ_DEFAULT_GROUP, "client");

  if (ssl)
    mysql_ssl_set (&mysql, key, cert, ca_cert, ca_dir, ciphers);

  if (!mysql_real_connect
      (&mysql, db_host, db_user, db_pass, "", db_port, db_socket, 0))
    {
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

  if (connection_name != NULL && strcmp (connection_name, "") != 0)
    {
      xasprintf (&query, "show slave '%s' status", connection_name);
    }
  else
    {
      xasprintf (&query, "show slave status");
    }

  if (mysql_query (&mysql, query) != 0)
    {
      error = strdup (mysql_error (&mysql));
      mysql_close (&mysql);
      die (STATE_CRITICAL, _("slave query error: %s\n"), error);
    }

  if ((res = mysql_store_result (&mysql)) == NULL)
    {
      error = strdup (mysql_error (&mysql));
      mysql_close (&mysql);
      die (STATE_CRITICAL, _("slave store_result error: %s\n"), error);
    }

  /* Check there is some data */
  if (mysql_num_rows (res) == 0)
    {
      mysql_close (&mysql);
      die (STATE_WARNING, "%s\n", _("No slaves defined"));
    }

  /* fetch the first row */
  if ((row = mysql_fetch_row (res)) == NULL)
    {
      error = strdup (mysql_error (&mysql));
      mysql_free_result (res);
      mysql_close (&mysql);
      die (STATE_CRITICAL, _("slave fetch row error: %s\n"), error);
    }

  const char *last_io_error = NULL;
  const char *last_sql_error = NULL;
  const char *seconds_behind_master = NULL;
  const char *slave_io = NULL;
  const char *slave_sql = NULL;
  MYSQL_FIELD *fields;

  int i, num_fields;
  num_fields = mysql_num_fields (res);
  fields = mysql_fetch_fields (res);
  for (i = 0; i < num_fields; i++)
    {
      if (strcmp (fields[i].name, "Last_IO_Error") == 0 && row[i]
          && row[i][0])
        {
          last_io_error = row[i];
          continue;
        }
      if (strcmp (fields[i].name, "Last_SQL_Error") == 0 && row[i]
          && row[i][0])
        {
          last_sql_error = row[i];
          continue;
        }
      if (strcmp (fields[i].name, "Slave_IO_Running") == 0)
        {
          slave_io = row[i];
          continue;
        }
      if (strcmp (fields[i].name, "Slave_SQL_Running") == 0)
        {
          slave_sql = row[i];
          continue;
        }
      if (strcmp (fields[i].name, "Seconds_Behind_Master") == 0)
        {
          seconds_behind_master = row[i];
          continue;
        }
    }

  /* Check if slave status is available */
  if ((slave_io == NULL) || (slave_sql == NULL))
    {
      mysql_free_result (res);
      mysql_close (&mysql);
      die (STATE_CRITICAL, "Slave status unavailable\n");
    }

  const char *last_error;
  if (last_sql_error)
    last_error = last_sql_error;
  else if (last_io_error)
    last_error = last_io_error;
  else
    last_error = NULL;

  if ((seconds_behind_master == NULL)
      || (strcmp (seconds_behind_master, "NULL") == 0))
    seconds_behind_master = "N/A";

  /* Save slave status in slaveresult */
  snprintf (slaveresult, SLAVERESULTSIZE,
            "Slave IO: %s, Slave SQL: %s, %s: %s",
            slave_io, slave_sql,
            (last_error ? "Last Error" : "Seconds Behind Master"),
            (last_error ? last_error : seconds_behind_master));

  if (strcmp (slave_io, "Yes") != 0 || strcmp (slave_sql, "Yes") != 0)
    {
      mysql_free_result (res);
      mysql_close (&mysql);
      if (last_io_error || last_sql_error)
        {
          die (STATE_CRITICAL, "%s\n", slaveresult);
        }
      else
        {
          die (STATE_WARNING, "%s\n", slaveresult);
        };
    }

  /* Check Seconds Behind against threshold */
  if (strcmp (seconds_behind_master, "N/A") != 0)
    {
      double value = atof (seconds_behind_master);
      int status;

      status = get_status (value, my_threshold);

      xasprintf (&perf, "%s %s", perf,
                 fperfdata ("lag", value, "s", TRUE,
                            (double) warning_time, TRUE,
                            (double) critical_time, FALSE, 0, FALSE, 0));

      if (status == STATE_WARNING)
        {
          printf ("LAG %s: %s|%s\n", _("WARNING"), slaveresult, perf);
          exit (STATE_WARNING);
        }
      else if (status == STATE_CRITICAL)
        {
          printf ("LAG %s: %s|%s\n", _("CRITICAL"), slaveresult, perf);
          exit (STATE_CRITICAL);
        }
    }

  mysql_free_result (res);
  mysql_close (&mysql);

  printf ("%s|%s\n", slaveresult, perf);
  return STATE_OK;
}


int
process_arguments (int argc, char **argv)
{
  int c;
  char *warning = NULL;
  char *critical = NULL;

  int option = 0;
  static struct option longopts[] = {
    {"ca-cert", optional_argument, 0, 'C'},
    {"ca-dir", required_argument, 0, 'D'},
    {"cert", required_argument, 0, 'a'},
    {"ciphers", required_argument, 0, 'L'},
    {"connection-name", required_argument, 0, 'N'},
    {"critical", required_argument, 0, 'c'},
    {"file", required_argument, 0, 'f'},
    {"group", required_argument, 0, 'g'},
    {"help", no_argument, 0, 'h'},
    {"hostname", required_argument, 0, 'H'},
    {"key", required_argument, 0, 'k'},
    {"password", required_argument, 0, 'p'},
    {"port", required_argument, 0, 'P'},
    {"socket", required_argument, 0, 's'},
    {"ssl", no_argument, 0, 'l'},
    {"username", required_argument, 0, 'u'},
    {"version", no_argument, 0, 'V'},
    {"warning", required_argument, 0, 'w'},
    {0, 0, 0, 0}
  };

  if (argc < 1)
    return ERROR;

  while (1)
    {
      c =
        getopt_long (argc, argv, "hlVnSP:p:u:H:s:c:w:a:k:C:D:L:f:g:N:",
                     longopts, &option);

      if (c == -1 || c == EOF)
        break;

      switch (c)
        {
        case 'H':              /* hostname */
          if (is_host (optarg))
            {
              db_host = optarg;
            }
          else
            {
              usage2 (_("Invalid hostname/address"), optarg);
            }
          break;
        case 's':              /* socket */
          db_socket = optarg;
          break;
        case 'N':
          connection_name = optarg;
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
        case 'u':              /* username */
          db_user = optarg;
          break;
        case 'p':              /* authentication information: password */
          db_pass = strdup (optarg);

          /* Delete the password from process list */
          while (*optarg != '\0')
            {
              *optarg = 'X';
              optarg++;
            }
          break;
        case 'f':              /* client options file */
          opt_file = optarg;
          break;
        case 'g':              /* client options group */
          opt_group = optarg;
          break;
        case 'P':              /* critical time threshold */
          db_port = atoi (optarg);
          break;
        case 'w':
          warning = optarg;
          warning_time = strtod (warning, NULL);
          break;
        case 'c':
          critical = optarg;
          critical_time = strtod (critical, NULL);
          break;
        case 'V':              /* version */
          print_revision (progname, NP_VERSION);
          exit (STATE_OK);
        case 'h':              /* help */
          print_help ();
          exit (STATE_OK);
        case '?':              /* help */
          usage5 ();
        }
    }

  c = optind;

  set_thresholds (&my_threshold, warning, critical);

  while (argc > c)
    {

      if (db_host == NULL)
        if (is_host (argv[c]))
          {
            db_host = argv[c++];
          }
        else
          {
            usage2 (_("Invalid hostname/address"), argv[c]);
          }
      else if (db_user == NULL)
        db_user = argv[c++];
      else if (db_pass == NULL)
        db_pass = argv[c++];
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
    db_user = strdup ("");

  if (db_host == NULL)
    db_host = strdup ("");

  return OK;
}


void
print_help (void)
{
  char *myport;
  xasprintf (&myport, "%d", MYSQL_PORT);

  print_revision (progname, NP_VERSION);

  printf (_(COPYRIGHT), copyright, email);

  printf ("%s\n", _("This program tests MySQL/MariaDB slaves"));

  printf ("\n\n");

  print_usage ();

  printf (UT_HELP_VRSN);
  printf (UT_EXTRA_OPTS);

  printf (UT_HOST_PORT, 'P', myport);

  printf (" %s\n", "-s, --socket=STRING");
  printf ("    %s\n",
          _("Use the specified socket (has no effect if -H is used)"));

  printf (" %s\n", "-f, --file=STRING");
  printf ("    %s\n", _("Read from the specified client options file"));
  printf (" %s\n", "-g, --group=STRING");
  printf ("    %s\n", _("Use a client options group"));
  printf (" %s\n", "-u, --username=STRING");
  printf ("    %s\n", _("Connect using the indicated username"));
  printf (" %s\n", "-p, --password=STRING");
  printf ("    %s\n",
          _("Use the indicated password to authenticate the connection"));
  printf ("    ==> %s <==\n",
          _("IMPORTANT: THIS FORM OF AUTHENTICATION IS NOT SECURE!!!"));
  printf ("    %s\n",
          _
          ("Your clear-text password could be visible as a process table entry"));
  printf (" %s\n", "-N, --connection-name");
  printf ("    %s\n", _("Connection name if using multi-source replication"));

  printf (" %s\n", "-w, --warning");
  printf ("    %s\n",
          _
          ("Exit with WARNING status if slave server is more than INTEGER seconds"));
  printf ("    %s\n", _("behind master"));
  printf (" %s\n", "-c, --critical");
  printf ("    %s\n",
          _
          ("Exit with CRITICAL status if slave server is more then INTEGER seconds"));
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
  printf (" %s\n",
          _
          ("There are no required arguments. By default, the local database is checked"));
  printf (" %s\n",
          _
          ("using the default unix socket. You can force TCP on localhost by using an"));
  printf (" %s\n",
          _("IP address or FQDN ('localhost' will use the socket as well)."));

  printf ("\n");
  printf ("%s\n", _("Notes:"));
  printf (" %s\n",
          _
          ("You must specify -p with an empty string to force an empty password,"));
  printf (" %s\n", _("overriding any my.cnf settings."));

  printf (UT_SUPPORT);
}


void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
  printf (" %s [-H host] [-P port] [-s socket]\n", progname);
  printf ("       [-u user] [-p password] [-S] [-l] [-a cert] [-k key]\n");
  printf
    ("       [-C ca-cert] [-D ca-dir] [-L ciphers] [-f optfile] [-g group]\n");
}
