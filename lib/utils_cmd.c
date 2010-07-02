/*****************************************************************************
*
* Nagios run command utilities
*
* License: GPL
* Copyright (c) 2005-2006 Nagios Plugins Development Team
*
* Description :
*
* A simple interface to executing programs from other programs, using an
* optimized and safe popen()-like implementation. It is considered safe
* in that no shell needs to be spawned and the environment passed to the
* execve()'d program is essentially empty.
*
* The code in this file is a derivative of popen.c which in turn was taken
* from "Advanced Programming for the Unix Environment" by W. Richard Stevens.
*
* Care has been taken to make sure the functions are async-safe. The one
* function which isn't is cmd_init() which it doesn't make sense to
* call twice anyway, so the api as a whole should be considered async-safe.
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

#define NAGIOSPLUG_API_C 1

/** includes **/
#include "common.h"
#include "utils_cmd.h"
#include "utils_base.h"
#include <fcntl.h>

#ifdef HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif

/* used in _cmd_open to pass the environment to commands */
extern char **environ;

/** macros **/
#ifndef WEXITSTATUS
# define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif

#ifndef WIFEXITED
# define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif

/* 4.3BSD Reno <signal.h> doesn't define SIG_ERR */
#if defined(SIG_IGN) && !defined(SIG_ERR)
# define SIG_ERR ((Sigfunc *)-1)
#endif

/* This variable must be global, since there's no way the caller
 * can forcibly slay a dead or ungainly running program otherwise.
 * Multithreading apps and plugins can initialize it (via CMD_INIT)
 * in an async safe manner PRIOR to calling cmd_run() or cmd_run_array()
 * for the first time.
 *
 * The check for initialized values is atomic and can
 * occur in any number of threads simultaneously. */
static pid_t *_cmd_pids = NULL;

/* Try sysconf(_SC_OPEN_MAX) first, as it can be higher than OPEN_MAX.
 * If that fails and the macro isn't defined, we fall back to an educated
 * guess. There's no guarantee that our guess is adequate and the program
 * will die with SIGSEGV if it isn't and the upper boundary is breached. */
#ifdef _SC_OPEN_MAX
static long maxfd = 0;
#elif defined(OPEN_MAX)
# define maxfd OPEN_MAX
#else	/* sysconf macro unavailable, so guess (may be wildly inaccurate) */
# define maxfd 256
#endif


/** prototypes **/
static int _cmd_open (char *const *, int *, int *)
	__attribute__ ((__nonnull__ (1, 2, 3)));

static int _cmd_fetch_output (int, output *, int)
	__attribute__ ((__nonnull__ (2)));

static int _cmd_close (int);

/* prototype imported from utils.h */
extern void die (int, const char *, ...)
	__attribute__ ((__noreturn__, __format__ (__printf__, 2, 3)));


/* this function is NOT async-safe. It is exported so multithreaded
 * plugins (or other apps) can call it prior to running any commands
 * through this api and thus achieve async-safeness throughout the api */
void
cmd_init (void)
{
#ifndef maxfd
	if (!maxfd && (maxfd = sysconf (_SC_OPEN_MAX)) < 0) {
		/* possibly log or emit a warning here, since there's no
		 * guarantee that our guess at maxfd will be adequate */
		maxfd = 256;
	}
#endif

	if (!_cmd_pids)
		_cmd_pids = calloc (maxfd, sizeof (pid_t));
}


/* Start running a command, array style */
static int
_cmd_open (char *const *argv, int *pfd, int *pfderr)
{
	pid_t pid;
#ifdef RLIMIT_CORE
	struct rlimit limit;
#endif

	int i = 0;

	/* if no command was passed, return with no error */
	if (argv == NULL)
		return -1;

	if (!_cmd_pids)
		CMD_INIT;

	setenv("LC_ALL", "C", 1);

	if (pipe (pfd) < 0 || pipe (pfderr) < 0 || (pid = fork ()) < 0)
		return -1;									/* errno set by the failing function */

	/* child runs exceve() and _exit. */
	if (pid == 0) {
#ifdef 	RLIMIT_CORE
		/* the program we execve shouldn't leave core files */
		getrlimit (RLIMIT_CORE, &limit);
		limit.rlim_cur = 0;
		setrlimit (RLIMIT_CORE, &limit);
#endif
		close (pfd[0]);
		if (pfd[1] != STDOUT_FILENO) {
			dup2 (pfd[1], STDOUT_FILENO);
			close (pfd[1]);
		}
		close (pfderr[0]);
		if (pfderr[1] != STDERR_FILENO) {
			dup2 (pfderr[1], STDERR_FILENO);
			close (pfderr[1]);
		}

		/* close all descriptors in _cmd_pids[]
		 * This is executed in a separate address space (pure child),
		 * so we don't have to worry about async safety */
		for (i = 0; i < maxfd; i++)
			if (_cmd_pids[i] > 0)
				close (i);

		execve (argv[0], argv, environ);
		_exit (STATE_UNKNOWN);
	}

	/* parent picks up execution here */
	/* close childs descriptors in our address space */
	close (pfd[1]);
	close (pfderr[1]);

	/* tag our file's entry in the pid-list and return it */
	_cmd_pids[pfd[0]] = pid;

	return pfd[0];
}

static int
_cmd_close (int fd)
{
	int status;
	pid_t pid;

	/* make sure the provided fd was opened */
	if (fd < 0 || fd > maxfd || !_cmd_pids || (pid = _cmd_pids[fd]) == 0)
		return -1;

	_cmd_pids[fd] = 0;
	if (close (fd) == -1)
		return -1;

	/* EINTR is ok (sort of), everything else is bad */
	while (waitpid (pid, &status, 0) < 0)
		if (errno != EINTR)
			return -1;

	/* return child's termination status */
	return (WIFEXITED (status)) ? WEXITSTATUS (status) : -1;
}


static int
_cmd_fetch_output (int fd, output * op, int flags)
{
	size_t len = 0, i = 0, lineno = 0;
	size_t rsf = 6, ary_size = 0;	/* rsf = right shift factor, dec'ed uncond once */
	char *buf = NULL;
	int ret;
	char tmpbuf[4096];

	op->buf = NULL;
	op->buflen = 0;
	while ((ret = read (fd, tmpbuf, sizeof (tmpbuf))) > 0) {
		len = (size_t) ret;
		op->buf = realloc (op->buf, op->buflen + len + 1);
		memcpy (op->buf + op->buflen, tmpbuf, len);
		op->buflen += len;
		i++;
	}

	if (ret < 0) {
		printf ("read() returned %d: %s\n", ret, strerror (errno));
		return ret;
	}

	/* some plugins may want to keep output unbroken, and some commands
	 * will yield no output, so return here for those */
	if (flags & CMD_NO_ARRAYS || !op->buf || !op->buflen)
		return op->buflen;

	/* and some may want both */
	if (flags & CMD_NO_ASSOC) {
		buf = malloc (op->buflen);
		memcpy (buf, op->buf, op->buflen);
	}
	else
		buf = op->buf;

	op->line = NULL;
	op->lens = NULL;
	i = 0;
	while (i < op->buflen) {
		/* make sure we have enough memory */
		if (lineno >= ary_size) {
			/* ary_size must never be zero */
			do {
				ary_size = op->buflen >> --rsf;
			} while (!ary_size);

			op->line = realloc (op->line, ary_size * sizeof (char *));
			op->lens = realloc (op->lens, ary_size * sizeof (size_t));
		}

		/* set the pointer to the string */
		op->line[lineno] = &buf[i];

		/* hop to next newline or end of buffer */
		while (buf[i] != '\n' && i < op->buflen)
			i++;
		buf[i] = '\0';

		/* calculate the string length using pointer difference */
		op->lens[lineno] = (size_t) & buf[i] - (size_t) op->line[lineno];

		lineno++;
		i++;
	}

	return lineno;
}


int
cmd_run (const char *cmdstring, output * out, output * err, int flags)
{
	int fd, pfd_out[2], pfd_err[2];
	int i = 0, argc;
	size_t cmdlen;
	char **argv = NULL;
	char *cmd = NULL;
	char *str = NULL;

	if (cmdstring == NULL)
		return -1;

	/* initialize the structs */
	if (out)
		memset (out, 0, sizeof (output));
	if (err)
		memset (err, 0, sizeof (output));

	/* make copy of command string so strtok() doesn't silently modify it */
	/* (the calling program may want to access it later) */
	cmdlen = strlen (cmdstring);
	if ((cmd = malloc (cmdlen + 1)) == NULL)
		return -1;
	memcpy (cmd, cmdstring, cmdlen);
	cmd[cmdlen] = '\0';

	/* This is not a shell, so we don't handle "???" */
	if (strstr (cmdstring, "\"")) return -1;

	/* allow single quotes, but only if non-whitesapce doesn't occur on both sides */
	if (strstr (cmdstring, " ' ") || strstr (cmdstring, "'''"))
		return -1;

	/* each arg must be whitespace-separated, so args can be a maximum
	 * of (len / 2) + 1. We add 1 extra to the mix for NULL termination */
	argc = (cmdlen >> 1) + 2;
	argv = calloc (sizeof (char *), argc);

	if (argv == NULL) {
		printf ("%s\n", _("Could not malloc argv array in popen()"));
		return -1;
	}

	/* get command arguments (stupidly, but fairly quickly) */
	while (cmd) {
		str = cmd;
		str += strspn (str, " \t\r\n");	/* trim any leading whitespace */

		if (strstr (str, "'") == str) {	/* handle SIMPLE quoted strings */
			str++;
			if (!strstr (str, "'"))
				return -1;							/* balanced? */
			cmd = 1 + strstr (str, "'");
			str[strcspn (str, "'")] = 0;
		}
		else {
			if (strpbrk (str, " \t\r\n")) {
				cmd = 1 + strpbrk (str, " \t\r\n");
				str[strcspn (str, " \t\r\n")] = 0;
			}
			else {
				cmd = NULL;
			}
		}

		if (cmd && strlen (cmd) == strspn (cmd, " \t\r\n"))
			cmd = NULL;

		argv[i++] = str;
	}

	return cmd_run_array (argv, out, err, flags);
}

int
cmd_run_array (char *const *argv, output * out, output * err, int flags)
{
	int fd, pfd_out[2], pfd_err[2];

	/* initialize the structs */
	if (out)
		memset (out, 0, sizeof (output));
	if (err)
		memset (err, 0, sizeof (output));

	if ((fd = _cmd_open (argv, pfd_out, pfd_err)) == -1)
		die (STATE_UNKNOWN, _("Could not open pipe: %s\n"), argv[0]);

	if (out)
		out->lines = _cmd_fetch_output (pfd_out[0], out, flags);
	if (err)
		err->lines = _cmd_fetch_output (pfd_err[0], err, flags);

	return _cmd_close (fd);
}

int
cmd_file_read ( char *filename, output *out, int flags)
{
	int fd;
	if(out)
		memset (out, 0, sizeof(output));

	if ((fd = open(filename, O_RDONLY)) == -1) {
		die( STATE_UNKNOWN, _("Error opening %s: %s"), filename, strerror(errno) );
	}
	
	if(out)
		out->lines = _cmd_fetch_output (fd, out, flags);

	return 0;
}
