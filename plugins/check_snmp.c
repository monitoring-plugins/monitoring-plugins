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

#define MAX_OIDS 8

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
char *label;
char *units;
char *port;
char *snmpcmd;
char string_value[MAX_INPUT_BUFFER] = "";
char **labels = NULL;
char **unitv = NULL;
size_t nlabels = 0;
size_t labels_size = 8;
size_t nunits = 0;
size_t unitv_size = 8;
int numoids = 0;
int numauthpriv = 0;
int verbose = FALSE;
int usesnmpgetnext = FALSE;
char *warning_thresholds = NULL;
char *critical_thresholds = NULL;
thresholds *thlds[MAX_OIDS];
double response_value[MAX_OIDS];
int retries = 0;
int eval_method[MAX_OIDS];
char *delimiter;
char *output_delim;
char *miblist = NULL;
int needmibs = FALSE;


int
main (int argc, char **argv)
{
	int i;
	int iresult = STATE_UNKNOWN;
	int result = STATE_UNKNOWN;
	int return_code = 0;
	int external_error = 0;
	char **command_line = NULL;
	char *cl_hidden_auth = NULL;
	char *oidname = NULL;
	char *response = NULL;
	char *outbuff;
	char *ptr = NULL;
	char *show = NULL;
	char *th_warn=NULL;
	char *th_crit=NULL;
	char type[8] = "";
	output chld_out, chld_err;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	labels = malloc (labels_size);
	unitv = malloc (unitv_size);
	for (i = 0; i < MAX_OIDS; i++)
		eval_method[i] = CHECK_UNDEF;

	label = strdup ("SNMP");
	units = strdup ("");
	port = strdup (DEFAULT_PORT);
	outbuff = strdup ("");
	delimiter = strdup (" = ");
	output_delim = strdup (DEFAULT_OUTPUT_DELIMITER);
	timeout_interval = DEFAULT_TIMEOUT;
	retries = DEFAULT_RETRIES;

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* Populate the thresholds */
	th_warn=warning_thresholds;
	th_crit=critical_thresholds;
	for (i=0; i<numoids; i++) {
		char *w = th_warn ? strndup(th_warn, strcspn(th_warn, ",")) : NULL;
		char *c = th_crit ? strndup(th_crit, strcspn(th_crit, ",")) : NULL;
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
	
	/* 9 arguments to pass before authpriv options + 1 for host and numoids. Add one for terminating NULL */
	command_line = calloc (9 + numauthpriv + 1 + numoids + 1, sizeof (char *));
	command_line[0] = snmpcmd;
	command_line[1] = strdup ("-t");
	asprintf (&command_line[2], "%d", timeout_interval);
	command_line[3] = strdup ("-r");
	asprintf (&command_line[4], "%d", retries);
	command_line[5] = strdup ("-m");
	command_line[6] = strdup (miblist);
	command_line[7] = "-v";
	command_line[8] = strdup (proto);

	for (i = 0; i < numauthpriv; i++) {
		command_line[9 + i] = authpriv[i];
	}

	asprintf (&command_line[9 + numauthpriv], "%s:%s", server_address, port);

	/* This is just for display purposes, so it can remain a string */
	asprintf(&cl_hidden_auth, "%s -t %d -r %d -m %s -v %s %s %s:%s",
		snmpcmd, timeout_interval, retries, miblist, proto, "[authpriv]",
		server_address, port);

	for (i = 0; i < numoids; i++) {
		command_line[9 + numauthpriv + 1 + i] = oids[i];
		asprintf(&cl_hidden_auth, "%s %s", cl_hidden_auth, oids[i]);	
	}

	command_line[9 + numauthpriv + 1 + numoids] = NULL;

	if (verbose)
		printf ("%s\n", cl_hidden_auth);

	/* Run the command */
	return_code = cmd_run_array (command_line, &chld_out, &chld_err, 0);

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

	for (i = 0; i < chld_out.lines; i++) {
		const char *conv = "%.0f";

		ptr = chld_out.line[i];
		oidname = strpcpy (oidname, ptr, delimiter);
		response = strstr (ptr, delimiter);

		/* We strip out the datatype indicator for PHBs */

		/* Clean up type array - Sol10 does not necessarily zero it out */
		bzero(type, sizeof(type));

		if (strstr (response, "Gauge: "))
			show = strstr (response, "Gauge: ") + 7;
		else if (strstr (response, "Gauge32: "))
			show = strstr (response, "Gauge32: ") + 9;
		else if (strstr (response, "Counter32: ")) {
			show = strstr (response, "Counter32: ") + 11;
			strcpy(type, "c");
		}
		else if (strstr (response, "Counter64: ")) {
			show = strstr (response, "Counter64: ") + 11;
			strcpy(type, "c");
		}
		else if (strstr (response, "INTEGER: "))
			show = strstr (response, "INTEGER: ") + 9;
		else if (strstr (response, "STRING: ")) {
			show = strstr (response, "STRING: ") + 8;
			conv = "%.10g";
		}
		else if (strstr (response, "Timeticks: "))
			show = strstr (response, "Timeticks: ");
		else
			show = response;

		iresult = STATE_DEPENDENT;

		/* Process this block for integer comparisons */
		if (thlds[i]->warning || thlds[i]->critical) {
			ptr = strpbrk (show, "0123456789");
			if (ptr == NULL)
				die (STATE_UNKNOWN,_("No valid data returned"));
			response_value[i] = strtod (ptr, NULL);
			iresult = get_status(response_value[i], thlds[i]);
			asprintf (&show, conv, response_value[i]);
		}

		/* Process this block for string matching */
		else if (eval_method[i] & CRIT_STRING) {
			if (strcmp (show, string_value))
				iresult = STATE_CRITICAL;
			else
				iresult = STATE_OK;
		}

		/* Process this block for regex matching */
		else if (eval_method[i] & CRIT_REGEX) {
			excode = regexec (&preg, response, 10, pmatch, eflags);
			if (excode == 0) {
				iresult = STATE_OK;
			}
			else if (excode != REG_NOMATCH) {
				regerror (excode, &preg, errbuf, MAX_INPUT_BUFFER);
				printf (_("Execute Error: %s\n"), errbuf);
				exit (STATE_CRITICAL);
			}
			else {
				iresult = STATE_CRITICAL;
			}
		}

		/* Process this block for existence-nonexistence checks */
		else {
			if (eval_method[i] & CRIT_PRESENT)
				iresult = STATE_CRITICAL;
			else if (eval_method[i] & WARN_PRESENT)
				iresult = STATE_WARNING;
			else if (response && iresult == STATE_DEPENDENT)
				iresult = STATE_OK;
		}

		/* Result is the worst outcome of all the OIDs tested */
		result = max_state (result, iresult);

		/* Prepend a label for this OID if there is one */
		if (nlabels > (size_t)1 && (size_t)i < nlabels && labels[i] != NULL)
			asprintf (&outbuff, "%s%s%s %s%s%s", outbuff,
				(i == 0) ? " " : output_delim,
				labels[i], mark (iresult), show, mark (iresult));
		else
			asprintf (&outbuff, "%s%s%s%s%s", outbuff, (i == 0) ? " " : output_delim,
				mark (iresult), show, mark (iresult));

		/* Append a unit string for this OID if there is one */
		if (nunits > (size_t)0 && (size_t)i < nunits && unitv[i] != NULL)
			asprintf (&outbuff, "%s %s", outbuff, unitv[i]);

		if (is_numeric(show)) {
			strncat(perfstr, oidname, sizeof(perfstr)-strlen(perfstr)-1);
			strncat(perfstr, "=", sizeof(perfstr)-strlen(perfstr)-1);
			strncat(perfstr, show, sizeof(perfstr)-strlen(perfstr)-1);

			if (type)
				strncat(perfstr, type, sizeof(perfstr)-strlen(perfstr)-1);
			strncat(perfstr, " ", sizeof(perfstr)-strlen(perfstr)-1);
		}
	}

	printf ("%s %s -%s %s \n", label, state_text (result), outbuff, perfstr);

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
		c = getopt_long (argc, argv, "nhvVt:c:w:H:C:o:e:E:d:D:s:t:R:r:l:u:p:m:P:L:U:a:x:A:X:",
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
			verbose = TRUE;
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
			oids = calloc(MAX_OIDS, sizeof (char *));
			for (ptr = strtok(optarg, ", "); ptr != NULL && j < MAX_OIDS; ptr = strtok(NULL, ", "), j++) {
				oids[j] = strdup(ptr);
			}
			numoids = j;
			if (c == 'E' || c == 'e') {
				jj++;
				ii++;
			}
			if (c == 'E')
				eval_method[j+1] |= WARN_PRESENT;
			else if (c == 'e')
				eval_method[j+1] |= CRIT_PRESENT;
			break;
		case 's':									/* string or substring */
			strncpy (string_value, optarg, sizeof (string_value) - 1);
			string_value[sizeof (string_value) - 1] = 0;
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
			label = optarg;
			nlabels++;
			if (nlabels >= labels_size) {
				labels_size += 8;
				labels = realloc (labels, labels_size);
				if (labels == NULL)
					die (STATE_UNKNOWN, _("Could not reallocate labels[%d]"), (int)nlabels);
			}
			labels[nlabels - 1] = optarg;
			ptr = thisarg (optarg);
			labels[nlabels - 1] = ptr;
			if (strstr (ptr, "'") == ptr)
				labels[nlabels - 1] = ptr + 1;
			while (ptr && (ptr = nextarg (ptr))) {
				if (nlabels >= labels_size) {
					labels_size += 8;
					labels = realloc (labels, labels_size);
					if (labels == NULL)
						die (STATE_UNKNOWN, _("Could not reallocate labels\n"));
				}
				labels++;
				ptr = thisarg (ptr);
				if (strstr (ptr, "'") == ptr)
					labels[nlabels - 1] = ptr + 1;
				else
					labels[nlabels - 1] = ptr;
			}
			break;
		case 'u':									/* units */
			units = optarg;
			nunits++;
			if (nunits >= unitv_size) {
				unitv_size += 8;
				unitv = realloc (unitv, unitv_size);
				if (unitv == NULL)
					die (STATE_UNKNOWN, _("Could not reallocate units [%d]\n"), (int)nunits);
			}
			unitv[nunits - 1] = optarg;
			ptr = thisarg (optarg);
			unitv[nunits - 1] = ptr;
			if (strstr (ptr, "'") == ptr)
				unitv[nunits - 1] = ptr + 1;
			while (ptr && (ptr = nextarg (ptr))) {
				if (nunits >= unitv_size) {
					unitv_size += 8;
					unitv = realloc (unitv, unitv_size);
					if (units == NULL)
						die (STATE_UNKNOWN, _("Could not realloc() units\n"));
				}
				nunits++;
				ptr = thisarg (ptr);
				if (strstr (ptr, "'") == ptr)
					unitv[nunits - 1] = ptr + 1;
				else
					unitv[nunits - 1] = ptr;
			}
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
			miblist = "''";			/* don't read any mib files for numeric oids */
		}
	}

	/* Check server_address is given */
	if (server_address == NULL)
		die(STATE_UNKNOWN, _("No host specified\n"));

	/* Check oid is given */
	if (numoids == 0)
		die(STATE_UNKNOWN, _("No OIDs specified\n"));

	if (proto == NULL)
		asprintf(&proto, DEFAULT_PROTOCOL);

	if ((strcmp(proto,"1") == 0) || (strcmp(proto, "2c")==0)) {	/* snmpv1 or snmpv2c */
		numauthpriv = 2;
		authpriv = calloc (numauthpriv, sizeof (char *));
		authpriv[0] = strdup ("-c");
		authpriv[1] = strdup (community);
	}
	else if ( strcmp (proto, "3") == 0 ) {		/* snmpv3 args */
		if (seclevel == NULL)
			asprintf(&seclevel, "noAuthNoPriv");

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
				asprintf(&authproto, DEFAULT_AUTH_PROTOCOL);

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
					asprintf(&privproto, DEFAULT_PRIV_PROTOCOL);

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
	if (strstr (str, "'") == str) {	/* handle SIMPLE quoted strings */
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
	if (strstr (str, "'") == str) {
		str[0] = 0;
		if (strlen (str) > 1) {
			str = strstr (str + 1, "'");
			return (++str);
		}
		else {
			return NULL;
		}
	}
	if (strstr (str, ",") == str) {
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

	printf (_(UT_HELP_VRSN));
	printf (_(UT_EXTRA_OPTS));

	printf (_(UT_HOST_PORT), 'p', DEFAULT_PORT);

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

	/* Tests Against Strings */
	printf (" %s\n", "-s, --string=STRING");
	printf ("    %s\n", _("Return OK state (for that OID) if STRING is an exact match"));
	printf (" %s\n", "-r, --ereg=REGEX");
	printf ("    %s\n", _("Return OK state (for that OID) if extended regular expression REGEX matches"));
	printf (" %s\n", "-R, --eregi=REGEX");
	printf ("    %s\n", _("Return OK state (for that OID) if case-insensitive extended REGEX matches"));
	printf (" %s\n", "-l, --label=STRING");
	printf ("    %s\n", _("Prefix label for output from plugin (default -s 'SNMP')"));

	/* Output Formatting */
	printf (" %s\n", "-u, --units=STRING");
	printf ("    %s\n", _("Units label(s) for output data (e.g., 'sec.')."));
	printf (" %s\n", "-D, --output-delimiter=STRING");
	printf ("    %s\n", _("Separates output on multiple OID requests"));

	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);
	printf (" %s\n", "-e, --retries=INTEGER");
	printf ("    %s\n", _("Number of retries to be used in the requests"));

	printf (_(UT_VERBOSE));

	printf ("\n");
	printf ("%s\n", _("This plugin uses the 'snmpget' command included with the NET-SNMP package."));
	printf ("%s\n", _("if you don't have the package installed, you will need to download it from"));
	printf ("%s\n", _("http://net-snmp.sourceforge.net before you can use this plugin."));

	printf ("\n");
	printf ("%s\n", _("Notes:"));
	printf (" %s\n", _("- Multiple OIDs may be indicated by a comma- or space-delimited list (lists with"));
	printf ("   %s\n", _("internal spaces must be quoted) [max 8 OIDs]"));

	printf(" -%s", _(UT_THRESHOLDS_NOTES));

	printf (" %s\n", _("- When checking multiple OIDs, separate ranges by commas like '-w 1:10,1:,:20'"));
	printf (" %s\n", _("- Note that only one string and one regex may be checked at present"));
	printf (" %s\n", _("- All evaluation methods other than PR, STR, and SUBSTR expect that the value"));
	printf ("   %s\n", _("returned from the SNMP query is an unsigned integer."));
#ifdef NP_EXTRA_OPTS
	printf (" -%s", _(UT_EXTRA_OPTS_NOTES));
#endif

	printf (_(UT_SUPPORT));
}



void
print_usage (void)
{
	printf (_("Usage:"));
	printf ("%s -H <ip_address> -o <OID> [-w warn_range] [-c crit_range]\n",progname);
	printf ("[-C community] [-s string] [-r regex] [-R regexi] [-t timeout] [-e retries]\n");
	printf ("[-l label] [-u units] [-p port-number] [-d delimiter] [-D output-delimiter]\n");
	printf ("[-m miblist] [-P snmp version] [-L seclevel] [-U secname] [-a authproto]\n");
	printf ("[-A authpasswd] [-x privproto] [-X privpasswd]\n");
}
