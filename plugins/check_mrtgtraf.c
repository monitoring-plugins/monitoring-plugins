/******************************************************************************

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

******************************************************************************/

#include "common.h"
#include "utils.h"

const char *progname = "check_mrtgtraf";
const char *revision = "$Revision$";
const char *copyright = "1999-2003";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

int process_arguments (int, char **);
int validate_arguments (void);
void print_help(void);
void print_usage(void);

char *log_file = NULL;
int expire_minutes = -1;
int use_average = TRUE;
unsigned long incoming_warning_threshold = 0L;
unsigned long incoming_critical_threshold = 0L;
unsigned long outgoing_warning_threshold = 0L;
unsigned long outgoing_critical_threshold = 0L;





int
main (int argc, char **argv)
{
	int result = STATE_OK;
	FILE *fp;
	int line;
	char input_buffer[MAX_INPUT_BUFFER];
	char *temp_buffer;
	time_t current_time;
	char *error_message;
	time_t timestamp = 0L;
	unsigned long average_incoming_rate = 0L;
	unsigned long average_outgoing_rate = 0L;
	unsigned long maximum_incoming_rate = 0L;
	unsigned long maximum_outgoing_rate = 0L;
	unsigned long incoming_rate = 0L;
	unsigned long outgoing_rate = 0L;
	double adjusted_incoming_rate = 0.0;
	double adjusted_outgoing_rate = 0.0;
	char incoming_speed_rating[8];
	char outgoing_speed_rating[8];

	if (process_arguments (argc, argv) != OK)
		usage (_("Incorrect arguments supplied\n"));

	/* open the MRTG log file for reading */
	fp = fopen (log_file, "r");
	if (fp == NULL)
		usage (_("Unable to open MRTG log file\n"));

	line = 0;
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, fp)) {

		line++;

		/* skip the first line of the log file */
		if (line == 1)
			continue;

		/* break out of read loop */
		/* if we've passed the number of entries we want to read */
		if (line > 2)
			break;

		/* grab the timestamp */
		temp_buffer = strtok (input_buffer, " ");
		timestamp = strtoul (temp_buffer, NULL, 10);

		/* grab the average incoming transfer rate */
		temp_buffer = strtok (NULL, " ");
		average_incoming_rate = strtoul (temp_buffer, NULL, 10);

		/* grab the average outgoing transfer rate */
		temp_buffer = strtok (NULL, " ");
		average_outgoing_rate = strtoul (temp_buffer, NULL, 10);

		/* grab the maximum incoming transfer rate */
		temp_buffer = strtok (NULL, " ");
		maximum_incoming_rate = strtoul (temp_buffer, NULL, 10);

		/* grab the maximum outgoing transfer rate */
		temp_buffer = strtok (NULL, " ");
		maximum_outgoing_rate = strtoul (temp_buffer, NULL, 10);
	}

	/* close the log file */
	fclose (fp);

	/* if we couldn't read enough data, return an unknown error */
	if (line <= 2)
		usage (_("Unable to process MRTG log file\n"));

	/* make sure the MRTG data isn't too old */
	time (&current_time);
	if ((expire_minutes > 0) &&
	    (current_time - timestamp) > (expire_minutes * 60))
		die (STATE_WARNING,	_("MRTG data has expired (%d minutes old)\n"),
		     (int) ((current_time - timestamp) / 60));

	/* else check the incoming/outgoing rates */
	if (use_average == TRUE) {
		incoming_rate = average_incoming_rate;
		outgoing_rate = average_outgoing_rate;
	}
	else {
		incoming_rate = maximum_incoming_rate;
		outgoing_rate = maximum_outgoing_rate;
	}

	/* report incoming traffic in Bytes/sec */
	if (incoming_rate < 1024) {
		strcpy (incoming_speed_rating, "B/s");
		adjusted_incoming_rate = (double) incoming_rate;
	}

	/* report incoming traffic in KBytes/sec */
	else if (incoming_rate < (1024 * 1024)) {
		strcpy (incoming_speed_rating, "KB/s");
		adjusted_incoming_rate = (double) (incoming_rate / 1024.0);
	}

	/* report incoming traffic in MBytes/sec */
	else {
		strcpy (incoming_speed_rating, "MB/s");
		adjusted_incoming_rate = (double) (incoming_rate / 1024.0 / 1024.0);
	}

	/* report outgoing traffic in Bytes/sec */
	if (outgoing_rate < 1024) {
		strcpy (outgoing_speed_rating, "B/s");
		adjusted_outgoing_rate = (double) outgoing_rate;
	}

	/* report outgoing traffic in KBytes/sec */
	else if (outgoing_rate < (1024 * 1024)) {
		strcpy (outgoing_speed_rating, "KB/s");
		adjusted_outgoing_rate = (double) (outgoing_rate / 1024.0);
	}

	/* report outgoing traffic in MBytes/sec */
	else {
		strcpy (outgoing_speed_rating, "MB/s");
		adjusted_outgoing_rate = (double) (outgoing_rate / 1024.0 / 1024.0);
	}

	if (incoming_rate > incoming_critical_threshold
			|| outgoing_rate > outgoing_critical_threshold) {
		result = STATE_CRITICAL;
	}
	else if (incoming_rate > incoming_warning_threshold
					 || outgoing_rate > outgoing_warning_threshold) {
		result = STATE_WARNING;
	}

	asprintf (&error_message, _("%s. In = %0.1f %s, %s. Out = %0.1f %s|%s %s\n"),
	          (use_average == TRUE) ? _("Avg") : _("Max"), adjusted_incoming_rate,
	          incoming_speed_rating, (use_average == TRUE) ? _("Avg") : _("Max"),
	          adjusted_outgoing_rate, outgoing_speed_rating,
	          fperfdata("in", adjusted_incoming_rate, incoming_speed_rating,
	                   (int)incoming_warning_threshold, incoming_warning_threshold,
	                   (int)incoming_critical_threshold, incoming_critical_threshold,
	                   TRUE, 0, FALSE, 0),
	          fperfdata("in", adjusted_outgoing_rate, outgoing_speed_rating,
	                   (int)outgoing_warning_threshold, outgoing_warning_threshold,
	                   (int)outgoing_critical_threshold, outgoing_critical_threshold,
	                   TRUE, 0, FALSE, 0));

	printf (_("Traffic %s - %s\n"), state_text(result), error_message);

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
		{"verbose", no_argument, 0, 'v'},
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
		c = getopt_long (argc, argv, "hVF:e:a:c:w:", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'F':									/* input file */
			log_file = optarg;
			break;
		case 'e':									/* expiration time */
			expire_minutes = atoi (optarg);
			break;
		case 'a':									/* aggregation (AVE or MAX) */
			if (!strcmp (optarg, "MAX"))
				use_average = FALSE;
			else
				use_average = TRUE;
			break;
		case 'c':									/* warning threshold */
			sscanf (optarg, "%lu,%lu", &incoming_critical_threshold,
							&outgoing_critical_threshold);
			break;
		case 'w':									/* critical threshold */
			sscanf (optarg, "%lu,%lu", &incoming_warning_threshold,
							&outgoing_warning_threshold);
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
	if (argc > c && log_file == NULL) {
		log_file = argv[c++];
	}

	if (argc > c && expire_minutes == -1) {
		expire_minutes = atoi (argv[c++]);
	}

	if (argc > c && strcmp (argv[c], "MAX") == 0) {
		use_average = FALSE;
		c++;
	}
	else if (argc > c && strcmp (argv[c], "AVG") == 0) {
		use_average = TRUE;
		c++;
	}

	if (argc > c && incoming_warning_threshold == 0) {
		incoming_warning_threshold = strtoul (argv[c++], NULL, 10);
	}

	if (argc > c && incoming_critical_threshold == 0) {
		incoming_critical_threshold = strtoul (argv[c++], NULL, 10);
	}

	if (argc > c && outgoing_warning_threshold == 0) {
		outgoing_warning_threshold = strtoul (argv[c++], NULL, 10);
	}
	
	if (argc > c && outgoing_critical_threshold == 0) {
		outgoing_critical_threshold = strtoul (argv[c++], NULL, 10);
	}

	return validate_arguments ();
}





int
validate_arguments (void)
{
	return OK;
}






void
print_help (void)
{
	print_revision (progname, revision);

	printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf (COPYRIGHT, copyright, email);

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_("\
 -F, --filename=STRING\n\
   File to read log from\n\
 -e, --expires=INTEGER\n\
   Minutes after which log expires\n\
 -a, --aggregation=(AVG|MAX)\n\
   Test average or maximum\n\
 -w, --warning\n\
   Warning threshold pair \"<incoming>,<outgoing>\"\n\
 -c, --critical\n\
   Critical threshold pair \"<incoming>,<outgoing>\"\n"));

	printf (_("\n\
This plugin will check the incoming/outgoing transfer rates of a router,\n\
switch, etc recorded in an MRTG log.  If the newest log entry is older\n\
than <expire_minutes>, a WARNING status is returned. If either the\n\
incoming or outgoing rates exceed the <icl> or <ocl> thresholds (in\n\
Bytes/sec), a CRITICAL status results.  If either of the rates exceed\n\
the <iwl> or <owl> thresholds (in Bytes/sec), a WARNING status results.\n\n"));

	printf (_("Notes:\n\
- MRTG stands for Multi Router Traffic Grapher. It can be downloaded from\n\
  http://ee-staff.ethz.ch/~oetiker/webtools/mrtg/mrtg.html\n\
- While MRTG can monitor things other than traffic rates, this\n\
  plugin probably won't work with much else without modification.\n\
- The calculated i/o rates are a little off from what MRTG actually\n\
  reports.  I'm not sure why this is right now, but will look into it\n\
  for future enhancements of this plugin.\n"));

	printf (_(UT_SUPPORT));
}




void
print_usage (void)
{
	printf (_("\
Usage: %s -F <log_file> -a <AVG | MAX> -v <variable> -w <warning_pair> -c <critical_pair>\n\
  [-e expire_minutes] [-t timeout] [-v]\n"), progname);
	printf (_(UT_HLP_VRS), progname, progname);
}
