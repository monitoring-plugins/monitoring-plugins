/*****************************************************************************
* 
* Nagios check_game plugin
* 
* License: GPL
* Copyright (c) 2002-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_game plugin
* 
* This plugin tests game server connections with the specified host.
* using the qstat program
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

const char *progname = "check_game";
const char *copyright = "2002-2007";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "utils.h"
#include "runcmd.h"

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

#define QSTAT_DATA_DELIMITER  ","

#define QSTAT_HOST_ERROR  "ERROR"
#define QSTAT_HOST_DOWN   "DOWN"
#define QSTAT_HOST_TIMEOUT  "TIMEOUT"
#define QSTAT_MAX_RETURN_ARGS 12

char *server_ip;
char *game_type;
int port = 0;

int verbose;

int qstat_game_players_max = -1;
int qstat_game_players = -1;
int qstat_game_field = -1;
int qstat_map_field = -1;
int qstat_ping_field = -1;


int
main (int argc, char **argv)
{
  char *command_line;
  int result = STATE_UNKNOWN;
  char *p, *ret[QSTAT_MAX_RETURN_ARGS];
  size_t i = 0;
  output chld_out;

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  /* Parse extra opts if any */
  argv=np_extra_opts (&argc, argv, progname);

  if (process_arguments (argc, argv) == ERROR)
    usage_va(_("Could not parse arguments"));

  result = STATE_OK;

  /* create the command line to execute */
  xasprintf (&command_line, "%s -raw %s -%s %s",
            PATH_TO_QSTAT, QSTAT_DATA_DELIMITER, game_type, server_ip);

  if (port)
    xasprintf (&command_line, "%s:%-d", command_line, port);

  if (verbose > 0)
    printf ("%s\n", command_line);

  /* run the command. historically, this plugin ignores output on stderr,
   * as well as return status of the qstat program */
  (void)np_runcmd(command_line, &chld_out, NULL, 0);

  /* sanity check */
  /* was thinking about running qstat without any options, capturing the
     -default line, parsing it & making an array of all know server types
     but thought this would be too much hassle considering this is a tool
     for intelligent sysadmins (ha). Could put a static array of known
     server types in a header file but then we'd be limiting ourselves

     In the end, I figured I'd simply let an error occur & then trap it
   */

  if (!strncmp (chld_out.line[0], "unknown option", 14)) {
    printf (_("CRITICAL - Host type parameter incorrect!\n"));
    result = STATE_CRITICAL;
    return result;
  }

  p = (char *) strtok (chld_out.line[0], QSTAT_DATA_DELIMITER);
  while (p != NULL) {
    ret[i] = p;
    p = (char *) strtok (NULL, QSTAT_DATA_DELIMITER);
    i++;
    if (i >= QSTAT_MAX_RETURN_ARGS)
      break;
  }

  if (strstr (ret[2], QSTAT_HOST_ERROR)) {
    printf (_("CRITICAL - Host not found\n"));
    result = STATE_CRITICAL;
  }
  else if (strstr (ret[2], QSTAT_HOST_DOWN)) {
    printf (_("CRITICAL - Game server down or unavailable\n"));
    result = STATE_CRITICAL;
  }
  else if (strstr (ret[2], QSTAT_HOST_TIMEOUT)) {
    printf (_("CRITICAL - Game server timeout\n"));
    result = STATE_CRITICAL;
  }
  else {
    printf ("OK: %s/%s %s (%s), Ping: %s ms|%s %s\n",
            ret[qstat_game_players],
            ret[qstat_game_players_max],
            ret[qstat_game_field],
            ret[qstat_map_field],
            ret[qstat_ping_field],
            perfdata ("players", atol(ret[qstat_game_players]), "",
                      FALSE, 0, FALSE, 0,
                      TRUE, 0, TRUE, atol(ret[qstat_game_players_max])),
            fperfdata ("ping", strtod(ret[qstat_ping_field], NULL), "",
                      FALSE, 0, FALSE, 0,
                      TRUE, 0, FALSE, 0));
  }

  return result;
}


int
process_arguments (int argc, char **argv)
{
  int c;

  int opt_index = 0;
  static struct option long_opts[] = {
    {"help", no_argument, 0, 'h'},
    {"version", no_argument, 0, 'V'},
    {"verbose", no_argument, 0, 'v'},
    {"timeout", required_argument, 0, 't'},
    {"hostname", required_argument, 0, 'H'},
    {"port", required_argument, 0, 'P'},
    {"game-type", required_argument, 0, 'G'},
    {"map-field", required_argument, 0, 'm'},
    {"ping-field", required_argument, 0, 'p'},
    {"game-field", required_argument, 0, 'g'},
    {"players-field", required_argument, 0, 129},
    {"max-players-field", required_argument, 0, 130},
    {0, 0, 0, 0}
  };

  if (argc < 2)
    return ERROR;

  for (c = 1; c < argc; c++) {
    if (strcmp ("-mf", argv[c]) == 0)
      strcpy (argv[c], "-m");
    else if (strcmp ("-pf", argv[c]) == 0)
      strcpy (argv[c], "-p");
    else if (strcmp ("-gf", argv[c]) == 0)
      strcpy (argv[c], "-g");
  }

  while (1) {
    c = getopt_long (argc, argv, "hVvt:H:P:G:g:p:m:", long_opts, &opt_index);

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
      if (strlen (optarg) >= MAX_HOST_ADDRESS_LENGTH)
        die (STATE_UNKNOWN, _("Input buffer overflow\n"));
      server_ip = optarg;
      break;
    case 'P': /* port */
      port = atoi (optarg);
      break;
    case 'G': /* hostname */
      if (strlen (optarg) >= MAX_INPUT_BUFFER)
        die (STATE_UNKNOWN, _("Input buffer overflow\n"));
      game_type = optarg;
      break;
    case 'p': /* index of ping field */
      qstat_ping_field = atoi (optarg);
      if (qstat_ping_field < 0 || qstat_ping_field > QSTAT_MAX_RETURN_ARGS)
        return ERROR;
      break;
    case 'm': /* index on map field */
      qstat_map_field = atoi (optarg);
      if (qstat_map_field < 0 || qstat_map_field > QSTAT_MAX_RETURN_ARGS)
        return ERROR;
      break;
    case 'g': /* index of game field */
      qstat_game_field = atoi (optarg);
      if (qstat_game_field < 0 || qstat_game_field > QSTAT_MAX_RETURN_ARGS)
        return ERROR;
      break;
    case 129: /* index of player count field */
      qstat_game_players = atoi (optarg);
      if (qstat_game_players_max == 0)
        qstat_game_players_max = qstat_game_players - 1;
      if (qstat_game_players < 0 || qstat_game_players > QSTAT_MAX_RETURN_ARGS)
        return ERROR;
      break;
    case 130: /* index of max players field */
      qstat_game_players_max = atoi (optarg);
      if (qstat_game_players_max < 0 || qstat_game_players_max > QSTAT_MAX_RETURN_ARGS)
        return ERROR;
      break;
    default: /* args not parsable */
      usage5();
    }
  }

  c = optind;
  /* first option is the game type */
  if (!game_type && c<argc)
    game_type = strdup (argv[c++]);

  /* Second option is the server name */
  if (!server_ip && c<argc)
    server_ip = strdup (argv[c++]);

  return validate_arguments ();
}


int
validate_arguments (void)
{
  if (qstat_game_players_max < 0)
    qstat_game_players_max = 4;

  if (qstat_game_players < 0)
    qstat_game_players = 5;

  if (qstat_game_field < 0)
    qstat_game_field = 2;

  if (qstat_map_field < 0)
    qstat_map_field = 3;

  if (qstat_ping_field < 0)
    qstat_ping_field = 5;

  return OK;
}


void
print_help (void)
{
  print_revision (progname, NP_VERSION);

  printf ("Copyright (c) 1999 Ian Cass, Knowledge Matters Limited\n");
  printf (COPYRIGHT, copyright, email);

  printf (_("This plugin tests game server connections with the specified host."));

  printf ("\n\n");

  print_usage ();

  printf (UT_HELP_VRSN);
  printf (UT_EXTRA_OPTS);

  printf (" %s\n", "-p");
  printf ("    %s\n", _("Optional port of which to connect"));
  printf (" %s\n", "gf");
  printf ("    %s\n", _("Field number in raw qstat output that contains game name"));
  printf (" %s\n", "-mf");
  printf ("    %s\n", _("Field number in raw qstat output that contains map name"));
  printf (" %s\n", "-pf");
  printf ("    %s\n", _("Field number in raw qstat output that contains ping time"));

  printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

  printf ("\n");
  printf ("%s\n", _("Notes:"));
  printf (" %s\n", _("This plugin uses the 'qstat' command, the popular game server status query tool."));
  printf (" %s\n", _("If you don't have the package installed, you will need to download it from"));
  printf (" %s\n", _("http://www.activesw.com/people/steve/qstat.html before you can use this plugin."));

  printf (UT_SUPPORT);
}



void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
  printf (" %s [-hvV] [-P port] [-t timeout] [-g game_field] [-m map_field] [-p ping_field] [-G game-time] [-H hostname] <game> <ip_address>\n", progname);
}

/******************************************************************************
 *
 * Test Cases:
 *
 * ./check_game --players 7 -p 8 --map 5 qs 67.20.190.61 26000
 *
 * qstat -raw , -qs 67.20.190.61
 *  ==> QS,67.20.190.61,Nightmare.fintek.ca,67.20.190.61:26000,3,e2m1,6,0,83,0
 *
 * qstat -qs 67.20.190.61
 *  ==> ADDRESS           PLAYERS      MAP   RESPONSE TIME    NAME
 *  ==> 67.20.190.61            0/ 6     e2m1     79 / 0   Nightmare.fintek.ca
 *
 ******************************************************************************/
