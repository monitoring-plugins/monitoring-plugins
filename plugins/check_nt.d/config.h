#pragma once

#include "../../config.h"
#include <stddef.h>

enum {
	PORT = 1248,
};

enum checkvars {
	CHECK_NONE,
	CHECK_CLIENTVERSION,
	CHECK_CPULOAD,
	CHECK_UPTIME,
	CHECK_USEDDISKSPACE,
	CHECK_SERVICESTATE,
	CHECK_PROCSTATE,
	CHECK_MEMUSE,
	CHECK_COUNTER,
	CHECK_FILEAGE,
	CHECK_INSTANCES
};

typedef struct {
	char *server_address;
	int server_port;
	char *req_password;
	enum checkvars vars_to_check;
	bool show_all;
	char *value_list;
	bool check_warning_value;
	unsigned long warning_value;
	bool check_critical_value;
	unsigned long critical_value;
} check_nt_config;

check_nt_config check_nt_config_init() {
	check_nt_config tmp = {
		.server_address = NULL,
		.server_port = PORT,
		.req_password = NULL,

		.vars_to_check = CHECK_NONE,
		.show_all = false,
		.value_list = NULL,

		.check_warning_value = false,
		.warning_value = 0,
		.check_critical_value = false,
		.critical_value = 0,
	};
	return tmp;
}
