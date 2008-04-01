#ifndef _EXTRA_OPTS_H_
#define _EXTRA_OPTS_H_

/*
 * extra_opts.h: routines for loading nagios-plugin defaults from ini
 * configuration files.
 */

/* np_extra_opts: Process the --extra-opts arguments and create a new argument
 * array with ini-processed and argument-passed arguments together. The
 * ini-procesed arguments always come first (in the ord of --extra-opts
 * arguments). If no --extra-opts arguments are provided or returned nothing
 * it returns **argv otherwise the new array is returned. --extra-opts are
 * always removed from **argv. The original pointers from **argv are kept in
 * the new array to preserve ability to overwrite arguments in processlist.
 */
char **np_extra_opts(int *argc, char **argv, const char *plugin_name);

#endif /* _EXTRA_OPTS_H_ */

