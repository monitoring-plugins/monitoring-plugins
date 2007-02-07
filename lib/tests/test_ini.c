/******************************************************************************

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 $Id$
 
******************************************************************************/

#include "common.h"
#include "parse_ini.h"

#include "tap.h"

void my_free(char *string) {
	if (string != NULL) {
		printf("string:\n\t|%s|\n", string);
		free(string);
	}
}

int
main (int argc, char **argv)
{
	char *optstr=NULL;

	plan_tests(4);

	optstr=np_get_defaults("section@./config-tiny.ini", "check_disk");
	ok( !strcmp(optstr, "--one=two --Foo=Bar --this=Your Mother! --blank="), "config-tiny.ini's section as expected");
	my_free(optstr);

	optstr=np_get_defaults("@./config-tiny.ini", "section");
	ok( !strcmp(optstr, "--one=two --Foo=Bar --this=Your Mother! --blank="), "Used default section name, without specific");
	my_free(optstr);

	/* This test currently crashes */
	/*
	optstr=np_get_defaults("section_unknown@./config-tiny.ini", "section");
	ok( !strcmp(optstr, "--one=two --Foo=Bar --this=Your Mother! --blank="), "Used default section name over specified one");
	my_free(optstr);
	*/

	optstr=np_get_defaults("Section Two@./config-tiny.ini", "check_disk");
	ok( !strcmp(optstr, "--something else=blah --remove=whitespace"), "config-tiny.ini's Section Two as expected");
	my_free(optstr);

	/* These tests currently crash parse_ini.c */
	/*
	optstr=np_get_defaults("/path/to/file.txt@./config-tiny.ini", "check_disk");
	ok( !strcmp(optstr, "--this=that"), "config-tiny.ini's filename as section name");
	my_free(optstr);

	optstr=np_get_defaults("section2@./config-tiny.ini", "check_disk");
	ok( !strcmp(optstr, "--this=that"), "config-tiny.ini's section2 with whitespace before section name");
	my_free(optstr);

	optstr=np_get_defaults("section3@./config-tiny.ini", "check_disk");
	ok( !strcmp(optstr, "--this=that"), "config-tiny.ini's section3 with whitespace after section name");
	my_free(optstr);
	*/

	optstr=np_get_defaults("check_mysql@./plugin.ini", "check_disk");
	ok( !strcmp(optstr, "--username=operator --password=secret"), "plugin.ini's check_mysql as expected");
	my_free(optstr);

	/* This test crashes at the moment. I think it is not expecting single character parameter names */
	/*
	optstr=np_get_defaults("check_mysql2@./config-tiny.ini", "check_disk");
	ok( !strcmp(optstr, "-u=admin -p=secret"), "plugin.ini's check_mysql2 as expected");
	my_free(optstr);
	*/

	return exit_status();
}

