#pragma once

#include "../../config.h"
#include "output.h"
#include "perfdata.h"
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

	mp_thresholds time_thresholds;
	mp_thresholds qthresholds;

	bool output_format_is_set;
	mp_output_format output_format;
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

		.time_thresholds = mp_thresholds_init(),
		.qthresholds = mp_thresholds_init(),

		.output_format_is_set = false,
	};

	mp_range tmp_range = mp_range_init();
	tmp_range = mp_range_set_end(tmp_range, mp_create_pd_value(DEFAULT_WARN));
	tmp.time_thresholds = mp_thresholds_set_warn(tmp.time_thresholds, tmp_range);

	tmp_range = mp_range_set_end(tmp_range, mp_create_pd_value(DEFAULT_CRIT));
	tmp.time_thresholds = mp_thresholds_set_crit(tmp.time_thresholds, tmp_range);

	return tmp;
}
