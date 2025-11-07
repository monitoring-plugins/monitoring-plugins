#pragma once

#include "../../config.h"
#include "thresholds.h"
#include <mysql.h>

typedef struct {
	char *db_host;
	char *db_socket;
	char *db;
	char *db_user;
	char *db_pass;
	char *opt_file;
	char *opt_group;
	unsigned int db_port;

	char *sql_query;
	mp_thresholds thresholds;
} check_mysql_query_config;

check_mysql_query_config check_mysql_query_config_init() {
	check_mysql_query_config tmp = {
		.db_host = NULL,
		.db_socket = NULL,
		.db = NULL,
		.db_user = NULL,
		.db_pass = NULL,
		.opt_file = NULL,
		.opt_group = NULL,
		.db_port = MYSQL_PORT,

		.sql_query = NULL,
		.thresholds = mp_thresholds_init(),
	};
	return tmp;
}
