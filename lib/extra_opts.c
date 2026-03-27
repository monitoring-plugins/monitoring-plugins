/*****************************************************************************
 *
 * Monitoring Plugins extra_opts library
 *
 * License: GPL
 * Copyright (c) 2007 - 2024 Monitoring Plugins Development Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *****************************************************************************/

#include "common.h"
#include "utils_base.h"
#include "parse_ini.h"
#include "extra_opts.h"

/* FIXME: copied from utils.h; we should move a bunch of libs! */
bool is_option2(char *str) {
	if (!str) {
		return false;
	}

	if (strspn(str, "-") == 1 || strspn(str, "-") == 2) {
		return true;
	}

	return false;
}

/* this is the externally visible function used by plugins */
char **np_extra_opts(int *argc, char **argv, const char *plugin_name) {
	if (*argc < 2) {
		/* No arguments provided */
		return argv;
	}

	np_arg_list *extra_args = NULL;
	np_arg_list *ea1 = NULL;
	np_arg_list *ea_tmp = NULL;
	char *argptr = NULL;
	int optfound;
	size_t ea_num = (size_t)*argc;

	for (int i = 1; i < *argc; i++) {
		argptr = NULL;
		optfound = 0;

		/* Do we have an extra-opts parameter? */
		if (strncmp(argv[i], "--extra-opts=", 13) == 0) {
			/* It is a single argument with value */
			argptr = argv[i] + 13;
			/* Delete the extra opts argument */
			for (int j = i; j < *argc; j++) {
				argv[j] = argv[j + 1];
			}

			i--;
			*argc -= 1;
		} else if (strcmp(argv[i], "--extra-opts") == 0) {
			if ((i + 1 < *argc) && !is_option2(argv[i + 1])) {
				/* It is a argument with separate value */
				argptr = argv[i + 1];
				/* Delete the extra-opts argument/value */
				for (int j = i; j < *argc - 1; j++) {
					argv[j] = argv[j + 2];
				}

				i -= 2;
				*argc -= 2;
				ea_num--;
			} else {
				/* It has no value */
				optfound = 1;
				/* Delete the extra opts argument */
				for (int j = i; j < *argc; j++) {
					argv[j] = argv[j + 1];
				}

				i--;
				*argc -= 1;
			}
		}

		/* If we found extra-opts, expand them and store them for later*/
		if (argptr || optfound) {
			/* Process ini section, returning a linked list of arguments */
			ea1 = np_get_defaults(argptr, plugin_name);
			if (ea1 == NULL) {
				/* no extra args (empty section)? */
				ea_num--;
				continue;
			}

			/* append the list to extra_args */
			if (extra_args == NULL) {
				extra_args = ea1;
				while ((ea1 = ea1->next)) {
					ea_num++;
				}
			} else {
				ea_tmp = extra_args;
				while (ea_tmp->next) {
					ea_tmp = ea_tmp->next;
				}
				ea_tmp->next = ea1;
				while ((ea1 = ea1->next)) {
					ea_num++;
				}
			}
			ea1 = ea_tmp = NULL;
		}
	} /* lather, rinse, repeat */

	if (ea_num == (size_t)*argc && extra_args == NULL) {
		/* No extra-opts */
		return argv;
	}

	/* done processing arguments. now create a new argv array... */
	char **argv_new = (char **)malloc((ea_num + 1) * sizeof(char **));
	if (argv_new == NULL) {
		die(STATE_UNKNOWN, _("malloc() failed!\n"));
	}

	/* starting with program name */
	argv_new[0] = argv[0];
	int argc_new = 1;
	/* then parsed ini opts (frying them up in the same run) */
	while (extra_args) {
		argv_new[argc_new++] = extra_args->arg;
		ea1 = extra_args;
		extra_args = extra_args->next;
		free(ea1);
	}
	/* finally the rest of the argv array */
	for (int i = 1; i < *argc; i++) {
		argv_new[argc_new++] = argv[i];
	}
	*argc = argc_new;
	/* and terminate. */
	argv_new[argc_new] = NULL;

	return argv_new;
}
