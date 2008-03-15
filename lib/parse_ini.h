#ifndef _PARSE_INI_H_
#define _PARSE_INI_H_

/*
 * parse_ini.h: routines for loading nagios-plugin defaults from ini
 * configuration files.
 */

/* np_arg_list is a linked list of arguments passed between the ini
 * parser and the argument parser to construct the final array */
typedef struct np_arg_el {
	char *arg;
	struct np_arg_el *next;
} np_arg_list;

/* NP_DEFAULT_INI_PATH: compile-time default location for ini file */
#ifndef NP_DEFAULT_INI_PATH
# define NP_DEFAULT_INI_PATH "/etc/nagios-plugins/plugins.ini"
#endif /* NP_DEFAULT_INI_PATH */

/* np_load_defaults: load the default configuration (if present) for
 * a plugin from the ini file
 */
np_arg_list* np_get_defaults(const char *locator, const char *default_section);

#endif /* _PARSE_INI_H_ */
