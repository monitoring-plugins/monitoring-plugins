/*****************************************************************************
*
* Monitoring check_users plugin
*
* License: GPL
* Copyright (c) 2000-2012 Monitoring Plugins Development Team
*
* Description:
*
* This file contains the check_users plugin
*
* This plugin checks the number of users currently logged in on the local
* system and generates an error if the number exceeds the thresholds
* specified.
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

const char *progname = "check_users";
const char *copyright = "2000-2007";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "utils.h"

#if HAVE_WTSAPI32_H
# include <windows.h>
# include <wtsapi32.h>
# undef ERROR
# define ERROR -1
#elif HAVE_UTMPX_H
# include <utmpx.h>
#else
# include "popen.h"
#endif

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#include <systemd/sd-login.h>
#endif

#define possibly_set(a,b) ((a) == 0 ? (b) : 0)

int process_arguments (int, char **);
void print_help (void);
void print_usage (void);

char *warning_range = NULL;
char *critical_range = NULL;
thresholds *thlds = NULL;

int
main (int argc, char **argv)
{
	int users = -1;
	int result = STATE_UNKNOWN;
#if HAVE_WTSAPI32_H
	WTS_SESSION_INFO *wtsinfo;
	DWORD wtscount;
	DWORD index;
#elif HAVE_UTMPX_H
	struct utmpx *putmpx;
#else
	char input_buffer[MAX_INPUT_BUFFER];
#endif

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	users = 0;

#ifdef HAVE_LIBSYSTEMD
	if (sd_booted () > 0)
	        users = sd_get_sessions (NULL);
	else {
#endif
#if HAVE_WTSAPI32_H
	if (!WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE,
	  0, 1, &wtsinfo, &wtscount)) {
		printf(_("Could not enumerate RD sessions: %d\n"), GetLastError());
		return STATE_UNKNOWN;
	}

	for (index = 0; index < wtscount; index++) {
		LPTSTR username;
		DWORD size;
		int len;

		if (!WTSQuerySessionInformation(WTS_CURRENT_SERVER_HANDLE,
		  wtsinfo[index].SessionId, WTSUserName, &username, &size))
			continue;

		len = lstrlen(username);

		WTSFreeMemory(username);

		if (len == 0)
			continue;

		if (wtsinfo[index].State == WTSActive ||
		  wtsinfo[index].State == WTSDisconnected)
			users++;
	}

	WTSFreeMemory(wtsinfo);
#elif HAVE_UTMPX_H
	/* get currently logged users from utmpx */
	setutxent ();

	while ((putmpx = getutxent ()) != NULL)
		if (putmpx->ut_type == USER_PROCESS)
			users++;

	endutxent ();
#else
	/* run the command */
	child_process = spopen (WHO_COMMAND);
	if (child_process == NULL) {
		printf (_("Could not open pipe: %s\n"), WHO_COMMAND);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL)
		printf (_("Could not open stderr for %s\n"), WHO_COMMAND);

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {
		/* increment 'users' on all lines except total user count */
		if (input_buffer[0] != '#') {
			users++;
			continue;
		}

		/* get total logged in users */
		if (sscanf (input_buffer, _("# users=%d"), &users) == 1)
			break;
	}

	/* check STDERR */
	if (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr))
		result = possibly_set (result, STATE_UNKNOWN);
	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process))
		result = possibly_set (result, STATE_UNKNOWN);
#endif
#ifdef HAVE_LIBSYSTEMD
	}
#endif

	/* check the user count against warning and critical thresholds */
	result = get_status((double)users, thlds);

	if (result == STATE_UNKNOWN)
		printf ("%s\n", _("Unable to read output"));
	else {
		printf (_("USERS %s - %d users currently logged in |%s\n"),
				state_text(result), users,
				sperfdata_int("users", users, "", warning_range,
							critical_range, true, 0, false, 0));
	}

	return result;
}

/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;
	int option = 0;
	static struct option longopts[] = {
		{"critical", required_argument, 0, 'c'},
		{"warning", required_argument, 0, 'w'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		usage ("\n");

	while (true) {
		c = getopt_long (argc, argv, "+hVvc:w:", longopts, &option);

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case '?':									/* print short usage statement if args not parsable */
			usage5 ();
		case 'h':									/* help */
			print_help ();
			exit (STATE_UNKNOWN);
		case 'V':									/* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_UNKNOWN);
		case 'c':									/* critical */
			critical_range = optarg;
			break;
		case 'w':									/* warning */
			warning_range = optarg;
			break;
		}
	}

	c = optind;

	if (warning_range == NULL && argc > c)
		warning_range = argv[c++];

	if (critical_range == NULL && argc > c)
		critical_range = argv[c++];

	/* this will abort in case of invalid ranges */
	set_thresholds (&thlds, warning_range, critical_range);

	if (!thlds->warning) {
		usage4 (_("Warning threshold must be a valid range expression"));
	}

	if (!thlds->critical) {
		usage4 (_("Critical threshold must be a valid range expression"));
	}

	return OK;
}

void
print_help (void)
{
	print_revision (progname, NP_VERSION);

	printf ("Copyright (c) 1999 Ethan Galstad\n");
	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("This plugin checks the number of users currently logged in on the local"));
	printf ("%s\n", _("system and generates an error if the number exceeds the thresholds specified."));

	printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (" %s\n", "-w, --warning=RANGE_EXPRESSION");
	printf ("    %s\n", _("Set WARNING status if number of logged in users violates RANGE_EXPRESSION"));
	printf (" %s\n", "-c, --critical=RANGE_EXPRESSION");
	printf ("    %s\n", _("Set CRITICAL status if number of logged in users violates RANGE_EXPRESSION"));

	printf (UT_SUPPORT);
}

void
print_usage (void)
{
	printf ("%s\n", _("Usage:"));
	printf ("%s -w <users> -c <users>\n", progname);
}
