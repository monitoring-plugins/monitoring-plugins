/*****************************************************************************
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

#include "common.h"
#include "utils_cmd.h"
#include "utils_base.h"
#include "tap.h"

#define COMMAND_LINE 1024
#define UNSET 65530

char *
get_command (char *const *line)
{
	char *cmd;
	int i = 0;

	asprintf (&cmd, " %s", line[i++]);
	while (line[i] != NULL) {
		asprintf (&cmd, "%s %s", cmd, line[i]);
		i++;
	}

	return cmd;
}

int
main (int argc, char **argv)
{
	char **command_line = malloc (sizeof (char *) * COMMAND_LINE);
	char *command = NULL;
	char *perl;
	output chld_out, chld_err;
	int c;
	int result = UNSET;

	plan_tests(51);

	diag ("Running plain echo command, set one");

	/* ensure everything is empty before we begin */
	memset (&chld_out, 0, sizeof (output));
	memset (&chld_err, 0, sizeof (output));
	ok (chld_out.lines == 0, "(initialised) Checking stdout is reset");
	ok (chld_err.lines == 0, "(initialised) Checking stderr is reset");
	ok (result == UNSET, "(initialised) Checking exit code is reset");

	command_line[0] = strdup ("/bin/echo");
	command_line[1] = strdup ("this");
	command_line[2] = strdup ("is");
	command_line[3] = strdup ("test");
	command_line[4] = strdup ("one");

	command = get_command (command_line);

	result = cmd_run_array (command_line, &chld_out, &chld_err, 0);
	ok (chld_out.lines == 1,
			"(array) Check for expected number of stdout lines");
	ok (chld_err.lines == 0,
			"(array) Check for expected number of stderr lines");
	ok (strcmp (chld_out.line[0], "this is test one") == 0,
			"(array) Check for expected stdout output");
	ok (result == 0, "(array) Checking exit code");

	/* ensure everything is empty again */
	memset (&chld_out, 0, sizeof (output));
	memset (&chld_err, 0, sizeof (output));
	result = UNSET;
	ok (chld_out.lines == 0, "(initialised) Checking stdout is reset");
	ok (chld_err.lines == 0, "(initialised) Checking stderr is reset");
	ok (result == UNSET, "(initialised) Checking exit code is reset");

	result = cmd_run (command, &chld_out, &chld_err, 0);

	ok (chld_out.lines == 1,
			"(string) Check for expected number of stdout lines");
	ok (chld_err.lines == 0,
			"(string) Check for expected number of stderr lines");
	ok (strcmp (chld_out.line[0], "this is test one") == 0,
			"(string) Check for expected stdout output");
	ok (result == 0, "(string) Checking exit code");

	diag ("Running plain echo command, set two");

	/* ensure everything is empty again */
	memset (&chld_out, 0, sizeof (output));
	memset (&chld_err, 0, sizeof (output));
	result = UNSET;
	ok (chld_out.lines == 0, "(initialised) Checking stdout is reset");
	ok (chld_err.lines == 0, "(initialised) Checking stderr is reset");
	ok (result == UNSET, "(initialised) Checking exit code is reset");

	command_line[0] = strdup ("/bin/echo");
	command_line[1] = strdup ("this is test two");
	command_line[2] = NULL;
	command_line[3] = NULL;
	command_line[4] = NULL;

	result = cmd_run_array (command_line, &chld_out, &chld_err, 0);
	ok (chld_out.lines == 1,
			"(array) Check for expected number of stdout lines");
	ok (chld_err.lines == 0,
			"(array) Check for expected number of stderr lines");
	ok (strcmp (chld_out.line[0], "this is test two") == 0,
			"(array) Check for expected stdout output");
	ok (result == 0, "(array) Checking exit code");

	/* ensure everything is empty again */
	memset (&chld_out, 0, sizeof (output));
	memset (&chld_err, 0, sizeof (output));
	result = UNSET;
	ok (chld_out.lines == 0, "(initialised) Checking stdout is reset");
	ok (chld_err.lines == 0, "(initialised) Checking stderr is reset");
	ok (result == UNSET, "(initialised) Checking exit code is reset");

	result = cmd_run (command, &chld_out, &chld_err, 0);

	ok (chld_out.lines == 1,
			"(string) Check for expected number of stdout lines");
	ok (chld_err.lines == 0,
			"(string) Check for expected number of stderr lines");
	ok (strcmp (chld_out.line[0], "this is test one") == 0,
			"(string) Check for expected stdout output");
	ok (result == 0, "(string) Checking exit code");


	/* ensure everything is empty again */
	memset (&chld_out, 0, sizeof (output));
	memset (&chld_err, 0, sizeof (output));
	result = UNSET;
	ok (chld_out.lines == 0, "(initialised) Checking stdout is reset");
	ok (chld_err.lines == 0, "(initialised) Checking stderr is reset");
	ok (result == UNSET, "(initialised) Checking exit code is reset");

	/* Pass linefeeds via parameters through - those should be evaluated by echo to give multi line output */
	command_line[0] = strdup("/bin/echo");
	command_line[1] = strdup("this is a test via echo\nline two\nit's line 3");
	command_line[2] = strdup("and (note space between '3' and 'and') $$ will not get evaluated");

	result = cmd_run_array (command_line, &chld_out, &chld_err, 0);
	ok (chld_out.lines == 3,
			"(array) Check for expected number of stdout lines");
	ok (chld_err.lines == 0,
			"(array) Check for expected number of stderr lines");
	ok (strcmp (chld_out.line[0], "this is a test via echo") == 0,
			"(array) Check line 1 for expected stdout output");
	ok (strcmp (chld_out.line[1], "line two") == 0,
			"(array) Check line 2 for expected stdout output");
	ok (strcmp (chld_out.line[2], "it's line 3 and (note space between '3' and 'and') $$ will not get evaluated") == 0,
			"(array) Check line 3 for expected stdout output");
	ok (result == 0, "(array) Checking exit code");



	/* ensure everything is empty again */
	memset (&chld_out, 0, sizeof (output));
	memset (&chld_err, 0, sizeof (output));
	result = UNSET;
	ok (chld_out.lines == 0, "(initialised) Checking stdout is reset");
	ok (chld_err.lines == 0, "(initialised) Checking stderr is reset");
	ok (result == UNSET, "(initialised) Checking exit code is reset");

	command = (char *)malloc(COMMAND_LINE);
	strcpy(command, "/bin/echo3456 non-existant command");
	result = cmd_run (command, &chld_out, &chld_err, 0);

	ok (chld_out.lines == 0,
			"Non existant command, so no output");
	ok (chld_err.lines == 0,
			"No stderr either");
	ok (result == 3, "Get return code 3 (?) for non-existant command");


	/* ensure everything is empty again */
	memset (&chld_out, 0, sizeof (output));
	memset (&chld_err, 0, sizeof (output));
	result = UNSET;

	command = (char *)malloc(COMMAND_LINE);
	strcpy(command, "/bin/sh non-existant-file");
	result = cmd_run (command, &chld_out, &chld_err, 0);

	ok (chld_out.lines == 0,
			"/bin/sh returns no stdout when file is missing...");
	ok (chld_err.lines == 1,
			"...but does give an error line");
	ok (strstr(chld_err.line[0],"non-existant-file") != NULL, "And missing filename is in error message");
	ok (result != 0, "Get non-zero return code from /bin/sh");


	/* ensure everything is empty again */
	result = UNSET;

	command = (char *)malloc(COMMAND_LINE);
  strcpy(command, "/bin/sh -c 'exit 7'");
  result = cmd_run (command, NULL, NULL, 0);

  ok (result == 7, "Get return code 7 from /bin/sh");


	/* ensure everything is empty again */
	memset (&chld_out, 0, sizeof (output));
	memset (&chld_err, 0, sizeof (output));
	result = UNSET;

	command = (char *)malloc(COMMAND_LINE);
	strcpy(command, "/bin/non-existant-command");
	result = cmd_run (command, &chld_out, &chld_err, 0);

	ok (chld_out.lines == 0,
			"/bin/non-existant-command returns no stdout...");
	ok (chld_err.lines == 0,
			"...and no stderr output either");
	ok (result == 3, "Get return code 3 = UNKNOWN when command does not exist");


	return exit_status ();
}
