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
* 
*****************************************************************************/

#include "common.h"
#include "utils_base.h"
#include "parse_ini.h"

#include "tap.h"

void my_free(char *string) {
	if (string != NULL) {
		printf("string:\n\t|%s|\n", string);
		free(string);
	}
}

char*
list2str(np_arg_list *optlst)
{
	char *optstr=NULL;
	np_arg_list *optltmp;

	/* Put everything as a space-separated string */
	asprintf(&optstr, "");
	while (optlst) {
		asprintf(&optstr, "%s%s ", optstr, optlst->arg);
		optltmp=optlst;
		optlst=optlst->next;
		free(optltmp);
	}
	/* Strip last whitespace */
	if (strlen(optstr)>1) optstr[strlen(optstr)-1]='\0';

	return optstr;
}

int
main (int argc, char **argv)
{
	char *optstr=NULL;

	plan_tests(12);

	optstr=list2str(np_get_defaults("section@./config-tiny.ini", "check_disk"));
	ok( !strcmp(optstr, "--one=two --Foo=Bar --this=Your Mother! --blank"), "config-tiny.ini's section as expected");
	my_free(optstr);

	optstr=list2str(np_get_defaults("@./config-tiny.ini", "section"));
	ok( !strcmp(optstr, "--one=two --Foo=Bar --this=Your Mother! --blank"), "Used default section name, without specific");
	my_free(optstr);

	optstr=list2str(np_get_defaults("Section Two@./config-tiny.ini", "check_disk"));
	ok( !strcmp(optstr, "--something else=blah --remove=whitespace"), "config-tiny.ini's Section Two as expected");
	my_free(optstr);

	optstr=list2str(np_get_defaults("/path/to/file.txt@./config-tiny.ini", "check_disk"));
	ok( !strcmp(optstr, "--this=that"), "config-tiny.ini's filename as section name");
	my_free(optstr);

	optstr=list2str(np_get_defaults("section2@./config-tiny.ini", "check_disk"));
	ok( !strcmp(optstr, "--this=that"), "config-tiny.ini's section2 with whitespace before section name");
	my_free(optstr);

	optstr=list2str(np_get_defaults("section3@./config-tiny.ini", "check_disk"));
	ok( !strcmp(optstr, "--this=that"), "config-tiny.ini's section3 with whitespace after section name");
	my_free(optstr);

	optstr=list2str(np_get_defaults("check_mysql@./plugin.ini", "check_disk"));
	ok( !strcmp(optstr, "--username=operator --password=secret"), "plugin.ini's check_mysql as expected");
	my_free(optstr);

	optstr=list2str(np_get_defaults("check_mysql2@./plugin.ini", "check_disk"));
	ok( !strcmp(optstr, "-u=admin -p=secret"), "plugin.ini's check_mysql2 as expected");
	my_free(optstr);

	optstr=list2str(np_get_defaults("check space_and_flags@./plugin.ini", "check_disk"));
	ok( !strcmp(optstr, "--foo=bar -a -b --bar"), "plugin.ini space in stanza and flag arguments");
	my_free(optstr);

	optstr=list2str(np_get_defaults("Section Two@./config-dos.ini", "check_disk"));
	ok( !strcmp(optstr, "--something else=blah --remove=whitespace"), "config-dos.ini's Section Two as expected");
	my_free(optstr);

	optstr=list2str(np_get_defaults("section_twice@./plugin.ini", "check_disk"));
	ok( !strcmp(optstr, "--foo=bar --bar=foo"), "plugin.ini's section_twice defined twice in the file");
	my_free(optstr);

	optstr=list2str(np_get_defaults("tcp_long_lines@plugins.ini", "check_tcp"));
	ok( !strcmp(optstr, "--escape --send=Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda --expect=Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda Foo bar BAZ yadda yadda yadda --jail"), "Long options");
	my_free(optstr);

	return exit_status();
}

