#pragma once

#include "../../config.h"
#include "thresholds.h"
#include <mysql/udf_registration_types.h>
#include <stddef.h>

static char ld_defattr[] = "(objectclass=*)";

enum {
#ifdef HAVE_LDAP_SET_OPTION
	DEFAULT_PROTOCOL = 2,
#endif
};

typedef struct {
	char *ld_host;
	char *ld_base;
	char *ld_passwd;
	char *ld_binddn;
	char *ld_attr;
	int ld_port;
	bool starttls;
	bool ssl_on_connect;
#ifdef HAVE_LDAP_SET_OPTION
	int ld_protocol;
#endif

	char *warn_entries;
	char *crit_entries;
	thresholds *entries_thresholds;
	bool warn_time_set;
	double warn_time;
	bool crit_time_set;
	double crit_time;
} check_ldap_config;

check_ldap_config check_ldap_config_init() {
	check_ldap_config tmp = {
		.ld_host = NULL,
		.ld_base = NULL,
		.ld_passwd = NULL,
		.ld_binddn = NULL,
		.ld_attr = ld_defattr,
		.ld_port = -1,
		.starttls = false,
		.ssl_on_connect = false,
#ifdef HAVE_LDAP_SET_OPTION
		.ld_protocol = DEFAULT_PROTOCOL,
#endif

		.warn_entries = NULL,
		.crit_entries = NULL,
		.entries_thresholds = NULL,
		.warn_time_set = false,
		.warn_time = 0,
		.crit_time_set = false,
		.crit_time = 0,
	};
	return tmp;
}
