/*****************************************************************************
* 
* Nagios plugins popen
* 
* License: GPL
* Copyright (c) 2005-2007 Nagios Plugins Development Team
* 
* Description:
* 
* A safe alternative to popen
* 
* Provides spopen and spclose
* 
* FILE * spopen(const char *);
* int spclose(FILE *);
* 
* Code taken with liitle modification from "Advanced Programming for the Unix
* Environment" by W. Richard Stevens
* 
* This is considered safe in that no shell is spawned, and the environment
* and path passed to the exec'd program are essentially empty. (popen create
* a shell and passes the environment to it).
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

#include "common.h"

/* extern so plugin has pid to kill exec'd process on timeouts */
extern int timeout_interval;
extern pid_t *childpid;
extern int *child_stderr_array;
extern FILE *child_process;

FILE *spopen (const char *);
int spclose (FILE *);
#ifdef REDHAT_SPOPEN_ERROR
RETSIGTYPE popen_sigchld_handler (int);
#endif
RETSIGTYPE popen_timeout_alarm_handler (int);

#include <stdarg.h>							/* ANSI C header file */
#include <fcntl.h>

#include <limits.h>
#include <sys/resource.h>

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#ifndef WEXITSTATUS
# define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif

#ifndef WIFEXITED
# define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif

/* 4.3BSD Reno <signal.h> doesn't define SIG_ERR */
#if defined(SIG_IGN) && !defined(SIG_ERR)
#define	SIG_ERR	((Sigfunc *)-1)
#endif

#define	min(a,b)	((a) < (b) ? (a) : (b))
#define	max(a,b)	((a) > (b) ? (a) : (b))
int open_max (void);						/* {Prog openmax} */
static void err_sys (const char *, ...) __attribute__((noreturn,format(printf, 1, 2)));
char *rtrim (char *, const char *);

char *pname = NULL;							/* caller can set this from argv[0] */

/*int *childerr = NULL;*//* ptr to array allocated at run-time */
/*extern pid_t *childpid = NULL; *//* ptr to array allocated at run-time */
static int maxfd;								/* from our open_max(), {Prog openmax} */

#ifdef REDHAT_SPOPEN_ERROR
static volatile int childtermd = 0;
#endif

FILE *
spopen (const char *cmdstring)
{
	char *env[2];
	char *cmd = NULL;
	char **argv = NULL;
	char *str, *tmp;
	int argc;

	int i = 0, pfd[2], pfderr[2];
	pid_t pid;

#ifdef 	RLIMIT_CORE
	/* do not leave core files */
	struct rlimit limit;
	getrlimit (RLIMIT_CORE, &limit);
	limit.rlim_cur = 0;
	setrlimit (RLIMIT_CORE, &limit);
#endif

	env[0] = strdup("LC_ALL=C");
	env[1] = '\0';

	/* if no command was passed, return with no error */
	if (cmdstring == NULL)
		return (NULL);

	/* make copy of command string so strtok() doesn't silently modify it */
	/* (the calling program may want to access it later) */
	cmd = malloc (strlen (cmdstring) + 1);
	if (cmd == NULL)
		return NULL;
	strcpy (cmd, cmdstring);

	/* This is not a shell, so we don't handle "???" */
	if (strstr (cmdstring, "\""))
		return NULL;

	/* allow single quotes, but only if non-whitesapce doesn't occur on both sides */
	if (strstr (cmdstring, " ' ") || strstr (cmdstring, "'''"))
		return NULL;

	/* there cannot be more args than characters */
	argc = strlen (cmdstring) + 1;	/* add 1 for NULL termination */
	argv = malloc (sizeof(char*)*argc);

	if (argv == NULL) {
		printf ("%s\n", _("Could not malloc argv array in popen()"));
		return NULL;
	}

	/* loop to get arguments to command */
	while (cmd) {
		str = cmd;
		str += strspn (str, " \t\r\n");	/* trim any leading whitespace */

		if (i >= argc - 2) {
			printf ("%s\n",_("CRITICAL - You need more args!!!"));
			return (NULL);
		}

		if (strstr (str, "'") == str) {	/* handle SIMPLE quoted strings */
			str++;
			if (!strstr (str, "'"))
				return NULL;						/* balanced? */
			cmd = 1 + strstr (str, "'");
			str[strcspn (str, "'")] = 0;
		}
		else if (strcspn(str,"'") < strcspn (str, " \t\r\n")) {
										/* handle --option='foo bar' strings */
			tmp = str + strcspn(str, "'") + 1;
			if (!strstr (tmp, "'"))
				return NULL;						/* balanced? */
			tmp += strcspn(tmp,"'") + 1;
			*tmp = 0;
			cmd = tmp + 1;
		} else {
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
	argv[i] = NULL;

	if (childpid == NULL) {				/* first time through */
		maxfd = open_max ();				/* allocate zeroed out array for child pids */
		if ((childpid = calloc ((size_t)maxfd, sizeof (pid_t))) == NULL)
			return (NULL);
	}

	if (child_stderr_array == NULL) {	/* first time through */
		maxfd = open_max ();				/* allocate zeroed out array for child pids */
		if ((child_stderr_array = calloc ((size_t)maxfd, sizeof (int))) == NULL)
			return (NULL);
	}

	if (pipe (pfd) < 0)
		return (NULL);							/* errno set by pipe() */

	if (pipe (pfderr) < 0)
		return (NULL);							/* errno set by pipe() */

#ifdef REDHAT_SPOPEN_ERROR
	if (signal (SIGCHLD, popen_sigchld_handler) == SIG_ERR) {
		usage4 (_("Cannot catch SIGCHLD"));
	}
#endif

	if ((pid = fork ()) < 0)
		return (NULL);							/* errno set by fork() */
	else if (pid == 0) {					/* child */
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
		/* close all descriptors in childpid[] */
		for (i = 0; i < maxfd; i++)
			if (childpid[i] > 0)
				close (i);

		execve (argv[0], argv, env);
		_exit (0);
	}

	close (pfd[1]);								/* parent */
	if ((child_process = fdopen (pfd[0], "r")) == NULL)
		return (NULL);
	close (pfderr[1]);

	childpid[fileno (child_process)] = pid;	/* remember child pid for this fd */
	child_stderr_array[fileno (child_process)] = pfderr[0];	/* remember STDERR */
	return (child_process);
}

int
spclose (FILE * fp)
{
	int fd, status;
	pid_t pid;

	if (childpid == NULL)
		return (1);								/* popen() has never been called */

	fd = fileno (fp);
	if ((pid = childpid[fd]) == 0)
		return (1);								/* fp wasn't opened by popen() */

	childpid[fd] = 0;
	if (fclose (fp) == EOF)
		return (1);

#ifdef REDHAT_SPOPEN_ERROR
	while (!childtermd);								/* wait until SIGCHLD */
#endif

	while (waitpid (pid, &status, 0) < 0)
		if (errno != EINTR)
			return (1);							/* error other than EINTR from waitpid() */

	if (WIFEXITED (status))
		return (WEXITSTATUS (status));	/* return child's termination status */

	return (1);
}

#ifdef	OPEN_MAX
static int openmax = OPEN_MAX;
#else
static int openmax = 0;
#endif

#define	OPEN_MAX_GUESS	256			/* if OPEN_MAX is indeterminate */
				/* no guarantee this is adequate */

#ifdef REDHAT_SPOPEN_ERROR
RETSIGTYPE
popen_sigchld_handler (int signo)
{
	if (signo == SIGCHLD)
		childtermd = 1;
}
#endif

RETSIGTYPE
popen_timeout_alarm_handler (int signo)
{
	int fh;
	if (signo == SIGALRM) {
		if (child_process != NULL) {
			fh=fileno (child_process);
			if(fh >= 0){
				kill (childpid[fh], SIGKILL);
			}
			printf (_("CRITICAL - Plugin timed out after %d seconds\n"),
						timeout_interval);
		} else {
			printf ("%s\n", _("CRITICAL - popen timeout received, but no child process"));
		}
		exit (STATE_CRITICAL);
	}
}


int
open_max (void)
{
	if (openmax == 0) {						/* first time through */
		errno = 0;
		if ((openmax = sysconf (_SC_OPEN_MAX)) < 0) {
			if (errno == 0)
				openmax = OPEN_MAX_GUESS;	/* it's indeterminate */
			else
				err_sys (_("sysconf error for _SC_OPEN_MAX"));
		}
	}
	return (openmax);
}


/* Fatal error related to a system call.
 * Print a message and die. */

#define MAXLINE 2048
static void
err_sys (const char *fmt, ...)
{
	int errnoflag = 1;
	int errno_save;
	char buf[MAXLINE];

	va_list ap;

	va_start (ap, fmt);
	/* err_doit (1, fmt, ap); */
	errno_save = errno;						/* value caller might want printed */
	vsprintf (buf, fmt, ap);
	if (errnoflag)
		sprintf (buf + strlen (buf), ": %s", strerror (errno_save));
	strcat (buf, "\n");
	fflush (stdout);							/* in case stdout and stderr are the same */
	fputs (buf, stderr);
	fflush (NULL);								/* flushes all stdio output streams */
	va_end (ap);
	exit (1);
}

char *
rtrim (char *str, const char *tok)
{
	int i = 0;
	int j = sizeof (str);

	while (str != NULL && i < j) {
		if (*(str + i) == *tok) {
			sprintf (str + i, "%s", "\0");
			return str;
		}
		i++;
	}
	return str;
}
