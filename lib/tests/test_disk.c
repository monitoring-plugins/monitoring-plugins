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
#include "utils_disk.h"
#include "tap.h"
#include "regex.h"

void np_test_mount_entry_regex (struct mount_entry *dummy_mount_list,
	       			char *regstr, int cflags, int expect,
			       	char *desc);


int
main (int argc, char **argv)
{
	struct name_list *exclude_filesystem=NULL;
	struct name_list *exclude_fstype=NULL;
	struct name_list *dummy_mountlist = NULL;
	struct name_list *temp_name;
	struct parameter_list *paths = NULL;
	struct parameter_list *p, *prev = NULL, *last = NULL;

	struct mount_entry *dummy_mount_list;
	struct mount_entry *me;
	struct mount_entry **mtail = &dummy_mount_list;
	int cflags = REG_NOSUB | REG_EXTENDED;
	int found = 0, count = 0;

	plan_tests(33);

	ok( np_find_name(exclude_filesystem, "/var/log") == FALSE, "/var/log not in list");
	np_add_name(&exclude_filesystem, "/var/log");
	ok( np_find_name(exclude_filesystem, "/var/log") == TRUE, "is in list now");
	ok( np_find_name(exclude_filesystem, "/home") == FALSE, "/home not in list");
	np_add_name(&exclude_filesystem, "/home");
	ok( np_find_name(exclude_filesystem, "/home") == TRUE, "is in list now");
	ok( np_find_name(exclude_filesystem, "/var/log") == TRUE, "/var/log still in list");

	ok( np_find_name(exclude_fstype, "iso9660") == FALSE, "iso9660 not in list");
	np_add_name(&exclude_fstype, "iso9660");
	ok( np_find_name(exclude_fstype, "iso9660") == TRUE, "is in list now");

	ok( np_find_name(exclude_filesystem, "iso9660") == FALSE, "Make sure no clashing in variables");

	/*
	for (temp_name = exclude_filesystem; temp_name; temp_name = temp_name->next) {
		printf("Name: %s\n", temp_name->name);
	}
	*/

	me = (struct mount_entry *) malloc(sizeof *me);
	me->me_devname = strdup("/dev/c0t0d0s0");
	me->me_mountdir = strdup("/");
	*mtail = me;
	mtail = &me->me_next;

	me = (struct mount_entry *) malloc(sizeof *me);
	me->me_devname = strdup("/dev/c1t0d1s0");
	me->me_mountdir = strdup("/var");
	*mtail = me;
	mtail = &me->me_next;

	me = (struct mount_entry *) malloc(sizeof *me);
	me->me_devname = strdup("/dev/c2t0d0s0");
	me->me_mountdir = strdup("/home");
	*mtail = me;
	mtail = &me->me_next;

	np_test_mount_entry_regex(dummy_mount_list, strdup("/"),
		                  cflags, 3, strdup("a"));
	np_test_mount_entry_regex(dummy_mount_list, strdup("/dev"),
		                  cflags, 3,strdup("regex on dev names:"));
	np_test_mount_entry_regex(dummy_mount_list, strdup("/foo"),
		                  cflags, 0,
			 	  strdup("regex on non existant dev/path:"));
	np_test_mount_entry_regex(dummy_mount_list, strdup("/Foo"),
		                  cflags | REG_ICASE,0,
			 	  strdup("regi on non existant dev/path:"));
	np_test_mount_entry_regex(dummy_mount_list, strdup("/c.t0"),
		                  cflags, 3,
			 	  strdup("partial devname regex match:"));
	np_test_mount_entry_regex(dummy_mount_list, strdup("c0t0"),
		                  cflags, 1,
			 	  strdup("partial devname regex match:"));
	np_test_mount_entry_regex(dummy_mount_list, strdup("C0t0"),
		                  cflags | REG_ICASE, 1,
			 	  strdup("partial devname regi match:"));
	np_test_mount_entry_regex(dummy_mount_list, strdup("home"),
		                  cflags, 1,
			 	  strdup("partial pathname regex match:"));
	np_test_mount_entry_regex(dummy_mount_list, strdup("hOme"),
		                  cflags | REG_ICASE, 1,
			 	  strdup("partial pathname regi match:"));
	np_test_mount_entry_regex(dummy_mount_list, strdup("(/home)|(/var)"),
		                  cflags, 2,
			 	  strdup("grouped regex pathname match:"));
	np_test_mount_entry_regex(dummy_mount_list, strdup("(/homE)|(/Var)"),
		                  cflags | REG_ICASE, 2,
			 	  strdup("grouped regi pathname match:"));

	np_add_parameter(&paths, "/home/groups");
	np_add_parameter(&paths, "/var");
	np_add_parameter(&paths, "/tmp");
	np_add_parameter(&paths, "/home/tonvoon");
	np_add_parameter(&paths, "/dev/c2t0d0s0");

	np_set_best_match(paths, dummy_mount_list, FALSE);
	for (p = paths; p; p = p->name_next) {
		struct mount_entry *temp_me;
		temp_me = p->best_match;
		if (! strcmp(p->name, "/home/groups")) {
			ok( temp_me && !strcmp(temp_me->me_mountdir, "/home"), "/home/groups got right best match: /home");
		} else if (! strcmp(p->name, "/var")) {
			ok( temp_me && !strcmp(temp_me->me_mountdir, "/var"), "/var got right best match: /var");
		} else if (! strcmp(p->name, "/tmp")) {
			ok( temp_me && !strcmp(temp_me->me_mountdir, "/"), "/tmp got right best match: /");
		} else if (! strcmp(p->name, "/home/tonvoon")) {
			ok( temp_me && !strcmp(temp_me->me_mountdir, "/home"), "/home/tonvoon got right best match: /home");
		} else if (! strcmp(p->name, "/dev/c2t0d0s0")) {
			ok( temp_me && !strcmp(temp_me->me_devname, "/dev/c2t0d0s0"), "/dev/c2t0d0s0 got right best match: /dev/c2t0d0s0");
		}
	}

	paths = NULL;	/* Bad boy - should free, but this is a test suite */
	np_add_parameter(&paths, "/home/groups");
	np_add_parameter(&paths, "/var");
	np_add_parameter(&paths, "/tmp");
	np_add_parameter(&paths, "/home/tonvoon");
	np_add_parameter(&paths, "/home");

	np_set_best_match(paths, dummy_mount_list, TRUE);
	for (p = paths; p; p = p->name_next) {
		if (! strcmp(p->name, "/home/groups")) {
			ok( ! p->best_match , "/home/groups correctly not found");
		} else if (! strcmp(p->name, "/var")) {
			ok( p->best_match, "/var found");
		} else if (! strcmp(p->name, "/tmp")) {
			ok(! p->best_match, "/tmp correctly not found");
		} else if (! strcmp(p->name, "/home/tonvoon")) {
			ok(! p->best_match, "/home/tonvoon not found");
		} else if (! strcmp(p->name, "/home")) {
			ok( p->best_match, "/home found");
		}
	}

	/* test deleting first element in paths */
	paths = np_del_parameter(paths, NULL);
	for (p = paths; p; p = p->name_next) {
		if (! strcmp(p->name, "/home/groups"))
			found = 1;
	}
	ok(found == 0, "first element successfully deleted");
	found = 0;
	
	p=paths;
	while (p) {
		if (! strcmp(p->name, "/tmp"))
			p = np_del_parameter(p, prev);
		else {
			prev = p;
			p = p->name_next;
		}
	}

	for (p = paths; p; p = p->name_next) {
		if (! strcmp(p->name, "/tmp"))
			found = 1;
		if (p->name_next)
			prev = p;
		else
			last = p;
	}
	ok(found == 0, "/tmp element successfully deleted");

	p = np_del_parameter(last, prev);
	for (p = paths; p; p = p->name_next) {
		if (! strcmp(p->name, "/home"))
			found = 1;
		last = p;
		count++;
	}
	ok(found == 0, "last (/home) element successfully deleted");
	ok(count == 2, "two elements remaining");


	return exit_status();
}


void 
np_test_mount_entry_regex (struct mount_entry *dummy_mount_list, char *regstr, int cflags, int expect, char *desc)
{	
	int matches = 0;
	regex_t re;
	struct mount_entry *me;
	if (regcomp(&re,regstr, cflags) == 0) {
		for (me = dummy_mount_list; me; me= me->me_next) {
			if(np_regex_match_mount_entry(me,&re))
				matches++;
		}
		ok( matches == expect, 
	    	    "%s '%s' matched %i/3 entries. ok: %i/3",
		    desc, regstr, expect, matches);

	} else
		ok ( false, "regex '%s' not compilable", regstr);
}

