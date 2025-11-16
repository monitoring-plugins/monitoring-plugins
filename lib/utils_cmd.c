/*****************************************************************************
 *
 * Monitoring run command utilities
 *
 * License: GPL
 * Copyright (c) 2005-2024 Monitoring Plugins Development Team
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

#include <stddef.h>
#define NAGIOSPLUG_API_C 1

/** includes **/
#include "common.h"
#include "utils_cmd.h"
/* This variable must be global, since there's no way the caller
 * can forcibly slay a dead or ungainly running program otherwise.
 * Multithreading apps and plugins can initialize it (via CMD_INIT)
 * in an async safe manner PRIOR to calling cmd_run() or cmd_run_array()
 * for the first time.
 *
 * The check for initialized values is atomic and can
 * occur in any number of threads simultaneously. */
static pid_t *_cmd_pids = NULL;

#include "utils_base.h"

#include "./maxfd.h"

#include <fcntl.h>

#ifdef HAVE_SYS_WAIT_H
#	include <sys/wait.h>
#endif

/** macros **/
#ifndef WEXITSTATUS
#	define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif

#ifndef WIFEXITED
#	define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif

/* 4.3BSD Reno <signal.h> doesn't define SIG_ERR */
#if defined(SIG_IGN) && !defined(SIG_ERR)
#	define SIG_ERR ((Sigfunc *)-1)
#endif

/** prototypes **/
static int _cmd_open(char *const *argv, int *pfd, int *pfderr)
	__attribute__((__nonnull__(1, 2, 3)));

static int _cmd_fetch_output(int fileDescriptor, output *cmd_output, int flags)
	__attribute__((__nonnull__(2)));

static int _cmd_close(int fileDescriptor);

/* this function is NOT async-safe. It is exported so multithreaded
 * plugins (or other apps) can call it prior to running any commands
 * through this api and thus achieve async-safeness throughout the api */
void cmd_init(void) {
	long maxfd = mp_open_max();

	/* if maxfd is unnaturally high, we force it to a lower value
	 * ( e.g. on SunOS, when ulimit is set to unlimited: 2147483647 this would cause
	 * a segfault when following calloc is called ...  ) */

	if (maxfd > MAXFD_LIMIT) {
		maxfd = MAXFD_LIMIT;
	}

	if (!_cmd_pids) {
		_cmd_pids = calloc(maxfd, sizeof(pid_t));
	}
}

typedef struct {
	int stdout_pipe_fd[2];
	int stderr_pipe_fd[2];
	int file_descriptor;
	int error_code;
} int_cmd_open_result;
static int_cmd_open_result _cmd_open2(char *const *argv) {
#ifdef RLIMIT_CORE
	struct rlimit limit;
#endif

	if (!_cmd_pids) {
		CMD_INIT;
	}

	setenv("LC_ALL", "C", 1);

	int_cmd_open_result result = {
		.error_code = 0,
		.stdout_pipe_fd = {0, 0},
		.stderr_pipe_fd = {0, 0},
	};
	pid_t pid;
	if (pipe(result.stdout_pipe_fd) < 0 || pipe(result.stderr_pipe_fd) < 0 || (pid = fork()) < 0) {
		result.error_code = -1;
		return result; /* errno set by the failing function */
	}

	/* child runs exceve() and _exit. */
	if (pid == 0) {
#ifdef RLIMIT_CORE
		/* the program we execve shouldn't leave core files */
		getrlimit(RLIMIT_CORE, &limit);
		limit.rlim_cur = 0;
		setrlimit(RLIMIT_CORE, &limit);
#endif
		close(result.stdout_pipe_fd[0]);
		if (result.stdout_pipe_fd[1] != STDOUT_FILENO) {
			dup2(result.stdout_pipe_fd[1], STDOUT_FILENO);
			close(result.stdout_pipe_fd[1]);
		}
		close(result.stderr_pipe_fd[0]);
		if (result.stderr_pipe_fd[1] != STDERR_FILENO) {
			dup2(result.stderr_pipe_fd[1], STDERR_FILENO);
			close(result.stderr_pipe_fd[1]);
		}

		/* close all descriptors in _cmd_pids[]
		 * This is executed in a separate address space (pure child),
		 * so we don't have to worry about async safety */
		long maxfd = mp_open_max();
		for (int i = 0; i < maxfd; i++) {
			if (_cmd_pids[i] > 0) {
				close(i);
			}
		}

		execve(argv[0], argv, environ);
		_exit(STATE_UNKNOWN);
	}

	/* parent picks up execution here */
	/* close children descriptors in our address space */
	close(result.stdout_pipe_fd[1]);
	close(result.stderr_pipe_fd[1]);

	/* tag our file's entry in the pid-list and return it */
	_cmd_pids[result.stdout_pipe_fd[0]] = pid;

	result.file_descriptor = result.stdout_pipe_fd[0];
	return result;
}

/* Start running a command, array style */
static int _cmd_open(char *const *argv, int *pfd, int *pfderr) {
#ifdef RLIMIT_CORE
	struct rlimit limit;
#endif

	if (!_cmd_pids) {
		CMD_INIT;
	}

	setenv("LC_ALL", "C", 1);

	pid_t pid;
	if (pipe(pfd) < 0 || pipe(pfderr) < 0 || (pid = fork()) < 0) {
		return -1; /* errno set by the failing function */
	}

	/* child runs exceve() and _exit. */
	if (pid == 0) {
#ifdef RLIMIT_CORE
		/* the program we execve shouldn't leave core files */
		getrlimit(RLIMIT_CORE, &limit);
		limit.rlim_cur = 0;
		setrlimit(RLIMIT_CORE, &limit);
#endif
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

		/* close all descriptors in _cmd_pids[]
		 * This is executed in a separate address space (pure child),
		 * so we don't have to worry about async safety */
		long maxfd = mp_open_max();
		for (int i = 0; i < maxfd; i++) {
			if (_cmd_pids[i] > 0) {
				close(i);
			}
		}

		execve(argv[0], argv, environ);
		_exit(STATE_UNKNOWN);
	}

	/* parent picks up execution here */
	/* close children descriptors in our address space */
	close(pfd[1]);
	close(pfderr[1]);

	/* tag our file's entry in the pid-list and return it */
	_cmd_pids[pfd[0]] = pid;

	return pfd[0];
}

static int _cmd_close(int fileDescriptor) {
	pid_t pid;

	/* make sure the provided fd was opened */
	long maxfd = mp_open_max();
	if (fileDescriptor < 0 || fileDescriptor > maxfd || !_cmd_pids ||
		(pid = _cmd_pids[fileDescriptor]) == 0) {
		return -1;
	}

	_cmd_pids[fileDescriptor] = 0;
	if (close(fileDescriptor) == -1) {
		return -1;
	}

	/* EINTR is ok (sort of), everything else is bad */
	int status;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) {
			return -1;
		}
	}

	/* return child's termination status */
	return (WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
}

typedef struct {
	int error_code;
	output output_container;
} int_cmd_fetch_output2;
static int_cmd_fetch_output2 _cmd_fetch_output2(int fileDescriptor, int flags) {
	char tmpbuf[4096];

	int_cmd_fetch_output2 result = {
		.error_code = 0,
		.output_container =
			{
				.buf = NULL,
				.buflen = 0,
				.line = NULL,
				.lines = 0,
			},
	};
	ssize_t ret;
	while ((ret = read(fileDescriptor, tmpbuf, sizeof(tmpbuf))) > 0) {
		size_t len = (size_t)ret;
		result.output_container.buf =
			realloc(result.output_container.buf, result.output_container.buflen + len + 1);
		memcpy(result.output_container.buf + result.output_container.buflen, tmpbuf, len);
		result.output_container.buflen += len;
	}

	if (ret < 0) {
		printf("read() returned %zd: %s\n", ret, strerror(errno));
		result.error_code = -1;
		return result;
	}

	/* some plugins may want to keep output unbroken, and some commands
	 * will yield no output, so return here for those */
	if (flags & CMD_NO_ARRAYS || !result.output_container.buf || !result.output_container.buflen) {
		return result;
	}

	/* and some may want both */
	char *buf = NULL;
	if (flags & CMD_NO_ASSOC) {
		buf = malloc(result.output_container.buflen);
		memcpy(buf, result.output_container.buf, result.output_container.buflen);
	} else {
		buf = result.output_container.buf;
	}

	result.output_container.line = NULL;
	size_t ary_size = 0; /* rsf = right shift factor, dec'ed uncond once */
	size_t rsf = 6;
	size_t lineno = 0;
	for (size_t i = 0; i < result.output_container.buflen;) {
		/* make sure we have enough memory */
		if (lineno >= ary_size) {
			/* ary_size must never be zero */
			do {
				ary_size = result.output_container.buflen >> --rsf;
			} while (!ary_size);

			result.output_container.line =
				realloc(result.output_container.line, ary_size * sizeof(char *));
		}

		/* set the pointer to the string */
		result.output_container.line[lineno] = &buf[i];

		/* hop to next newline or end of buffer */
		while (buf[i] != '\n' && i < result.output_container.buflen) {
			i++;
		}
		buf[i] = '\0';

		lineno++;
		i++;
	}

	result.output_container.lines = lineno;

	return result;
}

static int _cmd_fetch_output(int fileDescriptor, output *cmd_output, int flags) {
	char tmpbuf[4096];
	cmd_output->buf = NULL;
	cmd_output->buflen = 0;
	ssize_t ret;
	while ((ret = read(fileDescriptor, tmpbuf, sizeof(tmpbuf))) > 0) {
		size_t len = (size_t)ret;
		cmd_output->buf = realloc(cmd_output->buf, cmd_output->buflen + len + 1);
		memcpy(cmd_output->buf + cmd_output->buflen, tmpbuf, len);
		cmd_output->buflen += len;
	}

	if (ret < 0) {
		printf("read() returned %zd: %s\n", ret, strerror(errno));
		return ret;
	}

	/* some plugins may want to keep output unbroken, and some commands
	 * will yield no output, so return here for those */
	if (flags & CMD_NO_ARRAYS || !cmd_output->buf || !cmd_output->buflen) {
		return cmd_output->buflen;
	}

	/* and some may want both */
	char *buf = NULL;
	if (flags & CMD_NO_ASSOC) {
		buf = malloc(cmd_output->buflen);
		memcpy(buf, cmd_output->buf, cmd_output->buflen);
	} else {
		buf = cmd_output->buf;
	}

	cmd_output->line = NULL;
	size_t i = 0;
	size_t ary_size = 0; /* rsf = right shift factor, dec'ed uncond once */
	size_t rsf = 6;
	size_t lineno = 0;
	while (i < cmd_output->buflen) {
		/* make sure we have enough memory */
		if (lineno >= ary_size) {
			/* ary_size must never be zero */
			do {
				ary_size = cmd_output->buflen >> --rsf;
			} while (!ary_size);

			cmd_output->line = realloc(cmd_output->line, ary_size * sizeof(char *));
		}

		/* set the pointer to the string */
		cmd_output->line[lineno] = &buf[i];

		/* hop to next newline or end of buffer */
		while (buf[i] != '\n' && i < cmd_output->buflen) {
			i++;
		}
		buf[i] = '\0';

		lineno++;
		i++;
	}

	return lineno;
}

int cmd_run(const char *cmdstring, output *out, output *err, int flags) {
	if (cmdstring == NULL) {
		return -1;
	}

	/* initialize the structs */
	if (out) {
		memset(out, 0, sizeof(output));
	}
	if (err) {
		memset(err, 0, sizeof(output));
	}

	/* make copy of command string so strtok() doesn't silently modify it */
	/* (the calling program may want to access it later) */
	size_t cmdlen = strlen(cmdstring);
	char *cmd = NULL;
	if ((cmd = malloc(cmdlen + 1)) == NULL) {
		return -1;
	}
	memcpy(cmd, cmdstring, cmdlen);
	cmd[cmdlen] = '\0';

	/* This is not a shell, so we don't handle "???" */
	if (strstr(cmdstring, "\"")) {
		return -1;
	}

	/* allow single quotes, but only if non-whitesapce doesn't occur on both sides */
	if (strstr(cmdstring, " ' ") || strstr(cmdstring, "'''")) {
		return -1;
	}

	/* each arg must be whitespace-separated, so args can be a maximum
	 * of (len / 2) + 1. We add 1 extra to the mix for NULL termination */
	int argc = (cmdlen >> 1) + 2;
	char **argv = calloc((size_t)argc, sizeof(char *));

	if (argv == NULL) {
		printf("%s\n", _("Could not malloc argv array in popen()"));
		return -1;
	}

	/* get command arguments (stupidly, but fairly quickly) */
	int i = 0;
	while (cmd) {
		char *str = cmd;
		str += strspn(str, " \t\r\n"); /* trim any leading whitespace */

		if (strstr(str, "'") == str) { /* handle SIMPLE quoted strings */
			str++;
			if (!strstr(str, "'")) {
				return -1; /* balanced? */
			}
			cmd = 1 + strstr(str, "'");
			str[strcspn(str, "'")] = 0;
		} else {
			if (strpbrk(str, " \t\r\n")) {
				cmd = 1 + strpbrk(str, " \t\r\n");
				str[strcspn(str, " \t\r\n")] = 0;
			} else {
				cmd = NULL;
			}
		}

		if (cmd && strlen(cmd) == strspn(cmd, " \t\r\n")) {
			cmd = NULL;
		}

		argv[i++] = str;
	}

	return cmd_run_array(argv, out, err, flags);
}

cmd_run_result cmd_run2(const char *cmd_string, int flags) {
	cmd_run_result result = {
		.cmd_error_code = 0,
		.error_code = 0,
		.stderr =
			{
				.buf = NULL,
				.buflen = 0,
				.line = NULL,
				.lines = 0,
			},
		.stdout =
			{
				.buf = NULL,
				.buflen = 0,
				.line = NULL,
				.lines = 0,
			},
	};

	if (cmd_string == NULL) {
		result.error_code = -1;
		return result;
	}

	/* make copy of command string so strtok() doesn't silently modify it */
	/* (the calling program may want to access it later) */
	char *cmd = strdup(cmd_string);
	if (cmd == NULL) {
		result.error_code = -1;
		return result;
	}

	/* This is not a shell, so we don't handle "???" */
	if (strstr(cmd, "\"")) {
		result.error_code = -1;
		return result;
	}

	/* allow single quotes, but only if non-whitesapce doesn't occur on both sides */
	if (strstr(cmd, " ' ") || strstr(cmd, "'''")) {
		result.error_code = -1;
		return result;
	}

	/* each arg must be whitespace-separated, so args can be a maximum
	 * of (len / 2) + 1. We add 1 extra to the mix for NULL termination */
	size_t cmdlen = strlen(cmd_string);
	size_t argc = (cmdlen >> 1) + 2;
	char **argv = calloc(argc, sizeof(char *));

	if (argv == NULL) {
		printf("%s\n", _("Could not malloc argv array in popen()"));
		result.error_code = -1;
		return result;
	}

	/* get command arguments (stupidly, but fairly quickly) */
	for (int i = 0; cmd; i++) {
		char *str = cmd;
		str += strspn(str, " \t\r\n"); /* trim any leading whitespace */

		if (strstr(str, "'") == str) { /* handle SIMPLE quoted strings */
			str++;
			if (!strstr(str, "'")) {
				result.error_code = -1;
				return result; /* balanced? */
			}

			cmd = 1 + strstr(str, "'");
			str[strcspn(str, "'")] = 0;
		} else {
			if (strpbrk(str, " \t\r\n")) {
				cmd = 1 + strpbrk(str, " \t\r\n");
				str[strcspn(str, " \t\r\n")] = 0;
			} else {
				cmd = NULL;
			}
		}

		if (cmd && strlen(cmd) == strspn(cmd, " \t\r\n")) {
			cmd = NULL;
		}

		argv[i++] = str;
	}

	result = cmd_run_array2(argv, flags);

	return result;
}

cmd_run_result cmd_run_array2(char *const *cmd, int flags) {
	cmd_run_result result = {
		.cmd_error_code = 0,
		.error_code = 0,
		.stderr =
			{
				.buf = NULL,
				.buflen = 0,
				.line = NULL,
				.lines = 0,
			},
		.stdout =
			{
				.buf = NULL,
				.buflen = 0,
				.line = NULL,
				.lines = 0,
			},
	};

	int_cmd_open_result cmd_open_result = _cmd_open2(cmd);
	if (cmd_open_result.error_code != 0) {
		// result.error_code = -1;
		// return result;
		die(STATE_UNKNOWN, _("Could not open pipe: %s\n"), cmd[0]);
	}

	int file_descriptor = cmd_open_result.file_descriptor;
	int pfd_out[2] = {cmd_open_result.stdout_pipe_fd[0], cmd_open_result.stdout_pipe_fd[1]};
	int pfd_err[2] = {cmd_open_result.stderr_pipe_fd[0], cmd_open_result.stderr_pipe_fd[1]};

	int_cmd_fetch_output2 tmp_stdout = _cmd_fetch_output2(pfd_out[0], flags);
	result.stdout = tmp_stdout.output_container;
	int_cmd_fetch_output2 tmp_stderr = _cmd_fetch_output2(pfd_err[0], flags);
	result.stderr = tmp_stderr.output_container;

	result.cmd_error_code = _cmd_close(file_descriptor);
	return result;
}

int cmd_run_array(char *const *argv, output *out, output *err, int flags) {
	/* initialize the structs */
	if (out) {
		memset(out, 0, sizeof(output));
	}
	if (err) {
		memset(err, 0, sizeof(output));
	}

	int fd;
	int pfd_out[2];
	int pfd_err[2];
	if ((fd = _cmd_open(argv, pfd_out, pfd_err)) == -1) {
		die(STATE_UNKNOWN, _("Could not open pipe: %s\n"), argv[0]);
	}

	if (out) {
		out->lines = _cmd_fetch_output(pfd_out[0], out, flags);
	}
	if (err) {
		err->lines = _cmd_fetch_output(pfd_err[0], err, flags);
	}

	return _cmd_close(fd);
}

int cmd_file_read(const char *filename, output *out, int flags) {
	int fd;
	if (out) {
		memset(out, 0, sizeof(output));
	}

	if ((fd = open(filename, O_RDONLY)) == -1) {
		die(STATE_UNKNOWN, _("Error opening %s: %s"), filename, strerror(errno));
	}

	if (out) {
		out->lines = _cmd_fetch_output(fd, out, flags);
	}

	if (close(fd) == -1) {
		die(STATE_UNKNOWN, _("Error closing %s: %s"), filename, strerror(errno));
	}

	return 0;
}

void timeout_alarm_handler(int signo) {
	if (signo == SIGALRM) {
		printf(_("%s - Plugin timed out after %d seconds\n"), state_text(timeout_state),
			   timeout_interval);

		long maxfd = mp_open_max();
		if (_cmd_pids) {
			for (long int i = 0; i < maxfd; i++) {
				if (_cmd_pids[i] != 0) {
					kill(_cmd_pids[i], SIGKILL);
				}
			}
		}

		exit(timeout_state);
	}
}
