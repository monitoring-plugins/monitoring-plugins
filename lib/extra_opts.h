#ifndef _EXTRA_OPTS_H_
#define _EXTRA_OPTS_H_

/*
 * extra_opts.h: routines for loading nagios-plugin defaults from ini
 * configuration files.
 */

/* np_extra_opts: Process the --extra-opts arguments and create a new argument
 * array load the default configuration (if present) for
 * a plugin from the ini file
 */
char **np_extra_opts(int argc, char **argv, const char *plugin_name, int *argc_new);

#endif /* _EXTRA_OPTS_H_ */
