#pragma once

#include "../../config.h"
#include <stddef.h>

/* some constants */
typedef enum {
	UPGRADE,
	DIST_UPGRADE,
	NO_UPGRADE
} upgrade_type;

typedef struct {
	bool do_update;       /* whether to call apt-get update */
	upgrade_type upgrade; /* which type of upgrade to do */
	bool only_critical;   /* whether to warn about non-critical updates */
	bool list;            /* list packages available for upgrade */
	/* number of packages available for upgrade to return WARNING status */
	int packages_warning;

	char *upgrade_opts;   /* options to override defaults for upgrade */
	char *update_opts;    /* options to override defaults for update */
	char *do_include;     /* regexp to only include certain packages */
	char *do_exclude;     /* regexp to only exclude certain packages */
	char *do_critical;    /* regexp specifying critical packages */
	char *input_filename; /* input filename for testing */
} check_apt_config;

check_apt_config check_apt_config_init() {
	check_apt_config tmp = {.do_update = false,
							.upgrade = UPGRADE,
							.only_critical = false,
							.list = false,
							.packages_warning = 1,
							.update_opts = NULL,
							.do_include = NULL,
							.do_exclude = NULL,
							.do_critical = NULL,
							.input_filename = NULL};
	return tmp;
}
