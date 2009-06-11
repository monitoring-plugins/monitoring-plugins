/*****************************************************************************
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
#include "extra_opts.h"

#include "tap.h"

/* This my_free would be the most efficient way of freeing an extra-opts array,
 * provided as an example */
#if 0
void my_free(int *argc, char **newargv, char **argv) {
	int i;
	/* Return if both arrays are the same */
	if (newargv == argv) return;

	for (i=1; i<*argc; i++) {
		/* Free array items until we reach the start of the original argv */
		if (newargv[i] == argv[1]) break;
		free(newargv[i]);
	}
	/* Free the array itself */
	free(newargv);
}
#else
void my_free(int *argc, char **newargv, char **argv) {
	/* Free stuff (and print while we're at it) */
	int i, freeflag=1;
	printf ("    Arg(%i): ", *argc+1);
	printf ("'%s' ", newargv[0]);
	for (i=1; i<*argc; i++) {
		printf ("'%s' ", newargv[i]);
		/* Stop freeing when we get to the start of the original array */
		if (freeflag) {
			if (newargv[i] == argv[1])
				freeflag=0;
			else
				free(newargv[i]);
		}
	}
	printf ("\n");
	/* Free only if it's a different array */
	if (newargv != argv) free(newargv);
	*argc=0;
}
#endif

int array_diff(int i1, char **a1, int i2, char **a2) {
	int i;

	if (i1 != i2) {
		printf("    Argument count doesn't match!\n");
		return 0;
	}
	for (i=0; i<=i1; i++) {
		if (a1[i]==NULL && a2[i]==NULL) continue;
		if (a1[i]==NULL || a2[i]==NULL) {
			printf("    Argument # %i null in one array!\n", i);
			return 0;
		}
		if (strcmp(a1[i], a2[i])) {
			printf("    Argument # %i doesn't match!\n", i);
			return 0;
		}
	}
	return 1;
}

int
main (int argc, char **argv)
{
	char **argv_new=NULL;
	int i, argc_test;

	plan_tests(5);

	{
		char *argv_test[] = {"prog_name", (char *) NULL};
		argc_test=1;
		char *argv_known[] = {"prog_name", (char *) NULL};
		argv_new=np_extra_opts(&argc_test, argv_test, "check_disk");
		ok(array_diff(argc_test, argv_new, 1, argv_known), "No opts, returns correct argv/argc");
		my_free(&argc_test, argv_new, argv_test);
	}

	{
		char *argv_test[] = {"prog_name", "arg1", "--arg2=val1", "--arg3", "val2", (char *) NULL};
		argc_test=5;
		char *argv_known[] = {"prog_name", "arg1", "--arg2=val1", "--arg3", "val2", (char *) NULL};
		argv_new=np_extra_opts(&argc_test, argv_test, "check_disk");
		ok(array_diff(argc_test, argv_new, 5, argv_known), "No extra opts, verbatim copy of argv");
		my_free(&argc_test, argv_new, argv_test);
	}

	{
		char *argv_test[] = {"prog_name", "--extra-opts=@./config-opts.ini", (char *) NULL};
		argc_test=2;
		char *argv_known[] = {"prog_name", "--foo=Bar", "--this=Your Mother!", "--blank", (char *) NULL};
		argv_new=np_extra_opts(&argc_test, argv_test, "check_disk");
		ok(array_diff(argc_test, argv_new, 4, argv_known), "Only extra opts using default section");
		my_free(&argc_test, argv_new, argv_test);
	}

	{
		char *argv_test[] = {"prog_name", "--extra-opts=sect1@./config-opts.ini", "--extra-opts", "sect2@./config-opts.ini", (char *) NULL};
		argc_test=4;
		char *argv_known[] = {"prog_name", "--one=two", "--something else=oops", "--this=that", (char *) NULL};
		argv_new=np_extra_opts(&argc_test, argv_test, "check_disk");
		ok(array_diff(argc_test, argv_new, 4, argv_known), "Only extra opts specified twice");
		my_free(&argc_test, argv_new, argv_test);
	}

	{
		char *argv_test[] = {"prog_name", "--arg1=val1", "--extra-opts=@./config-opts.ini", "--extra-opts", "sect1@./config-opts.ini", "--arg2", (char *) NULL};
		argc_test=6;
		char *argv_known[] = {"prog_name", "--foo=Bar", "--this=Your Mother!", "--blank", "--one=two", "--arg1=val1", "--arg2", (char *) NULL};
		argv_new=np_extra_opts(&argc_test, argv_test, "check_disk");
		ok(array_diff(argc_test, argv_new, 7, argv_known), "twice extra opts using two sections");
		my_free(&argc_test, argv_new, argv_test);
	}

	return exit_status();
}

