#include "./users.h"

#ifdef _WIN32
#	ifdef HAVE_WTSAPI32_H
#		include <windows.h>
#		include <wtsapi32.h>
#		undef ERROR
#		define ERROR -1

get_num_of_users_wrapper get_num_of_users_windows() {
	WTS_SESSION_INFO *wtsinfo;
	DWORD wtscount;

	get_num_of_users_wrapper result = {};

	if (!WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &wtsinfo, &wtscount)) {
		// printf(_("Could not enumerate RD sessions: %d\n"), GetLastError());
		result.error = WINDOWS_COULD_NOT_ENUMERATE_SESSIONS;
		return result;
	}

	for (DWORD index = 0; index < wtscount; index++) {
		LPTSTR username;
		DWORD size;

		if (!WTSQuerySessionInformation(WTS_CURRENT_SERVER_HANDLE, wtsinfo[index].SessionId, WTSUserName, &username, &size)) {
			continue;
		}

		int len = lstrlen(username);

		WTSFreeMemory(username);

		if (len == 0) {
			continue;
		}

		if (wtsinfo[index].State == WTSActive || wtsinfo[index].State == WTSDisconnected) {
			result.users++;
		}
	}

	WTSFreeMemory(wtsinfo);
	return result;
}
#	else // HAVE_WTSAPI32_H
#		error On windows but without the WTSAPI32 lib
#	endif // HAVE_WTSAPI32_H

#else // _WIN32

#	include "../../config.h"

#	ifdef HAVE_LIBSYSTEMD
#		include <systemd/sd-daemon.h>
#		include <systemd/sd-login.h>

get_num_of_users_wrapper get_num_of_users_systemd() {
	get_num_of_users_wrapper result = {};

	// Test whether we booted with systemd
	if (sd_booted() > 0) {
		int users = sd_get_sessions(NULL);
		if (users >= 0) {
			// Success
			result.users = users;
			return result;
		}

		// Failure! return the error code
		result.errorcode = users;
		return result;
	}

	// Looks like we are not running systemd,
	// return with error here
	result.errorcode = NO_SYSTEMD_ERROR;
	return result;
}
#	endif

#	ifdef HAVE_UTMPX_H
#		include <utmpx.h>

get_num_of_users_wrapper get_num_of_users_utmp() {
	int users = 0;

	/* get currently logged users from utmpx */
	setutxent();

	struct utmpx *putmpx;
	while ((putmpx = getutxent()) != NULL) {
		if (putmpx->ut_type == USER_PROCESS) {
			users++;
		}
	}

	endutxent();

	get_num_of_users_wrapper result = {
		.errorcode = 0,
		.users = users,
	};

	return result;
}
#	endif

#	ifndef HAVE_WTSAPI32_H
#		ifndef HAVE_LIBSYSTEMD
#			ifndef HAVE_UTMPX_H
//  Fall back option here for the others (probably still not on windows)

#				include "../popen.h"
#				include "../common.h"
#				include "../utils.h"

get_num_of_users_wrapper get_num_of_users_who_command() {
	/* run the command */
	child_process = spopen(WHO_COMMAND);
	if (child_process == NULL) {
		// printf(_("Could not open pipe: %s\n"), WHO_COMMAND);
		get_num_of_users_wrapper result = {
			.errorcode = COULD_NOT_OPEN_PIPE,
		};
		return result;
	}

	child_stderr = fdopen(child_stderr_array[fileno(child_process)], "r");
	if (child_stderr == NULL) {
		// printf(_("Could not open stderr for %s\n"), WHO_COMMAND);
		//  TODO this error should probably be reported
	}

	get_num_of_users_wrapper result = {};
	char input_buffer[MAX_INPUT_BUFFER];
	while (fgets(input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {
		/* increment 'users' on all lines except total user count */
		if (input_buffer[0] != '#') {
			result.users++;
			continue;
		}

		/* get total logged in users */
		if (sscanf(input_buffer, _("# users=%d"), &result.users) == 1) {
			break;
		}
	}

	/* check STDERR */
	if (fgets(input_buffer, MAX_INPUT_BUFFER - 1, child_stderr)) {
		// if this fails, something broke and the result can not be relied upon or so is the theorie here
		result.errorcode = STDERR_COULD_NOT_BE_READ;
	}
	(void)fclose(child_stderr);

	/* close the pipe */
	spclose(child_process);

	return result;
}

#			endif
#		endif
#	endif
#endif
