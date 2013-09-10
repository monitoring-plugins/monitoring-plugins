/*****************************************************************************
* 
* Nagios negate plugin
* 
* License: GPL
* Copyright (c) 2002-2008 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the negate plugin
* 
* Negates the status of a plugin (returns OK for CRITICAL, and vice-versa).
* Can also perform custom state switching.
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

const char *progname = "negate";
const char *copyright = "2002-2008";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#define DEFAULT_TIMEOUT 11

#include <ctype.h>

#include "common.h"
#include "utils.h"
#include "utils_cmd.h"

/* char *command_line; */

static const char **process_arguments (int, char **);
int validate_arguments (char **);
int translate_state (char *);
void print_help (void);
void print_usage (void);
int subst_text = FALSE;

static int state[4] = {
	STATE_OK,
	STATE_WARNING,
	STATE_CRITICAL,
	STATE_UNKNOWN,
};

int
main (int argc, char **argv)
{
	int found = 0, result = STATE_UNKNOWN;
	char *buf, *sub;
	char **command_line;
	output chld_out, chld_err;
	int i;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	timeout_interval = DEFAULT_TIMEOUT;

	command_line = (char **) process_arguments (argc, argv);

	/* Set signal handling and alarm */
	if (signal (SIGALRM, timeout_alarm_handler) == SIG_ERR)
		die (STATE_UNKNOWN, _("Cannot catch SIGALRM"));

	(void) alarm ((unsigned) timeout_interval);

	/* catch when the command is quoted */
	if(command_line[1] == NULL) {
		result = cmd_run (command_line[0], &chld_out, &chld_err, 0);
	} else {
		result = cmd_run_array (command_line, &chld_out, &chld_err, 0);
	}
	if (chld_err.lines > 0) {
		printf ("Error output from command:\n");
		for (i = 0; i < chld_err.lines; i++) {
			printf ("%s\n", chld_err.line[i]);
		}
		exit (STATE_WARNING);
	}

	/* Return UNKNOWN or worse if no output is returned */
	if (chld_out.lines == 0)
		die (max_state_alt (result, STATE_UNKNOWN), _("No data returned from command\n"));

	for (i = 0; i < chld_out.lines; i++) {
		if (subst_text && result != state[result] &&
		    result >= 0 && result <= 4) {
			/* Loop over each match found */
			while ((sub = strstr (chld_out.line[i], state_text (result)))) {
				/* Terminate the first part and skip over the string we'll substitute */
				*sub = '\0';
				sub += strlen (state_text (result));
				/* then put everything back together */
				xasprintf (&chld_out.line[i], "%s%s%s", chld_out.line[i], state_text (state[result]), sub);
			}
		}
		printf ("%s\n", chld_out.line[i]);
	}

	if (result >= 0 && result <= 4) {
		exit (state[result]);
	} else {
		exit (result);
	}
}


/* process command-line arguments */
static const char **
process_arguments (int argc, char **argv)
{
	int c;
	int permute = TRUE;

	int option = 0;
	static struct option longopts[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"timeout", required_argument, 0, 't'},
		{"timeout-result", required_argument, 0, 'T'},
		{"ok", required_argument, 0, 'o'},
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"unknown", required_argument, 0, 'u'},
		{"substitute", no_argument, 0, 's'},
		{0, 0, 0, 0}
	};

	while (1) {
		c = getopt_long (argc, argv, "+hVt:T:o:w:c:u:s", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':     /* help */
			usage5 ();
			break;
		case 'h':     /* help */
			print_help ();
			exit (EXIT_SUCCESS);
			break;
		case 'V':     /* version */
			print_revision (progname, NP_VERSION);
			exit (EXIT_SUCCESS);
		case 't':     /* timeout period */
			if (!is_integer (optarg))
				usage2 (_("Timeout interval must be a positive integer"), optarg);
			else
				timeout_interval = atoi (optarg);
			break;
		case 'T':     /* Result to return on timeouts */
			if ((timeout_state = translate_state(optarg)) == ERROR)
				usage4 (_("Timeout result must be a valid state name (OK, WARNING, CRITICAL, UNKNOWN) or integer (0-3)."));
			break;
		case 'o':     /* replacement for OK */
			if ((state[STATE_OK] = translate_state(optarg)) == ERROR)
				usage4 (_("Ok must be a valid state name (OK, WARNING, CRITICAL, UNKNOWN) or integer (0-3)."));
			permute = FALSE;
			break;

		case 'w':     /* replacement for WARNING */
			if ((state[STATE_WARNING] = translate_state(optarg)) == ERROR)
				usage4 (_("Warning must be a valid state name (OK, WARNING, CRITICAL, UNKNOWN) or integer (0-3)."));
			permute = FALSE;
			break;
		case 'c':     /* replacement for CRITICAL */
			if ((state[STATE_CRITICAL] = translate_state(optarg)) == ERROR)
				usage4 (_("Critical must be a valid state name (OK, WARNING, CRITICAL, UNKNOWN) or integer (0-3)."));
			permute = FALSE;
			break;
		case 'u':     /* replacement for UNKNOWN */
			if ((state[STATE_UNKNOWN] = translate_state(optarg)) == ERROR)
				usage4 (_("Unknown must be a valid state name (OK, WARNING, CRITICAL, UNKNOWN) or integer (0-3)."));
			permute = FALSE;
			break;
		case 's':     /* Substitute status text */
			subst_text = TRUE;
			break;
		}
	}

	validate_arguments (&argv[optind]);

	if (permute) { /* No [owcu] switch specified, default to this */
		state[STATE_OK] = STATE_CRITICAL;
		state[STATE_CRITICAL] = STATE_OK;
	}

	return (const char **) &argv[optind];
}


int
validate_arguments (char **command_line)
{
	if (command_line[0] == NULL)
		usage4 (_("Could not parse arguments"));

	if (strncmp(command_line[0],"/",1) != 0 && strncmp(command_line[0],"./",2) != 0)
		usage4 (_("Require path to command"));
}


int
translate_state (char *state_text)
{
	char *temp_ptr;
	for (temp_ptr = state_text; *temp_ptr; temp_ptr++) {
		*temp_ptr = toupper(*temp_ptr);
	}
	if (!strcmp(state_text,"OK") || !strcmp(state_text,"0"))
		return STATE_OK;
	if (!strcmp(state_text,"WARNING") || !strcmp(state_text,"1"))
		return STATE_WARNING;
	if (!strcmp(state_text,"CRITICAL") || !strcmp(state_text,"2"))
		return STATE_CRITICAL;
	if (!strcmp(state_text,"UNKNOWN") || !strcmp(state_text,"3"))
		return STATE_UNKNOWN;
	return ERROR;
}

void
print_help (void)
{
	print_revision (progname, NP_VERSION);

	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("Negates the status of a plugin (returns OK for CRITICAL and vice-versa)."));
	printf ("%s\n", _("Additional switches can be used to control which state becomes what."));

	printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);

	printf (UT_TIMEOUT, timeout_interval);
	printf ("    %s\n", _("Keep timeout longer than the plugin timeout to retain CRITICAL status."));
	printf (" -T, --timeout-result=STATUS\n");
	printf ("    %s\n", _("Custom result on Negate timeouts; see below for STATUS definition\n"));

	printf(" -o, --ok=STATUS\n");
	printf(" -w, --warning=STATUS\n");
	printf(" -c, --critical=STATUS\n");
	printf(" -u, --unknown=STATUS\n");
	printf(_("    STATUS can be 'OK', 'WARNING', 'CRITICAL' or 'UNKNOWN' without single\n"));
	printf(_("    quotes. Numeric values are accepted. If nothing is specified, permutes\n"));
	printf(_("    OK and CRITICAL.\n"));
	printf(" -s, --substitute\n");
	printf(_("    Substitute output text as well. Will only substitute text in CAPITALS\n"));

	printf ("\n");
	printf ("%s\n", _("Examples:"));
	printf (" %s\n", "negate /usr/local/nagios/libexec/check_ping -H host");
	printf ("    %s\n", _("Run check_ping and invert result. Must use full path to plugin"));
	printf (" %s\n", "negate -w OK -c UNKNOWN /usr/local/nagios/libexec/check_procs -a 'vi negate.c'");
	printf ("    %s\n", _("This will return OK instead of WARNING and UNKNOWN instead of CRITICAL"));
	printf ("\n");
	printf ("%s\n", _("Notes:"));
	printf (" %s\n", _("This plugin is a wrapper to take the output of another plugin and invert it."));
	printf (" %s\n", _("The full path of the plugin must be provided."));
	printf (" %s\n", _("If the wrapped plugin returns OK, the wrapper will return CRITICAL."));
	printf (" %s\n", _("If the wrapped plugin returns CRITICAL, the wrapper will return OK."));
	printf (" %s\n", _("Otherwise, the output state of the wrapped plugin is unchanged."));
	printf ("\n");
	printf (" %s\n", _("Using timeout-result, it is possible to override the timeout behaviour or a"));
	printf (" %s\n", _("plugin by setting the negate timeout a bit lower."));

	printf (UT_SUPPORT);
}



void
print_usage (void)
{
	printf ("%s\n", _("Usage:"));
	printf ("%s [-t timeout] [-Towcu STATE] [-s] <definition of wrapped plugin>\n", progname);
}
