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
		char *argv_test[] = {"prog_name", "arg1", "--extra-opts", "--arg3", "val2", (char *) NULL};
		argc_test=5;
		char *argv_known[] = {"prog_name", "--foo=bar", "arg1", "--arg3", "val2", (char *) NULL};
		argv_new=np_extra_opts(&argc_test, argv_test, "check_disk");
		ok(array_diff(argc_test, argv_new, 5, argv_known), "Default section 1");
		my_free(&argc_test, argv_new, argv_test);
	}

	{
		char *argv_test[] = {"prog_name", "--extra-opts", (char *) NULL};
		argc_test=2;
		char *argv_known[] = {"prog_name", "--foo=bar", (char *) NULL};
		argv_new=np_extra_opts(&argc_test, argv_test, "check_disk");
		ok(array_diff(argc_test, argv_new, 2, argv_known), "Default section 2");
		my_free(&argc_test, argv_new, argv_test);
	}

	{
		char *argv_test[] = {"prog_name", "arg1", "--extra-opts=section1", "--arg3", "val2", (char *) NULL};
		argc_test=5;
		char *argv_known[] = {"prog_name", "--foobar=baz", "arg1", "--arg3", "val2", (char *) NULL};
		argv_new=np_extra_opts(&argc_test, argv_test, "check_disk");
		ok(array_diff(argc_test, argv_new, 5, argv_known), "Default section 3");
		my_free(&argc_test, argv_new, argv_test);
	}

	{
		char *argv_test[] = {"prog_name", "arg1", "--extra-opts", "-arg3", "val2", (char *) NULL};
		argc_test=5;
		char *argv_known[] = {"prog_name", "--foo=bar", "arg1", "-arg3", "val2", (char *) NULL};
		argv_new=np_extra_opts(&argc_test, argv_test, "check_disk");
		ok(array_diff(argc_test, argv_new, 5, argv_known), "Default section 4");
		my_free(&argc_test, argv_new, argv_test);
	}

	{
		char *argv_test[] = {"check_tcp", "--extra-opts", "--extra-opts=tcp_long_lines", (char *) NULL};
		argc_test=3;
		char *argv_known[] = {"check_tcp", "--timeout=10", "--escape", "--send=Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda", "--expect=Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda", "--jail", (char *) NULL};
		argv_new=np_extra_opts(&argc_test, argv_test, "check_tcp");
		ok(array_diff(argc_test, argv_new, 6, argv_known), "Long lines test");
		my_free(&argc_test, argv_new, argv_test);
	}

	return exit_status();
}

