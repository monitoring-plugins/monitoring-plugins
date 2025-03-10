#pragma once

#include "../../config.h"
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
	bool warn_on_stderr;
	int skip_stdout;
	int skip_stderr;
	bool passive;
	char *outputfile;
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
		.warn_on_stderr = false,
		.skip_stderr = 0,
		.skip_stdout = 0,
		.passive = false,
		.outputfile = NULL,
	};
	return tmp;
}
