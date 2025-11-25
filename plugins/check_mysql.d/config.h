#pragma once

#include "../../config.h"
#include "thresholds.h"
#include <stddef.h>
#include <mysql.h>

typedef struct {
	char *db_host;
	unsigned int db_port;
	char *db_user;
	char *db_socket;
	char *db_pass;
	char *db;
	char *ca_cert;
	char *ca_dir;
	char *cert;
	char *key;
	char *ciphers;
	bool ssl;
	char *opt_file;
	char *opt_group;

	bool check_replica;
	bool ignore_auth;

	mp_thresholds replica_thresholds;

} check_mysql_config;

check_mysql_config check_mysql_config_init() {
	check_mysql_config tmp = {
		.db_host = NULL,
		.db_port = MYSQL_PORT,
		.db = NULL,
		.db_pass = NULL,
		.db_socket = NULL,
		.db_user = NULL,
		.ca_cert = NULL,
		.ca_dir = NULL,
		.cert = NULL,
		.key = NULL,
		.ciphers = NULL,
		.ssl = false,
		.opt_file = NULL,
		.opt_group = NULL,

		.check_replica = false,
		.ignore_auth = false,

		.replica_thresholds = mp_thresholds_init(),
	};
	return tmp;
}
