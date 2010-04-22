/*****************************************************************************
* 
* Nagios check_mrtg plugin
* 
* License: GPL
* Copyright (c) 1999-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_mrtg plugin
* 
* This plugin will check either the average or maximum value of one of the
* two variables recorded in an MRTG log file.
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

const char *progname = "check_mrtg";
const char *copyright = "1999-2007";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "utils.h"

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

char *log_file = NULL;
int expire_minutes = 0;
int use_average = TRUE;
int variable_number = -1;
unsigned long value_warning_threshold = 0L;
unsigned long value_critical_threshold = 0L;
char *label;
char *units;

int
main (int argc, char **argv)
{
	int result = STATE_OK;
	FILE *fp;
	int line;
	char input_buffer[MAX_INPUT_BUFFER];
	char *temp_buffer;
	time_t current_time;
	time_t timestamp = 0L;
	unsigned long average_value_rate = 0L;
	unsigned long maximum_value_rate = 0L;
	unsigned long rate = 0L;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments\n"));

	/* open the MRTG log file for reading */
	fp = fopen (log_file, "r");
	if (fp == NULL) {
		printf (_("Unable to open MRTG log file\n"));
		return STATE_UNKNOWN;
	}

	line = 0;
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, fp)) {

		line++;

		/* skip the first line of the log file */
		if (line == 1)
			continue;

		/* break out of read loop if we've passed the number of entries we want to read */
		if (line > 2)
			break;

		/* grab the timestamp */
		temp_buffer = strtok (input_buffer, " ");
		timestamp = strtoul (temp_buffer, NULL, 10);

		/* grab the average value 1 rate */
		temp_buffer = strtok (NULL, " ");
		if (variable_number == 1)
			average_value_rate = strtoul (temp_buffer, NULL, 10);

		/* grab the average value 2 rate */
		temp_buffer = strtok (NULL, " ");
		if (variable_number == 2)
			average_value_rate = strtoul (temp_buffer, NULL, 10);

		/* grab the maximum value 1 rate */
		temp_buffer = strtok (NULL, " ");
		if (variable_number == 1)
			maximum_value_rate = strtoul (temp_buffer, NULL, 10);

		/* grab the maximum value 2 rate */
		temp_buffer = strtok (NULL, " ");
		if (variable_number == 2)
			maximum_value_rate = strtoul (temp_buffer, NULL, 10);
	}

	/* close the log file */
	fclose (fp);

	/* if we couldn't read enough data, return an unknown error */
	if (line <= 2) {
		printf (_("Unable to process MRTG log file\n"));
		return STATE_UNKNOWN;
	}

	/* make sure the MRTG data isn't too old */
	time (&current_time);
	if (expire_minutes > 0
			&& (current_time - timestamp) > (expire_minutes * 60)) {
		printf (_("MRTG data has expired (%d minutes old)\n"),
		        (int) ((current_time - timestamp) / 60));
		return STATE_WARNING;
	}

	/* else check the incoming/outgoing rates */
	if (use_average == TRUE)
		rate = average_value_rate;
	else
		rate = maximum_value_rate;

	if (rate > value_critical_threshold)
		result = STATE_CRITICAL;
	else if (rate > value_warning_threshold)
		result = STATE_WARNING;

	printf("%s. %s = %lu %s|%s\n",
	       (use_average == TRUE) ? _("Avg") : _("Max"),
	       label, rate, units,
	       perfdata(label, (long) rate, units,
		        (int) value_warning_threshold, (long) value_warning_threshold,
		        (int) value_critical_threshold, (long) value_critical_threshold,
		        0, 0, 0, 0));

	return result;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option = 0;
	static struct option longopts[] = {
		{"logfile", required_argument, 0, 'F'},
		{"expires", required_argument, 0, 'e'},
		{"aggregation", required_argument, 0, 'a'},
		{"variable", required_argument, 0, 'v'},
		{"critical", required_argument, 0, 'c'},
		{"warning", required_argument, 0, 'w'},
		{"label", required_argument, 0, 'l'},
		{"units", required_argument, 0, 'u'},
		{"variable", required_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
		else if (strcmp ("-wt", argv[c]) == 0)
			strcpy (argv[c], "-w");
		else if (strcmp ("-ct", argv[c]) == 0)
			strcpy (argv[c], "-c");
	}

	while (1) {
		c = getopt_long (argc, argv, "hVF:e:a:v:c:w:l:u:", longopts,
									 &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'F':									/* input file */
			log_file = optarg;
			break;
		case 'e':									/* ups name */
			expire_minutes = atoi (optarg);
			break;
		case 'a':									/* port */
			if (!strcmp (optarg, "MAX"))
				use_average = FALSE;
			else
				use_average = TRUE;
			break;
		case 'v':
			variable_number = atoi (optarg);
			if (variable_number < 1 || variable_number > 2)
				usage4 (_("Invalid variable number"));
			break;
		case 'w':									/* critical time threshold */
			value_warning_threshold = strtoul (optarg, NULL, 10);
			break;
		case 'c':									/* warning time threshold */
			value_critical_threshold = strtoul (optarg, NULL, 10);
			break;
		case 'l':									/* label */
			label = optarg;
			break;
		case 'u':									/* timeout */
			units = optarg;
			break;
		case 'V':									/* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case '?':									/* help */
			usage5 ();
		}
	}

	c = optind;
	if (log_file == NULL && argc > c) {
		log_file = argv[c++];
	}

	if (expire_minutes <= 0 && argc > c) {
		if (is_intpos (argv[c]))
			expire_minutes = atoi (argv[c++]);
		else
			die (STATE_UNKNOWN,
			           _("%s is not a valid expiration time\nUse '%s -h' for additional help\n"),
			           argv[c], progname);
	}

	if (argc > c && strcmp (argv[c], "MAX") == 0) {
		use_average = FALSE;
		c++;
	}
	else if (argc > c && strcmp (argv[c], "AVG") == 0) {
		use_average = TRUE;
		c++;
	}

	if (argc > c && variable_number == -1) {
		variable_number = atoi (argv[c++]);
		if (variable_number < 1 || variable_number > 2) {
			printf ("%s :", argv[c]);
			usage (_("Invalid variable number\n"));
		}
	}

	if (argc > c && value_warning_threshold == 0) {
		value_warning_threshold = strtoul (argv[c++], NULL, 10);
	}

	if (argc > c && value_critical_threshold == 0) {
		value_critical_threshold = strtoul (argv[c++], NULL, 10);
	}

	if (argc > c && strlen (label) == 0) {
		label = argv[c++];
	}

	if (argc > c && strlen (units) == 0) {
		units = argv[c++];
	}

	return validate_arguments ();
}

int
validate_arguments (void)
{
	if (variable_number == -1)
		usage4 (_("You must supply the variable number"));

	if (label == NULL)
		label = strdup ("value");

	if (units == NULL)
		units = strdup ("");

	return OK;
}



void
print_help (void)
{
	print_revision (progname, NP_VERSION);

	printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("This plugin will check either the average or maximum value of one of the"));
  printf ("%s\n", _("two variables recorded in an MRTG log file."));

  printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (" %s\n", "-F, --logfile=FILE");
  printf ("   %s\n", _("The MRTG log file containing the data you want to monitor"));
  printf (" %s\n", "-e, --expires=MINUTES");
  printf ("   %s\n", _("Minutes before MRTG data is considered to be too old"));
  printf (" %s\n", "-a, --aggregation=AVG|MAX");
  printf ("   %s\n", _("Should we check average or maximum values?"));
  printf (" %s\n", "-v, --variable=INTEGER");
  printf ("   %s\n", _("Which variable set should we inspect? (1 or 2)"));
  printf (" %s\n", "-w, --warning=INTEGER");
  printf ("   %s\n", _("Threshold value for data to result in WARNING status"));
  printf (" %s\n", "-c, --critical=INTEGER");
  printf ("   %s\n", _("Threshold value for data to result in CRITICAL status"));
	printf (" %s\n", "-l, --label=STRING");
  printf ("   %s\n", _("Type label for data (Examples: Conns, \"Processor Load\", In, Out)"));
  printf (" %s\n", "-u, --units=STRING");
  printf ("   %s\n", _("Option units label for data (Example: Packets/Sec, Errors/Sec,"));
  printf ("   %s\n", _("\"Bytes Per Second\", \"%% Utilization\")"));

  printf ("\n");
	printf (" %s\n", _("If the value exceeds the <vwl> threshold, a WARNING status is returned. If"));
  printf (" %s\n", _("the value exceeds the <vcl> threshold, a CRITICAL status is returned.  If"));
  printf (" %s\n", _("the data in the log file is older than <expire_minutes> old, a WARNING"));
  printf (" %s\n", _("status is returned and a warning message is printed."));

  printf ("\n");
	printf (" %s\n", _("This plugin is useful for monitoring MRTG data that does not correspond to"));
  printf (" %s\n", _("bandwidth usage.  (Use the check_mrtgtraf plugin for monitoring bandwidth)."));
  printf (" %s\n", _("It can be used to monitor any kind of data that MRTG is monitoring - errors,"));
  printf (" %s\n", _("packets/sec, etc.  I use MRTG in conjuction with the Novell NLM that allows"));
  printf (" %s\n", _("me to track processor utilization, user connections, drive space, etc and"));
  printf (" %s\n\n", _("this plugin works well for monitoring that kind of data as well."));

	printf ("%s\n", _("Notes:"));
  printf (" %s\n", _("- This plugin only monitors one of the two variables stored in the MRTG log"));
  printf ("   %s\n", _("file.  If you want to monitor both values you will have to define two"));
  printf ("   %s\n", _("commands with different values for the <variable> argument.  Of course,"));
  printf ("   %s\n", _("you can always hack the code to make this plugin work for you..."));
  printf (" %s\n", _("- MRTG stands for the Multi Router Traffic Grapher.  It can be downloaded from"));
  printf ("   %s\n", "http://ee-staff.ethz.ch/~oetiker/webtools/mrtg/mrtg.html");

	printf (UT_SUPPORT);
}



/* original command line:
	 <log_file> <expire_minutes> <AVG|MAX> <variable> <vwl> <vcl> <label> [units] */

void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
	printf ("%s -F log_file -a <AVG | MAX> -v variable -w warning -c critical\n",progname);
  printf ("[-l label] [-u units] [-e expire_minutes] [-t timeout] [-v]\n");
}
