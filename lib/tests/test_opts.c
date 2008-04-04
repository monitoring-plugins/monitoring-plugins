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
* $Id: test_ini.c 1951 2008-03-16 18:10:47Z dermoth $
* 
*****************************************************************************/

#include "common.h"
#include "utils_base.h"
#include "extra_opts.h"

#include "tap.h"

void my_free(int *argc, char **argv) {
	int i;
	printf ("    Arg(%i): ", *argc);
	for (i=1; i<*argc; i++) printf ("'%s' ", argv[i]);
	printf ("\n");
	free(argv);
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
	char **argv_test=NULL, **argv_known=NULL;
	int i, argc_test;

	plan_tests(9);

	argv_test=(char **)malloc(2*sizeof(char **));
	argv_test[0] = "prog_name";
	argv_test[1] = NULL;
	argc_test=1;
	argv_known=(char **)realloc(argv_known, 2*sizeof(char **));
	argv_known[0] = "prog_name";
	argv_known[1] = NULL;
	argv_test=np_extra_opts(&argc_test, argv_test, "check_disk");
	ok(array_diff(argc_test, argv_test, 1, argv_known), "No opts, returns correct argv/argc");
	my_free(&argc_test, argv_test);

	argv_test=(char **)malloc(6*sizeof(char **));
	argv_test[0] = "prog_name";
	argv_test[1] = "arg1";
	argv_test[2] = "--arg2=val1";
	argv_test[3] = "--arg3";
	argv_test[4] = "val2";
	argv_test[5] = NULL;
	argc_test=5;
	argv_known=(char **)realloc(argv_known, 6*sizeof(char **));
	argv_known[0] = "prog_name";
	argv_known[1] = "arg1";
	argv_known[2] = "--arg2=val1";
	argv_known[3] = "--arg3";
	argv_known[4] = "val2";
	argv_known[5] = NULL;
	argv_test=np_extra_opts(&argc_test, argv_test, "check_disk");
	ok(array_diff(argc_test, argv_test, 5, argv_known), "No extra opts, verbatim copy of argv");
	my_free(&argc_test,argv_test);

	argv_test=(char **)malloc(3*sizeof(char **));
	argv_test[0] = "prog_name";
	argv_test[1] = "--extra-opts=@./config-opts.ini";
	argv_test[2] = NULL;
	argc_test=2;
	argv_known=(char **)realloc(argv_known, 5*sizeof(char **));
	argv_known[0] = "prog_name";
	argv_known[1] = "--foo=Bar";
	argv_known[2] = "--this=Your Mother!";
	argv_known[3] = "--blank";
	argv_known[4] = NULL;
	argv_test=np_extra_opts(&argc_test, argv_test, "check_disk");
	ok(array_diff(argc_test, argv_test, 4, argv_known), "Only extra opts using default section");
	my_free(&argc_test,argv_test);

	argv_test=(char **)malloc(5*sizeof(char **));
	argv_test[0] = "prog_name";
	argv_test[1] = "--extra-opts=sect1@./config-opts.ini";
	argv_test[2] = "--extra-opts";
	argv_test[3] = "sect2@./config-opts.ini";
	argv_test[4] = NULL;
	argc_test=4;
	argv_known=(char **)realloc(argv_known, 5*sizeof(char **));
	argv_known[0] = "prog_name";
	argv_known[1] = "--one=two";
	argv_known[2] = "--something else=oops";
	argv_known[3] = "--this=that";
	argv_known[4] = NULL;
	argv_test=np_extra_opts(&argc_test, argv_test, "check_disk");
	ok(array_diff(argc_test, argv_test, 4, argv_known), "Only extra opts specified twice");
	my_free(&argc_test,argv_test);

	argv_test=(char **)malloc(7*sizeof(char **));
	argv_test[0] = "prog_name";
	argv_test[1] = "--arg1=val1";
	argv_test[2] = "--extra-opts=@./config-opts.ini";
	argv_test[3] = "--extra-opts";
	argv_test[4] = "sect1@./config-opts.ini";
	argv_test[5] = "--arg2";
	argv_test[6] = NULL;
	argc_test=6;
	argv_known=(char **)realloc(argv_known, 8*sizeof(char **));
	argv_known[0] = "prog_name";
	argv_known[1] = "--foo=Bar";
	argv_known[2] = "--this=Your Mother!";
	argv_known[3] = "--blank";
	argv_known[4] = "--one=two";
	argv_known[5] = "--arg1=val1";
	argv_known[6] = "--arg2";
	argv_known[7] = NULL;
	argv_test=np_extra_opts(&argc_test, argv_test, "check_disk");
	ok(array_diff(argc_test, argv_test, 7, argv_known), "twice extra opts using two sections");
	my_free(&argc_test,argv_test);

	/* Next three checks dre expected to die. They are commented out as they
	 * could possibly go in a sepatare test checked for return value.
	 */
	/* argv_test=(char **)malloc(6*sizeof(char **));
	argv_test[0] = "prog_name";
	argv_test[1] = "arg1";
	argv_test[2] = "--extra-opts=missing@./config-opts.ini";
	argv_test[3] = "--arg3";
	argv_test[4] = "val2";
	argv_test[5] = NULL;
	argc_test=5;
	argv_known=(char **)realloc(argv_known, 5*sizeof(char **));
	argv_known[0] = "prog_name";
	argv_known[1] = "arg1";
	argv_known[2] = "--arg3";
	argv_known[3] = "val2";
	argv_known[4] = NULL;
	argv_test=np_extra_opts(&argc_test, argv_test, "check_missing");
	ok(array_diff(argc_test, argv_test, 4, argv_known), "Missing section 1");
	my_free(&argc_test,argv_test); */

	/* argv_test=(char **)malloc(7*sizeof(char **));
	argv_test[0] = "prog_name";
	argv_test[1] = "arg1";
	argv_test[2] = "--extra-opts";
	argv_test[3] = "missing@./config-opts.ini";
	argv_test[4] = "--arg3";
	argv_test[5] = "val2";
	argv_test[6] = NULL;
	argc_test=6;
	argv_known=(char **)realloc(argv_known, 5*sizeof(char **));
	argv_known[0] = "prog_name";
	argv_known[1] = "arg1";
	argv_known[2] = "--arg3";
	argv_known[3] = "val2";
	argv_known[4] = NULL;
	argv_test=np_extra_opts(&argc_test, argv_test, "check_missing");
	ok(array_diff(argc_test, argv_test, 4, argv_known), "Missing section 2");
	my_free(&argc_test,argv_test); */

	/* argv_test=(char **)malloc(6*sizeof(char **));
	argv_test[0] = "prog_name";
	argv_test[1] = "arg1";
	argv_test[2] = "--extra-opts";
	argv_test[3] = "--arg3";
	argv_test[4] = "val2";
	argv_test[5] = NULL;
	argc_test=5;
	argv_known=(char **)realloc(argv_known, 5*sizeof(char **));
	argv_known[0] = "prog_name";
	argv_known[1] = "arg1";
	argv_known[2] = "--arg3";
	argv_known[3] = "val2";
	argv_known[4] = NULL;
	argv_test=np_extra_opts(&argc_test, argv_test, "check_missing");
	ok(array_diff(argc_test, argv_test, 4, argv_known), "Missing section 3");
	my_free(&argc_test,argv_test); */

	setenv("NAGIOS_CONFIG_PATH", ".", 1);
	argv_test=(char **)malloc(6*sizeof(char **));
	argv_test[0] = "prog_name";
	argv_test[1] = "arg1";
	argv_test[2] = "--extra-opts";
	argv_test[3] = "--arg3";
	argv_test[4] = "val2";
	argv_test[5] = NULL;
	argc_test=5;
	argv_known=(char **)realloc(argv_known, 6*sizeof(char **));
	argv_known[0] = "prog_name";
	argv_known[1] = "--foo=bar";
	argv_known[2] = "arg1";
	argv_known[3] = "--arg3";
	argv_known[4] = "val2";
	argv_known[5] = NULL;
	argv_test=np_extra_opts(&argc_test, argv_test, "check_disk");
	ok(array_diff(argc_test, argv_test, 5, argv_known), "Default section 1");

	argv_test=(char **)malloc(3*sizeof(char **));
	argv_test[0] = "prog_name";
	argv_test[1] = "--extra-opts";
	argv_test[2] = NULL;
	argc_test=2;
	argv_known=(char **)realloc(argv_known, 3*sizeof(char **));
	argv_known[0] = "prog_name";
	argv_known[1] = "--foo=bar";
	argv_known[2] = NULL;
	argv_test=np_extra_opts(&argc_test, argv_test, "check_disk");
	ok(array_diff(argc_test, argv_test, 2, argv_known), "Default section 2");
	my_free(&argc_test,argv_test);

	argv_test=(char **)malloc(6*sizeof(char **));
	argv_test[0] = "prog_name";
	argv_test[1] = "arg1";
	argv_test[2] = "--extra-opts=section1";
	argv_test[3] = "--arg3";
	argv_test[4] = "val2";
	argv_test[5] = NULL;
	argc_test=5;
	argv_known=(char **)realloc(argv_known, 6*sizeof(char **));
	argv_known[0] = "prog_name";
	argv_known[1] = "--foobar=baz";
	argv_known[2] = "arg1";
	argv_known[3] = "--arg3";
	argv_known[4] = "val2";
	argv_known[5] = NULL;
	argv_test=np_extra_opts(&argc_test, argv_test, "check_disk");
	ok(array_diff(argc_test, argv_test, 5, argv_known), "Default section 3");
	my_free(&argc_test,argv_test);

	argv_test=(char **)malloc(6*sizeof(char **));
	argv_test[0] = "prog_name";
	argv_test[1] = "arg1";
	argv_test[2] = "--extra-opts";
	argv_test[3] = "-arg3";
	argv_test[4] = "val2";
	argv_test[5] = NULL;
	argc_test=5;
	argv_known=(char **)realloc(argv_known, 6*sizeof(char **));
	argv_known[0] = "prog_name";
	argv_known[1] = "--foo=bar";
	argv_known[2] = "arg1";
	argv_known[3] = "-arg3";
	argv_known[4] = "val2";
	argv_known[5] = NULL;
	argv_test=np_extra_opts(&argc_test, argv_test, "check_disk");
	ok(array_diff(argc_test, argv_test, 5, argv_known), "Default section 4");
	my_free(&argc_test,argv_test);

	return exit_status();
}

