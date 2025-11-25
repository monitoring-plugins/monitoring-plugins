#pragma once

#include "../../config.h"
#include "output.h"
#include "thresholds.h"
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

	mp_thresholds entries_thresholds;
	mp_thresholds connection_time_threshold;

	bool output_format_is_set;
	mp_output_format output_format;
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

		.entries_thresholds = mp_thresholds_init(),
		.connection_time_threshold = mp_thresholds_init(),

		.output_format_is_set = false,
	};
	return tmp;
}
