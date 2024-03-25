/*****************************************************************************
*
* utils_base.c
*
* License: GPL
* Copyright (c) 2006 Monitoring Plugins Development Team
*
* Library of useful functions for plugins
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
*
*****************************************************************************/

#include "../plugins/common.h"
#include "../plugins/utils.h"
#include <stdarg.h>
#include "./utils_base.h"
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>

#include <string.h>
#include <stdlib.h>

#define np_free(ptr) { if(ptr) { free(ptr); ptr = NULL; } }

monitoring_plugin *this_monitoring_plugin=NULL;

int timeout_state = STATE_CRITICAL;
unsigned int timeout_interval = DEFAULT_SOCKET_TIMEOUT;

bool _np_state_read_file(FILE *);

void np_init( char *plugin_name, int argc, char **argv ) {
	if (this_monitoring_plugin==NULL) {
		this_monitoring_plugin = (monitoring_plugin *)calloc(1, sizeof(monitoring_plugin));
		if (this_monitoring_plugin==NULL) {
			die(STATE_UNKNOWN, _("Cannot allocate memory: %s"),
			    strerror(errno));
		}
		this_monitoring_plugin->plugin_name = strdup(plugin_name);
		if (this_monitoring_plugin->plugin_name==NULL)
			die(STATE_UNKNOWN, _("Cannot execute strdup: %s"), strerror(errno));
		this_monitoring_plugin->argc = argc;
		this_monitoring_plugin->argv = argv;
	}
}

void np_set_args( int argc, char **argv ) {
	if (this_monitoring_plugin==NULL)
		die(STATE_UNKNOWN, _("This requires np_init to be called"));

	this_monitoring_plugin->argc = argc;
	this_monitoring_plugin->argv = argv;
}


void np_cleanup() {
	if (this_monitoring_plugin!=NULL) {
		if(this_monitoring_plugin->state!=NULL) {
			if(this_monitoring_plugin->state->state_data) {
				np_free(this_monitoring_plugin->state->state_data->data);
				np_free(this_monitoring_plugin->state->state_data);
			}
			np_free(this_monitoring_plugin->state->name);
			np_free(this_monitoring_plugin->state);
		}
		np_free(this_monitoring_plugin->plugin_name);
		np_free(this_monitoring_plugin);
	}
	this_monitoring_plugin=NULL;
}

/* Hidden function to get a pointer to this_monitoring_plugin for testing */
void _get_monitoring_plugin( monitoring_plugin **pointer ){
	*pointer = this_monitoring_plugin;
}

void
die (int result, const char *fmt, ...)
{
	if(fmt!=NULL) {
		va_list ap;
		va_start (ap, fmt);
		vprintf (fmt, ap);
		va_end (ap);
	}

	if(this_monitoring_plugin!=NULL) {
		np_cleanup();
	}
	exit (result);
}

#define mp_set_range_start(R, V) \
	_Generic(V, \
	double: set_range_start_double \
	long long: set_range_start_int \
	) (R, V)

#define mp_set_range_end(R, V) \
	_Generic(V, \
	double: set_range_end_double \
	long long: set_range_end_int \
	) (R, V)

mp_range set_range_start_int (mp_range r, long long value) {
	r.start.pd_int = value;
	r.start.type = PD_TYPE_INT;
	r.start_infinity = false;
	return r;
}

mp_range set_range_start_double (mp_range r, double value) {
	r.start.pd_double = value;
	r.start.type = PD_TYPE_DOUBLE;
	r.start_infinity = false;
	return r;
}

mp_range set_range_end_int (mp_range r, long long value) {
	r.end.pd_int = value;
	r.end.type = PD_TYPE_INT;
	r.end_infinity = false;
	return r;
}

mp_range set_range_end_double (mp_range r, double value) {
	r.end.pd_double = value;
	r.end.type = PD_TYPE_DOUBLE;
	r.end_infinity = false;
	return r;
}

void set_range_start (range *this, double value) {
	this->start = value;
	this->start_infinity = false;
}

void set_range_end (range *this, double value) {
	this->end = value;
	this->end_infinity = false;
}

range *parse_range_string (char *str) {
	range *temp_range;
	double start;
	double end;
	char *end_str;

	temp_range = (range *) calloc(1, sizeof(range));

	/* Set defaults */
	temp_range->start = 0;
	temp_range->start_infinity = false;
	temp_range->end = 0;
	temp_range->end_infinity = true;
	temp_range->alert_on = OUTSIDE;
	temp_range->text = strdup(str);

	if (str[0] == '@') {
		temp_range->alert_on = INSIDE;
		str++;
	}

	end_str = index(str, ':');
	if (end_str != NULL) {
		if (str[0] == '~') {
			temp_range->start_infinity = true;
		} else {
			start = strtod(str, NULL);      /* Will stop at the ':' */
			set_range_start(temp_range, start);
		}
		end_str++;              /* Move past the ':' */
	} else {
		end_str = str;
	}
	end = strtod(end_str, NULL);
	if (strcmp(end_str, "") != 0) {
		set_range_end(temp_range, end);
	}

	if (temp_range->start_infinity == true ||
			temp_range->end_infinity == true ||
			temp_range->start <= temp_range->end) {
		return temp_range;
	}
	free(temp_range);
	return NULL;
}

mp_range parse_mp_range_string (char *str) {
	mp_range temp_range = { 0 };
	char *end_str;


	/* Set defaults */
	temp_range.start.pd_int = 0;
	temp_range.start_infinity = false;
	temp_range.end.pd_int = 0;
	temp_range.end_infinity = true;
	temp_range.alert_on = OUTSIDE;

	if (str[0] == '@') {
		temp_range.alert_on = INSIDE;
		str++;
	}

	end_str = index(str, ':');
	mp_perfdata_value tmp;

	if (end_str != NULL) {
		if (str[0] == '~') {
			temp_range.start_infinity = true;
		} else {
			if (is_integer(str)) {
				tmp.pd_int = strtoll(str, NULL, 0);
				set_range_start_int(temp_range, tmp.pd_int);
				tmp.type = PD_TYPE_INT;
			} else {
				set_range_start_double(temp_range, strtod(str, NULL));
				tmp.type = PD_TYPE_DOUBLE;
			}
		}
		end_str++;		/* Move past the ':' */
	} else {
		end_str = str;
	}

	if (strcmp(end_str, "") != 0) {
		if (is_integer(end_str)) {
				tmp.pd_int = strtoll(str, NULL, 0);
			set_range_end_int(temp_range, tmp.pd_int);
		} else {
			set_range_end_double(temp_range, strtod(end_str, NULL));
		}
	}

	if (temp_range.start_infinity == true ||
		temp_range.end_infinity == true ||
		cmp_perfdata_value(temp_range.start, temp_range.end) != -1) {
		return temp_range;
	}

	return temp_range;
}

/* returns 0 if okay, otherwise error codes */
int
_set_thresholds(thresholds **my_thresholds, char *warn_string, char *critical_string)
{
	thresholds *temp_thresholds = NULL;

	if ((temp_thresholds = calloc(1, sizeof(thresholds))) == NULL)
		die(STATE_UNKNOWN, _("Cannot allocate memory: %s"),
		    strerror(errno));

	temp_thresholds->warning = NULL;
	temp_thresholds->critical = NULL;

	if (warn_string != NULL) {
		if ((temp_thresholds->warning = parse_range_string(warn_string)) == NULL) {
			return NP_RANGE_UNPARSEABLE;
		}
	}
	if (critical_string != NULL) {
		if ((temp_thresholds->critical = parse_range_string(critical_string)) == NULL) {
			return NP_RANGE_UNPARSEABLE;
		}
	}

	*my_thresholds = temp_thresholds;

	return 0;
}

void
set_thresholds(thresholds **my_thresholds, char *warn_string, char *critical_string)
{
	switch (_set_thresholds(my_thresholds, warn_string, critical_string)) {
	case 0:
		return;
	case NP_RANGE_UNPARSEABLE:
		die(STATE_UNKNOWN, _("Range format incorrect"));
	case NP_WARN_WITHIN_CRIT:
		die(STATE_UNKNOWN, _("Warning level is a subset of critical and will not be alerted"));
		break;
	}
}

void print_thresholds(const char *threshold_name, thresholds *my_threshold) {
	printf("%s - ", threshold_name);
	if (! my_threshold) {
		printf("Threshold not set");
	} else {
		if (my_threshold->warning) {
			printf("Warning: start=%g end=%g; ", my_threshold->warning->start, my_threshold->warning->end);
		} else {
			printf("Warning not set; ");
		}
		if (my_threshold->critical) {
			printf("Critical: start=%g end=%g", my_threshold->critical->start, my_threshold->critical->end);
		} else {
			printf("Critical not set");
		}
	}
	printf("\n");
}

void mp_print_thresholds(const char *threshold_name, mp_thresholds *my_threshold) {
	printf("%s - ", threshold_name);
	if (! my_threshold) {
		printf("Threshold not set");
	} else {
		if (my_threshold->warning) {
			printf("Warning: start=%s end=%s; ",
					pd_value_to_string(my_threshold->warning->start),
					pd_value_to_string(my_threshold->warning->end)
				  );
		} else {
			printf("Warning not set; ");
		}

		if (my_threshold->critical) {
			printf("Critical: start=%s end=%s; ",
					pd_value_to_string(my_threshold->critical->start),
					pd_value_to_string(my_threshold->critical->end)
				  );
		} else {
			printf("Critical not set");
		}
	}
	printf("\n");
}

bool check_range(double value, range *my_range) {
	bool no = false;
	bool yes = true;

	if (my_range->alert_on == INSIDE) {
		no = true;
		yes = false;
	}

	if (my_range->end_infinity == false && my_range->start_infinity == false) {
		if ((my_range->start <= value) && (value <= my_range->end)) {
			return no;
		} else {
			return yes;
		}
	} else if (my_range->start_infinity == false && my_range->end_infinity == true) {
		if (my_range->start <= value) {
			return no;
		} else {
			return yes;
		}
	} else if (my_range->start_infinity == true && my_range->end_infinity == false) {
		if (value <= my_range->end) {
			return no;
		} else {
			return yes;
		}
	} else {
		return no;
	}
}

/* Returns true if alert should be raised based on the range, false otherwise */
bool mp_check_range(mp_perfdata_value value, mp_range *my_range) {
	bool is_inside = false;

	if (my_range->end_infinity == false && my_range->start_infinity == false) {
		// range:  .........|---inside---|...........
		// value
		if (
			(cmp_perfdata_value(my_range->start, value) < 1) &&
			(cmp_perfdata_value(value, my_range->end) <= 0)) {
			is_inside = true;
		} else {
			is_inside = false;
		}
	} else if (my_range->start_infinity == false && my_range->end_infinity == true) {
		// range:  .........|---inside---------
		// value
		if (cmp_perfdata_value(my_range->start, value) < 0) {
			is_inside = true;
		} else {
			is_inside = false;
		}
	} else if (my_range->start_infinity == true && my_range->end_infinity == false) {
		// range:  -inside--------|....................
		// value
		if (cmp_perfdata_value(value, my_range->end) == -1) {
			is_inside = true;
		} else {
			is_inside = false;
		}
	} else {
		// range from -inf to inf, so always inside
		is_inside = true;
	}

	if ( (is_inside && my_range->alert_on == INSIDE) ||
	   (!is_inside && my_range->alert_on == OUTSIDE) ) {
		   return true;
	   }

	return false;
}

/* Returns status */
int get_status(double value, thresholds *my_thresholds) {
	if (my_thresholds->critical != NULL) {
		if (check_range(value, my_thresholds->critical) == true) {
			return STATE_CRITICAL;
		}
	}
	if (my_thresholds->warning != NULL) {
		if (check_range(value, my_thresholds->warning) == true) {
			return STATE_WARNING;
		}
	}
	return STATE_OK;
}

int get_status2(mp_perfdata_value value, mp_thresholds *my_thresholds) {
	if (my_thresholds->critical != NULL) {
		if (mp_check_range(value, my_thresholds->critical)) {
			return STATE_CRITICAL;
		}
	}

	if (my_thresholds->warning != NULL) {
		if (mp_check_range(value, my_thresholds->warning)) {
			return STATE_WARNING;
		}
	}
	return STATE_OK;
}

char *np_escaped_string (const char *string) {
	char *data;
	int i, j=0;
	data = strdup(string);
	for (i=0; data[i]; i++) {
		if (data[i] == '\\') {
			switch(data[++i]) {
				case 'n':
					data[j++] = '\n';
					break;
				case 'r':
					data[j++] = '\r';
					break;
				case 't':
					data[j++] = '\t';
					break;
				case '\\':
					data[j++] = '\\';
					break;
				default:
					data[j++] = data[i];
			}
		} else {
			data[j++] = data[i];
		}
	}
	data[j] = '\0';
	return data;
}

int np_check_if_root(void) { return (geteuid() == 0); }

/*
 * Extract the value from key/value pairs, or return NULL. The value returned
 * can be free()ed.
 * This function can be used to parse NTP control packet data and performance
 * data strings.
 */
char *np_extract_value(const char *varlist, const char *name, char sep) {
	char *tmp=NULL, *value=NULL;
	int i;

	while (1) {
		/* Strip any leading space */
		for (; isspace(varlist[0]); varlist++);

		if (strncmp(name, varlist, strlen(name)) == 0) {
			varlist += strlen(name);
			/* strip trailing spaces */
			for (; isspace(varlist[0]); varlist++);

			if (varlist[0] == '=') {
				/* We matched the key, go past the = sign */
				varlist++;
				/* strip leading spaces */
				for (; isspace(varlist[0]); varlist++);

				if ((tmp = index(varlist, sep))) {
					/* Value is delimited by a comma */
					if (tmp-varlist == 0) continue;
					value = (char *)calloc(1, tmp-varlist+1);
					strncpy(value, varlist, tmp-varlist);
					value[tmp-varlist] = '\0';
				} else {
					/* Value is delimited by a \0 */
					if (strlen(varlist) == 0) continue;
					value = (char *)calloc(1, strlen(varlist) + 1);
					strncpy(value, varlist, strlen(varlist));
					value[strlen(varlist)] = '\0';
				}
				break;
			}
		}
		if ((tmp = index(varlist, sep))) {
			/* More keys, keep going... */
			varlist = tmp + 1;
		} else {
			/* We're done */
			break;
		}
	}

	/* Clean-up trailing spaces/newlines */
	if (value) for (i=strlen(value)-1; isspace(value[i]); i--) value[i] = '\0';

	return value;
}

const char *
state_text (int result)
{
	switch (result) {
	case STATE_OK:
		return "OK";
	case STATE_WARNING:
		return "WARNING";
	case STATE_CRITICAL:
		return "CRITICAL";
	case STATE_DEPENDENT:
		return "DEPENDENT";
	default:
		return "UNKNOWN";
	}
}

/*
 * Read a string representing a state (ok, warning... or numeric: 0, 1) and
 * return the corresponding STATE_ value or ERROR)
 */
int mp_translate_state (char *state_text) {
	if (!strcasecmp(state_text,"OK") || !strcmp(state_text,"0"))
		return STATE_OK;
	if (!strcasecmp(state_text,"WARNING") || !strcmp(state_text,"1"))
		return STATE_WARNING;
	if (!strcasecmp(state_text,"CRITICAL") || !strcmp(state_text,"2"))
		return STATE_CRITICAL;
	if (!strcasecmp(state_text,"UNKNOWN") || !strcmp(state_text,"3"))
		return STATE_UNKNOWN;
	return ERROR;
}

/*
 * Returns a string to use as a keyname, based on an md5 hash of argv, thus
 * hopefully a unique key per service/plugin invocation. Use the extra-opts
 * parse of argv, so that uniqueness in parameters are reflected there.
 */
char *_np_state_generate_key() {
	int i;
	char **argv = this_monitoring_plugin->argv;
	char keyname[41];
	char *p=NULL;

	unsigned char result[256];

#ifdef USE_OPENSSL
	/*
	 * This code path is chosen if openssl is available (which should be the most common
	 * scenario). Alternatively, the gnulib implementation/
	 *
	 */
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();

	EVP_DigestInit(ctx, EVP_sha256());

	for(i=0; i<this_monitoring_plugin->argc; i++) {
		EVP_DigestUpdate(ctx, argv[i], strlen(argv[i]));
	}

	EVP_DigestFinal(ctx, result, NULL);
#else

	struct sha256_ctx ctx;

	for(i=0; i<this_monitoring_plugin->argc; i++) {
		sha256_process_bytes(argv[i], strlen(argv[i]), &ctx);
	}

	sha256_finish_ctx(&ctx, result);
#endif // FOUNDOPENSSL

	for (i=0; i<20; ++i) {
		sprintf(&keyname[2*i], "%02x", result[i]);
	}

	keyname[40]='\0';

	p = strdup(keyname);
	if(p==NULL) {
		die(STATE_UNKNOWN, _("Cannot execute strdup: %s"), strerror(errno));
	}
	return p;
}

void _cleanup_state_data() {
	if (this_monitoring_plugin->state->state_data!=NULL) {
		np_free(this_monitoring_plugin->state->state_data->data);
		np_free(this_monitoring_plugin->state->state_data);
	}
}

/*
 * Internal function. Returns either:
 *   envvar NAGIOS_PLUGIN_STATE_DIRECTORY
 *   statically compiled shared state directory
 */
char* _np_state_calculate_location_prefix(){
	char *env_dir;

	/* Do not allow passing MP_STATE_PATH in setuid plugins
	 * for security reasons */
	if (!mp_suid()) {
		env_dir = getenv("MP_STATE_PATH");
		if(env_dir && env_dir[0] != '\0')
			return env_dir;
		/* This is the former ENV, for backward-compatibility */
		env_dir = getenv("NAGIOS_PLUGIN_STATE_DIRECTORY");
		if(env_dir && env_dir[0] != '\0')
			return env_dir;
	}

	return NP_STATE_DIR_PREFIX;
}

/*
 * Initiatializer for state routines.
 * Sets variables. Generates filename. Returns np_state_key. die with
 * UNKNOWN if exception
 */
void np_enable_state(char *keyname, int expected_data_version) {
	state_key *this_state = NULL;
	char *temp_filename = NULL;
	char *temp_keyname = NULL;
	char *p=NULL;
	int ret;

	if(this_monitoring_plugin==NULL)
		die(STATE_UNKNOWN, _("This requires np_init to be called"));

	this_state = (state_key *) calloc(1, sizeof(state_key));
	if(this_state==NULL)
		die(STATE_UNKNOWN, _("Cannot allocate memory: %s"),
		    strerror(errno));

	if(keyname==NULL) {
		temp_keyname = _np_state_generate_key();
	} else {
		temp_keyname = strdup(keyname);
		if(temp_keyname==NULL)
			die(STATE_UNKNOWN, _("Cannot execute strdup: %s"), strerror(errno));
	}
	/* Die if invalid characters used for keyname */
	p = temp_keyname;
	while(*p!='\0') {
		if(! (isalnum(*p) || *p == '_')) {
			die(STATE_UNKNOWN, _("Invalid character for keyname - only alphanumerics or '_'"));
		}
		p++;
	}
	this_state->name=temp_keyname;
	this_state->plugin_name=this_monitoring_plugin->plugin_name;
	this_state->data_version=expected_data_version;
	this_state->state_data=NULL;

	/* Calculate filename */
	ret = asprintf(&temp_filename, "%s/%lu/%s/%s",
	    _np_state_calculate_location_prefix(), (unsigned long)geteuid(),
	    this_monitoring_plugin->plugin_name, this_state->name);
	if (ret < 0)
		die(STATE_UNKNOWN, _("Cannot allocate memory: %s"),
		    strerror(errno));

	this_state->_filename=temp_filename;

	this_monitoring_plugin->state = this_state;
}

/*
 * Will return NULL if no data is available (first run). If key currently
 * exists, read data. If state file format version is not expected, return
 * as if no data. Get state data version number and compares to expected.
 * If numerically lower, then return as no previous state. die with UNKNOWN
 * if exceptional error.
 */
state_data *np_state_read() {
	state_data *this_state_data=NULL;
	FILE *statefile;
	bool rc = false;

	if(this_monitoring_plugin==NULL)
		die(STATE_UNKNOWN, _("This requires np_init to be called"));

	/* Open file. If this fails, no previous state found */
	statefile = fopen( this_monitoring_plugin->state->_filename, "r" );
	if(statefile!=NULL) {

		this_state_data = (state_data *) calloc(1, sizeof(state_data));
		if(this_state_data==NULL)
			die(STATE_UNKNOWN, _("Cannot allocate memory: %s"),
			    strerror(errno));

		this_state_data->data=NULL;
		this_monitoring_plugin->state->state_data = this_state_data;

		rc = _np_state_read_file(statefile);

		fclose(statefile);
	}

	if(!rc) {
		_cleanup_state_data();
	}

	return this_monitoring_plugin->state->state_data;
}

/*
 * Read the state file
 */
bool _np_state_read_file(FILE *f) {
	bool status = false;
	size_t pos;
	char *line;
	int i;
	int failure=0;
	time_t current_time, data_time;
	enum { STATE_FILE_VERSION, STATE_DATA_VERSION, STATE_DATA_TIME, STATE_DATA_TEXT, STATE_DATA_END } expected=STATE_FILE_VERSION;

	time(&current_time);

	/* Note: This introduces a limit of 1024 bytes in the string data */
	line = (char *) calloc(1, 1024);
	if(line==NULL)
		die(STATE_UNKNOWN, _("Cannot allocate memory: %s"),
		    strerror(errno));

	while(!failure && (fgets(line,1024,f))!=NULL){
		pos=strlen(line);
		if(line[pos-1]=='\n') {
			line[pos-1]='\0';
		}

		if(line[0] == '#') continue;

		switch(expected) {
			case STATE_FILE_VERSION:
				i=atoi(line);
				if(i!=NP_STATE_FORMAT_VERSION)
					failure++;
				else
					expected=STATE_DATA_VERSION;
				break;
			case STATE_DATA_VERSION:
				i=atoi(line);
				if(i != this_monitoring_plugin->state->data_version)
					failure++;
				else
					expected=STATE_DATA_TIME;
				break;
			case STATE_DATA_TIME:
				/* If time > now, error */
				data_time=strtoul(line,NULL,10);
				if(data_time > current_time)
					failure++;
				else {
					this_monitoring_plugin->state->state_data->time = data_time;
					expected=STATE_DATA_TEXT;
				}
				break;
			case STATE_DATA_TEXT:
				this_monitoring_plugin->state->state_data->data = strdup(line);
				if(this_monitoring_plugin->state->state_data->data==NULL)
					die(STATE_UNKNOWN, _("Cannot execute strdup: %s"), strerror(errno));
				expected=STATE_DATA_END;
				status=true;
				break;
			case STATE_DATA_END:
				;
		}
	}

	np_free(line);
	return status;
}

/*
 * If time=NULL, use current time. Create state file, with state format
 * version, default text. Writes version, time, and data. Avoid locking
 * problems - use mv to write and then swap. Possible loss of state data if
 * two things writing to same key at same time.
 * Will die with UNKNOWN if errors
 */
void np_state_write_string(time_t data_time, char *data_string) {
	FILE *fp;
	char *temp_file=NULL;
	int fd=0, result=0;
	time_t current_time;
	char *directories=NULL;
	char *p=NULL;

	if(data_time==0)
		time(&current_time);
	else
		current_time=data_time;

	/* If file doesn't currently exist, create directories */
	if(access(this_monitoring_plugin->state->_filename,F_OK)!=0) {
		result = asprintf(&directories, "%s", this_monitoring_plugin->state->_filename);
		if(result < 0)
			die(STATE_UNKNOWN, _("Cannot allocate memory: %s"),
			    strerror(errno));

		for(p=directories+1; *p; p++) {
			if(*p=='/') {
				*p='\0';
				if((access(directories,F_OK)!=0) && (mkdir(directories, S_IRWXU)!=0)) {
					/* Can't free this! Otherwise error message is wrong! */
					/* np_free(directories); */
					die(STATE_UNKNOWN, _("Cannot create directory: %s"), directories);
				}
				*p='/';
			}
		}
		np_free(directories);
	}

	result = asprintf(&temp_file,"%s.XXXXXX",this_monitoring_plugin->state->_filename);
	if(result < 0)
		die(STATE_UNKNOWN, _("Cannot allocate memory: %s"),
		    strerror(errno));

	if((fd=mkstemp(temp_file))==-1) {
		np_free(temp_file);
		die(STATE_UNKNOWN, _("Cannot create temporary filename"));
	}

	fp=(FILE *)fdopen(fd,"w");
	if(fp==NULL) {
		close(fd);
		unlink(temp_file);
		np_free(temp_file);
		die(STATE_UNKNOWN, _("Unable to open temporary state file"));
	}

	fprintf(fp,"# NP State file\n");
	fprintf(fp,"%d\n",NP_STATE_FORMAT_VERSION);
	fprintf(fp,"%d\n",this_monitoring_plugin->state->data_version);
	fprintf(fp,"%lu\n",current_time);
	fprintf(fp,"%s\n",data_string);

	fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP);

	fflush(fp);

	result=fclose(fp);

	fsync(fd);

	if(result!=0) {
		unlink(temp_file);
		np_free(temp_file);
		die(STATE_UNKNOWN, _("Error writing temp file"));
	}

	if(rename(temp_file, this_monitoring_plugin->state->_filename)!=0) {
		unlink(temp_file);
		np_free(temp_file);
		die(STATE_UNKNOWN, _("Cannot rename state temp file"));
	}

	np_free(temp_file);
}

void
strip (char *buffer)
{
	size_t x;
	int i;

	for (x = strlen (buffer); x >= 1; x--) {
		i = x - 1;
		if (buffer[i] == ' ' ||
				buffer[i] == '\r' || buffer[i] == '\n' || buffer[i] == '\t')
			buffer[i] = '\0';
		else
			break;
	}
	return;
}


/******************************************************************************
 *
 * Copies one string to another. Any previously existing data in
 * the destination string is lost.
 *
 * Example:
 *
 * char *str=NULL;
 * str = strscpy("This is a line of text with no trailing newline");
 *
 *****************************************************************************/

char *
strscpy (char *dest, const char *src)
{
	if (src == NULL)
		return NULL;

	xasprintf (&dest, "%s", src);

	return dest;
}



/******************************************************************************
 *
 * Returns a pointer to the next line of a multiline string buffer
 *
 * Given a pointer string, find the text following the next sequence
 * of \r and \n characters. This has the effect of skipping blank
 * lines as well
 *
 * Example:
 *
 * Given text as follows:
 *
 * ==============================
 * This
 * is
 * a
 * 
 * multiline string buffer
 * ==============================
 *
 * int i=0;
 * char *str=NULL;
 * char *ptr=NULL;
 * str = strscpy(str,"This\nis\r\na\n\nmultiline string buffer\n");
 * ptr = str;
 * while (ptr) {
 *   printf("%d %s",i++,firstword(ptr));
 *   ptr = strnl(ptr);
 * }
 * 
 * Produces the following:
 *
 * 1 This
 * 2 is
 * 3 a
 * 4 multiline
 *
 * NOTE: The 'firstword()' function is conceptual only and does not
 *       exist in this package.
 *
 * NOTE: Although the second 'ptr' variable is not strictly needed in
 *       this example, it is good practice with these utilities. Once
 *       the * pointer is advance in this manner, it may no longer be
 *       handled with * realloc(). So at the end of the code fragment
 *       above, * strscpy(str,"foo") work perfectly fine, but
 *       strscpy(ptr,"foo") will * cause the the program to crash with
 *       a segmentation fault.
 *
 *****************************************************************************/

char *
strnl (char *str)
{
	size_t len;
	if (str == NULL)
		return NULL;
	str = strpbrk (str, "\r\n");
	if (str == NULL)
		return NULL;
	len = strspn (str, "\r\n");
	if (str[len] == '\0')
		return NULL;
	str += len;
	if (strlen (str) == 0)
		return NULL;
	return str;
}


/******************************************************************************
 *
 * Like strscpy, except only the portion of the source string up to
 * the provided delimiter is copied.
 *
 * Example:
 *
 * str = strpcpy(str,"This is a line of text with no trailing newline","x");
 * printf("%s\n",str);
 *
 * Produces:
 *
 *This is a line of te
 *
 *****************************************************************************/

char *
strpcpy (char *dest, const char *src, const char *str)
{
	size_t len;

	if (src)
		len = strcspn (src, str);
	else
		return NULL;

	if (dest == NULL || strlen (dest) < len)
		dest = realloc (dest, len + 1);
	if (dest == NULL)
		die (STATE_UNKNOWN, _("failed realloc in strpcpy\n"));

	strncpy (dest, src, len);
	dest[len] = '\0';

	return dest;
}



/******************************************************************************
 *
 * Like strscat, except only the portion of the source string up to
 * the provided delimiter is copied.
 *
 * str = strpcpy(str,"This is a line of text with no trailing newline","x");
 * str = strpcat(str,"This is a line of text with no trailing newline","x");
 * printf("%s\n",str);
 * 
 *This is a line of texThis is a line of tex
 *
 *****************************************************************************/

char *
strpcat (char *dest, const char *src, const char *str)
{
	size_t len, l2;

	if (dest)
		len = strlen (dest);
	else
		len = 0;

	if (src) {
		l2 = strcspn (src, str);
	}
	else {
		return dest;
	}

	dest = realloc (dest, len + l2 + 1);
	if (dest == NULL)
		die (STATE_UNKNOWN, _("failed malloc in strscat\n"));

	strncpy (dest + len, src, l2);
	dest[len + l2] = '\0';

	return dest;
}


/******************************************************************************
 *
 * asprintf, but die on failure
 *
 ******************************************************************************/

int
xvasprintf (char **strp, const char *fmt, va_list ap)
{
	int result = vasprintf (strp, fmt, ap);
	if (result == -1 || *strp == NULL)
		die (STATE_UNKNOWN, _("failed malloc in xvasprintf\n"));
	return result;
}

int
xasprintf (char **strp, const char *fmt, ...)
{
	va_list ap;
	int result;
	va_start (ap, fmt);
	result = xvasprintf (strp, fmt, ap);
	va_end (ap);
	return result;
}

/*
 * Test whether a string contains only a number which would fit into an int
 */
bool is_integer (char *number) {
	long int n;

	if (!number || (strspn (number, "-0123456789 ") != strlen (number)))
		return false;

	n = strtol (number, NULL, 10);

	if (errno != ERANGE && n >= INT_MIN && n <= INT_MAX)
		return true;
	else
		return false;
}
