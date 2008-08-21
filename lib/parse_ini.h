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

/* FIXME: This is in plugins/common.c. Should be eventually moved to lib/
 * (although for this particular one a configure settings should be ideal)
 */
#ifndef MAX_INPUT_BUFFER
# define MAX_INPUT_BUFFER 8192
#endif /* MAX_INPUT_BUFFER */

/* Filenames (see below) */
#ifndef NP_DEFAULT_INI_FILENAME1
# define NP_DEFAULT_INI_FILENAME1 "plugins.ini"
#endif /* NP_DEFAULT_INI_FILENAME1 */
#ifndef NP_DEFAULT_INI_FILENAME2
# define NP_DEFAULT_INI_FILENAME2 "nagios-plugins.ini"
#endif /* NP_DEFAULT_INI_FILENAME2 */

/* Config paths ending in nagios (search for NP_DEFAULT_INI_FILENAME1) */
#ifndef NP_DEFAULT_INI_NAGIOS_PATH1
# define NP_DEFAULT_INI_NAGIOS_PATH1 "/etc/nagios"
#endif /* NP_DEFAULT_INI_NAGIOS_PATH1 */
#ifndef NP_DEFAULT_INI_NAGIOS_PATH2
# define NP_DEFAULT_INI_NAGIOS_PATH2 "/usr/local/nagios/etc"
#endif /* NP_DEFAULT_INI_NAGIOS_PATH2 */
#ifndef NP_DEFAULT_INI_NAGIOS_PATH3
# define NP_DEFAULT_INI_NAGIOS_PATH3 "/usr/local/etc/nagios"
#endif /* NP_DEFAULT_INI_NAGIOS_PATH3 */
#ifndef NP_DEFAULT_INI_NAGIOS_PATH4
# define NP_DEFAULT_INI_NAGIOS_PATH4 "/etc/opt/nagios"
#endif /* NP_DEFAULT_INI_NAGIOS_PATH4 */

/* Config paths not ending in nagios (search for NP_DEFAULT_INI_FILENAME2) */
#ifndef NP_DEFAULT_INI_PATH1
# define NP_DEFAULT_INI_PATH1 "/etc"
#endif /* NP_DEFAULT_INI_PATH1 */
#ifndef NP_DEFAULT_INI_PATH2
# define NP_DEFAULT_INI_PATH2 "/usr/local/etc"
#endif /* NP_DEFAULT_INI_PATH2 */
#ifndef NP_DEFAULT_INI_PATH3
# define NP_DEFAULT_INI_PATH3 "/etc/opt"
#endif /* NP_DEFAULT_INI_PATH3 */

/* np_load_defaults: load the default configuration (if present) for
 * a plugin from the ini file
 */
np_arg_list* np_get_defaults(const char *locator, const char *default_section);

#endif /* _PARSE_INI_H_ */

