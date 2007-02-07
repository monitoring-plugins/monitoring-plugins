#include "common.h"
#include "parse_ini.h"
#include "utils_base.h"
#include <ctype.h>

/* np_ini_info contains the result of parsing a "locator" in the format
 * [stanza_name][@config_filename] (check_foo@/etc/foo.ini, for example)
 */
typedef struct {
	char *file;
	char *stanza;
} np_ini_info;

/* eat all characters from a FILE pointer until n is encountered */
#define GOBBLE_TO(f, c, n) do { (c)=fgetc((f)); } while((c)!=EOF && (c)!=(n))

/* internal function that returns the constructed defaults options */
static char* read_defaults(FILE *f, const char *stanza);
/* internal function that converts a single line into options format */
static int add_option(FILE *f, char **optbuf, size_t *bufsize);

/* parse_locator decomposes a string of the form
 * 	[stanza][@filename]
 * into its seperate parts
 */
static void parse_locator(const char *locator, const char *def_stanza, np_ini_info *i){
	size_t locator_len, stanza_len;

	locator_len=strlen(locator);
	stanza_len=strcspn(locator, "@");
	/* if a non-default stanza is provided */
	if(stanza_len>0){
		i->stanza=(char*)malloc(sizeof(char)*(stanza_len+1));
		strncpy(i->stanza, locator, stanza_len);
		i->stanza[stanza_len]='\0';
	} else { /* otherwise we use the default stanza */
		i->stanza=strdup(def_stanza);
	}
	/* if there is no @file part */
	if(stanza_len==locator_len){
		i->file=strdup(NP_DEFAULT_INI_PATH);
	} else {
		i->file=strdup(&(locator[stanza_len+1]));
	}
	
	if(i->file==NULL || i->stanza==NULL){
		die(STATE_UNKNOWN, _("malloc() failed!\n"));
	}
}

/* this is the externally visible function used by plugins */
char* np_get_defaults(const char *locator, const char *default_section){
	FILE *inifile=NULL;
	char *defaults=NULL;
	np_ini_info i;

	parse_locator(locator, default_section, &i);
	/* if a file was specified or if we're using the default file */
	if(i.file != NULL && strlen(i.file) > 0){
		if(strcmp(i.file, "-")==0){
			inifile=stdout;
		} else {
			inifile=fopen(i.file, "r");
		}
		if(inifile==NULL) die(STATE_UNKNOWN, _("Config file error"));
		defaults=read_defaults(inifile, i.stanza);
		free(i.file);
		if(inifile!=stdout) fclose(inifile);
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
static char* read_defaults(FILE *f, const char *stanza){
	int c;
	char *opts=NULL;
	size_t i, stanza_len, opts_buf_size=0;
	enum { NOSTANZA, WRONGSTANZA, RIGHTSTANZA } stanzastate=NOSTANZA;

	stanza_len=strlen(stanza);

	/* our little stanza-parsing state machine.  */
	while((c=fgetc(f))!=EOF){
		/* gobble up leading whitespace */
		if(isspace(c)) continue;
		switch(c){
			/* globble up coment lines */
			case '#':
				GOBBLE_TO(f, c, '\n');
				break;
			/* start of a stanza.  check to see if it matches */
			case '[':
				stanzastate=WRONGSTANZA;
				for(i=0; i<stanza_len; i++){
					c=fgetc(f);
					/* nope, read to the end of the stanza header */
					if(c!=stanza[i]) {
						GOBBLE_TO(f, c, ']');
						break;
					}
				}
				/* if it matched up to here and the next char is ']'... */
				if(i==stanza_len){
					c=fgetc(f);
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
						die(STATE_UNKNOWN, _("Config file error"));
						break;
					/* we're in a stanza, but for a different plugin */
					case WRONGSTANZA:
						GOBBLE_TO(f, c, '\n');
						break;
					/* okay, this is where we start taking the config */
					case RIGHTSTANZA:
						ungetc(c, f);
						if(add_option(f, &opts, &opts_buf_size)){
							die(STATE_UNKNOWN, _("Config file error"));
						}
						break;
				}
				break;
		}
	}
	return opts;
}

/*
 * read one line of input in the format
 * 	^option[[:space:]]*(=[[:space:]]*value)?
 * and creates it as a cmdline argument
 * 	--option[=value]
 * appending it to the string pointed to by optbuf (which will
 * be dynamically grown if needed)
 */
static int add_option(FILE *f, char **optbuf, size_t *bufsize){
	char *newbuf=*optbuf;
	char *linebuf=NULL, *lineend=NULL, *optptr=NULL, *optend=NULL;
	char *eqptr=NULL, *valptr=NULL, *spaceptr=NULL, *valend=NULL;
	short done_reading=0, equals=0, value=0;
	size_t cfg_len=0, read_sz=8, linebuf_sz=0, read_pos=0, bs=*bufsize;
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
	if(optptr==eqptr) die(STATE_UNKNOWN, _("Config file error\n"));
	/* continue from '=' to start of value or EOL */
	for(valptr=eqptr+1; valptr<lineend && isspace(*valptr); valptr++);
	/* continue to the end of value, watching for trailing space/comments */
	for(valend=valptr; valend<lineend; valend++){
		if(isspace(*valend) && spaceptr==NULL) spaceptr=valend;
		else if(*valend=='#') break;
		else spaceptr=NULL;
	}
	if(spaceptr!=NULL) valend=spaceptr;
	--valend;
	/* calculate the length of "--foo" */
	opt_len=1+optend-optptr;
	cfg_len=2+(opt_len);
	/* if valptr<lineend then we have to also allocate space for "=bar" */
	if(valptr<lineend) {
		equals=value=1;
		val_len=1+valend-valptr;
		cfg_len+=1+val_len;
	}
	/* if valptr==valend then we have "=" but no "bar" */
	else if (valptr==lineend) {
		equals=1;
		cfg_len+=1;
	}

	/* okay, now we have all the info we need, so we grow the default opts
	 * buffer if it's necessary, and put everything together.
	 * (+2 is for a potential space and a null byte)
	 */
	read_pos=(newbuf==NULL)?0:strlen(newbuf);
	if(newbuf==NULL || read_pos+cfg_len+2 >= bs){
		bs=(bs>0)?(bs+cfg_len+2)<<1:cfg_len+1;
		newbuf=realloc(newbuf, bs);
		if(newbuf==NULL) die(STATE_UNKNOWN, _("malloc() failed!\n"));
	}
	if(read_pos>0) newbuf[read_pos++]=' ';
	strncpy(&newbuf[read_pos], "--", 2); read_pos+=2;
	strncpy(&newbuf[read_pos], optptr, opt_len); read_pos+=opt_len;
	if(equals) newbuf[read_pos++]='=';
	if(value) {
		strncpy(&newbuf[read_pos], valptr, val_len); read_pos+=val_len;
	}
	newbuf[read_pos]='\0';

	*optbuf=newbuf;
	*bufsize=bs;

	free(linebuf);
	return 0;
}

