/******************************************************************************
 *
 * CHECK_SNMP.C
 *
 * Program: SNMP plugin for Nagios
 * License: GPL
 * Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
 *
 * Last Modified: $Date$
 *
 * Description:
 *
 * This plugin uses the 'snmpget' command included with the UCD-SNMP
 * package.  If you don't have the package installed you will need to
 * download it from http://ucd-snmp.ucdavis.edu before you can use
 * this plugin.
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
 *./plugins/check_snmp 127.0.0.1 -c public -o .1.3.6.1.4.1.2021.9.1.2.1
 *****************************************************************************/

#include "common.h"
#include "utils.h"
#include "popen.h"

#define PROGNAME check_snmp

#define mark(a) ((a)!=0?"*":"")

#define CHECK_UNDEF 0
#define CRIT_PRESENT 1
#define CRIT_STRING 2
#define CRIT_REGEX 4
#define CRIT_GT 8
#define CRIT_LT 16
#define CRIT_GE 32
#define CRIT_LE 64
#define CRIT_EQ 128
#define CRIT_NE 256
#define CRIT_RANGE 512
#define WARN_PRESENT 1024
#define WARN_STRING 2048
#define WARN_REGEX 4096
#define WARN_GT 8192
#define WARN_LT 16384
#define WARN_GE 32768
#define WARN_LE 65536
#define WARN_EQ 131072
#define WARN_NE 262144
#define WARN_RANGE 524288

#define MAX_OIDS 8
#define MAX_DELIM_LENGTH 8
#define DEFAULT_DELIMITER "="
#define DEFAULT_OUTPUT_DELIMITER " "

void print_usage (void);
void print_help (char *);
int process_arguments (int, char **);
int call_getopt (int, char **);
int check_num (int);
char *clarify_message (char *);
int lu_getll (unsigned long *, char *);
int lu_getul (unsigned long *, char *);
char *thisarg (char *str);
char *nextarg (char *str);

#ifdef HAVE_REGEX_H
#include <regex.h>
char regex_expect[MAX_INPUT_BUFFER] = "";
regex_t preg;
regmatch_t pmatch[10];
char timestamp[10] = "";
char regex[MAX_INPUT_BUFFER];
char errbuf[MAX_INPUT_BUFFER];
int cflags = REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
int eflags = 0;
int errcode, excode;
#endif

char *server_address = NULL;
char *community = NULL;
char oid[MAX_INPUT_BUFFER] = "";
char *label = NULL;
char *units = NULL;
char *port = NULL;
char string_value[MAX_INPUT_BUFFER] = "";
char **labels = NULL;
char **unitv = NULL;
int nlabels = 0;
int labels_size = 8;
int nunits = 0;
int unitv_size = 8;
unsigned long lower_warn_lim[MAX_OIDS];
unsigned long upper_warn_lim[MAX_OIDS];
unsigned long lower_crit_lim[MAX_OIDS];
unsigned long upper_crit_lim[MAX_OIDS];
unsigned long response_value[MAX_OIDS];
int check_warning_value = FALSE;
int check_critical_value = FALSE;
int eval_method[MAX_OIDS];
char *delimiter = NULL;
char *output_delim = NULL;


int
main (int argc, char **argv)
{
	int i = 0;
	int iresult = STATE_UNKNOWN;
	int found = 0;
	int result = STATE_DEPENDENT;
	char input_buffer[MAX_INPUT_BUFFER];
	char *command_line = NULL;
	char *response = NULL;
	char *outbuff = NULL;
	char *output = NULL;
	char *ptr = NULL;
	char *p2 = NULL;
	char *show = NULL;

	labels = malloc (labels_size);
	unitv = malloc (unitv_size);
	outbuff = strscpy (outbuff, "");
	for (i = 0; i < MAX_OIDS; i++)
		eval_method[i] = CHECK_UNDEF;
	i = 0;

	if (process_arguments (argc, argv) == ERROR)
		usage ("Incorrect arguments supplied\n");

	/* create the command line to execute */
	command_line = ssprintf
		(command_line,
		 "%s -m ALL -v 1 %s %s %s",
		 PATH_TO_SNMPGET, server_address, community, oid);

	/* run the command */
	child_process = spopen (command_line);
	if (child_process == NULL) {
		printf ("Could not open pipe: %s\n", command_line);
		exit (STATE_UNKNOWN);
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL) {
		printf ("Could not open stderr for %s\n", command_line);
	}

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process))
		output = strscat (output, input_buffer);

	ptr = output;

	while (ptr) {

		ptr = strstr (ptr, delimiter);
		if (ptr == NULL)
			break;

		ptr += strlen (delimiter);
		ptr += strspn (ptr, " ");

		found++;

		if (ptr[0] == '"') {
			ptr++;
			response = strpcpy (response, ptr, "\"");
			ptr = strpbrk (ptr, "\"");
			ptr += strspn (ptr, "\"\n");
		}
		else {
			response = strpcpy (response, ptr, "\n");
			ptr = strpbrk (ptr, "\n");
			ptr += strspn (ptr, "\n");
			while
				(strstr (ptr, delimiter) &&
				 strstr (ptr, "\n") && strstr (ptr, "\n") < strstr (ptr, delimiter)) {
				response = strpcat (response, ptr, "\n");
				ptr = strpbrk (ptr, "\n");
			}
			if (ptr && strstr (ptr, delimiter) == NULL) {
				response = strscat (response, ptr);
				ptr = NULL;
			}
		}

		if (strstr (response, "Gauge: "))
			show = strstr (response, "Gauge: ") + 7;
		else if (strstr (response, "Gauge32: "))
			show = strstr (response, "Gauge32: ") + 9;
		else
			show = response;
		p2 = show;

		if (eval_method[i] & CRIT_GT ||
				eval_method[i] & CRIT_LT ||
				eval_method[i] & CRIT_GE ||
				eval_method[i] & CRIT_LE ||
				eval_method[i] & CRIT_EQ ||
				eval_method[i] & CRIT_NE ||
				eval_method[i] & WARN_GT ||
				eval_method[i] & WARN_LT ||
				eval_method[i] & WARN_GE ||
				eval_method[i] & WARN_LE ||
				eval_method[i] & WARN_EQ || eval_method[i] & WARN_NE) {
			p2 = strpbrk (p2, "0123456789");
			response_value[i] = strtoul (p2, NULL, 10);
			iresult = check_num (i);
			show = ssprintf (show, "%lu", response_value[i]);
		}

		else if (eval_method[i] & CRIT_STRING) {
			if (strcmp (response, string_value))
				iresult = STATE_CRITICAL;
			else
				iresult = STATE_OK;
		}

		else if (eval_method[i] & CRIT_REGEX) {
#ifdef HAVE_REGEX_H
			excode = regexec (&preg, response, 10, pmatch, eflags);
			if (excode == 0) {
				iresult = STATE_OK;
			}
			else if (excode != REG_NOMATCH) {
				regerror (excode, &preg, errbuf, MAX_INPUT_BUFFER);
				printf ("Execute Error: %s\n", errbuf);
				exit (STATE_CRITICAL);
			}
			else {
				iresult = STATE_CRITICAL;
			}
#else
			printf ("SNMP UNKNOWN: call for regex which was not a compiled option");
			exit (STATE_UNKNOWN);
#endif
		}

		else {
			if (response)
				iresult = STATE_OK;
			else if (eval_method[i] & CRIT_PRESENT)
				iresult = STATE_CRITICAL;
			else
				iresult = STATE_WARNING;
		}

		result = max_state (result, iresult);

		if (nlabels > 1 && i < nlabels && labels[i] != NULL)
			outbuff = ssprintf
				(outbuff,
				 "%s%s%s %s%s%s",
				 outbuff,
				 (i == 0) ? " " : output_delim,
				 labels[i], mark (iresult), show, mark (iresult));
		else
			outbuff = ssprintf
				(outbuff,
				 "%s%s%s%s%s",
				 outbuff,
				 (i == 0) ? " " : output_delim, mark (iresult), show, mark (iresult));

		if (nunits > 0 && i < nunits)
			outbuff = ssprintf (outbuff, "%s %s", outbuff, unitv[i]);

		i++;

	}															/* end while */

	if (found == 0)
		terminate
			(STATE_UNKNOWN,
			 "%s problem - No data recieved from host\nCMD: %s\n",
			 label, command_line);

	/* WARNING if output found on stderr */
	if (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr))
		result = max_state (result, STATE_WARNING);

	/* close stderr */
	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process))
		result = max_state (result, STATE_WARNING);

	if (nunits > 0)
		printf ("%s %s -%s\n", label, state_text (result), outbuff);
	else
		printf ("%s %s -%s %s\n", label, state_text (result), outbuff, units);

	return result;
}

/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
		if (strcmp ("-wv", argv[c]) == 0)
			strcpy (argv[c], "-w");
		if (strcmp ("-cv", argv[c]) == 0)
			strcpy (argv[c], "-c");
	}

	c = 0;
	while (c += (call_getopt (argc - c, &argv[c]))) {
		if (argc <= c)
			break;
		if (server_address == NULL)
			server_address = strscpy (NULL, argv[c]);
	}

	if (community == NULL)
		community = strscpy (NULL, "public");

	if (delimiter == NULL)
		delimiter = strscpy (NULL, DEFAULT_DELIMITER);

	if (output_delim == NULL)
		output_delim = strscpy (NULL, DEFAULT_OUTPUT_DELIMITER);

	if (label == NULL)
		label = strscpy (NULL, "SNMP");

	if (units == NULL)
		units = strscpy (NULL, "");

	if (port == NULL)
		port = strscpy(NULL,"161");

	if (port == NULL)
		port = strscpy(NULL,"161");

	return c;
}

int
call_getopt (int argc, char **argv)
{
	char *ptr;
	int c, i = 1;
	int j = 0, jj = 0;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"timeout", required_argument, 0, 't'},
		{"critical", required_argument, 0, 'c'},
		{"warning", required_argument, 0, 'w'},
		{"hostname", required_argument, 0, 'H'},
		{"community", required_argument, 0, 'C'},
		{"oid", required_argument, 0, 'o'},
		{"object", required_argument, 0, 'o'},
		{"delimiter", required_argument, 0, 'd'},
		{"output-delimiter", required_argument, 0, 'D'},
		{"string", required_argument, 0, 's'},
		{"regex", required_argument, 0, 'r'},
		{"ereg", required_argument, 0, 'r'},
		{"eregi", required_argument, 0, 'R'},
		{"label", required_argument, 0, 'l'},
		{"units", required_argument, 0, 'u'},
		{0, 0, 0, 0}
	};
#endif

	while (1) {
#ifdef HAVE_GETOPT_H
		c =
			getopt_long (argc, argv, "+?hVt:c:w:H:C:o:d:D:s:R:r:l:u:",
									 long_options, &option_index);
#else
		c = getopt (argc, argv, "+?hVt:c:w:H:C:o:d:D:s:R:r:l:u:");
#endif

		if (c == -1 || c == EOF)
			break;

		i++;
		switch (c) {
		case 't':
		case 'c':
		case 'w':
		case 'H':
		case 'C':
		case 'o':
		case 'd':
		case 'D':
		case 's':
		case 'R':
		case 'r':
		case 'l':
		case 'u':
		case 'p':
			i++;
		}

		switch (c) {
		case '?':									/* help */
			printf ("%s: Unknown argument: %s\n\n", my_basename (argv[0]), optarg);
			print_usage ();
			exit (STATE_UNKNOWN);
		case 'h':									/* help */
			print_help (my_basename (argv[0]));
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (my_basename (argv[0]), "$Revision$");
			exit (STATE_OK);
		case 't':									/* timeout period */
			if (!is_integer (optarg)) {
				printf ("%s: Timeout Interval must be an integer!\n\n",
								my_basename (argv[0]));
				print_usage ();
				exit (STATE_UNKNOWN);
			}
			timeout_interval = atoi (optarg);
			break;
		case 'c':									/* critical time threshold */
			if (strspn (optarg, "0123456789:,") < strlen (optarg)) {
				printf ("Invalid critical threshold: %s\n", optarg);
				print_usage ();
				exit (STATE_UNKNOWN);
			}
			for (ptr = optarg, jj = 0; ptr && jj < MAX_OIDS; jj++) {
				if (lu_getll (&lower_crit_lim[jj], ptr) == 1)
					eval_method[jj] |= CRIT_LT;
				if (lu_getul (&upper_crit_lim[jj], ptr) == 1)
					eval_method[jj] |= CRIT_GT;
				(ptr = index (ptr, ',')) ? ptr++ : ptr;
			}
			break;
		case 'w':									/* warning time threshold */
			if (strspn (optarg, "0123456789:,") < strlen (optarg)) {
				printf ("Invalid warning threshold: %s\n", optarg);
				print_usage ();
				exit (STATE_UNKNOWN);
			}
			for (ptr = optarg, jj = 0; ptr && jj < MAX_OIDS; jj++) {
				if (lu_getll (&lower_warn_lim[jj], ptr) == 1)
					eval_method[jj] |= WARN_LT;
				if (lu_getul (&upper_warn_lim[jj], ptr) == 1)
					eval_method[jj] |= WARN_GT;
				(ptr = index (ptr, ',')) ? ptr++ : ptr;
			}
			break;
		case 'H':									/* Host or server */
			server_address = strscpy (server_address, optarg);
			break;
		case 'C':									/* group or community */
			community = strscpy (community, optarg);
			break;
		case 'o':									/* object identifier */
			for (ptr = optarg; (ptr = index (ptr, ',')); ptr++)
				ptr[0] = ' ';
			strncpy (oid, optarg, sizeof (oid) - 1);
			oid[sizeof (oid) - 1] = 0;
			for (ptr = optarg, j = 1; (ptr = index (ptr, ' ')); ptr++)
				j++;
			break;
		case 'd':									/* delimiter */
			delimiter = strscpy (delimiter, optarg);
			break;
		case 'D':									/* output-delimiter */
			output_delim = strscpy (output_delim, optarg);
			break;
		case 's':									/* string or substring */
			strncpy (string_value, optarg, sizeof (string_value) - 1);
			string_value[sizeof (string_value) - 1] = 0;
			eval_method[jj++] = CRIT_STRING;
			break;
		case 'R':									/* regex */
#ifdef HAVE_REGEX_H
			cflags = REG_ICASE;
#endif
		case 'r':									/* regex */
#ifdef HAVE_REGEX_H
			cflags |= REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
			strncpy (regex_expect, optarg, sizeof (regex_expect) - 1);
			regex_expect[sizeof (regex_expect) - 1] = 0;
			errcode = regcomp (&preg, regex_expect, cflags);
			if (errcode != 0) {
				regerror (errcode, &preg, errbuf, MAX_INPUT_BUFFER);
				printf ("Could Not Compile Regular Expression");
				return ERROR;
			}
			eval_method[jj++] = CRIT_REGEX;
#else
			printf ("SNMP UNKNOWN: call for regex which was not a compiled option");
			exit (STATE_UNKNOWN);
#endif
			break;
		case 'l':									/* label */
			label = optarg;
			nlabels++;
			if (nlabels >= labels_size) {
				labels_size += 8;
				labels = realloc (labels, labels_size);
				if (labels == NULL)
					terminate (STATE_UNKNOWN,
										 "Could not realloc() labels[%d]", nlabels);
			}
			labels[nlabels - 1] = optarg;
			ptr = thisarg (optarg);
			if (strstr (ptr, "'") == ptr)
				labels[nlabels - 1] = ptr + 1;
			else
				labels[nlabels - 1] = ptr;
			while (ptr && (ptr = nextarg (ptr))) {
				if (nlabels >= labels_size) {
					labels_size += 8;
					labels = realloc (labels, labels_size);
					if (labels == NULL)
						terminate (STATE_UNKNOWN, "Could not realloc() labels\n");
				}
				labels++;
				ptr = thisarg (ptr);
				if (strstr (ptr, "'") == ptr)
					labels[nlabels - 1] = ptr + 1;
				else
					labels[nlabels - 1] = ptr;
			}
			break;
		case 'u':									/* units */
			units = optarg;
			nunits++;
			if (nunits >= unitv_size) {
				unitv_size += 8;
				unitv = realloc (unitv, unitv_size);
				if (unitv == NULL)
					terminate (STATE_UNKNOWN,
										 "Could not realloc() units [%d]\n", nunits);
			}
			unitv[nunits - 1] = optarg;
			ptr = thisarg (optarg);
			if (strstr (ptr, "'") == ptr)
				unitv[nunits - 1] = ptr + 1;
			else
				unitv[nunits - 1] = ptr;
			while (ptr && (ptr = nextarg (ptr))) {
				if (nunits >= unitv_size) {
					unitv_size += 8;
					unitv = realloc (unitv, unitv_size);
					if (units == NULL)
						terminate (STATE_UNKNOWN, "Could not realloc() units\n");
				}
				nunits++;
				ptr = thisarg (ptr);
				if (strstr (ptr, "'") == ptr)
					unitv[nunits - 1] = ptr + 1;
				else
					unitv[nunits - 1] = ptr;
			}
			break;
		}
	}
	return i;
}

void
print_usage (void)
{
	printf
		("Usage: check_snmp -H <ip_address> -o <OID> [-w warn_range] [-c crit_range] \n"
		 "                  [-C community] [-s string] [-r regex] [-R regexi] [-t timeout]\n"
		 "                  [-l label] [-u units] [-d delimiter] [-D output-delimiter]\n"
		 "       check_snmp --help\n" "       check_snmp --version\n");
}

void
print_help (char *cmd)
{
	printf ("Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)\n"
					"License: GPL\n\n");
	print_usage ();
	printf
		("\nOptions:\n"
		 " -h, --help\n"
		 "    Print detailed help screen\n"
		 " -V, --version\n"
		 "    Print version information\n"
		 " -H, --hostname=HOST\n"
		 "    Name or IP address of the device you wish to query\n"
		 " -o, --oid=OID(s)\n"
		 "    Object identifier(s) whose value you wish to query\n"
		 " -w, --warning=INTEGER_RANGE(s)\n"
		 "    Range(s) which will not result in a WARNING status\n"
		 " -c, --critical=INTEGER_RANGE(s)\n"
		 "    Range(s) which will not result in a CRITICAL status\n"
		 " -C, --community=STRING\n"
		 "    Optional community string for SNMP communication\n"
		 "    (default is \"public\")\n"
		 " -u, --units=STRING\n"
		 "    Units label(s) for output data (e.g., 'sec.').\n"
		 " -p, --port=STRING\n"
		 "    TCP port number target is listening on.\n"
		 " -d, --delimiter=STRING\n"
		 "    Delimiter to use when parsing returned data. Default is \"%s\"\n"
		 "    Any data on the right hand side of the delimiter is considered\n"
		 "    to be the data that should be used in the evaluation.\n"
		 " -t, --timeout=INTEGER\n"
		 "    Seconds to wait before plugin times out (see also nagios server timeout)\n"
		 " -D, --output-delimiter=STRING\n"
		 "    Separates output on multiple OID requests\n"
		 " -s, --string=STRING\n"
		 "    Return OK state (for that OID) if STRING is an exact match\n"
		 " -r, --ereg=REGEX\n"
		 "    Return OK state (for that OID) if extended regular expression REGEX matches\n"
		 " -R, --eregi=REGEX\n"
		 "    Return OK state (for that OID) if case-insensitive extended REGEX matches\n"
		 " -l, --label=STRING\n"
		 "    Prefix label for output from plugin (default -s 'SNMP')\n\n"
		 "- This plugin uses the 'snmpget' command included with the UCD-SNMP package.\n"
		 "  If you don't have the package installed, you will need to download it from\n"
		 "  http://ucd-snmp.ucdavis.edu before you can use this plugin.\n"
		 "- Multiple OIDs may be indicated by a comma- or space-delimited list (lists with\n"
		 "  internal spaces must be quoted)\n"
		 "- Ranges are inclusive and are indicated with colons. When specified as\n"
		 "  'min:max' a STATE_OK will be returned if the result is within the indicated\n"
		 "  range or is equal to the upper or lower bound. A non-OK state will be\n"
		 "  returned if the result is outside the specified range.\n"
		 "- If spcified in the order 'max:min' a non-OK state will be returned if the\n"
		 "  result is within the (inclusive) range.\n"
		 "- Upper or lower bounds may be omitted to skip checking the respective limit.\n"
		 "- Bare integers are interpreted as upper limits.\n"
		 "- When checking multiple OIDs, separate ranges by commas like '-w 1:10,1:,:20'\n"
		 "- Note that only one string and one regex may be checked at present\n"
		 "- All evaluation methods other than PR, STR, and SUBSTR expect that the value\n"
		 "  returned from the SNMP query is an unsigned integer.\n\n",
		 DEFAULT_DELIMITER);
}

char *
clarify_message (char *msg)
{
	int i = 0;
	int foo;
	char tmpmsg_c[MAX_INPUT_BUFFER];
	char *tmpmsg = (char *) &tmpmsg_c;
	tmpmsg = strcpy (tmpmsg, msg);
	if (!strncmp (tmpmsg, " Hex:", 5)) {
		tmpmsg = strtok (tmpmsg, ":");
		while ((tmpmsg = strtok (NULL, " "))) {
			foo = strtol (tmpmsg, NULL, 16);
			/* Translate chars that are not the same value in the printers
			 * character set.
			 */
			switch (foo) {
			case 208:
				{
					foo = 197;
					break;
				}
			case 216:
				{
					foo = 196;
					break;
				}
			}
			msg[i] = foo;
			i++;
		}
		msg[i] = 0;
	}
	return (msg);
}


int
check_num (int i)
{
	int result;
	result = STATE_OK;
	if (eval_method[i] & WARN_GT && eval_method[i] & WARN_LT &&
			lower_warn_lim[i] > upper_warn_lim[i]) {
		if (response_value[i] <= lower_warn_lim[i] &&
				response_value[i] >= upper_warn_lim[i]) {
			result = STATE_WARNING;
		}
	}
	else if
		((eval_method[i] & WARN_GT && response_value[i] > upper_warn_lim[i]) ||
		 (eval_method[i] & WARN_GE && response_value[i] >= upper_warn_lim[i]) ||
		 (eval_method[i] & WARN_LT && response_value[i] < lower_warn_lim[i]) ||
		 (eval_method[i] & WARN_LE && response_value[i] <= lower_warn_lim[i]) ||
		 (eval_method[i] & WARN_EQ && response_value[i] == upper_warn_lim[i]) ||
		 (eval_method[i] & WARN_NE && response_value[i] != upper_warn_lim[i])) {
		result = STATE_WARNING;
	}

	if (eval_method[i] & CRIT_GT && eval_method[i] & CRIT_LT &&
			lower_warn_lim[i] > upper_warn_lim[i]) {
		if (response_value[i] <= lower_crit_lim[i] &&
				response_value[i] >= upper_crit_lim[i]) {
			result = STATE_CRITICAL;
		}
	}
	else if
		((eval_method[i] & CRIT_GT && response_value[i] > upper_crit_lim[i]) ||
		 (eval_method[i] & CRIT_GE && response_value[i] >= upper_crit_lim[i]) ||
		 (eval_method[i] & CRIT_LT && response_value[i] < lower_crit_lim[i]) ||
		 (eval_method[i] & CRIT_LE && response_value[i] <= lower_crit_lim[i]) ||
		 (eval_method[i] & CRIT_EQ && response_value[i] == upper_crit_lim[i]) ||
		 (eval_method[i] & CRIT_NE && response_value[i] != upper_crit_lim[i])) {
		result = STATE_CRITICAL;
	}

	return result;
}


int
lu_getll (unsigned long *ll, char *str)
{
	char tmp[100];
	if (strchr (str, ':') == NULL)
		return 0;
	if (strchr (str, ',') != NULL && (strchr (str, ',') < strchr (str, ':')))
		return 0;
	if (sscanf (str, "%lu%[:]", ll, tmp) == 2)
		return 1;
	return 0;
}

int
lu_getul (unsigned long *ul, char *str)
{
	char tmp[100];
	if (sscanf (str, "%lu%[^,]", ul, tmp) == 1)
		return 1;
	if (sscanf (str, ":%lu%[^,]", ul, tmp) == 1)
		return 1;
	if (sscanf (str, "%*u:%lu%[^,]", ul, tmp) == 1)
		return 1;
	return 0;
}






/* trim leading whitespace
	 if there is a leading quote, make sure it balances */

char *
thisarg (char *str)
{
	str += strspn (str, " \t\r\n");	/* trim any leading whitespace */
	if (strstr (str, "'") == str) {	/* handle SIMPLE quoted strings */
		if (strlen (str) == 1 || !strstr (str + 1, "'"))
			terminate (STATE_UNKNOWN, "Unbalanced quotes\n");
	}
	return str;
}


/* if there's a leading quote, advance to the trailing quote
	 set the trailing quote to '\x0'
	 if the string continues, advance beyond the comma */

char *
nextarg (char *str)
{
	if (strstr (str, "'") == str) {
		if (strlen (str) > 1) {
			str = strstr (str + 1, "'");
			str[0] = 0;
			return (++str);
		}
		else {
			str[0] = 0;
			return NULL;
		}
	}
	if (strstr (str, ",") == str) {
		if (strlen (str) > 1) {
			str[0] = 0;
			return (++str);
		}
		else {
			str[0] = 0;
			return NULL;
		}
	}
	if ((str = strstr (str, ",")) && strlen (str) > 1) {
		str[0] = 0;
		return (++str);
	}
	return NULL;
}
