/*****************************************************************************
* 
* Nagios check_dig plugin
* 
* License: GPL
* Copyright (c) 2002-2008 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_dig plugin
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

/* Hackers note:
 *  There are typecasts to (char *) from _("foo bar") in this file.
 *  They prevent compiler warnings. Never (ever), permute strings obtained
 *  that are typecast from (const char *) (which happens when --disable-nls)
 *  because on some architectures those strings are in non-writable memory */

const char *progname = "check_dig";
const char *copyright = "2002-2008";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"
#include "runcmd.h"

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

#define UNDEFINED 0
#define DEFAULT_PORT 53

char *query_address = NULL;
char *record_type = "A";
char *expected_address = NULL;
char *dns_server = NULL;
char *dig_args = "";
char *query_transport = "";
int verbose = FALSE;
int server_port = DEFAULT_PORT;
double warning_interval = UNDEFINED;
double critical_interval = UNDEFINED;
struct timeval tv;

int
main (int argc, char **argv)
{
  char *command_line;
  output chld_out, chld_err;
  char *msg = NULL;
  size_t i;
  char *t;
  long microsec;
  double elapsed_time;
  int result = STATE_UNKNOWN;

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  /* Set signal handling and alarm */
  if (signal (SIGALRM, runcmd_timeout_alarm_handler) == SIG_ERR)
    usage_va(_("Cannot catch SIGALRM"));

  /* Parse extra opts if any */
  argv=np_extra_opts (&argc, argv, progname);

  if (process_arguments (argc, argv) == ERROR)
    usage_va(_("Could not parse arguments"));

  /* get the command to run */
  xasprintf (&command_line, "%s @%s -p %d %s -t %s %s %s",
            PATH_TO_DIG, dns_server, server_port, query_address, record_type, dig_args, query_transport);

  alarm (timeout_interval);
  gettimeofday (&tv, NULL);

  if (verbose) {
    printf ("%s\n", command_line);
    if(expected_address != NULL) {
      printf (_("Looking for: '%s'\n"), expected_address);
    } else {
      printf (_("Looking for: '%s'\n"), query_address);
    }
  }

  /* run the command */
  if(np_runcmd(command_line, &chld_out, &chld_err, 0) != 0) {
    result = STATE_WARNING;
    msg = (char *)_("dig returned an error status");
  }

  for(i = 0; i < chld_out.lines; i++) {
    /* the server is responding, we just got the host name... */
    if (strstr (chld_out.line[i], ";; ANSWER SECTION:")) {

      /* loop through the whole 'ANSWER SECTION' */
      for(; i < chld_out.lines; i++) {
        /* get the host address */
        if (verbose)
          printf ("%s\n", chld_out.line[i]);

        if (strstr (chld_out.line[i], (expected_address == NULL ? query_address : expected_address)) != NULL) {
          msg = chld_out.line[i];
          result = STATE_OK;

          /* Translate output TAB -> SPACE */
          t = msg;
          while ((t = strchr(t, '\t')) != NULL) *t = ' ';
          break;
        }
      }

      if (result == STATE_UNKNOWN) {
        msg = (char *)_("Server not found in ANSWER SECTION");
        result = STATE_WARNING;
      }

      /* we found the answer section, so break out of the loop */
      break;
    }
  }

  if (result == STATE_UNKNOWN) {
    msg = (char *)_("No ANSWER SECTION found");
    result = STATE_CRITICAL;
  }

  /* If we get anything on STDERR, at least set warning */
  if(chld_err.buflen > 0) {
    result = max_state(result, STATE_WARNING);
    if(!msg) for(i = 0; i < chld_err.lines; i++) {
      msg = strchr(chld_err.line[0], ':');
      if(msg) {
        msg++;
        break;
      }
    }
  }

  microsec = deltime (tv);
  elapsed_time = (double)microsec / 1.0e6;

  if (critical_interval > UNDEFINED && elapsed_time > critical_interval)
    result = STATE_CRITICAL;

  else if (warning_interval > UNDEFINED && elapsed_time > warning_interval)
    result = STATE_WARNING;

  printf ("DNS %s - %.3f seconds response time (%s)|%s\n",
          state_text (result), elapsed_time,
          msg ? msg : _("Probably a non-existent host/domain"),
          fperfdata("time", elapsed_time, "s",
                    (warning_interval>UNDEFINED?TRUE:FALSE),
                    warning_interval,
                    (critical_interval>UNDEFINED?TRUE:FALSE),
            critical_interval,
            TRUE, 0, FALSE, 0));
  return result;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
  int c;

  int option = 0;
  static struct option longopts[] = {
    {"hostname", required_argument, 0, 'H'},
    {"query_address", required_argument, 0, 'l'},
    {"warning", required_argument, 0, 'w'},
    {"critical", required_argument, 0, 'c'},
    {"timeout", required_argument, 0, 't'},
    {"dig-arguments", required_argument, 0, 'A'},
    {"verbose", no_argument, 0, 'v'},
    {"version", no_argument, 0, 'V'},
    {"help", no_argument, 0, 'h'},
    {"record_type", required_argument, 0, 'T'},
    {"expected_address", required_argument, 0, 'a'},
    {"port", required_argument, 0, 'p'},
    {"use-ipv4", no_argument, 0, '4'},
    {"use-ipv6", no_argument, 0, '6'},
    {0, 0, 0, 0}
  };

  if (argc < 2)
    return ERROR;

  while (1) {
    c = getopt_long (argc, argv, "hVvt:l:H:w:c:T:p:a:A:46", longopts, &option);

    if (c == -1 || c == EOF)
      break;

    switch (c) {
    case 'h':                 /* help */
      print_help ();
      exit (STATE_OK);
    case 'V':                 /* version */
      print_revision (progname, NP_VERSION);
      exit (STATE_OK);
    case 'H':                 /* hostname */
      host_or_die(optarg);
      dns_server = optarg;
      break;
    case 'p':                 /* server port */
      if (is_intpos (optarg)) {
        server_port = atoi (optarg);
      }
      else {
        usage_va(_("Port must be a positive integer - %s"), optarg);
      }
      break;
    case 'l':                 /* address to lookup */
      query_address = optarg;
      break;
    case 'w':                 /* warning */
      if (is_nonnegative (optarg)) {
        warning_interval = strtod (optarg, NULL);
      }
      else {
        usage_va(_("Warning interval must be a positive integer - %s"), optarg);
      }
      break;
    case 'c':                 /* critical */
      if (is_nonnegative (optarg)) {
        critical_interval = strtod (optarg, NULL);
      }
      else {
        usage_va(_("Critical interval must be a positive integer - %s"), optarg);
      }
      break;
    case 't':                 /* timeout */
      if (is_intnonneg (optarg)) {
        timeout_interval = atoi (optarg);
      }
      else {
        usage_va(_("Timeout interval must be a positive integer - %s"), optarg);
      }
      break;
    case 'A':                 /* dig arguments */
      dig_args = strdup(optarg);
      break;
    case 'v':                 /* verbose */
      verbose = TRUE;
      break;
    case 'T':
      record_type = optarg;
      break;
    case 'a':
      expected_address = optarg;
      break;
    case '4':
      query_transport = "-4";
      break;
    case '6':
      query_transport = "-6";
      break;
    default:                  /* usage5 */
      usage5();
    }
  }

  c = optind;
  if (dns_server == NULL) {
    if (c < argc) {
      host_or_die(argv[c]);
      dns_server = argv[c];
    }
    else {
      dns_server = strdup ("127.0.0.1");
    }
  }

  return validate_arguments ();
}



int
validate_arguments (void)
{
  if (query_address != NULL)
    return OK;
  else
    return ERROR;
}



void
print_help (void)
{
  char *myport;

  xasprintf (&myport, "%d", DEFAULT_PORT);

  print_revision (progname, NP_VERSION);

  printf ("Copyright (c) 2000 Karl DeBisschop <kdebisschop@users.sourceforge.net>\n");
  printf (COPYRIGHT, copyright, email);

  printf (_("This plugin test the DNS service on the specified host using dig"));

  printf ("\n\n");

  print_usage ();

  printf (UT_HELP_VRSN);

  printf (UT_EXTRA_OPTS);

  printf (UT_HOST_PORT, 'p', myport);

  printf (" %s\n","-4, --use-ipv4");
  printf ("    %s\n",_("Force dig to only use IPv4 query transport"));
  printf (" %s\n","-6, --use-ipv6");
  printf ("    %s\n",_("Force dig to only use IPv6 query transport"));
  printf (" %s\n","-l, --query_address=STRING");
  printf ("    %s\n",_("Machine name to lookup"));
  printf (" %s\n","-T, --record_type=STRING");
  printf ("    %s\n",_("Record type to lookup (default: A)"));
  printf (" %s\n","-a, --expected_address=STRING");
  printf ("    %s\n",_("An address expected to be in the answer section. If not set, uses whatever"));
  printf ("    %s\n",_("was in -l"));
  printf (" %s\n","-A, --dig-arguments=STRING");
  printf ("    %s\n",_("Pass STRING as argument(s) to dig"));
  printf (UT_WARN_CRIT);
  printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);
  printf (UT_VERBOSE);

  printf ("\n");
  printf ("%s\n", _("Examples:"));
  printf (" %s\n", "check_dig -H DNSSERVER -l www.example.com -A \"+tcp\"");
  printf (" %s\n", "This will send a tcp query to DNSSERVER for www.example.com");

  printf (UT_SUPPORT);
}



void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
  printf ("%s -l <query_address> [-H <host>] [-p <server port>]\n", progname);
  printf (" [-T <query type>] [-w <warning interval>] [-c <critical interval>]\n");
  printf (" [-t <timeout>] [-a <expected answer address>] [-v]\n");
}
