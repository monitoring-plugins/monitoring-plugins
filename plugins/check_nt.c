/*****************************************************************************
* 
* Nagios check_nt plugin
* 
* License: GPL
* Copyright (c) 2000-2002 Yves Rubin (rubiyz@yahoo.com)
* Copyright (c) 2003-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_nt plugin
* 
* This plugin collects data from the NSClient service running on a
* Windows NT/2000/XP/2003 server.
* This plugin requires NSClient software to run on NT
* (http://nsclient.ready2run.nl/)
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

const char *progname = "check_nt";
const char *copyright = "2000-2007";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"

enum checkvars {
	CHECK_NONE,
	CHECK_CLIENTVERSION,
	CHECK_CPULOAD,
	CHECK_UPTIME,
	CHECK_USEDDISKSPACE,
	CHECK_SERVICESTATE,
	CHECK_PROCSTATE,
	CHECK_MEMUSE,
	CHECK_COUNTER,
	CHECK_FILEAGE,
	CHECK_INSTANCES
};

enum {
	MAX_VALUE_LIST = 30,
	PORT = 1248
};

char *server_address=NULL;
char *volume_name=NULL;
int server_port=PORT;
char *value_list=NULL;
char *req_password=NULL;
unsigned long lvalue_list[MAX_VALUE_LIST];
unsigned long warning_value=0L;
unsigned long critical_value=0L;
int check_warning_value=FALSE;
int check_critical_value=FALSE;
enum checkvars vars_to_check = CHECK_NONE;
int show_all=FALSE;

char recv_buffer[MAX_INPUT_BUFFER];

void fetch_data (const char* address, int port, const char* sendb);
int process_arguments(int, char **);
void preparelist(char *string);
int strtoularray(unsigned long *array, char *string, const char *delim);
void print_help(void);
void print_usage(void);

int main(int argc, char **argv){

/* should be 	int result = STATE_UNKNOWN; */

	int return_code = STATE_UNKNOWN;
	char *send_buffer=NULL;
	char *output_message=NULL;
	char *perfdata=NULL;
	char *temp_string=NULL;
	char *temp_string_perf=NULL;
	char *description=NULL,*counter_unit = NULL;
	char *minval = NULL, *maxval = NULL, *errcvt = NULL;
	char *fds=NULL, *tds=NULL;
	char *numstr;

	double total_disk_space=0;
	double free_disk_space=0;
	double percent_used_space=0;
	double warning_used_space=0;
	double critical_used_space=0;
	double mem_commitLimit=0;
	double mem_commitByte=0;
	double fminval = 0, fmaxval = 0;
	unsigned long utilization;
	unsigned long uptime;
	unsigned long age_in_minutes;
	double counter_value = 0.0;
	int offset=0;
	int updays=0;
	int uphours=0;
	int upminutes=0;

	int isPercent = FALSE;
	int allRight = FALSE;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if(process_arguments(argc,argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* initialize alarm signal handling */
	signal(SIGALRM,socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm(socket_timeout);

	switch (vars_to_check) {

	case CHECK_CLIENTVERSION:

		xasprintf(&send_buffer, "%s&1", req_password);
		fetch_data (server_address, server_port, send_buffer);
		if (value_list != NULL && strcmp(recv_buffer, value_list) != 0) {
			xasprintf (&output_message, _("Wrong client version - running: %s, required: %s"), recv_buffer, value_list);
			return_code = STATE_WARNING;
		} else {
			xasprintf (&output_message, "%s", recv_buffer);
			return_code = STATE_OK;
		}
		break;

	case CHECK_CPULOAD:

		if (value_list==NULL)
			output_message = strdup (_("missing -l parameters"));
		else if (strtoularray(lvalue_list,value_list,",")==FALSE)
			output_message = strdup (_("wrong -l parameter."));
		else {
			/* -l parameters is present with only integers */
			return_code=STATE_OK;
			temp_string = strdup (_("CPU Load"));
			temp_string_perf = strdup (" ");

			/* loop until one of the parameters is wrong or not present */
			while (lvalue_list[0+offset]> (unsigned long)0 &&
						 lvalue_list[0+offset]<=(unsigned long)17280 &&
						 lvalue_list[1+offset]> (unsigned long)0 &&
						 lvalue_list[1+offset]<=(unsigned long)100 &&
						 lvalue_list[2+offset]> (unsigned long)0 &&
						 lvalue_list[2+offset]<=(unsigned long)100) {

				/* Send request and retrieve data */
				xasprintf(&send_buffer,"%s&2&%lu",req_password,lvalue_list[0+offset]);
				fetch_data (server_address, server_port, send_buffer);

				utilization=strtoul(recv_buffer,NULL,10);

				/* Check if any of the request is in a warning or critical state */
				if(utilization >= lvalue_list[2+offset])
					return_code=STATE_CRITICAL;
				else if(utilization >= lvalue_list[1+offset] && return_code<STATE_WARNING)
					return_code=STATE_WARNING;

				xasprintf(&output_message,_(" %lu%% (%lu min average)"), utilization, lvalue_list[0+offset]);
				xasprintf(&temp_string,"%s%s",temp_string,output_message);
				xasprintf(&perfdata,_(" '%lu min avg Load'=%lu%%;%lu;%lu;0;100"), lvalue_list[0+offset], utilization,
				  lvalue_list[1+offset], lvalue_list[2+offset]);
				xasprintf(&temp_string_perf,"%s%s",temp_string_perf,perfdata);
				offset+=3;	/* move across the array */
			}

			if (strlen(temp_string)>10) {  /* we had at least one loop */
				output_message = strdup (temp_string);
				perfdata = temp_string_perf;
			} else
				output_message = strdup (_("not enough values for -l parameters"));
		}
		break;

	case CHECK_UPTIME:

		xasprintf(&send_buffer, "%s&3", req_password);
		fetch_data (server_address, server_port, send_buffer);
		uptime=strtoul(recv_buffer,NULL,10);
		updays = uptime / 86400;
		uphours = (uptime % 86400) / 3600;
		upminutes = ((uptime % 86400) % 3600) / 60;
		xasprintf(&output_message,_("System Uptime - %u day(s) %u hour(s) %u minute(s)"),updays,uphours, upminutes);
		if (check_critical_value==TRUE && uptime <= critical_value)
			return_code=STATE_CRITICAL;
		else if (check_warning_value==TRUE && uptime <= warning_value)
			return_code=STATE_WARNING;
		else
			return_code=STATE_OK;
		break;

	case CHECK_USEDDISKSPACE:

		if (value_list==NULL)
			output_message = strdup (_("missing -l parameters"));
		else if (strlen(value_list)!=1)
			output_message = strdup (_("wrong -l argument"));
		else {
			xasprintf(&send_buffer,"%s&4&%s", req_password, value_list);
			fetch_data (server_address, server_port, send_buffer);
			fds=strtok(recv_buffer,"&");
			tds=strtok(NULL,"&");
			if(fds!=NULL)
				free_disk_space=atof(fds);
			if(tds!=NULL)
				total_disk_space=atof(tds);

			if (total_disk_space>0 && free_disk_space>=0) {
				percent_used_space = ((total_disk_space - free_disk_space) / total_disk_space) * 100;
				warning_used_space = ((float)warning_value / 100) * total_disk_space;
				critical_used_space = ((float)critical_value / 100) * total_disk_space;

				xasprintf(&temp_string,_("%s:\\ - total: %.2f Gb - used: %.2f Gb (%.0f%%) - free %.2f Gb (%.0f%%)"),
				  value_list, total_disk_space / 1073741824, (total_disk_space - free_disk_space) / 1073741824,
				  percent_used_space, free_disk_space / 1073741824, (free_disk_space / total_disk_space)*100);
				xasprintf(&temp_string_perf,_("'%s:\\ Used Space'=%.2fGb;%.2f;%.2f;0.00;%.2f"), value_list,
				  (total_disk_space - free_disk_space) / 1073741824, warning_used_space / 1073741824,
				  critical_used_space / 1073741824, total_disk_space / 1073741824);

				if(check_critical_value==TRUE && percent_used_space >= critical_value)
					return_code=STATE_CRITICAL;
				else if (check_warning_value==TRUE && percent_used_space >= warning_value)
					return_code=STATE_WARNING;
				else
					return_code=STATE_OK;

				output_message = strdup (temp_string);
				perfdata = temp_string_perf;
			} else {
				output_message = strdup (_("Free disk space : Invalid drive"));
				return_code=STATE_UNKNOWN;
			}
		}
		break;

	case CHECK_SERVICESTATE:
	case CHECK_PROCSTATE:

		if (value_list==NULL)
			output_message = strdup (_("No service/process specified"));
		else {
			preparelist(value_list);		/* replace , between services with & to send the request */
			xasprintf(&send_buffer,"%s&%u&%s&%s", req_password,(vars_to_check==CHECK_SERVICESTATE)?5:6,
							 (show_all==TRUE) ? "ShowAll" : "ShowFail",value_list);
			fetch_data (server_address, server_port, send_buffer);
			numstr = strtok(recv_buffer,"&");
			if (numstr == NULL)
				die(STATE_UNKNOWN, _("could not fetch information from server\n"));
			return_code=atoi(numstr);
			temp_string=strtok(NULL,"&");
			output_message = strdup (temp_string);
		}
		break;

	case CHECK_MEMUSE:

		xasprintf(&send_buffer,"%s&7", req_password);
		fetch_data (server_address, server_port, send_buffer);
		numstr = strtok(recv_buffer,"&");
		if (numstr == NULL)
			die(STATE_UNKNOWN, _("could not fetch information from server\n"));
		mem_commitLimit=atof(numstr);
		numstr = strtok(NULL,"&");
		if (numstr == NULL)
			die(STATE_UNKNOWN, _("could not fetch information from server\n"));
		mem_commitByte=atof(numstr);
		percent_used_space = (mem_commitByte / mem_commitLimit) * 100;
		warning_used_space = ((float)warning_value / 100) * mem_commitLimit;
		critical_used_space = ((float)critical_value / 100) * mem_commitLimit;

		/* Divisor should be 1048567, not 3044515, as we are measuring "Commit Charge" here,
		which equals RAM + Pagefiles. */
		xasprintf(&output_message,_("Memory usage: total:%.2f Mb - used: %.2f Mb (%.0f%%) - free: %.2f Mb (%.0f%%)"),
		  mem_commitLimit / 1048567, mem_commitByte / 1048567, percent_used_space,
		  (mem_commitLimit - mem_commitByte) / 1048567, (mem_commitLimit - mem_commitByte) / mem_commitLimit * 100);
		xasprintf(&perfdata,_("'Memory usage'=%.2fMb;%.2f;%.2f;0.00;%.2f"), mem_commitByte / 1048567,
		  warning_used_space / 1048567, critical_used_space / 1048567, mem_commitLimit / 1048567);

		return_code=STATE_OK;
		if(check_critical_value==TRUE && percent_used_space >= critical_value)
			return_code=STATE_CRITICAL;
		else if (check_warning_value==TRUE && percent_used_space >= warning_value)
			return_code=STATE_WARNING;

		break;

	case CHECK_COUNTER:


		/*
		CHECK_COUNTER has been modified to provide extensive perfdata information.
		In order to do this, some modifications have been done to the code
		and some constraints have been introduced.

		1) For the sake of simplicity of the code, perfdata information will only be
		 provided when the "description" field is added.

		2) If the counter you're going to measure is percent-based, the code will detect
		 the percent sign in its name and will attribute minimum (0%) and maximum (100%)
		 values automagically, as well the ¨%" sign to graph units.

		3) OTOH, if the counter is "absolute", you'll have to provide the following
		 the counter unit - that is, the dimensions of the counter you're getting. Examples:
		 pages/s, packets transferred, etc.

		4) If you want, you may provide the minimum and maximum values to expect. They aren't mandatory,
		 but once specified they MUST have the same order of magnitude and units of -w and -c; otherwise.
		 strange things will happen when you make graphs of your data.
		*/

		if (value_list == NULL)
			output_message = strdup (_("No counter specified"));
		else
		{
			preparelist (value_list);	/* replace , between services with & to send the request */
			isPercent = (strchr (value_list, '%') != NULL);

			strtok (value_list, "&");	/* burn the first parameters */
			description = strtok (NULL, "&");
			counter_unit = strtok (NULL, "&");
			xasprintf (&send_buffer, "%s&8&%s", req_password, value_list);
			fetch_data (server_address, server_port, send_buffer);
			counter_value = atof (recv_buffer);

			if (description == NULL)
			xasprintf (&output_message, "%.f", counter_value);
			else if (isPercent)
			{
				counter_unit = strdup ("%");
				allRight = TRUE;
			}

			if ((counter_unit != NULL) && (!allRight))
			{
				minval = strtok (NULL, "&");
				maxval = strtok (NULL, "&");

				/* All parameters specified. Let's check the numbers */

				fminval = (minval != NULL) ? strtod (minval, &errcvt) : -1;
				fmaxval = (minval != NULL) ? strtod (maxval, &errcvt) : -1;

				if ((fminval == 0) && (minval == errcvt))
					output_message = strdup (_("Minimum value contains non-numbers"));
				else
				{
					if ((fmaxval == 0) && (maxval == errcvt))
						output_message = strdup (_("Maximum value contains non-numbers"));
					else
						allRight = TRUE;	/* Everything is OK. */

				}
			}
			else if ((counter_unit == NULL) && (description != NULL))
				output_message = strdup (_("No unit counter specified"));

			if (allRight)
			{
				/* Let's format the output string, finally... */
					if (strstr(description, "%") == NULL) {
						xasprintf (&output_message, "%s = %.2f %s", description, counter_value, counter_unit);
					} else {
						/* has formatting, will segv if wrong */
						xasprintf (&output_message, description, counter_value);
					}
					xasprintf (&output_message, "%s |", output_message);
					xasprintf (&output_message,"%s %s", output_message,
						fperfdata (description, counter_value,
							counter_unit, 1, warning_value, 1, critical_value,
							(!(isPercent) && (minval != NULL)), fminval,
							(!(isPercent) && (minval != NULL)), fmaxval));
			}
		}

		if (critical_value > warning_value)
		{			/* Normal thresholds */
			if (check_critical_value == TRUE && counter_value >= critical_value)
				return_code = STATE_CRITICAL;
			else if (check_warning_value == TRUE && counter_value >= warning_value)
				return_code = STATE_WARNING;
			else
				return_code = STATE_OK;
		}
		else
		{			/* inverse thresholds */
			return_code = STATE_OK;
			if (check_critical_value == TRUE && counter_value <= critical_value)
				return_code = STATE_CRITICAL;
			else if (check_warning_value == TRUE && counter_value <= warning_value)
				return_code = STATE_WARNING;
		}
	break;

	case CHECK_FILEAGE:

		if (value_list==NULL)
			output_message = strdup (_("No counter specified"));
		else {
			preparelist(value_list);		/* replace , between services with & to send the request */
			xasprintf(&send_buffer,"%s&9&%s", req_password,value_list);
			fetch_data (server_address, server_port, send_buffer);
			age_in_minutes = atoi(strtok(recv_buffer,"&"));
			description = strtok(NULL,"&");
			output_message = strdup (description);

			if (critical_value > warning_value) {        /* Normal thresholds */
				if(check_critical_value==TRUE && age_in_minutes >= critical_value)
					return_code=STATE_CRITICAL;
				else if (check_warning_value==TRUE && age_in_minutes >= warning_value)
					return_code=STATE_WARNING;
				else
					return_code=STATE_OK;
			}
			else {                                       /* inverse thresholds */
				if(check_critical_value==TRUE && age_in_minutes <= critical_value)
					return_code=STATE_CRITICAL;
				else if (check_warning_value==TRUE && age_in_minutes <= warning_value)
					return_code=STATE_WARNING;
				else
					return_code=STATE_OK;
			}
		}
		break;

	case CHECK_INSTANCES:
		if (value_list==NULL)
			output_message = strdup (_("No counter specified"));
		else {
			xasprintf(&send_buffer,"%s&10&%s", req_password,value_list);
			fetch_data (server_address, server_port, send_buffer);
			if (!strncmp(recv_buffer,"ERROR",5)) {
				printf("NSClient - %s\n",recv_buffer);
				exit(STATE_UNKNOWN);
			}
			xasprintf(&output_message,"%s",recv_buffer);
			return_code=STATE_OK;
		}
		break;

	case CHECK_NONE:
	default:
		usage4 (_("Please specify a variable to check"));
		break;

	}

	/* reset timeout */
	alarm(0);

	if (perfdata==NULL)
		printf("%s\n",output_message);
	else
		printf("%s | %s\n",output_message,perfdata);
	return return_code;
}



/* process command-line arguments */
int process_arguments(int argc, char **argv){
	int c;

	int option = 0;
	static struct option longopts[] =
	{
		{"port",     required_argument,0,'p'},
		{"timeout",  required_argument,0,'t'},
		{"critical", required_argument,0,'c'},
		{"warning",  required_argument,0,'w'},
		{"variable", required_argument,0,'v'},
		{"hostname", required_argument,0,'H'},
		{"params",   required_argument,0,'l'},
		{"secret",   required_argument,0,'s'},
		{"display",  required_argument,0,'d'},
		{"unknown-timeout", no_argument, 0, 'u'},
		{"version",  no_argument,      0,'V'},
		{"help",     no_argument,      0,'h'},
		{0,0,0,0}
	};

	/* no options were supplied */
	if(argc<2) return ERROR;

	/* backwards compatibility */
	if (! is_option(argv[1])) {
		server_address = strdup(argv[1]);
		argv[1]=argv[0];
		argv=&argv[1];
		argc--;
	}

	for (c=1;c<argc;c++) {
		if(strcmp("-to",argv[c])==0)
			strcpy(argv[c],"-t");
		else if (strcmp("-wv",argv[c])==0)
			strcpy(argv[c],"-w");
		else if (strcmp("-cv",argv[c])==0)
			strcpy(argv[c],"-c");
	}

	while (1) {
		c = getopt_long(argc,argv,"+hVH:t:c:w:p:v:l:s:d:u",longopts,&option);

		if (c==-1||c==EOF||c==1)
			break;

		switch (c) {
			case '?': /* print short usage statement if args not parsable */
			usage5 ();
			case 'h': /* help */
				print_help();
				exit(STATE_OK);
			case 'V': /* version */
				print_revision(progname, NP_VERSION);
				exit(STATE_OK);
			case 'H': /* hostname */
				server_address = optarg;
				break;
			case 's': /* password */
				req_password = optarg;
				break;
			case 'p': /* port */
				if (is_intnonneg(optarg))
					server_port=atoi(optarg);
				else
					die(STATE_UNKNOWN,_("Server port must be an integer\n"));
				break;
			case 'v':
				if(strlen(optarg)<4)
					return ERROR;
				if(!strcmp(optarg,"CLIENTVERSION"))
					vars_to_check=CHECK_CLIENTVERSION;
				else if(!strcmp(optarg,"CPULOAD"))
					vars_to_check=CHECK_CPULOAD;
				else if(!strcmp(optarg,"UPTIME"))
					vars_to_check=CHECK_UPTIME;
				else if(!strcmp(optarg,"USEDDISKSPACE"))
					vars_to_check=CHECK_USEDDISKSPACE;
				else if(!strcmp(optarg,"SERVICESTATE"))
					vars_to_check=CHECK_SERVICESTATE;
				else if(!strcmp(optarg,"PROCSTATE"))
					vars_to_check=CHECK_PROCSTATE;
				else if(!strcmp(optarg,"MEMUSE"))
					vars_to_check=CHECK_MEMUSE;
				else if(!strcmp(optarg,"COUNTER"))
					vars_to_check=CHECK_COUNTER;
				else if(!strcmp(optarg,"FILEAGE"))
					vars_to_check=CHECK_FILEAGE;
				else if(!strcmp(optarg,"INSTANCES"))
					vars_to_check=CHECK_INSTANCES;
				else
					return ERROR;
				break;
			case 'l': /* value list */
				value_list = optarg;
				break;
			case 'w': /* warning threshold */
				warning_value=strtoul(optarg,NULL,10);
				check_warning_value=TRUE;
				break;
			case 'c': /* critical threshold */
				critical_value=strtoul(optarg,NULL,10);
				check_critical_value=TRUE;
				break;
			case 'd': /* Display select for services */
				if (!strcmp(optarg,"SHOWALL"))
					show_all = TRUE;
				break;
			case 'u':
				socket_timeout_state=STATE_UNKNOWN;
				break;
			case 't': /* timeout */
				socket_timeout=atoi(optarg);
				if(socket_timeout<=0)
					return ERROR;
			}

	}
	if (server_address == NULL)
		usage4 (_("You must provide a server address or host name"));

	if (vars_to_check==CHECK_NONE)
		return ERROR;

	if (req_password == NULL)
		req_password = strdup (_("None"));

	return OK;
}



void fetch_data (const char *address, int port, const char *sendb) {
	int result;

	result=process_tcp_request(address, port, sendb, recv_buffer,sizeof(recv_buffer));

	if(result!=STATE_OK)
		die (result, _("could not fetch information from server\n"));

	if (!strncmp(recv_buffer,"ERROR",5))
		die (STATE_UNKNOWN, "NSClient - %s\n",recv_buffer);
}

int strtoularray(unsigned long *array, char *string, const char *delim) {
	/* split a <delim> delimited string into a long array */
	int idx=0;
	char *t1;

	for (idx=0;idx<MAX_VALUE_LIST;idx++)
		array[idx]=0;

	idx=0;
	for(t1 = strtok(string,delim);t1 != NULL; t1 = strtok(NULL, delim)) {
		if (is_numeric(t1) && idx<MAX_VALUE_LIST) {
			array[idx]=strtoul(t1,NULL,10);
			idx++;
		} else
			return FALSE;
	}
	return TRUE;
}

void preparelist(char *string) {
	/* Replace all , with & which is the delimiter for the request */
	int i;

	for (i = 0; (size_t)i < strlen(string); i++)
		if (string[i] == ',') {
			string[i]='&';
		}
}



void print_help(void)
{
	print_revision(progname, NP_VERSION);

	printf ("Copyright (c) 2000 Yves Rubin (rubiyz@yahoo.com)\n");
	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("This plugin collects data from the NSClient service running on a"));
	printf ("%s\n", _("Windows NT/2000/XP/2003 server."));

	printf ("\n\n");

	print_usage();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf ("%s\n", _("Options:"));
	printf (" %s\n", "-H, --hostname=HOST");
	printf ("   %s\n", _("Name of the host to check"));
	printf (" %s\n", "-p, --port=INTEGER");
	printf ("   %s", _("Optional port number (default: "));
	printf ("%d)\n", PORT);
	printf (" %s\n", "-s, --secret=<password>");
	printf ("   %s\n", _("Password needed for the request"));
	printf (" %s\n", "-w, --warning=INTEGER");
	printf ("   %s\n", _("Threshold which will result in a warning status"));
	printf (" %s\n", "-c, --critical=INTEGER");
	printf ("   %s\n", _("Threshold which will result in a critical status"));
	printf (" %s\n", "-t, --timeout=INTEGER");
	printf ("   %s", _("Seconds before connection attempt times out (default: "));
	printf (" %s\n", "-l, --params=<parameters>");
	printf ("   %s", _("Parameters passed to specified check (see below)"));
	printf (" %s\n", "-d, --display={SHOWALL}");
	printf ("   %s", _("Display options (currently only SHOWALL works)"));
	printf (" %s\n", "-u, --unknown-timeout");
	printf ("   %s", _("Return UNKNOWN on timeouts"));
	printf ("%d)\n", DEFAULT_SOCKET_TIMEOUT);
	printf (" %s\n", "-h, --help");
	printf ("   %s\n", _("Print this help screen"));
	printf (" %s\n", "-V, --version");
	printf ("   %s\n", _("Print version information"));
	printf (" %s\n", "-v, --variable=STRING");
	printf ("   %s\n\n", _("Variable to check"));
	printf ("%s\n", _("Valid variables are:"));
	printf (" %s", "CLIENTVERSION =");
	printf (" %s\n", _("Get the NSClient version"));
	printf ("  %s\n", _("If -l <version> is specified, will return warning if versions differ."));
	printf (" %s\n", "CPULOAD =");
	printf ("  %s\n", _("Average CPU load on last x minutes."));
	printf ("  %s\n", _("Request a -l parameter with the following syntax:"));
	printf ("  %s\n", _("-l <minutes range>,<warning threshold>,<critical threshold>."));
	printf ("  %s\n", _("<minute range> should be less than 24*60."));
	printf ("  %s\n", _("Thresholds are percentage and up to 10 requests can be done in one shot."));
	printf ("  %s\n", "ie: -l 60,90,95,120,90,95");
	printf (" %s\n", "UPTIME =");
	printf ("  %s\n", _("Get the uptime of the machine."));
	printf ("  %s\n", _("No specific parameters. No warning or critical threshold"));
	printf (" %s\n", "USEDDISKSPACE =");
	printf ("  %s\n", _("Size and percentage of disk use."));
	printf ("  %s\n", _("Request a -l parameter containing the drive letter only."));
	printf ("  %s\n", _("Warning and critical thresholds can be specified with -w and -c."));
	printf (" %s\n", "MEMUSE =");
	printf ("  %s\n", _("Memory use."));
	printf ("  %s\n", _("Warning and critical thresholds can be specified with -w and -c."));
	printf (" %s\n", "SERVICESTATE =");
	printf ("  %s\n", _("Check the state of one or several services."));
	printf ("  %s\n", _("Request a -l parameters with the following syntax:"));
	printf ("  %s\n", _("-l <service1>,<service2>,<service3>,..."));
	printf ("  %s\n", _("You can specify -d SHOWALL in case you want to see working services"));
	printf ("  %s\n", _("in the returned string."));
	printf (" %s\n", "PROCSTATE =");
	printf ("  %s\n", _("Check if one or several process are running."));
	printf ("  %s\n", _("Same syntax as SERVICESTATE."));
	printf (" %s\n", "COUNTER =");
	printf ("  %s\n", _("Check any performance counter of Windows NT/2000."));
	printf ("	%s\n", _("Request a -l parameters with the following syntax:"));
	printf ("	%s\n", _("-l \"\\\\<performance object>\\\\counter\",\"<description>"));
	printf ("	%s\n", _("The <description> parameter is optional and is given to a printf "));
	printf ("  %s\n", _("output command which requires a float parameter."));
	printf ("  %s\n", _("If <description> does not include \"%%\", it is used as a label."));
	printf ("  %s\n", _("Some examples:"));
	printf ("  %s\n", "\"Paging file usage is %%.2f %%%%\"");
	printf ("  %s\n", "\"%%.f %%%% paging file used.\"");
	printf (" %s\n", "INSTANCES =");
	printf ("  %s\n", _("Check any performance counter object of Windows NT/2000."));
	printf ("  %s\n", _("Syntax: check_nt -H <hostname> -p <port> -v INSTANCES -l <counter object>"));
	printf ("  %s\n", _("<counter object> is a Windows Perfmon Counter object (eg. Process),"));
	printf ("  %s\n", _("if it is two words, it should be enclosed in quotes"));
	printf ("  %s\n", _("The returned results will be a comma-separated list of instances on "));
	printf ("  %s\n", _(" the selected computer for that object."));
	printf ("  %s\n", _("The purpose of this is to be run from command line to determine what instances"));
	printf ("  %s\n", _(" are available for monitoring without having to log onto the Windows server"));
	printf ("  %s\n", _("  to run Perfmon directly."));
	printf ("  %s\n", _("It can also be used in scripts that automatically create Nagios service"));
	printf ("  %s\n", _(" configuration files."));
	printf ("  %s\n", _("Some examples:"));
	printf ("  %s\n\n", _("check_nt -H 192.168.1.1 -p 1248 -v INSTANCES -l Process"));

	printf ("%s\n", _("Notes:"));
	printf (" %s\n", _("- The NSClient service should be running on the server to get any information"));
	printf ("   %s\n", "(http://nsclient.ready2run.nl).");
	printf (" %s\n", _("- Critical thresholds should be lower than warning thresholds"));
	printf (" %s\n", _("- Default port 1248 is sometimes in use by other services. The error"));
	printf ("   %s\n", _("output when this happens contains \"Cannot map xxxxx to protocol number\"."));
	printf ("   %s\n", _("One fix for this is to change the port to something else on check_nt "));
	printf ("   %s\n", _("and on the client service it\'s connecting to."));

	printf (UT_SUPPORT);
}



void print_usage(void)
{
	printf ("%s\n", _("Usage:"));
	printf ("%s -H host -v variable [-p port] [-w warning] [-c critical]\n",progname);
	printf ("[-l params] [-d SHOWALL] [-u] [-t timeout]\n");
}

