/*****************************************************************************
 *
 * Monitoring Plugins popen
 *
 * License: GPL
 * Copyright (c) 2005-2024 Monitoring Plugins Development Team
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
 * Code taken with little modification from "Advanced Programming for the Unix
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

#include "./common.h"
#include "./utils.h"

/* extern so plugin has pid to kill exec'd process on timeouts */
extern pid_t *childpid;
extern int *child_stderr_array;
extern FILE *child_process;

FILE *spopen(const char * /*cmdstring*/);
int spclose(FILE * /*fp*/);
#ifdef REDHAT_SPOPEN_ERROR
void popen_sigchld_handler(int);
#endif
void popen_timeout_alarm_handler(int /*signo*/);

#include <stdarg.h> /* ANSI C header file */
#include <fcntl.h>

#include <limits.h>
#include <sys/resource.h>

#ifdef HAVE_SYS_WAIT_H
#	include <sys/wait.h>
#endif

#ifndef WEXITSTATUS
#	define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif

#ifndef WIFEXITED
#	define WIFEXITED(stat_val) (((stat_val)&255) == 0)
#endif

/* 4.3BSD Reno <signal.h> doesn't define SIG_ERR */
#if defined(SIG_IGN) && !defined(SIG_ERR)
#	define SIG_ERR ((Sigfunc *)-1)
#endif

char *pname = NULL; /* caller can set this from argv[0] */

#ifdef REDHAT_SPOPEN_ERROR
static volatile int childtermd = 0;
#endif

FILE *spopen(const char *cmdstring) {
#ifdef RLIMIT_CORE
	/* do not leave core files */
	struct rlimit limit;
	getrlimit(RLIMIT_CORE, &limit);
	limit.rlim_cur = 0;
	setrlimit(RLIMIT_CORE, &limit);
#endif

	char *env[2];
	env[0] = strdup("LC_ALL=C");
	env[1] = NULL;

	/* if no command was passed, return with no error */
	if (cmdstring == NULL)
		return (NULL);

	char *cmd = NULL;
	/* make copy of command string so strtok() doesn't silently modify it */
	/* (the calling program may want to access it later) */
	cmd = malloc(strlen(cmdstring) + 1);
	if (cmd == NULL)
		return NULL;
	strcpy(cmd, cmdstring);

	/* This is not a shell, so we don't handle "???" */
	if (strstr(cmdstring, "\""))
		return NULL;

	/* allow single quotes, but only if non-whitesapce doesn't occur on both sides */
	if (strstr(cmdstring, " ' ") || strstr(cmdstring, "'''"))
		return NULL;

	int argc;
	char **argv = NULL;
	/* there cannot be more args than characters */
	argc = strlen(cmdstring) + 1; /* add 1 for NULL termination */
	argv = malloc(sizeof(char *) * argc);

	if (argv == NULL) {
		printf("%s\n", _("Could not malloc argv array in popen()"));
		return NULL;
	}

	int i = 0;
	char *str;
	/* loop to get arguments to command */
	while (cmd) {
		str = cmd;
		str += strspn(str, " \t\r\n"); /* trim any leading whitespace */

		if (i >= argc - 2) {
			printf("%s\n", _("CRITICAL - You need more args!!!"));
			return (NULL);
		}

		if (strstr(str, "'") == str) { /* handle SIMPLE quoted strings */
			str++;
			if (!strstr(str, "'"))
				return NULL; /* balanced? */
			cmd = 1 + strstr(str, "'");
			str[strcspn(str, "'")] = 0;
		} else if (strcspn(str, "'") < strcspn(str, " \t\r\n")) {
			/* handle --option='foo bar' strings */
			char *tmp = str + strcspn(str, "'") + 1;
			if (!strstr(tmp, "'"))
				return NULL; /* balanced? */
			tmp += strcspn(tmp, "'") + 1;
			*tmp = 0;
			cmd = tmp + 1;
		} else {
			if (strpbrk(str, " \t\r\n")) {
				cmd = 1 + strpbrk(str, " \t\r\n");
				str[strcspn(str, " \t\r\n")] = 0;
			} else {
				cmd = NULL;
			}
		}

		if (cmd && strlen(cmd) == strspn(cmd, " \t\r\n"))
			cmd = NULL;

		argv[i++] = str;
	}
	argv[i] = NULL;

	long maxfd = mp_open_max();

	if (childpid == NULL) { /* first time through */
		if ((childpid = calloc((size_t)maxfd, sizeof(pid_t))) == NULL)
			return (NULL);
	}

	if (child_stderr_array == NULL) { /* first time through */
		if ((child_stderr_array = calloc((size_t)maxfd, sizeof(int))) == NULL)
			return (NULL);
	}

	int pfd[2];
	if (pipe(pfd) < 0)
		return (NULL); /* errno set by pipe() */

	int pfderr[2];
	if (pipe(pfderr) < 0)
		return (NULL); /* errno set by pipe() */

#ifdef REDHAT_SPOPEN_ERROR
	if (signal(SIGCHLD, popen_sigchld_handler) == SIG_ERR) {
		usage4(_("Cannot catch SIGCHLD"));
	}
#endif

	pid_t pid;
	if ((pid = fork()) < 0)
		return (NULL); /* errno set by fork() */

	if (pid == 0) { /* child */
		close(pfd[0]);
		if (pfd[1] != STDOUT_FILENO) {
			dup2(pfd[1], STDOUT_FILENO);
			close(pfd[1]);
		}
		close(pfderr[0]);
		if (pfderr[1] != STDERR_FILENO) {
			dup2(pfderr[1], STDERR_FILENO);
			close(pfderr[1]);
		}
		/* close all descriptors in childpid[] */
		for (i = 0; i < maxfd; i++)
			if (childpid[i] > 0)
				close(i);

		execve(argv[0], argv, env);
		_exit(0);
	}

	close(pfd[1]); /* parent */
	if ((child_process = fdopen(pfd[0], "r")) == NULL)
		return (NULL);
	close(pfderr[1]);

	childpid[fileno(child_process)] = pid;                 /* remember child pid for this fd */
	child_stderr_array[fileno(child_process)] = pfderr[0]; /* remember STDERR */
	return (child_process);
}

int spclose(FILE *fp) {
	if (childpid == NULL)
		return (1); /* popen() has never been called */

	pid_t pid;
	int fd = fileno(fp);
	if ((pid = childpid[fd]) == 0)
		return (1); /* fp wasn't opened by popen() */

	childpid[fd] = 0;
	if (fclose(fp) == EOF)
		return (1);

#ifdef REDHAT_SPOPEN_ERROR
	while (!childtermd)
		; /* wait until SIGCHLD */
#endif

	int status;
	while (waitpid(pid, &status, 0) < 0)
		if (errno != EINTR)
			return (1); /* error other than EINTR from waitpid() */

	if (WIFEXITED(status))
		return (WEXITSTATUS(status)); /* return child's termination status */

	return (1);
}

#ifdef REDHAT_SPOPEN_ERROR
void popen_sigchld_handler(int signo) {
	if (signo == SIGCHLD)
		childtermd = 1;
}
#endif

void popen_timeout_alarm_handler(int signo) {
	if (signo == SIGALRM) {
		if (child_process != NULL) {
			int fh = fileno(child_process);
			if (fh >= 0) {
				kill(childpid[fh], SIGKILL);
			}
			printf(_("CRITICAL - Plugin timed out after %d seconds\n"), timeout_interval);
		} else {
			printf("%s\n", _("CRITICAL - popen timeout received, but no child process"));
		}
		exit(STATE_CRITICAL);
	}
}
