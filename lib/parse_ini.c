/*****************************************************************************
* 
* Monitoring Plugins parse_ini library
* 
* License: GPL
* Copyright (c) 2007 Monitoring Plugins Development Team
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
#include <ctype.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/* TODO: die like N::P if config file is not found */

/* np_ini_info contains the result of parsing a "locator" in the format
 * [stanza_name][@config_filename] (check_foo@/etc/foo.ini, for example)
 */
typedef struct {
	char *file;
	char *stanza;
} np_ini_info;

static char *default_ini_file_names[] = {
	"monitoring-plugins.ini",
	"plugins.ini",
	"nagios-plugins.ini",
	NULL
};

static char *default_ini_path_names[] = {
	"/usr/local/etc/monitoring-plugins.ini",
	"/etc/monitoring-plugins.ini",
	/* Deprecated path names (for backward compatibility): */
	"/etc/nagios/plugins.ini",
	"/usr/local/nagios/etc/plugins.ini",
	"/usr/local/etc/nagios/plugins.ini",
	"/etc/opt/nagios/plugins.ini",
	"/etc/nagios-plugins.ini",
	"/usr/local/etc/nagios-plugins.ini",
	"/etc/opt/nagios-plugins.ini",
	NULL
};

/* eat all characters from a FILE pointer until n is encountered */
#define GOBBLE_TO(f, c, n) do { (c)=fgetc((f)); } while((c)!=EOF && (c)!=(n))

/* internal function that returns the constructed defaults options */
static int read_defaults(FILE *f, const char *stanza, np_arg_list **opts);
/* internal function that converts a single line into options format */
static int add_option(FILE *f, np_arg_list **optlst);
/* internal function to find default file */
static char* default_file(void);

/* parse_locator decomposes a string of the form
 * 	[stanza][@filename]
 * into its seperate parts
 */
static void parse_locator(const char *locator, const char *def_stanza, np_ini_info *i){
	size_t locator_len=0, stanza_len=0;

	/* if locator is NULL we'll use default values */
	if(locator){
		locator_len=strlen(locator);
		stanza_len=strcspn(locator, "@");
	}
	/* if a non-default stanza is provided */
	if(stanza_len>0){
		i->stanza=(char*)malloc(sizeof(char)*(stanza_len+1));
		strncpy(i->stanza, locator, stanza_len);
		i->stanza[stanza_len]='\0';
	} else { /* otherwise we use the default stanza */
		i->stanza=strdup(def_stanza);
	}
	if(i->stanza==NULL){
		die(STATE_UNKNOWN, _("malloc() failed!\n"));
	}
	/* if there is no @file part */
	if(stanza_len==locator_len){
		i->file=default_file();
	} else {
		i->file=strdup(&(locator[stanza_len+1]));
	}
	if(i->file==NULL || i->file[0]=='\0'){
		die(STATE_UNKNOWN, _("Cannot find config file in any standard location.\n"));
	}
}

/* this is the externally visible function used by extra_opts */
np_arg_list* np_get_defaults(const char *locator, const char *default_section){
	FILE *inifile=NULL;
	np_arg_list *defaults=NULL;
	np_ini_info i;

	parse_locator(locator, default_section, &i);
	/* if a file was specified or if we're using the default file */
	if(i.file != NULL && strlen(i.file) > 0){
		if(strcmp(i.file, "-")==0){
			inifile=stdin;
		} else {
			inifile=fopen(i.file, "r");
		}
		if(inifile==NULL) die(STATE_UNKNOWN, "%s\n", _("Can't read config file"));
		if(read_defaults(inifile, i.stanza, &defaults)==FALSE)
			die(STATE_UNKNOWN, _("Invalid section '%s' in config file '%s'\n"), i.stanza, i.file);

		free(i.file);
		if(inifile!=stdin) fclose(inifile);
	}
	free(i.stanza);
	return defaults;
}

/* read_defaults is where the meat of the parsing takes place.
 *
 * note that this may be called by a setuid binary, so we need to
 * be extra careful about user-supplied input (i.e. avoiding possible
 * format string vulnerabilities, etc)
 */
static int read_defaults(FILE *f, const char *stanza, np_arg_list **opts){
	int c, status=FALSE;
	size_t i, stanza_len;
	enum { NOSTANZA, WRONGSTANZA, RIGHTSTANZA } stanzastate=NOSTANZA;

	stanza_len=strlen(stanza);

	/* our little stanza-parsing state machine.  */
	while((c=fgetc(f))!=EOF){
		/* gobble up leading whitespace */
		if(isspace(c)) continue;
		switch(c){
			/* globble up coment lines */
			case ';':
			case '#':
				GOBBLE_TO(f, c, '\n');
				break;
			/* start of a stanza.  check to see if it matches */
			case '[':
				stanzastate=WRONGSTANZA;
				for(i=0; i<stanza_len; i++){
					c=fgetc(f);
					/* Strip leading whitespace */
					if(i==0) for(c; isspace(c); c=fgetc(f));
					/* nope, read to the end of the line */
					if(c!=stanza[i]) {
						GOBBLE_TO(f, c, '\n');
						break;
					}
				}
				/* if it matched up to here and the next char is ']'... */
				if(i==stanza_len){
					c=fgetc(f);
					/* Strip trailing whitespace */
					for(c; isspace(c); c=fgetc(f));
					if(c==']') stanzastate=RIGHTSTANZA;
				}
				break;
			/* otherwise, we're in the body of a stanza or a parse error */
			default:
				switch(stanzastate){
					/* we never found the start of the first stanza, so
					 * we're dealing with a config error
					 */
					case NOSTANZA:
						die(STATE_UNKNOWN, "%s\n", _("Config file error"));
						break;
					/* we're in a stanza, but for a different plugin */
					case WRONGSTANZA:
						GOBBLE_TO(f, c, '\n');
						break;
					/* okay, this is where we start taking the config */
					case RIGHTSTANZA:
						ungetc(c, f);
						if(add_option(f, opts)){
							die(STATE_UNKNOWN, "%s\n", _("Config file error"));
						}
						status=TRUE;
						break;
				}
				break;
		}
	}
	return status;
}

/*
 * read one line of input in the format
 * 	^option[[:space:]]*(=[[:space:]]*value)?
 * and creates it as a cmdline argument
 * 	--option[=value]
 * appending it to the linked list optbuf.
 */
static int add_option(FILE *f, np_arg_list **optlst){
	np_arg_list *opttmp=*optlst, *optnew;
	char *linebuf=NULL, *lineend=NULL, *optptr=NULL, *optend=NULL;
	char *eqptr=NULL, *valptr=NULL, *spaceptr=NULL, *valend=NULL;
	short done_reading=0, equals=0, value=0;
	size_t cfg_len=0, read_sz=8, linebuf_sz=0, read_pos=0;
	size_t opt_len=0, val_len=0;

	/* read one line from the file */
	while(!done_reading){
		/* grow if necessary */
		if(linebuf==NULL || read_pos+read_sz >= linebuf_sz){
			linebuf_sz=(linebuf_sz>0)?linebuf_sz<<1:read_sz;
			linebuf=realloc(linebuf, linebuf_sz);
			if(linebuf==NULL) die(STATE_UNKNOWN, _("malloc() failed!\n"));
		}
		if(fgets(&linebuf[read_pos], read_sz, f)==NULL) done_reading=1;
		else {
			read_pos=strlen(linebuf);
			if(linebuf[read_pos-1]=='\n') {
				linebuf[--read_pos]='\0';
				done_reading=1;
			}
		}
	}
	lineend=&linebuf[read_pos];
	/* all that to read one line.  isn't C fun? :) now comes the parsing :/ */

	/* skip leading whitespace */
	for(optptr=linebuf; optptr<lineend && isspace(*optptr); optptr++);
	/* continue to '=' or EOL, watching for spaces that might precede it */
	for(eqptr=optptr; eqptr<lineend && *eqptr!='='; eqptr++){
		if(isspace(*eqptr) && optend==NULL) optend=eqptr;
		else optend=NULL;
	}
	if(optend==NULL) optend=eqptr;
	--optend;
	/* ^[[:space:]]*=foo is a syntax error */
	if(optptr==eqptr) die(STATE_UNKNOWN, "%s\n", _("Config file error"));
	/* continue from '=' to start of value or EOL */
	for(valptr=eqptr+1; valptr<lineend && isspace(*valptr); valptr++);
	/* continue to the end of value */
	for(valend=valptr; valend<lineend; valend++);
	--valend;
	/* Finally trim off trailing spaces */
	for(valend; isspace(*valend); valend--);
	/* calculate the length of "--foo" */
	opt_len=1+optend-optptr;
	/* 1-character params needs only one dash */
	if(opt_len==1)
		cfg_len=1+(opt_len);
	else
		cfg_len=2+(opt_len);
	/* if valptr<lineend then we have to also allocate space for "=bar" */
	if(valptr<lineend) {
		equals=value=1;
		val_len=1+valend-valptr;
		cfg_len+=1+val_len;
	}
	/* if valptr==valend then we have "=" but no "bar" */
	else if(valptr==lineend) {
		equals=1;
		cfg_len+=1;
	}
	/* A line with no equal sign isn't valid */
	if(equals==0) die(STATE_UNKNOWN, "%s\n", _("Config file error"));

	/* okay, now we have all the info we need, so we create a new np_arg_list
	 * element and set the argument...
	 */
	optnew=(np_arg_list *)malloc(sizeof(np_arg_list));
	optnew->next=NULL;

	read_pos=0;
	optnew->arg=(char *)malloc(cfg_len+1);
	/* 1-character params needs only one dash */
	if(opt_len==1) {
		strncpy(&optnew->arg[read_pos], "-", 1);
		read_pos+=1;
	} else {
		strncpy(&optnew->arg[read_pos], "--", 2);
		read_pos+=2;
	}
	strncpy(&optnew->arg[read_pos], optptr, opt_len); read_pos+=opt_len;
	if(value) {
		optnew->arg[read_pos++]='=';
		strncpy(&optnew->arg[read_pos], valptr, val_len); read_pos+=val_len;
	}
	optnew->arg[read_pos]='\0';

	/* ...and put that to the end of the list */
	if(*optlst==NULL) {
		*optlst=optnew;
	}	else {
		while(opttmp->next!=NULL) {
			opttmp=opttmp->next;
		}
		opttmp->next = optnew;
	}

	free(linebuf);
	return 0;
}

static char *default_file_in_path(void){
	char *config_path, **file;
	char *dir, *ini_file, *tokens;

	if((config_path=getenv("NAGIOS_CONFIG_PATH"))==NULL)
		return NULL;

	if((tokens=strdup(config_path))==NULL)
		die(STATE_UNKNOWN, _("Insufficient Memory"));
	for(dir=strtok(tokens, ":"); dir!=NULL; dir=strtok(NULL, ":")){
		for(file=default_ini_file_names; *file!=NULL; file++){
			if((asprintf(&ini_file, "%s/%s", dir, *file))<0)
				die(STATE_UNKNOWN, _("Insufficient Memory"));
			if(access(ini_file, F_OK)==0){
				free(tokens);
				return ini_file;
			}
		}
	}
	free(tokens);
	return NULL;
}

static char *default_file(void){
	char **p, *ini_file;

	if((ini_file=getenv("MP_CONFIG_FILE"))!=NULL ||
	    (ini_file=default_file_in_path())!=NULL)
		return ini_file;
	for(p=default_ini_path_names; *p!=NULL; p++)
		if (access(*p, F_OK)==0)
			return *p;
	return NULL;
}
