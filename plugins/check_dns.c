/*****************************************************************************
* 
* Nagios check_dns plugin
* 
* License: GPL
* Copyright (c) 2000-2008 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_dns plugin
* 
* LIMITATION: nslookup on Solaris 7 can return output over 2 lines, which
* will not be picked up by this plugin
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

const char *progname = "check_dns";
const char *copyright = "2000-2008";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "utils.h"
#include "utils_base.h"
#include "netutils.h"
#include "runcmd.h"

int process_arguments (int, char **);
int validate_arguments (void);
int error_scan (char *);
void print_help (void);
void print_usage (void);

#define ADDRESS_LENGTH 256
char query_address[ADDRESS_LENGTH] = "";
char dns_server[ADDRESS_LENGTH] = "";
char ptr_server[ADDRESS_LENGTH] = "";
int verbose = FALSE;
char **expected_address = NULL;
int expected_address_cnt = 0;

int expect_authority = FALSE;
thresholds *time_thresholds = NULL;

static int
qstrcmp(const void *p1, const void *p2)
{
	/* The actual arguments to this function are "pointers to
	   pointers to char", but strcmp() arguments are "pointers
	   to char", hence the following cast plus dereference */
	return strcmp(* (char * const *) p1, * (char * const *) p2);
}


int
main (int argc, char **argv)
{
  char *command_line = NULL;
  char input_buffer[MAX_INPUT_BUFFER];
  char *address = NULL; /* comma seperated str with addrs/ptrs (sorted) */
  char **addresses = NULL;
  int n_addresses = 0;
  char *msg = NULL;
  char *temp_buffer = NULL;
  int non_authoritative = FALSE;
  int result = STATE_UNKNOWN;
  double elapsed_time;
  long microsec;
  struct timeval tv;
  int multi_address;
  int parse_address = FALSE; /* This flag scans for Address: but only after Name: */
  output chld_out, chld_err;
  size_t i;

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  /* Set signal handling and alarm */
  if (signal (SIGALRM, runcmd_timeout_alarm_handler) == SIG_ERR) {
    usage_va(_("Cannot catch SIGALRM"));
  }

  /* Parse extra opts if any */
  argv=np_extra_opts (&argc, argv, progname);

  if (process_arguments (argc, argv) == ERROR) {
    usage_va(_("Could not parse arguments"));
  }

  /* get the command to run */
  xasprintf (&command_line, "%s %s %s", NSLOOKUP_COMMAND, query_address, dns_server);

  alarm (timeout_interval);
  gettimeofday (&tv, NULL);

  if (verbose)
    printf ("%s\n", command_line);

  /* run the command */
  if((np_runcmd(command_line, &chld_out, &chld_err, 0)) != 0) {
    msg = (char *)_("nslookup returned an error status");
    result = STATE_WARNING;
  }

  /* scan stdout */
  for(i = 0; i < chld_out.lines; i++) {
    if (addresses == NULL)
      addresses = malloc(sizeof(*addresses)*10);
    else if (!(n_addresses % 10))
      addresses = realloc(addresses,sizeof(*addresses) * (n_addresses + 10));

    if (verbose)
      puts(chld_out.line[i]);

    if (strstr (chld_out.line[i], ".in-addr.arpa")) {
      if ((temp_buffer = strstr (chld_out.line[i], "name = ")))
        addresses[n_addresses++] = strdup (temp_buffer + 7);
      else {
        msg = (char *)_("Warning plugin error");
        result = STATE_WARNING;
      }
    }

    /* the server is responding, we just got the host name... */
    if (strstr (chld_out.line[i], "Name:"))
      parse_address = TRUE;
    else if (parse_address == TRUE && (strstr (chld_out.line[i], "Address:") ||
             strstr (chld_out.line[i], "Addresses:"))) {
      temp_buffer = index (chld_out.line[i], ':');
      temp_buffer++;

      /* Strip leading spaces */
      for (; *temp_buffer != '\0' && *temp_buffer == ' '; temp_buffer++)
        /* NOOP */;

      strip(temp_buffer);
      if (temp_buffer==NULL || strlen(temp_buffer)==0) {
        die (STATE_CRITICAL,
             _("DNS CRITICAL - '%s' returned empty host name string\n"),
             NSLOOKUP_COMMAND);
      }

      addresses[n_addresses++] = strdup(temp_buffer);
    }
    else if (strstr (chld_out.line[i], _("Non-authoritative answer:"))) {
      non_authoritative = TRUE;
    }


    result = error_scan (chld_out.line[i]);
    if (result != STATE_OK) {
      msg = strchr (chld_out.line[i], ':');
      if(msg) msg++;
      break;
    }
  }

  /* scan stderr */
  for(i = 0; i < chld_err.lines; i++) {
    if (verbose)
      puts(chld_err.line[i]);

    if (error_scan (chld_err.line[i]) != STATE_OK) {
      result = max_state (result, error_scan (chld_err.line[i]));
      msg = strchr(input_buffer, ':');
      if(msg) msg++;
    }
  }

  if (addresses) {
    int i,slen;
    char *adrp;
    qsort(addresses, n_addresses, sizeof(*addresses), qstrcmp);
    for(i=0, slen=1; i < n_addresses; i++) {
      slen += strlen(addresses[i])+1;
    }
    adrp = address = malloc(slen);
    for(i=0; i < n_addresses; i++) {
      if (i) *adrp++ = ',';
      strcpy(adrp, addresses[i]);
      adrp += strlen(addresses[i]);
    }
    *adrp = 0;
  } else
    die (STATE_CRITICAL,
         _("DNS CRITICAL - '%s' msg parsing exited with no address\n"),
         NSLOOKUP_COMMAND);

  /* compare to expected address */
  if (result == STATE_OK && expected_address_cnt > 0) {
    result = STATE_CRITICAL;
    temp_buffer = "";
    for (i=0; i<expected_address_cnt; i++) {
      /* check if we get a match and prepare an error string */
      if (strcmp(address, expected_address[i]) == 0) result = STATE_OK;
      xasprintf(&temp_buffer, "%s%s; ", temp_buffer, expected_address[i]);
    }
    if (result == STATE_CRITICAL) {
      /* Strip off last semicolon... */
      temp_buffer[strlen(temp_buffer)-2] = '\0';
      xasprintf(&msg, _("expected '%s' but got '%s'"), temp_buffer, address);
    }
  }

  /* check if authoritative */
  if (result == STATE_OK && expect_authority && non_authoritative) {
    result = STATE_CRITICAL;
    xasprintf(&msg, _("server %s is not authoritative for %s"), dns_server, query_address);
  }

  microsec = deltime (tv);
  elapsed_time = (double)microsec / 1.0e6;

  if (result == STATE_OK) {
    if (strchr (address, ',') == NULL)
      multi_address = FALSE;
    else
      multi_address = TRUE;

    result = get_status(elapsed_time, time_thresholds);
    if (result == STATE_OK) {
      printf ("DNS %s: ", _("OK"));
    } else if (result == STATE_WARNING) {
      printf ("DNS %s: ", _("WARNING"));
    } else if (result == STATE_CRITICAL) {
      printf ("DNS %s: ", _("CRITICAL"));
    }
    printf (ngettext("%.3f second response time", "%.3f seconds response time", elapsed_time), elapsed_time);
    printf (_(". %s returns %s"), query_address, address);
    printf ("|%s\n", fperfdata ("time", elapsed_time, "s", FALSE, 0, FALSE, 0, TRUE, 0, FALSE, 0));
  }
  else if (result == STATE_WARNING)
    printf (_("DNS WARNING - %s\n"),
            !strcmp (msg, "") ? _(" Probably a non-existent host/domain") : msg);
  else if (result == STATE_CRITICAL)
    printf (_("DNS CRITICAL - %s\n"),
            !strcmp (msg, "") ? _(" Probably a non-existent host/domain") : msg);
  else
    printf (_("DNS UNKNOWN - %s\n"),
            !strcmp (msg, "") ? _(" Probably a non-existent host/domain") : msg);

  return result;
}



int
error_scan (char *input_buffer)
{

  /* the DNS lookup timed out */
  if (strstr (input_buffer, _("Note: nslookup is deprecated and may be removed from future releases.")) ||
      strstr (input_buffer, _("Consider using the `dig' or `host' programs instead.  Run nslookup with")) ||
      strstr (input_buffer, _("the `-sil[ent]' option to prevent this message from appearing.")))
    return STATE_OK;

  /* DNS server is not running... */
  else if (strstr (input_buffer, "No response from server"))
    die (STATE_CRITICAL, _("No response from DNS %s\n"), dns_server);

  /* Host name is valid, but server doesn't have records... */
  else if (strstr (input_buffer, "No records"))
    die (STATE_CRITICAL, _("DNS %s has no records\n"), dns_server);

  /* Connection was refused */
  else if (strstr (input_buffer, "Connection refused") ||
     strstr (input_buffer, "Couldn't find server") ||
           strstr (input_buffer, "Refused") ||
           (strstr (input_buffer, "** server can't find") &&
            strstr (input_buffer, ": REFUSED")))
    die (STATE_CRITICAL, _("Connection to DNS %s was refused\n"), dns_server);

  /* Query refused (usually by an ACL in the namserver) */
  else if (strstr (input_buffer, "Query refused"))
    die (STATE_CRITICAL, _("Query was refused by DNS server at %s\n"), dns_server);

  /* No information (e.g. nameserver IP has two PTR records) */
  else if (strstr (input_buffer, "No information"))
    die (STATE_CRITICAL, _("No information returned by DNS server at %s\n"), dns_server);

  /* Host or domain name does not exist */
  else if (strstr (input_buffer, "Non-existent") ||
           strstr (input_buffer, "** server can't find") ||
     strstr (input_buffer,"NXDOMAIN"))
    die (STATE_CRITICAL, _("Domain %s was not found by the server\n"), query_address);

  /* Network is unreachable */
  else if (strstr (input_buffer, "Network is unreachable"))
    die (STATE_CRITICAL, _("Network is unreachable\n"));

  /* Internal server failure */
  else if (strstr (input_buffer, "Server failure"))
    die (STATE_CRITICAL, _("DNS failure for %s\n"), dns_server);

  /* Request error or the DNS lookup timed out */
  else if (strstr (input_buffer, "Format error") ||
           strstr (input_buffer, "Timed out"))
    return STATE_WARNING;

  return STATE_OK;

}


/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
  int c;
  char *warning = NULL;
  char *critical = NULL;

  int opt_index = 0;
  static struct option long_opts[] = {
    {"help", no_argument, 0, 'h'},
    {"version", no_argument, 0, 'V'},
    {"verbose", no_argument, 0, 'v'},
    {"timeout", required_argument, 0, 't'},
    {"hostname", required_argument, 0, 'H'},
    {"server", required_argument, 0, 's'},
    {"reverse-server", required_argument, 0, 'r'},
    {"expected-address", required_argument, 0, 'a'},
    {"expect-authority", no_argument, 0, 'A'},
    {"warning", required_argument, 0, 'w'},
    {"critical", required_argument, 0, 'c'},
    {0, 0, 0, 0}
  };

  if (argc < 2)
    return ERROR;

  for (c = 1; c < argc; c++)
    if (strcmp ("-to", argv[c]) == 0)
      strcpy (argv[c], "-t");

  while (1) {
    c = getopt_long (argc, argv, "hVvAt:H:s:r:a:w:c:", long_opts, &opt_index);

    if (c == -1 || c == EOF)
      break;

    switch (c) {
    case 'h': /* help */
      print_help ();
      exit (STATE_OK);
    case 'V': /* version */
      print_revision (progname, NP_VERSION);
      exit (STATE_OK);
    case 'v': /* version */
      verbose = TRUE;
      break;
    case 't': /* timeout period */
      timeout_interval = atoi (optarg);
      break;
    case 'H': /* hostname */
      if (strlen (optarg) >= ADDRESS_LENGTH)
        die (STATE_UNKNOWN, _("Input buffer overflow\n"));
      strcpy (query_address, optarg);
      break;
    case 's': /* server name */
      /* TODO: this host_or_die check is probably unnecessary.
       * Better to confirm nslookup response matches */
      host_or_die(optarg);
      if (strlen (optarg) >= ADDRESS_LENGTH)
        die (STATE_UNKNOWN, _("Input buffer overflow\n"));
      strcpy (dns_server, optarg);
      break;
    case 'r': /* reverse server name */
      /* TODO: Is this host_or_die necessary? */
      host_or_die(optarg);
      if (strlen (optarg) >= ADDRESS_LENGTH)
        die (STATE_UNKNOWN, _("Input buffer overflow\n"));
      strcpy (ptr_server, optarg);
      break;
    case 'a': /* expected address */
      if (strlen (optarg) >= ADDRESS_LENGTH)
        die (STATE_UNKNOWN, _("Input buffer overflow\n"));
      expected_address = (char **)realloc(expected_address, (expected_address_cnt+1) * sizeof(char**));
      expected_address[expected_address_cnt] = strdup(optarg);
      expected_address_cnt++;
      break;
    case 'A': /* expect authority */
      expect_authority = TRUE;
      break;
    case 'w':
      warning = optarg;
      break;
    case 'c':
      critical = optarg;
      break;
    default: /* args not parsable */
      usage5();
    }
  }

  c = optind;
  if (strlen(query_address)==0 && c<argc) {
    if (strlen(argv[c])>=ADDRESS_LENGTH)
      die (STATE_UNKNOWN, _("Input buffer overflow\n"));
    strcpy (query_address, argv[c++]);
  }

  if (strlen(dns_server)==0 && c<argc) {
    /* TODO: See -s option */
    host_or_die(argv[c]);
    if (strlen(argv[c]) >= ADDRESS_LENGTH)
      die (STATE_UNKNOWN, _("Input buffer overflow\n"));
    strcpy (dns_server, argv[c++]);
  }

  set_thresholds(&time_thresholds, warning, critical);

  return validate_arguments ();
}


int
validate_arguments ()
{
  if (query_address[0] == 0)
    return ERROR;

  return OK;
}


void
print_help (void)
{
  print_revision (progname, NP_VERSION);

  printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
  printf (COPYRIGHT, copyright, email);

  printf ("%s\n", _("This plugin uses the nslookup program to obtain the IP address for the given host/domain query."));
  printf ("%s\n", _("An optional DNS server to use may be specified."));
  printf ("%s\n", _("If no DNS server is specified, the default server(s) specified in /etc/resolv.conf will be used."));

  printf ("\n\n");

  print_usage ();

  printf (UT_HELP_VRSN);
  printf (UT_EXTRA_OPTS);

  printf (" -H, --hostname=HOST\n");
  printf ("    %s\n", _("The name or address you want to query"));
  printf (" -s, --server=HOST\n");
  printf ("    %s\n", _("Optional DNS server you want to use for the lookup"));
  printf (" -a, --expected-address=IP-ADDRESS|HOST\n");
  printf ("    %s\n", _("Optional IP-ADDRESS you expect the DNS server to return. HOST must end with"));
  printf ("    %s\n", _("a dot (.). This option can be repeated multiple times (Returns OK if any"));
  printf ("    %s\n", _("value match). If multiple addresses are returned at once, you have to match"));
  printf ("    %s\n", _("the whole string of addresses separated with commas (sorted alphabetically)."));
  printf (" -A, --expect-authority\n");
  printf ("    %s\n", _("Optionally expect the DNS server to be authoritative for the lookup"));
  printf (" -w, --warning=seconds\n");
  printf ("    %s\n", _("Return warning if elapsed time exceeds value. Default off"));
  printf (" -c, --critical=seconds\n");
  printf ("    %s\n", _("Return critical if elapsed time exceeds value. Default off"));

  printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

  printf (UT_SUPPORT);
}


void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
  printf ("%s -H host [-s server] [-a expected-address] [-A] [-t timeout] [-w warn] [-c crit]\n", progname);
}
