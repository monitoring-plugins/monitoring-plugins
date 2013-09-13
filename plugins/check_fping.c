/*****************************************************************************
* 
* Nagios check_fping plugin
* 
* License: GPL
* Copyright (c) 2000-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_disk plugin
* 
* This plugin will use the fping command to ping the specified host for a
* fast check
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

const char *progname = "check_fping";
const char *copyright = "2000-2007";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "popen.h"
#include "netutils.h"
#include "utils.h"

enum {
  PACKET_COUNT = 1,
  PACKET_SIZE = 56,
  PL = 0,
  RTA = 1
};

int textscan (char *buf);
int process_arguments (int, char **);
int get_threshold (char *arg, char *rv[2]);
void print_help (void);
void print_usage (void);

char *server_name = NULL;
char *sourceip = NULL;
char *sourceif = NULL;
int packet_size = PACKET_SIZE;
int packet_count = PACKET_COUNT;
int target_timeout = 0;
int packet_interval = 0;
int verbose = FALSE;
int cpl;
int wpl;
double crta;
double wrta;
int cpl_p = FALSE;
int wpl_p = FALSE;
int crta_p = FALSE;
int wrta_p = FALSE;

int
main (int argc, char **argv)
{
/* normaly should be  int result = STATE_UNKNOWN; */

  int status = STATE_UNKNOWN;
  int result = 0;
  char *fping_prog = NULL;
  char *server = NULL;
  char *command_line = NULL;
  char *input_buffer = NULL;
  char *option_string = "";
  input_buffer = malloc (MAX_INPUT_BUFFER);

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  /* Parse extra opts if any */
  argv=np_extra_opts (&argc, argv, progname);

  if (process_arguments (argc, argv) == ERROR)
    usage4 (_("Could not parse arguments"));

  server = strscpy (server, server_name);

  /* compose the command */
  if (target_timeout)
    xasprintf(&option_string, "%s-t %d ", option_string, target_timeout);
  if (packet_interval)
    xasprintf(&option_string, "%s-p %d ", option_string, packet_interval);
  if (sourceip)
    xasprintf(&option_string, "%s-S %s ", option_string, sourceip);
  if (sourceif)
    xasprintf(&option_string, "%s-I %s ", option_string, sourceif);

#ifdef PATH_TO_FPING6
  if (address_family == AF_INET6)
    fping_prog = strdup(PATH_TO_FPING6);
  else
    fping_prog = strdup(PATH_TO_FPING);
#else
  fping_prog = strdup(PATH_TO_FPING);
#endif

  xasprintf (&command_line, "%s %s-b %d -c %d %s", fping_prog,
            option_string, packet_size, packet_count, server);

  if (verbose)
    printf ("%s\n", command_line);

  /* run the command */
  child_process = spopen (command_line);
  if (child_process == NULL) {
    printf (_("Could not open pipe: %s\n"), command_line);
    return STATE_UNKNOWN;
  }

  child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
  if (child_stderr == NULL) {
    printf (_("Could not open stderr for %s\n"), command_line);
  }

  while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {
    if (verbose)
      printf ("%s", input_buffer);
    status = max_state (status, textscan (input_buffer));
  }

  /* If we get anything on STDERR, at least set warning */
  while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr)) {
    status = max_state (status, STATE_WARNING);
    if (verbose)
      printf ("%s", input_buffer);
    status = max_state (status, textscan (input_buffer));
  }
  (void) fclose (child_stderr);

  /* close the pipe */
  if (result = spclose (child_process))
    /* need to use max_state not max */
    status = max_state (status, STATE_WARNING);

  if (result > 1 ) {
    status = max_state (status, STATE_UNKNOWN);
    if (result == 2) {
      die (STATE_UNKNOWN, _("FPING UNKNOWN - IP address not found\n"));
    }
    if (result == 3) {
      die (STATE_UNKNOWN, _("FPING UNKNOWN - invalid commandline argument\n"));
    }
    if (result == 4) {
      die (STATE_UNKNOWN, _("FPING UNKNOWN - failed system call\n"));
    }

  }

  printf ("FPING %s - %s\n", state_text (status), server_name);

  return status;
}



int
textscan (char *buf)
{
  char *rtastr = NULL;
  char *losstr = NULL;
  char *xmtstr = NULL;
  double loss;
  double rta;
  double xmt;
  int status = STATE_UNKNOWN;

  if (strstr (buf, "not found")) {
    die (STATE_CRITICAL, _("FPING UNKNOW - %s not found\n"), server_name);

  }
  else if (strstr (buf, "is unreachable") || strstr (buf, "Unreachable")) {
    die (STATE_CRITICAL, _("FPING CRITICAL - %s is unreachable\n"),
               "host");

  }
  else if (strstr (buf, "Operation not permitted") || strstr (buf, "No such device") ) {
    die (STATE_UNKNOWN, _("FPING UNKNOWN - %s parameter error\n"),
               "host");
  }
  else if (strstr (buf, "is down")) {
    die (STATE_CRITICAL, _("FPING CRITICAL - %s is down\n"), server_name);

  }
  else if (strstr (buf, "is alive")) {
    status = STATE_OK;

  }
  else if (strstr (buf, "xmt/rcv/%loss") && strstr (buf, "min/avg/max")) {
    losstr = strstr (buf, "=");
    losstr = 1 + strstr (losstr, "/");
    losstr = 1 + strstr (losstr, "/");
    rtastr = strstr (buf, "min/avg/max");
    rtastr = strstr (rtastr, "=");
    rtastr = 1 + index (rtastr, '/');
    loss = strtod (losstr, NULL);
    rta = strtod (rtastr, NULL);
    if (cpl_p == TRUE && loss > cpl)
      status = STATE_CRITICAL;
    else if (crta_p == TRUE  && rta > crta)
      status = STATE_CRITICAL;
    else if (wpl_p == TRUE && loss > wpl)
      status = STATE_WARNING;
    else if (wrta_p == TRUE && rta > wrta)
      status = STATE_WARNING;
    else
      status = STATE_OK;
    die (status,
          _("FPING %s - %s (loss=%.0f%%, rta=%f ms)|%s %s\n"),
         state_text (status), server_name, loss, rta,
         perfdata ("loss", (long int)loss, "%", wpl_p, wpl, cpl_p, cpl, TRUE, 0, TRUE, 100),
         fperfdata ("rta", rta/1.0e3, "s", wrta_p, wrta/1.0e3, crta_p, crta/1.0e3, TRUE, 0, FALSE, 0));

  }
  else if(strstr (buf, "xmt/rcv/%loss") ) {
    /* no min/max/avg if host was unreachable in fping v2.2.b1 */
    /* in v2.4b2: 10.99.0.1 : xmt/rcv/%loss = 0/0/0% */
    losstr = strstr (buf, "=");
    xmtstr = 1 + losstr;
    xmt = strtod (xmtstr, NULL);
    if(xmt == 0)
        die (STATE_CRITICAL, _("FPING CRITICAL - %s is down\n"), server_name);
    losstr = 1 + strstr (losstr, "/");
    losstr = 1 + strstr (losstr, "/");
    loss = strtod (losstr, NULL);
    if (atoi(losstr) == 100)
      status = STATE_CRITICAL;
    else if (cpl_p == TRUE && loss > cpl)
      status = STATE_CRITICAL;
    else if (wpl_p == TRUE && loss > wpl)
      status = STATE_WARNING;
    else
      status = STATE_OK;
    /* loss=%.0f%%;%d;%d;0;100 */
    die (status, _("FPING %s - %s (loss=%.0f%% )|%s\n"),
         state_text (status), server_name, loss ,
         perfdata ("loss", (long int)loss, "%", wpl_p, wpl, cpl_p, cpl, TRUE, 0, TRUE, 100));

  }
  else {
    status = max_state (status, STATE_WARNING);
  }

  return status;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
  int c;
  char *rv[2];

  int option = 0;
  static struct option longopts[] = {
    {"hostname", required_argument, 0, 'H'},
    {"sourceip", required_argument, 0, 'S'},
    {"sourceif", required_argument, 0, 'I'},
    {"critical", required_argument, 0, 'c'},
    {"warning", required_argument, 0, 'w'},
    {"bytes", required_argument, 0, 'b'},
    {"number", required_argument, 0, 'n'},
    {"target-timeout", required_argument, 0, 'T'},
    {"interval", required_argument, 0, 'i'},
    {"verbose", no_argument, 0, 'v'},
    {"version", no_argument, 0, 'V'},
    {"help", no_argument, 0, 'h'},
    {"use-ipv4", no_argument, 0, '4'},
    {"use-ipv6", no_argument, 0, '6'},
    {0, 0, 0, 0}
  };

  rv[PL] = NULL;
  rv[RTA] = NULL;

  if (argc < 2)
    return ERROR;

  if (!is_option (argv[1])) {
    server_name = argv[1];
    argv[1] = argv[0];
    argv = &argv[1];
    argc--;
  }

  while (1) {
    c = getopt_long (argc, argv, "+hVvH:S:c:w:b:n:T:i:I:46", longopts, &option);

    if (c == -1 || c == EOF || c == 1)
      break;

    switch (c) {
    case '?':                 /* print short usage statement if args not parsable */
      usage5 ();
    case 'h':                 /* help */
      print_help ();
      exit (STATE_OK);
    case 'V':                 /* version */
      print_revision (progname, NP_VERSION);
      exit (STATE_OK);
    case 'v':                 /* verbose mode */
      verbose = TRUE;
      break;
    case 'H':                 /* hostname */
      if (is_host (optarg) == FALSE) {
        usage2 (_("Invalid hostname/address"), optarg);
      }
      server_name = strscpy (server_name, optarg);
      break;
    case 'S':                 /* sourceip */
      if (is_host (optarg) == FALSE) {
        usage2 (_("Invalid hostname/address"), optarg);
      }
      sourceip = strscpy (sourceip, optarg);
      break;
    case 'I':                 /* sourceip */
      sourceif = strscpy (sourceif, optarg);
    case '4':                 /* IPv4 only */
      address_family = AF_INET;
      break;
    case '6':                 /* IPv6 only */
#ifdef USE_IPV6
      address_family = AF_INET6;
#else
      usage (_("IPv6 support not available\n"));
#endif
      break;
    case 'c':
      get_threshold (optarg, rv);
      if (rv[RTA]) {
        crta = strtod (rv[RTA], NULL);
        crta_p = TRUE;
        rv[RTA] = NULL;
      }
      if (rv[PL]) {
        cpl = atoi (rv[PL]);
        cpl_p = TRUE;
        rv[PL] = NULL;
      }
      break;
    case 'w':
      get_threshold (optarg, rv);
      if (rv[RTA]) {
        wrta = strtod (rv[RTA], NULL);
        wrta_p = TRUE;
        rv[RTA] = NULL;
      }
      if (rv[PL]) {
        wpl = atoi (rv[PL]);
        wpl_p = TRUE;
        rv[PL] = NULL;
      }
      break;
    case 'b':                 /* bytes per packet */
      if (is_intpos (optarg))
        packet_size = atoi (optarg);
      else
        usage (_("Packet size must be a positive integer"));
      break;
    case 'n':                 /* number of packets */
      if (is_intpos (optarg))
        packet_count = atoi (optarg);
      else
        usage (_("Packet count must be a positive integer"));
      break;
    case 'T':                 /* timeout in msec */
      if (is_intpos (optarg))
        target_timeout = atoi (optarg);
      else
        usage (_("Target timeout must be a positive integer"));
      break;
    case 'i':                 /* interval in msec */
      if (is_intpos (optarg))
        packet_interval = atoi (optarg);
      else
        usage (_("Interval must be a positive integer"));
      break;
    }
  }

  if (server_name == NULL)
    usage4 (_("Hostname was not supplied"));

  return OK;
}


int
get_threshold (char *arg, char *rv[2])
{
  char *arg1 = NULL;
  char *arg2 = NULL;

  arg1 = strscpy (arg1, arg);
  if (strpbrk (arg1, ",:"))
    arg2 = 1 + strpbrk (arg1, ",:");

  if (arg2) {
    arg1[strcspn (arg1, ",:")] = 0;
    if (strstr (arg1, "%") && strstr (arg2, "%"))
      die (STATE_UNKNOWN,
                 _("%s: Only one threshold may be packet loss (%s)\n"), progname,
                 arg);
    if (!strstr (arg1, "%") && !strstr (arg2, "%"))
      die (STATE_UNKNOWN,
                 _("%s: Only one threshold must be packet loss (%s)\n"),
                 progname, arg);
  }

  if (arg2 && strstr (arg2, "%")) {
    rv[PL] = arg2;
    rv[RTA] = arg1;
  }
  else if (arg2) {
    rv[PL] = arg1;
    rv[RTA] = arg2;
  }
  else if (strstr (arg1, "%")) {
    rv[PL] = arg1;
  }
  else {
    rv[RTA] = arg1;
  }

  return OK;
}


void
print_help (void)
{

  print_revision (progname, NP_VERSION);

  printf ("Copyright (c) 1999 Didi Rieder <adrieder@sbox.tu-graz.ac.at>\n");
  printf (COPYRIGHT, copyright, email);

  printf ("%s\n", _("This plugin will use the fping command to ping the specified host for a fast check"));

  printf ("%s\n", _("Note that it is necessary to set the suid flag on fping."));

  printf ("\n\n");

  print_usage ();

  printf (UT_HELP_VRSN);
  printf (UT_EXTRA_OPTS);

  printf (UT_IPv46);

  printf (" %s\n", "-H, --hostname=HOST");
  printf ("    %s\n", _("name or IP Address of host to ping (IP Address bypasses name lookup, reducing system load)"));
  printf (" %s\n", "-w, --warning=THRESHOLD");
  printf ("    %s\n", _("warning threshold pair"));
  printf (" %s\n", "-c, --critical=THRESHOLD");
  printf ("    %s\n", _("critical threshold pair"));
  printf (" %s\n", "-b, --bytes=INTEGER");
  printf ("    %s (default: %d)\n", _("size of ICMP packet"),PACKET_SIZE);
  printf (" %s\n", "-n, --number=INTEGER");
  printf ("    %s (default: %d)\n", _("number of ICMP packets to send"),PACKET_COUNT);
  printf (" %s\n", "-T, --target-timeout=INTEGER");
  printf ("    %s (default: fping's default for -t)\n", _("Target timeout (ms)"));
  printf (" %s\n", "-i, --interval=INTEGER");
  printf ("    %s (default: fping's default for -p)\n", _("Interval (ms) between sending packets"));
  printf (" %s\n", "-S, --sourceip=HOST");
  printf ("    %s\n", _("name or IP Address of sourceip"));
  printf (" %s\n", "-I, --sourceif=IF");
  printf ("    %s\n", _("source interface name"));
  printf (UT_VERBOSE);
  printf ("\n");
  printf (" %s\n", _("THRESHOLD is <rta>,<pl>%% where <rta> is the round trip average travel time (ms)"));
  printf (" %s\n", _("which triggers a WARNING or CRITICAL state, and <pl> is the percentage of"));
  printf (" %s\n", _("packet loss to trigger an alarm state."));

  printf ("\n");
  printf (" %s\n", _("IPv4 is used by default. Specify -6 to use IPv6."));

  printf (UT_SUPPORT);
}


void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
  printf (" %s <host_address> -w limit -c limit [-b size] [-n number] [-T number] [-i number]\n", progname);
}
