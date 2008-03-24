/*****************************************************************************
* 
* Nagios-plugins extra_opts library
* 
* License: GPL
* Copyright (c) 2007 Nagios Plugins Development Team
* 
* Last Modified: $Date: 2008-03-15 18:42:01 -0400 (Sat, 15 Mar 2008) $
* 
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
* $Id: parse_ini.c 1950 2008-03-15 22:42:01Z dermoth $
* 
*****************************************************************************/

#include "common.h"
#include "extra_opts.h"
#include "parse_ini.h"
#include "utils_base.h"
#include <ctype.h>

/* FIXME: copied from utils.h; we should move a bunch of libs! */
int
is_option (char *str)
{
	if (!str)
		return 0;
	else if (strspn (str, "-") == 1 || strspn (str, "-") == 2)
		return 1;
	else
		return 0;
}

/* this is the externally visible function used by plugins */
/* Shouldn't se modify directly **argv (passed as a char ***) and argc
 * (as int *) ?
 */
char **np_extra_opts(int argc, char **argv, const char *plugin_name, int *argc_new){
	np_arg_list *extra_args=NULL, *ea_tmp1=NULL, *ea_tmp2=NULL;
	char **argv_new=NULL;
	char *argptr=NULL;
	int i, j, optfound, ea_num=argc;

	if(argc<2) {
		/* No arguments provided */
		*argc_new=argc;
		argv_new=argv;
		return argv_new;
	}

	for(i=1; i<argc; i++){
		argptr=NULL;
		optfound=0;

		/* Do we have an extra-opts parameter? */
		if(strncmp(argv[i], "--extra-opts=", 13)==0){
			/* It is a single argument with value */
			argptr=argv[i]+13;
			/* Delete the extra opts argument */
			for(j=i;j<argc;j++) argv[j]=argv[j+1];
			i--;
			argc--;
		}else if(strcmp(argv[i], "--extra-opts")==0){
			if(!is_option(argv[i+1])){
				/* It is a argument with separate value */
				argptr=argv[i+1];
				/* Delete the extra-opts argument/value */
				for(j=i;j<argc-1;j++) argv[j]=argv[j+2];
				i-=2;
				argc-=2;
				ea_num--;
			}else{
				/* It has no value */
				optfound=1;
				/* Delete the extra opts argument */
				for(j=i;j<argc;j++) argv[j]=argv[j+1];
				i--;
				argc--;
			}
		}

		if(argptr||optfound){
			/* Process ini section, returning a linked list of arguments */
			ea_tmp1=np_get_defaults(argptr, plugin_name);
			if(ea_tmp1==NULL) {
				/* no extra args? */
				ea_num--;
				continue;
			}

			/* append the list to extra_args */
			if(extra_args==NULL){
				extra_args=ea_tmp2=ea_tmp1;
				while(ea_tmp2->next) {
					ea_tmp2=ea_tmp2->next;
					ea_num++;
				}
			}else{
				ea_tmp2=extra_args;
				while(ea_tmp2->next) {
					ea_tmp2=ea_tmp2->next;
					ea_num++;
				}
				ea_tmp2->next=ea_tmp1;
			}
			ea_tmp1=ea_tmp2=NULL;
		}
		/* lather, rince, repeat */
	}

	if(ea_num==argc && extra_args==NULL){
		/* No extra-opts */
		*argc_new=argc;
		argv_new=argv;
		return argv_new;
	}

	/* done processing arguments. now create a new argc/argv set... */
	argv_new=(char**)malloc((ea_num+1)*sizeof(char**));
	if(argv_new==NULL) die(STATE_UNKNOWN, _("malloc() failed!\n"));

	/* starting with program name (Should we strdup or just use the poiter?) */
	argv_new[0]=strdup(argv[0]);
	*argc_new=1;
	/* then parsed ini opts (frying them up in the same run) */
	while(extra_args){
		argv_new[*argc_new]=strdup(extra_args->arg);
		*argc_new+=1;
		ea_tmp1=extra_args;
		extra_args=extra_args->next;
		free(ea_tmp1);
	}
	/* finally the rest of the argv array (Should we strdup or just use the poiter?) */
	for (i=1; i<argc; i++){
		argv_new[*argc_new]=strdup(argv[i]);
		*argc_new+=1;
	}
	/* and terminate. */
	argv_new[*argc_new]=NULL;

	return argv_new;
}

