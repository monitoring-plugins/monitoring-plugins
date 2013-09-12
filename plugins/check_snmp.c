/*****************************************************************************
* 
* Nagios check_snmp plugin
* 
* License: GPL
* Copyright (c) 1999-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_snmp plugin
* 
* Check status of remote machines and obtain system information via SNMP
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

const char *progname = "check_snmp";
const char *copyright = "1999-2007";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "runcmd.h"
#include "utils.h"
#include "utils_cmd.h"

#define DEFAULT_COMMUNITY "public"
#define DEFAULT_PORT "161"
#define DEFAULT_MIBLIST "ALL"
#define DEFAULT_PROTOCOL "1"
#define DEFAULT_TIMEOUT 1
#define DEFAULT_RETRIES 5
#define DEFAULT_AUTH_PROTOCOL "MD5"
#define DEFAULT_PRIV_PROTOCOL "DES"
#define DEFAULT_DELIMITER "="
#define DEFAULT_OUTPUT_DELIMITER " "

#define mark(a) ((a)!=0?"*":"")

#define CHECK_UNDEF 0
#define CRIT_PRESENT 1
#define CRIT_STRING 2
#define CRIT_REGEX 4
#define WARN_PRESENT 8
#define WARN_STRING 16
#define WARN_REGEX 32

#define OID_COUNT_STEP 8

/* Longopts only arguments */
#define L_CALCULATE_RATE CHAR_MAX+1
#define L_RATE_MULTIPLIER CHAR_MAX+2
#define L_INVERT_SEARCH CHAR_MAX+3
#define L_OFFSET CHAR_MAX+4

/* Gobble to string - stop incrementing c when c[0] match one of the
 * characters in s */
#define GOBBLE_TOS(c, s) while(c[0]!='\0' && strchr(s, c[0])==NULL) { c++; }
/* Given c, keep track of backslashes (bk) and double-quotes (dq)
 * from c[0] */
#define COUNT_SEQ(c, bk, dq) switch(c[0]) {\
	case '\\': \
		if (bk) bk--; \
		else bk++; \
		break; \
	case '"': \
		if (!dq) { dq++; } \
		else if(!bk) { dq--; } \
		else { bk--; } \
		break; \
	}



int process_arguments (int, char **);
int validate_arguments (void);
char *thisarg (char *str);
char *nextarg (char *str);
void print_usage (void);
void print_help (void);

#include "regex.h"
char regex_expect[MAX_INPUT_BUFFER] = "";
regex_t preg;
regmatch_t pmatch[10];
char errbuf[MAX_INPUT_BUFFER] = "";
char perfstr[MAX_INPUT_BUFFER] = "| ";
int cflags = REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
int eflags = 0;
int errcode, excode;

char *server_address = NULL;
char *community = NULL;
char **authpriv = NULL;
char *proto = NULL;
char *seclevel = NULL;
char *secname = NULL;
char *authproto = NULL;
char *privproto = NULL;
char *authpasswd = NULL;
char *privpasswd = NULL;
char **oids = NULL;
size_t oids_size = 0;
char *label;
char *units;
char *port;
char *snmpcmd;
char string_value[MAX_INPUT_BUFFER] = "";
int  invert_search=0;
char **labels = NULL;
char **unitv = NULL;
size_t nlabels = 0;
size_t labels_size = OID_COUNT_STEP;
size_t nunits = 0;
size_t unitv_size = OID_COUNT_STEP;
int numoids = 0;
int numauthpriv = 0;
int verbose = 0;
int usesnmpgetnext = FALSE;
char *warning_thresholds = NULL;
char *critical_thresholds = NULL;
thresholds **thlds;
size_t thlds_size = OID_COUNT_STEP;
double *response_value;
size_t response_size = OID_COUNT_STEP;
int retries = 0;
int *eval_method;
size_t eval_size = OID_COUNT_STEP;
char *delimiter;
char *output_delim;
char *miblist = NULL;
int needmibs = FALSE;
int calculate_rate = 0;
double offset = 0.0;
int rate_multiplier = 1;
state_data *previous_state;
double *previous_value;
size_t previous_size = OID_COUNT_STEP;
int perf_labels = 1;


static char *fix_snmp_range(char *th)
{
	double left, right;
	char *colon, *ret;

	if ((colon = strchr(th, ':')) == NULL || *(colon + 1) == '\0')
		return th;

	left = strtod(th, NULL);
	right = strtod(colon + 1, NULL);
	if (right >= left)
		return th;

	if ((ret = malloc(strlen(th) + 2)) == NULL)
		die(STATE_UNKNOWN, _("Cannot malloc"));
	*colon = '\0';
	sprintf(ret, "@%s:%s", colon + 1, th);
	free(th);
	return ret;
}

int
main (int argc, char **argv)
{
	int i, len, line, total_oids;
	unsigned int bk_count = 0, dq_count = 0;
	int iresult = STATE_UNKNOWN;
	int result = STATE_UNKNOWN;
	int return_code = 0;
	int external_error = 0;
	char **command_line = NULL;
	char *cl_hidden_auth = NULL;
	char *oidname = NULL;
	char *response = NULL;
	char *mult_resp = NULL;
	char *outbuff;
	char *ptr = NULL;
	char *show = NULL;
	char *th_warn=NULL;
	char *th_crit=NULL;
	char type[8] = "";
	output chld_out, chld_err;
	char *previous_string=NULL;
	char *ap=NULL;
	char *state_string=NULL;
	size_t response_length, current_length, string_length;
	char *temp_string=NULL;
	char *quote_string=NULL;
	time_t current_time;
	double temp_double;
	time_t duration;
	char *conv = "12345678";
	int is_counter=0;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	labels = malloc (labels_size * sizeof(*labels));
	unitv = malloc (unitv_size * sizeof(*unitv));
	thlds = malloc (thlds_size * sizeof(*thlds));
	response_value = malloc (response_size * sizeof(*response_value));
	previous_value = malloc (previous_size * sizeof(*previous_value));
	eval_method = calloc (eval_size, sizeof(*eval_method));
	oids = calloc(oids_size, sizeof (char *));

	label = strdup ("SNMP");
	units = strdup ("");
	port = strdup (DEFAULT_PORT);
	outbuff = strdup ("");
	delimiter = strdup (" = ");
	output_delim = strdup (DEFAULT_OUTPUT_DELIMITER);
	timeout_interval = DEFAULT_TIMEOUT;
	retries = DEFAULT_RETRIES;

	np_init( (char *) progname, argc, argv );

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	np_set_args(argc, argv);

	time(&current_time);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	if(calculate_rate) {
		if (!strcmp(label, "SNMP"))
			label = strdup("SNMP RATE");
		i=0;
		previous_state = np_state_read();
		if(previous_state!=NULL) {
			/* Split colon separated values */
			previous_string = strdup((char *) previous_state->data);
			while((ap = strsep(&previous_string, ":")) != NULL) {
				if(verbose>2)
					printf("State for %d=%s\n", i, ap);
				while (i >= previous_size) {
					previous_size += OID_COUNT_STEP;
					previous_value = realloc(previous_value, previous_size * sizeof(*previous_value));
				}
				previous_value[i++]=strtod(ap,NULL);
			}
		}
	}

	/* Populate the thresholds */
	th_warn=warning_thresholds;
	th_crit=critical_thresholds;
	for (i=0; i<numoids; i++) {
		char *w = th_warn ? strndup(th_warn, strcspn(th_warn, ",")) : NULL;
		char *c = th_crit ? strndup(th_crit, strcspn(th_crit, ",")) : NULL;
		/* translate "2:1" to "@1:2" for backwards compatibility */
		w = w ? fix_snmp_range(w) : NULL;
		c = c ? fix_snmp_range(c) : NULL;

		while (i >= thlds_size) {
			thlds_size += OID_COUNT_STEP;
			thlds = realloc(thlds, thlds_size * sizeof(*thlds));
		}

		/* Skip empty thresholds, while avoiding segfault */
		set_thresholds(&thlds[i],
		               w ? strpbrk(w, NP_THRESHOLDS_CHARS) : NULL,
		               c ? strpbrk(c, NP_THRESHOLDS_CHARS) : NULL);
		if (w) {
			th_warn=strchr(th_warn, ',');
			if (th_warn) th_warn++;
			free(w);
		}
		if (c) {
			th_crit=strchr(th_crit, ',');
			if (th_crit) th_crit++;
			free(c);
		}
	}

	/* Create the command array to execute */
	if(usesnmpgetnext == TRUE) {
		snmpcmd = strdup (PATH_TO_SNMPGETNEXT);
	}else{
		snmpcmd = strdup (PATH_TO_SNMPGET);
	}

	/* 10 arguments to pass before authpriv options + 1 for host and numoids. Add one for terminating NULL */
	command_line = calloc (10 + numauthpriv + 1 + numoids + 1, sizeof (char *));
	command_line[0] = snmpcmd;
	command_line[1] = strdup ("-Le");
	command_line[2] = strdup ("-t");
	xasprintf (&command_line[3], "%d", timeout_interval);
	command_line[4] = strdup ("-r");
	xasprintf (&command_line[5], "%d", retries);
	command_line[6] = strdup ("-m");
	command_line[7] = strdup (miblist);
	command_line[8] = "-v";
	command_line[9] = strdup (proto);

	for (i = 0; i < numauthpriv; i++) {
		command_line[10 + i] = authpriv[i];
	}

	xasprintf (&command_line[10 + numauthpriv], "%s:%s", server_address, port);

	/* This is just for display purposes, so it can remain a string */
	xasprintf(&cl_hidden_auth, "%s -Le -t %d -r %d -m %s -v %s %s %s:%s",
		snmpcmd, timeout_interval, retries, strlen(miblist) ? miblist : "''", proto, "[authpriv]",
		server_address, port);

	for (i = 0; i < numoids; i++) {
		command_line[10 + numauthpriv + 1 + i] = oids[i];
		xasprintf(&cl_hidden_auth, "%s %s", cl_hidden_auth, oids[i]);	
	}

	command_line[10 + numauthpriv + 1 + numoids] = NULL;

	if (verbose)
		printf ("%s\n", cl_hidden_auth);

	/* Set signal handling and alarm */
	if (signal (SIGALRM, runcmd_timeout_alarm_handler) == SIG_ERR) {
		usage4 (_("Cannot catch SIGALRM"));
	}
	alarm(timeout_interval * retries + 5);

	/* Run the command */
	return_code = cmd_run_array (command_line, &chld_out, &chld_err, 0);

	/* disable alarm again */
	alarm(0);

	/* Due to net-snmp sometimes showing stderr messages with poorly formed MIBs,
	   only return state unknown if return code is non zero or there is no stdout.
	   Do this way so that if there is stderr, will get added to output, which helps problem diagnosis
	*/
	if (return_code != 0)
		external_error=1;
	if (chld_out.lines == 0)
		external_error=1;
	if (external_error) {
		if (chld_err.lines > 0) {
			printf (_("External command error: %s\n"), chld_err.line[0]);
			for (i = 1; i < chld_err.lines; i++) {
				printf ("%s\n", chld_err.line[i]);
			}
		} else {
			printf(_("External command error with no output (return code: %d)\n"), return_code);
		}
		exit (STATE_UNKNOWN);
	}

	if (verbose) {
		for (i = 0; i < chld_out.lines; i++) {
			printf ("%s\n", chld_out.line[i]);
		}
	}

	for (line=0, i=0; line < chld_out.lines; line++, i++) {
		if(calculate_rate)
			conv = "%.10g";
		else
			conv = "%.0f";

		ptr = chld_out.line[line];
		oidname = strpcpy (oidname, ptr, delimiter);
		response = strstr (ptr, delimiter);
		if (response == NULL)
			break;

		if (verbose > 2) {
			printf("Processing oid %i (line %i)\n  oidname: %s\n  response: %s\n", i+1, line+1, oidname, response);
		}

		/* Clean up type array - Sol10 does not necessarily zero it out */
		bzero(type, sizeof(type));

		is_counter=0;
		/* We strip out the datatype indicator for PHBs */
		if (strstr (response, "Gauge: ")) {
			show = strstr (response, "Gauge: ") + 7;
		} 
		else if (strstr (response, "Gauge32: ")) {
			show = strstr (response, "Gauge32: ") + 9;
		} 
		else if (strstr (response, "Counter32: ")) {
			show = strstr (response, "Counter32: ") + 11;
			is_counter=1;
			if(!calculate_rate) 
				strcpy(type, "c");
		}
		else if (strstr (response, "Counter64: ")) {
			show = strstr (response, "Counter64: ") + 11;
			is_counter=1;
			if(!calculate_rate)
				strcpy(type, "c");
		}
		else if (strstr (response, "INTEGER: ")) {
			show = strstr (response, "INTEGER: ") + 9;
		}
		else if (strstr (response, "STRING: ")) {
			show = strstr (response, "STRING: ") + 8;
			conv = "%.10g";

			/* Get the rest of the string on multi-line strings */
			ptr = show;
			COUNT_SEQ(ptr, bk_count, dq_count)
			while (dq_count && ptr[0] != '\n' && ptr[0] != '\0') {
				ptr++;
				GOBBLE_TOS(ptr, "\n\"\\")
				COUNT_SEQ(ptr, bk_count, dq_count)
			}

			if (dq_count) { /* unfinished line */
				/* copy show verbatim first */
				if (!mult_resp) mult_resp = strdup("");
				xasprintf (&mult_resp, "%s%s:\n%s\n", mult_resp, oids[i], show);
				/* then strip out unmatched double-quote from single-line output */
				if (show[0] == '"') show++;

				/* Keep reading until we match end of double-quoted string */
				for (line++; line < chld_out.lines; line++) {
					ptr = chld_out.line[line];
					xasprintf (&mult_resp, "%s%s\n", mult_resp, ptr);

					COUNT_SEQ(ptr, bk_count, dq_count)
					while (dq_count && ptr[0] != '\n' && ptr[0] != '\0') {
						ptr++;
						GOBBLE_TOS(ptr, "\n\"\\")
						COUNT_SEQ(ptr, bk_count, dq_count)
					}
					/* Break for loop before next line increment when done */
					if (!dq_count) break;
				}
			}

		}
		else if (strstr (response, "Timeticks: ")) {
			show = strstr (response, "Timeticks: ");
		}
		else
			show = response + 3;

		iresult = STATE_DEPENDENT;

		/* Process this block for numeric comparisons */
		/* Make some special values,like Timeticks numeric only if a threshold is defined */
		if (thlds[i]->warning || thlds[i]->critical || calculate_rate) {
			ptr = strpbrk (show, "0123456789");
			if (ptr == NULL)
				die (STATE_UNKNOWN,_("No valid data returned (%s)\n"), show);
			while (i >= response_size) {
				response_size += OID_COUNT_STEP;
				response_value = realloc(response_value, response_size * sizeof(*response_value));
			}
			response_value[i] = strtod (ptr, NULL) + offset;

			if(calculate_rate) {
				if (previous_state!=NULL) {
					duration = current_time-previous_state->time;
					if(duration<=0)
						die(STATE_UNKNOWN,_("Time duration between plugin calls is invalid"));
					temp_double = response_value[i]-previous_value[i];
					/* Simple overflow catcher (same as in rrdtool, rrd_update.c) */
					if(is_counter) {
						if(temp_double<(double)0.0)
							temp_double+=(double)4294967296.0; /* 2^32 */
						if(temp_double<(double)0.0)
							temp_double+=(double)18446744069414584320.0; /* 2^64-2^32 */;
					}
					/* Convert to per second, then use multiplier */
					temp_double = temp_double/duration*rate_multiplier;
					iresult = get_status(temp_double, thlds[i]);
					xasprintf (&show, conv, temp_double);
				}
			} else {
				iresult = get_status(response_value[i], thlds[i]);
				xasprintf (&show, conv, response_value[i]);
			}
		}

		/* Process this block for string matching */
		else if (eval_size > i && eval_method[i] & CRIT_STRING) {
			if (strcmp (show, string_value))
				iresult = (invert_search==0) ? STATE_CRITICAL : STATE_OK;
			else
				iresult = (invert_search==0) ? STATE_OK : STATE_CRITICAL;
		}

		/* Process this block for regex matching */
		else if (eval_size > i && eval_method[i] & CRIT_REGEX) {
			excode = regexec (&preg, response, 10, pmatch, eflags);
			if (excode == 0) {
				iresult = (invert_search==0) ? STATE_OK : STATE_CRITICAL;
			}
			else if (excode != REG_NOMATCH) {
				regerror (excode, &preg, errbuf, MAX_INPUT_BUFFER);
				printf (_("Execute Error: %s\n"), errbuf);
				exit (STATE_CRITICAL);
			}
			else {
				iresult = (invert_search==0) ? STATE_CRITICAL : STATE_OK;
			}
		}

		/* Process this block for existence-nonexistence checks */
		/* TV: Should this be outside of this else block? */
		else {
			if (eval_size > i && eval_method[i] & CRIT_PRESENT)
				iresult = STATE_CRITICAL;
			else if (eval_size > i && eval_method[i] & WARN_PRESENT)
				iresult = STATE_WARNING;
			else if (response && iresult == STATE_DEPENDENT)
				iresult = STATE_OK;
		}

		/* Result is the worst outcome of all the OIDs tested */
		result = max_state (result, iresult);

		/* Prepend a label for this OID if there is one */
		if (nlabels >= (size_t)1 && (size_t)i < nlabels && labels[i] != NULL)
			xasprintf (&outbuff, "%s%s%s %s%s%s", outbuff,
				(i == 0) ? " " : output_delim,
				labels[i], mark (iresult), show, mark (iresult));
		else
			xasprintf (&outbuff, "%s%s%s%s%s", outbuff, (i == 0) ? " " : output_delim,
				mark (iresult), show, mark (iresult));

		/* Append a unit string for this OID if there is one */
		if (nunits > (size_t)0 && (size_t)i < nunits && unitv[i] != NULL)
			xasprintf (&outbuff, "%s %s", outbuff, unitv[i]);

		/* Write perfdata with whatever can be parsed by strtod, if possible */
		ptr = NULL;
		strtod(show, &ptr);
		if (ptr > show) {
			if (perf_labels && nlabels >= (size_t)1 && (size_t)i < nlabels && labels[i] != NULL)
				temp_string=labels[i];
			else
				temp_string=oidname;
			if (strpbrk (temp_string, " ='\"") == NULL) {
				strncat(perfstr, temp_string, sizeof(perfstr)-strlen(perfstr)-1);
			} else {
				if (strpbrk (temp_string, "'") == NULL) {
					quote_string="'";
				} else {
					quote_string="\"";
				}
				strncat(perfstr, quote_string, sizeof(perfstr)-strlen(perfstr)-1);
				strncat(perfstr, temp_string, sizeof(perfstr)-strlen(perfstr)-1);
				strncat(perfstr, quote_string, sizeof(perfstr)-strlen(perfstr)-1);
			}
			strncat(perfstr, "=", sizeof(perfstr)-strlen(perfstr)-1);
			len = sizeof(perfstr)-strlen(perfstr)-1;
			strncat(perfstr, show, len>ptr-show ? ptr-show : len);

			if (type)
				strncat(perfstr, type, sizeof(perfstr)-strlen(perfstr)-1);
			strncat(perfstr, " ", sizeof(perfstr)-strlen(perfstr)-1);
		}
	}
	total_oids=i;

	/* Save state data, as all data collected now */
	if(calculate_rate) {
		string_length=1024;
		state_string=malloc(string_length);
		if(state_string==NULL)
			die(STATE_UNKNOWN, _("Cannot malloc"));
		
		current_length=0;
		for(i=0; i<total_oids; i++) {
			xasprintf(&temp_string,"%.0f",response_value[i]);
			if(temp_string==NULL)
				die(STATE_UNKNOWN,_("Cannot asprintf()"));
			response_length = strlen(temp_string);
			if(current_length+response_length>string_length) {
				string_length=current_length+1024;
				state_string=realloc(state_string,string_length);
				if(state_string==NULL)
					die(STATE_UNKNOWN, _("Cannot realloc()"));
			}
			strcpy(&state_string[current_length],temp_string);
			current_length=current_length+response_length;
			state_string[current_length]=':';
			current_length++;
			free(temp_string);
		}
		state_string[--current_length]='\0';
		if (verbose > 2)
			printf("State string=%s\n",state_string);
		
		/* This is not strictly the same as time now, but any subtle variations will cancel out */
		np_state_write_string(current_time, state_string );
		if(previous_state==NULL) {
			/* Or should this be highest state? */
			die( STATE_OK, _("No previous data to calculate rate - assume okay" ) );
		}
	}

	printf ("%s %s -%s %s\n", label, state_text (result), outbuff, perfstr);
	if (mult_resp) printf ("%s", mult_resp);

	return result;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	char *ptr;
	int c = 1;
	int j = 0, jj = 0, ii = 0;

	int option = 0;
	static struct option longopts[] = {
		STD_LONG_OPTS,
		{"community", required_argument, 0, 'C'},
		{"oid", required_argument, 0, 'o'},
		{"object", required_argument, 0, 'o'},
		{"delimiter", required_argument, 0, 'd'},
		{"output-delimiter", required_argument, 0, 'D'},
		{"string", required_argument, 0, 's'},
		{"timeout", required_argument, 0, 't'},
		{"regex", required_argument, 0, 'r'},
		{"ereg", required_argument, 0, 'r'},
		{"eregi", required_argument, 0, 'R'},
		{"label", required_argument, 0, 'l'},
		{"units", required_argument, 0, 'u'},
		{"port", required_argument, 0, 'p'},
		{"retries", required_argument, 0, 'e'},
		{"miblist", required_argument, 0, 'm'},
		{"protocol", required_argument, 0, 'P'},
		{"seclevel", required_argument, 0, 'L'},
		{"secname", required_argument, 0, 'U'},
		{"authproto", required_argument, 0, 'a'},
		{"privproto", required_argument, 0, 'x'},
		{"authpasswd", required_argument, 0, 'A'},
		{"privpasswd", required_argument, 0, 'X'},
		{"next", no_argument, 0, 'n'},
		{"rate", no_argument, 0, L_CALCULATE_RATE},
		{"rate-multiplier", required_argument, 0, L_RATE_MULTIPLIER},
		{"offset", required_argument, 0, L_OFFSET},
		{"invert-search", no_argument, 0, L_INVERT_SEARCH},
		{"perf-oids", no_argument, 0, 'O'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	/* reverse compatibility for very old non-POSIX usage forms */
	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
		if (strcmp ("-wv", argv[c]) == 0)
			strcpy (argv[c], "-w");
		if (strcmp ("-cv", argv[c]) == 0)
			strcpy (argv[c], "-c");
	}

	while (1) {
		c = getopt_long (argc, argv, "nhvVOt:c:w:H:C:o:e:E:d:D:s:t:R:r:l:u:p:m:P:L:U:a:x:A:X:",
									 longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':	/* usage */
			usage5 ();
		case 'h':	/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':	/* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_OK);
		case 'v': /* verbose */
			verbose++;
			break;

	/* Connection info */
		case 'C':									/* group or community */
			community = optarg;
			break;
		case 'H':									/* Host or server */
			server_address = optarg;
			break;
		case 'p':	/* TCP port number */
			port = optarg;
			break;
		case 'm':	/* List of MIBS */
			miblist = optarg;
			break;
		case 'n':	/* usesnmpgetnext */
			usesnmpgetnext = TRUE;
			break;
		case 'P':	/* SNMP protocol version */
			proto = optarg;
			break;
		case 'L':	/* security level */
			seclevel = optarg;
			break;
		case 'U':	/* security username */
			secname = optarg;
			break;
		case 'a':	/* auth protocol */
			authproto = optarg;
			break;
		case 'x':	/* priv protocol */
			privproto = optarg;
			break;
		case 'A':	/* auth passwd */
			authpasswd = optarg;
			break;
		case 'X':	/* priv passwd */
			privpasswd = optarg;
			break;
		case 't':	/* timeout period */
			if (!is_integer (optarg))
				usage2 (_("Timeout interval must be a positive integer"), optarg);
			else
				timeout_interval = atoi (optarg);
			break;

	/* Test parameters */
		case 'c':									/* critical threshold */
			critical_thresholds = optarg;
			break;
		case 'w':									/* warning threshold */
			warning_thresholds = optarg;
			break;
		case 'e': /* PRELIMINARY - may change */
		case 'E': /* PRELIMINARY - may change */
			if (!is_integer (optarg))
				usage2 (_("Retries interval must be a positive integer"), optarg);
			else
				retries = atoi(optarg);
			break;
		case 'o':									/* object identifier */
			if ( strspn( optarg, "0123456789.," ) != strlen( optarg ) ) {
					/*
					 * we have something other than digits, periods and comas,
					 * so we have a mib variable, rather than just an SNMP OID,
					 * so we have to actually read the mib files
					 */
					needmibs = TRUE;
			}
			for (ptr = strtok(optarg, ", "); ptr != NULL; ptr = strtok(NULL, ", "), j++) {
				while (j >= oids_size) {
					oids_size += OID_COUNT_STEP;
					oids = realloc(oids, oids_size * sizeof (*oids));
				}
				oids[j] = strdup(ptr);
			}
			numoids = j;
			if (c == 'E' || c == 'e') {
				jj++;
				ii++;
				while (j+1 >= eval_size) {
					eval_size += OID_COUNT_STEP;
					eval_method = realloc(eval_method, eval_size * sizeof(*eval_method));
					memset(eval_method + eval_size - OID_COUNT_STEP, 0, 8);
				}
				if (c == 'E')
					eval_method[j+1] |= WARN_PRESENT;
				else if (c == 'e')
					eval_method[j+1] |= CRIT_PRESENT;
			}
			break;
		case 's':									/* string or substring */
			strncpy (string_value, optarg, sizeof (string_value) - 1);
			string_value[sizeof (string_value) - 1] = 0;
			while (jj >= eval_size) {
				eval_size += OID_COUNT_STEP;
				eval_method = realloc(eval_method, eval_size * sizeof(*eval_method));
				memset(eval_method + eval_size - OID_COUNT_STEP, 0, 8);
			}
			eval_method[jj++] = CRIT_STRING;
			ii++;
			break;
		case 'R':									/* regex */
			cflags = REG_ICASE;
		case 'r':									/* regex */
			cflags |= REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
			strncpy (regex_expect, optarg, sizeof (regex_expect) - 1);
			regex_expect[sizeof (regex_expect) - 1] = 0;
			errcode = regcomp (&preg, regex_expect, cflags);
			if (errcode != 0) {
				regerror (errcode, &preg, errbuf, MAX_INPUT_BUFFER);
				printf (_("Could Not Compile Regular Expression"));
				return ERROR;
			}
			while (jj >= eval_size) {
				eval_size += OID_COUNT_STEP;
				eval_method = realloc(eval_method, eval_size * sizeof(*eval_method));
				memset(eval_method + eval_size - OID_COUNT_STEP, 0, 8);
			}
			eval_method[jj++] = CRIT_REGEX;
			ii++;
			break;

	/* Format */
		case 'd':									/* delimiter */
			delimiter = strscpy (delimiter, optarg);
			break;
		case 'D':									/* output-delimiter */
			output_delim = strscpy (output_delim, optarg);
			break;
		case 'l':									/* label */
			nlabels++;
			if (nlabels > labels_size) {
				labels_size += 8;
				labels = realloc (labels, labels_size * sizeof(*labels));
				if (labels == NULL)
					die (STATE_UNKNOWN, _("Could not reallocate labels[%d]"), (int)nlabels);
			}
			labels[nlabels - 1] = optarg;
			ptr = thisarg (optarg);
			labels[nlabels - 1] = ptr;
			if (ptr[0] == '\'')
				labels[nlabels - 1] = ptr + 1;
			while (ptr && (ptr = nextarg (ptr))) {
				nlabels++;
				if (nlabels > labels_size) {
					labels_size += 8;
					labels = realloc (labels, labels_size * sizeof(*labels));
					if (labels == NULL)
						die (STATE_UNKNOWN, _("Could not reallocate labels\n"));
				}
				ptr = thisarg (ptr);
				if (ptr[0] == '\'')
					labels[nlabels - 1] = ptr + 1;
				else
					labels[nlabels - 1] = ptr;
			}
			break;
		case 'u':									/* units */
			units = optarg;
			nunits++;
			if (nunits > unitv_size) {
				unitv_size += 8;
				unitv = realloc (unitv, unitv_size * sizeof(*unitv));
				if (unitv == NULL)
					die (STATE_UNKNOWN, _("Could not reallocate units [%d]\n"), (int)nunits);
			}
			unitv[nunits - 1] = optarg;
			ptr = thisarg (optarg);
			unitv[nunits - 1] = ptr;
			if (ptr[0] == '\'')
				unitv[nunits - 1] = ptr + 1;
			while (ptr && (ptr = nextarg (ptr))) {
				if (nunits > unitv_size) {
					unitv_size += 8;
					unitv = realloc (unitv, unitv_size * sizeof(*unitv));
					if (units == NULL)
						die (STATE_UNKNOWN, _("Could not realloc() units\n"));
				}
				nunits++;
				ptr = thisarg (ptr);
				if (ptr[0] == '\'')
					unitv[nunits - 1] = ptr + 1;
				else
					unitv[nunits - 1] = ptr;
			}
			break;
		case L_CALCULATE_RATE:
			if(calculate_rate==0)
				np_enable_state(NULL, 1);
			calculate_rate = 1;
			break;
		case L_RATE_MULTIPLIER:
			if(!is_integer(optarg)||((rate_multiplier=atoi(optarg))<=0))
				usage2(_("Rate multiplier must be a positive integer"),optarg);
			break;
		case L_OFFSET:
                        offset=strtod(optarg,NULL);
			break;
		case L_INVERT_SEARCH:
			invert_search=1;
			break;
		case 'O':
			perf_labels=0;
			break;
		}
	}

	if (server_address == NULL)
		server_address = argv[optind];

	if (community == NULL)
		community = strdup (DEFAULT_COMMUNITY);

	return validate_arguments ();
}


/******************************************************************************

@@-
<sect3>
<title>validate_arguments</title>

<para>&PROTO_validate_arguments;</para>

<para>Checks to see if the default miblist needs to be loaded. Also verifies
the authentication and authorization combinations based on protocol version
selected.</para>

<para></para>

</sect3>
-@@
******************************************************************************/



int
validate_arguments ()
{
	/* check whether to load locally installed MIBS (CPU/disk intensive) */
	if (miblist == NULL) {
		if ( needmibs == TRUE ) {
			miblist = strdup (DEFAULT_MIBLIST);
		}else{
			miblist = "";			/* don't read any mib files for numeric oids */
		}
	}

	/* Check server_address is given */
	if (server_address == NULL)
		die(STATE_UNKNOWN, _("No host specified\n"));

	/* Check oid is given */
	if (numoids == 0)
		die(STATE_UNKNOWN, _("No OIDs specified\n"));

	if (proto == NULL)
		xasprintf(&proto, DEFAULT_PROTOCOL);

	if ((strcmp(proto,"1") == 0) || (strcmp(proto, "2c")==0)) {	/* snmpv1 or snmpv2c */
		numauthpriv = 2;
		authpriv = calloc (numauthpriv, sizeof (char *));
		authpriv[0] = strdup ("-c");
		authpriv[1] = strdup (community);
	}
	else if ( strcmp (proto, "3") == 0 ) {		/* snmpv3 args */
		if (seclevel == NULL)
			xasprintf(&seclevel, "noAuthNoPriv");

		if (strcmp(seclevel, "noAuthNoPriv") == 0) {
			numauthpriv = 2;
			authpriv = calloc (numauthpriv, sizeof (char *));
			authpriv[0] = strdup ("-l");
			authpriv[1] = strdup ("noAuthNoPriv");
		} else {
			if (! ( (strcmp(seclevel, "authNoPriv")==0) || (strcmp(seclevel, "authPriv")==0) ) ) {
				usage2 (_("Invalid seclevel"), seclevel);
			}

			if (authproto == NULL )
				xasprintf(&authproto, DEFAULT_AUTH_PROTOCOL);

			if (secname == NULL)
				die(STATE_UNKNOWN, _("Required parameter: %s\n"), "secname");

			if (authpasswd == NULL)
				die(STATE_UNKNOWN, _("Required parameter: %s\n"), "authpasswd");

			if ( strcmp(seclevel, "authNoPriv") == 0 ) {
				numauthpriv = 8;
				authpriv = calloc (numauthpriv, sizeof (char *));
				authpriv[0] = strdup ("-l");
				authpriv[1] = strdup ("authNoPriv");
				authpriv[2] = strdup ("-a");
				authpriv[3] = strdup (authproto);
				authpriv[4] = strdup ("-u");
				authpriv[5] = strdup (secname);
				authpriv[6] = strdup ("-A");
				authpriv[7] = strdup (authpasswd);
			} else if ( strcmp(seclevel, "authPriv") == 0 ) {
				if (privproto == NULL )
					xasprintf(&privproto, DEFAULT_PRIV_PROTOCOL);

				if (privpasswd == NULL)
					die(STATE_UNKNOWN, _("Required parameter: %s\n"), "privpasswd");

				numauthpriv = 12;
				authpriv = calloc (numauthpriv, sizeof (char *));
				authpriv[0] = strdup ("-l");
				authpriv[1] = strdup ("authPriv");
				authpriv[2] = strdup ("-a");
				authpriv[3] = strdup (authproto);
				authpriv[4] = strdup ("-u");
				authpriv[5] = strdup (secname);
				authpriv[6] = strdup ("-A");
				authpriv[7] = strdup (authpasswd);
				authpriv[8] = strdup ("-x");
				authpriv[9] = strdup (privproto);
				authpriv[10] = strdup ("-X");
				authpriv[11] = strdup (privpasswd);
			}
		}

	}
	else {
		usage2 (_("Invalid SNMP version"), proto);
	}

	return OK;
}



/* trim leading whitespace
	 if there is a leading quote, make sure it balances */

char *
thisarg (char *str)
{
	str += strspn (str, " \t\r\n");	/* trim any leading whitespace */
	if (str[0] == '\'') {	/* handle SIMPLE quoted strings */
		if (strlen (str) == 1 || !strstr (str + 1, "'"))
			die (STATE_UNKNOWN, _("Unbalanced quotes\n"));
	}
	return str;
}



/* if there's a leading quote, advance to the trailing quote
	 set the trailing quote to '\x0'
	 if the string continues, advance beyond the comma */

char *
nextarg (char *str)
{
	if (str[0] == '\'') {
		str[0] = 0;
		if (strlen (str) > 1) {
			str = strstr (str + 1, "'");
			return (++str);
		}
		else {
			return NULL;
		}
	}
	if (str[0] == ',') {
		str[0] = 0;
		if (strlen (str) > 1) {
			return (++str);
		}
		else {
			return NULL;
		}
	}
	if ((str = strstr (str, ",")) && strlen (str) > 1) {
		str[0] = 0;
		return (++str);
	}
	return NULL;
}



void
print_help (void)
{
	print_revision (progname, NP_VERSION);

	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("Check status of remote machines and obtain system information via SNMP"));

	printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (UT_HOST_PORT, 'p', DEFAULT_PORT);

	/* SNMP and Authentication Protocol */
	printf (" %s\n", "-n, --next");
	printf ("    %s\n", _("Use SNMP GETNEXT instead of SNMP GET"));
	printf (" %s\n", "-P, --protocol=[1|2c|3]");
	printf ("    %s\n", _("SNMP protocol version"));
	printf (" %s\n", "-L, --seclevel=[noAuthNoPriv|authNoPriv|authPriv]");
	printf ("    %s\n", _("SNMPv3 securityLevel"));
	printf (" %s\n", "-a, --authproto=[MD5|SHA]");
	printf ("    %s\n", _("SNMPv3 auth proto"));
	printf (" %s\n", "-x, --privproto=[DES|AES]");
	printf ("    %s\n", _("SNMPv3 priv proto (default DES)"));

	/* Authentication Tokens*/
	printf (" %s\n", "-C, --community=STRING");
	printf ("    %s ", _("Optional community string for SNMP communication"));
	printf ("(%s \"%s\")\n", _("default is") ,DEFAULT_COMMUNITY);
	printf (" %s\n", "-U, --secname=USERNAME");
	printf ("    %s\n", _("SNMPv3 username"));
	printf (" %s\n", "-A, --authpassword=PASSWORD");
	printf ("    %s\n", _("SNMPv3 authentication password"));
	printf (" %s\n", "-X, --privpasswd=PASSWORD");
	printf ("    %s\n", _("SNMPv3 privacy password"));

	/* OID Stuff */
	printf (" %s\n", "-o, --oid=OID(s)");
	printf ("    %s\n", _("Object identifier(s) or SNMP variables whose value you wish to query"));
	printf (" %s\n", "-m, --miblist=STRING");
	printf ("    %s\n", _("List of MIBS to be loaded (default = none if using numeric OIDs or 'ALL'"));
	printf ("    %s\n", _("for symbolic OIDs.)"));
	printf (" %s\n", "-d, --delimiter=STRING");
	printf ("    %s \"%s\"\n", _("Delimiter to use when parsing returned data. Default is"), DEFAULT_DELIMITER);
	printf ("    %s\n", _("Any data on the right hand side of the delimiter is considered"));
	printf ("    %s\n", _("to be the data that should be used in the evaluation."));

	/* Tests Against Integers */
	printf (" %s\n", "-w, --warning=THRESHOLD(s)");
	printf ("    %s\n", _("Warning threshold range(s)"));
	printf (" %s\n", "-c, --critical=THRESHOLD(s)");
	printf ("    %s\n", _("Critical threshold range(s)"));
	printf (" %s\n", "--rate");
	printf ("    %s\n", _("Enable rate calculation. See 'Rate Calculation' below"));
	printf (" %s\n", "--rate-multiplier");
	printf ("    %s\n", _("Converts rate per second. For example, set to 60 to convert to per minute"));
	printf (" %s\n", "--offset=OFFSET");
	printf ("    %s\n", _("Add/substract the specified OFFSET to numeric sensor data"));

	/* Tests Against Strings */
	printf (" %s\n", "-s, --string=STRING");
	printf ("    %s\n", _("Return OK state (for that OID) if STRING is an exact match"));
	printf (" %s\n", "-r, --ereg=REGEX");
	printf ("    %s\n", _("Return OK state (for that OID) if extended regular expression REGEX matches"));
	printf (" %s\n", "-R, --eregi=REGEX");
	printf ("    %s\n", _("Return OK state (for that OID) if case-insensitive extended REGEX matches"));
	printf (" %s\n", "--invert-search");
	printf ("    %s\n", _("Invert search result (CRITICAL if found)"));

	/* Output Formatting */
	printf (" %s\n", "-l, --label=STRING");
	printf ("    %s\n", _("Prefix label for output from plugin"));
	printf (" %s\n", "-u, --units=STRING");
	printf ("    %s\n", _("Units label(s) for output data (e.g., 'sec.')."));
	printf (" %s\n", "-D, --output-delimiter=STRING");
	printf ("    %s\n", _("Separates output on multiple OID requests"));

	printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);
	printf (" %s\n", "-e, --retries=INTEGER");
	printf ("    %s\n", _("Number of retries to be used in the requests"));

	printf (" %s\n", "-O, --perf-oids");
	printf ("    %s\n", _("Label performance data with OIDs instead of --label's"));

	printf (UT_VERBOSE);

	printf ("\n");
	printf ("%s\n", _("This plugin uses the 'snmpget' command included with the NET-SNMP package."));
	printf ("%s\n", _("if you don't have the package installed, you will need to download it from"));
	printf ("%s\n", _("http://net-snmp.sourceforge.net before you can use this plugin."));

	printf ("\n");
	printf ("%s\n", _("Notes:"));
	printf (" %s\n", _("- Multiple OIDs (and labels) may be indicated by a comma or space-delimited  "));
	printf ("   %s\n", _("list (lists with internal spaces must be quoted)."));

	printf(" -%s", UT_THRESHOLDS_NOTES);

	printf (" %s\n", _("- When checking multiple OIDs, separate ranges by commas like '-w 1:10,1:,:20'"));
	printf (" %s\n", _("- Note that only one string and one regex may be checked at present"));
	printf (" %s\n", _("- All evaluation methods other than PR, STR, and SUBSTR expect that the value"));
	printf ("   %s\n", _("returned from the SNMP query is an unsigned integer."));

	printf("\n");
	printf("%s\n", _("Rate Calculation:"));
	printf(" %s\n", _("In many places, SNMP returns counters that are only meaningful when"));
	printf(" %s\n", _("calculating the counter difference since the last check. check_snmp"));
	printf(" %s\n", _("saves the last state information in a file so that the rate per second"));
	printf(" %s\n", _("can be calculated. Use the --rate option to save state information."));
	printf(" %s\n", _("On the first run, there will be no prior state - this will return with OK."));
	printf(" %s\n", _("The state is uniquely determined by the arguments to the plugin, so"));
	printf(" %s\n", _("changing the arguments will create a new state file."));

	printf (UT_SUPPORT);
}



void
print_usage (void)
{
	printf ("%s\n", _("Usage:"));
	printf ("%s -H <ip_address> -o <OID> [-w warn_range] [-c crit_range]\n",progname);
	printf ("[-C community] [-s string] [-r regex] [-R regexi] [-t timeout] [-e retries]\n");
	printf ("[-l label] [-u units] [-p port-number] [-d delimiter] [-D output-delimiter]\n");
	printf ("[-m miblist] [-P snmp version] [-L seclevel] [-U secname] [-a authproto]\n");
	printf ("[-A authpasswd] [-x privproto] [-X privpasswd]\n");
}
