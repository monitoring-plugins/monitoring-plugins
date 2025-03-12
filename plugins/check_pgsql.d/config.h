#pragma once

#include "../../config.h"
#include "thresholds.h"
#include <stddef.h>
#include <pg_config_manual.h>

#define DEFAULT_DB "template1"

enum {
	DEFAULT_WARN = 2,
	DEFAULT_CRIT = 8,
};

typedef struct {
	char *pghost;    /* host name of the backend server */
	char *pgport;    /* port of the backend server */
	char *pgoptions; /* special options to start up the backend server */
	char *pgtty;     /* debugging tty for the backend server */
	char dbName[NAMEDATALEN];
	char *pguser;
	char *pgpasswd;
	char *pgparams;
	char *pgquery;
	char *pgqueryname;

	double twarn;
	double tcrit;
	thresholds *qthresholds;
	char *query_warning;
	char *query_critical;
} check_pgsql_config;

/* begin, by setting the parameters for a backend connection if the
 * parameters are null, then the system will try to use reasonable
 * defaults by looking up environment variables or, failing that,
 * using hardwired constants
 * this targets  .pgoptions and .pgtty
 */

check_pgsql_config check_pgsql_config_init() {
	check_pgsql_config tmp = {
		.pghost = NULL,
		.pgport = NULL,
		.pgoptions = NULL,
		.pgtty = NULL,
		.dbName = DEFAULT_DB,
		.pguser = NULL,
		.pgpasswd = NULL,
		.pgparams = NULL,
		.pgquery = NULL,
		.pgqueryname = NULL,

		.twarn = (double)DEFAULT_WARN,
		.tcrit = (double)DEFAULT_CRIT,
		.qthresholds = NULL,
		.query_warning = NULL,
		.query_critical = NULL,
	};
	return tmp;
}
