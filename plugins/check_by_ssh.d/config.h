#pragma once

#include "../../config.h"
#include "output.h"
#include <stddef.h>

typedef struct {
	int commargc;
	char **commargv;
} command_construct;

typedef struct {
	char *hostname;
	char *host_shortname;

	char **service;
	unsigned int number_of_services;

	unsigned int commands; // Not needed during actual test run
	char *remotecmd;

	command_construct cmd;

	bool unknown_timeout;
	bool unknown_on_stderr;
	bool warn_on_stderr;
	bool skip_stdout;
	size_t stdout_lines_to_ignore;
	bool skip_stderr;
	size_t sterr_lines_to_ignore;

	bool passive;
	char *outputfile;

	bool output_format_is_set;
	mp_output_format output_format;
} check_by_ssh_config;

check_by_ssh_config check_by_ssh_config_init() {
	check_by_ssh_config tmp = {
		.hostname = NULL,
		.host_shortname = NULL,

		.service = NULL,
		.number_of_services = 0,

		.commands = 0,
		.remotecmd = "",

		.cmd =
			{
				.commargc = 0,
				.commargv = NULL,
			},

		.unknown_timeout = false,
		.unknown_on_stderr = false,
		.warn_on_stderr = false,

		.skip_stderr = false,
		.stdout_lines_to_ignore = 0,
		.skip_stdout = false,
		.sterr_lines_to_ignore = 0,

		.passive = false,
		.outputfile = NULL,

		.output_format_is_set = false,
	};
	return tmp;
}
