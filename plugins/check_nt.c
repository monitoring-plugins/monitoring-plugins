/******************************************************************************
 *
 * CHECK_NT.C
 *
 * Program: Windows NT plugin for NetSaint
 * License: GPL
 * Copyright (c) 2000-2002 Yves Rubin (rubiyz@yahoo.com)
 *
 * Description:
 * 
 * This requires NSClient software to run on NT (http://nsclient.ready2run.nl/)
 *
 * License Information:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/

//#include "stdlib.h"
#include "config.h"
#include "common.h"
#include "netutils.h"
#include "utils.h"

#define CHECK_NONE	0
#define CHECK_CLIENTVERSION  	1
#define CHECK_CPULOAD  	2
#define CHECK_UPTIME	3
#define CHECK_USEDDISKSPACE	4
#define CHECK_SERVICESTATE	5
#define CHECK_PROCSTATE	6
#define CHECK_MEMUSE	7
#define CHECK_COUNTER	8
#define CHECK_FILEAGE	9
#define MAX_VALUE_LIST 30

#define PORT	1248	

char *server_address=NULL;
char *volume_name=NULL;
int server_port=PORT;
char *value_list=NULL;
char *req_password=NULL;
unsigned long lvalue_list[MAX_VALUE_LIST];
unsigned long warning_value=0L;
unsigned long critical_value=0L;
int check_value_list=FALSE;
int check_warning_value=FALSE;
int check_critical_value=FALSE;
int vars_to_check=CHECK_NONE;
int show_all=FALSE;

const char *progname = "check_nt";

int process_arguments(int, char **);
void print_usage(void);
void print_help(void);
void preparelist(char *string);

int main(int argc, char **argv){
	int result;
	int return_code;
	char *send_buffer=NULL;
	char recv_buffer[MAX_INPUT_BUFFER];
	char *output_message=NULL;
	char *temp_buffer=NULL;
	char *temp_string=NULL;
	char *sep_string=NULL;
	char *description=NULL;

	double total_disk_space=0;
	double free_disk_space=0;
	double percent_used_space=0;
	double mem_commitLimit=0;
	double mem_commitByte=0;
	unsigned long current_connections=0;
	unsigned long utilization;
	unsigned long uptime;
	unsigned long cache_hits;
	unsigned long cache_buffers;
	unsigned long lru_time;
	unsigned long age_in_minutes;
	double counter_value;
	int offset=0;
	int updays=0;
	int uphours=0;
	int upminutes=0;
	asprintf(&req_password,"None");

	if(process_arguments(argc,argv)==ERROR)
		usage("Could not parse arguments\n");

	/* initialize alarm signal handling */
	signal(SIGALRM,socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm(socket_timeout);

	if (vars_to_check==CHECK_CLIENTVERSION) {
			
		asprintf(&send_buffer,strcat(req_password,"&1"));
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;
		asprintf(&output_message,recv_buffer);
		return_code=STATE_OK;
	}
	else if(vars_to_check==CHECK_CPULOAD){

		if (check_value_list==TRUE) {																			
			if (strtolarray(&lvalue_list,value_list,",")==TRUE) {
				// -l parameters is present with only integers
				return_code=STATE_OK;
				asprintf(&temp_string,"CPU Load");
				while (lvalue_list[0+offset]>0 && lvalue_list[0+offset]<=17280 && 
							lvalue_list[1+offset]>=0 && lvalue_list[1+offset]<=100 && 
							lvalue_list[2+offset]>=0 && lvalue_list[2+offset]<=100) {
					// loop until one of the parameters is wrong or not present

					// Send request and retrieve data
					asprintf(&send_buffer,"%s&2&%lu",req_password,lvalue_list[0+offset]);
					result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
					if(result!=STATE_OK)
						return result;

					if (!strncmp(recv_buffer,"ERROR",5)) {
						printf("NSClient - %s\n",recv_buffer);
						exit(STATE_UNKNOWN);
					}

					utilization=strtoul(recv_buffer,NULL,10);

					// Check if any of the request is in a warning or critical state
					if(utilization >= lvalue_list[2+offset])
						return_code=STATE_CRITICAL;
					else if(utilization >= lvalue_list[1+offset] && return_code<STATE_WARNING)
						return_code=STATE_WARNING;

					asprintf(&output_message," (%lu min. %lu%)",lvalue_list[0+offset], utilization);
					asprintf(&temp_string,"%s%s",temp_string,output_message);
					offset+=3;	//move accross the array 
				}		
				if (strlen(temp_string)>10) {
					// we had at least on loop
					asprintf(&output_message,"%s",temp_string);
				}	
				else
					asprintf(&output_message,"%s","not enough values for -l parameters");
					
			} else 
				asprintf(&output_message,"wrong -l parameter.");

		} else
			asprintf(&output_message,"missing -l parameters");
	}

	else if(vars_to_check==CHECK_UPTIME){

		asprintf(&send_buffer,strcat(req_password,"&3"));
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;

		if (!strncmp(recv_buffer,"ERROR",5)) {
			printf("NSClient - %s\n",recv_buffer);
			exit(STATE_UNKNOWN);
		}

		uptime=strtoul(recv_buffer,NULL,10);
		updays = uptime / 86400; 			
		uphours = (uptime % 86400) / 3600;
		upminutes = ((uptime % 86400) % 3600) / 60;
		asprintf(&output_message,"System Uptime : %u day(s) %u hour(s) %u minute(s)",updays,uphours, upminutes);
		return_code=STATE_OK;
	}

	else if(vars_to_check==CHECK_USEDDISKSPACE){

		return_code=STATE_UNKNOWN;	
		if (check_value_list==TRUE) {
			if (strlen(value_list)==1) {
				asprintf(&send_buffer,"%s&4&%s", req_password, value_list);
				result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
				if(result!=STATE_OK)
					return result;
		
				if (!strncmp(recv_buffer,"ERROR",5)) {
					printf("NSClient - %s\n",recv_buffer);
					exit(STATE_UNKNOWN);
				}

				free_disk_space=atof(strtok(recv_buffer,"&"));
				total_disk_space=atof(strtok(NULL,"&"));
				percent_used_space = ((total_disk_space - free_disk_space) / total_disk_space) * 100;

				if (free_disk_space>=0) {
					asprintf(&temp_string,"%s:\\ - total: %.2f Gb - used: %.2f Gb (%.0f%%) - free %.2f Gb (%.0f%%)",
							value_list, total_disk_space / 1073741824, (total_disk_space - free_disk_space) / 1073741824, percent_used_space,
							 free_disk_space / 1073741824, (free_disk_space / total_disk_space)*100); 


					if(check_critical_value==TRUE && percent_used_space >= critical_value)
						return_code=STATE_CRITICAL;
					else if (check_warning_value==TRUE && percent_used_space >= warning_value)
						return_code=STATE_WARNING;	
					else
						return_code=STATE_OK;	

					asprintf(&output_message,"%s",temp_string);

				}
				else {
					asprintf(&output_message,"Free disk space : Invalid drive ");
					return_code=STATE_UNKNOWN;
				}		
			}
			else 
				asprintf(&output_message,"wrong -l argument");
		} else 
			asprintf(&output_message,"missing -l parameters");
			
	}

	else if(vars_to_check==CHECK_SERVICESTATE || vars_to_check==CHECK_PROCSTATE){

		if (check_value_list==TRUE) {
			preparelist(value_list);		// replace , between services with & to send the request
			asprintf(&send_buffer,"%s&%u&%s&%s", req_password,(vars_to_check==CHECK_SERVICESTATE)?5:6,
				(show_all==TRUE)?"ShowAll":"ShowFail",value_list);
			result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
			if(result!=STATE_OK)
				return result;
	
			if (!strncmp(recv_buffer,"ERROR",5)) {
				printf("NSClient - %s\n",recv_buffer);
				exit(STATE_UNKNOWN);
			}
			return_code=atoi(strtok(recv_buffer,"&"));
			temp_string=strtok(NULL,"&");
			asprintf(&output_message, "%s",temp_string);
		}
		else 
			asprintf(&output_message,"No service/process specified");
	}

	else if(vars_to_check==CHECK_MEMUSE) {
		
		asprintf(&send_buffer,"%s&7", req_password);
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strncmp(recv_buffer,"ERROR",5)) {
			printf("NSClient - %s\n",recv_buffer);
			exit(STATE_UNKNOWN);
		}

		mem_commitLimit=atof(strtok(recv_buffer,"&"));
		mem_commitByte=atof(strtok(NULL,"&"));
		percent_used_space = (mem_commitByte / mem_commitLimit) * 100;
		asprintf(&output_message,"Memory usage: total:%.2f Mb - used: %.2f Mb (%.0f%%) - free: %.2f Mb (%.0f%%)", 
			mem_commitLimit / 1048576, mem_commitByte / 1048567, percent_used_space,  
			(mem_commitLimit - mem_commitByte) / 1048576, (mem_commitLimit - mem_commitByte) / mem_commitLimit * 100);
	
		if(check_critical_value==TRUE && percent_used_space >= critical_value)
			return_code=STATE_CRITICAL;
		else if (check_warning_value==TRUE && percent_used_space >= warning_value)
			return_code=STATE_WARNING;	
		else
			return_code=STATE_OK;	
		
	}

	else if(vars_to_check==CHECK_COUNTER) {

		if (check_value_list==TRUE) {																			
			preparelist(value_list);		// replace , between services with & to send the request
			asprintf(&send_buffer,"%s&8&%s", req_password,value_list);
			result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
			if (result!=STATE_OK)
				return result;
	
			if (!strncmp(recv_buffer,"ERROR",5)) {
				printf("NSClient - %s\n",recv_buffer);
				exit(STATE_UNKNOWN);
			}

			strtok(value_list,"&");			// burn the first parameters
			description = strtok(NULL,"&");
			counter_value = atof(recv_buffer);
			if (description == NULL) 
				asprintf(&output_message, "%.f", counter_value);
			else
				asprintf(&output_message, description, counter_value);
	
			if (critical_value > warning_value) {
				// Normal thresholds		
				if(check_critical_value==TRUE && counter_value >= critical_value)
					return_code=STATE_CRITICAL;
				else if (check_warning_value==TRUE && counter_value >= warning_value)
					return_code=STATE_WARNING;	
				else
					return_code=STATE_OK;	
			} 
			else {
				// inverse thresholds		
				if(check_critical_value==TRUE && counter_value <= critical_value)
					return_code=STATE_CRITICAL;
				else if (check_warning_value==TRUE && counter_value <= warning_value)
					return_code=STATE_WARNING;	
				else
					return_code=STATE_OK;	
			}	
		
		}
		else {
			asprintf(&output_message,"No counter specified");
			result=STATE_UNKNOWN;
		}
	}
	else if(vars_to_check==CHECK_FILEAGE) {

		if (check_value_list==TRUE) {																			
			preparelist(value_list);		// replace , between services with & to send the request
			asprintf(&send_buffer,"%s&9&%s", req_password,value_list);
			result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
			if (result!=STATE_OK)
				return result;
	
			if (!strncmp(recv_buffer,"ERROR",5)) {
				printf("NSClient - %s\n",recv_buffer);
				exit(STATE_UNKNOWN);
			}

			age_in_minutes = atoi(strtok(recv_buffer,"&"));
			description = strtok(NULL,"&");
			asprintf(&output_message, description);
	
			if (critical_value > warning_value) {
				// Normal thresholds		
				if(check_critical_value==TRUE && age_in_minutes >= critical_value)
					return_code=STATE_CRITICAL;
				else if (check_warning_value==TRUE && age_in_minutes >= warning_value)
					return_code=STATE_WARNING;	
				else
					return_code=STATE_OK;	
			} 
			else {
				// inverse thresholds		
				if(check_critical_value==TRUE && age_in_minutes <= critical_value)
					return_code=STATE_CRITICAL;
				else if (check_warning_value==TRUE && age_in_minutes <= warning_value)
					return_code=STATE_WARNING;	
				else
					return_code=STATE_OK;	
			}	
		
		}
		else {
			asprintf(&output_message,"No file specified");
			result=STATE_UNKNOWN;
		}
	}

	/* reset timeout */
	alarm(0);

	printf("%s\n",output_message);

	return return_code;
}


/* process command-line arguments */
int process_arguments(int argc, char **argv){
	int c;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] =
	{ 
		{"port",     required_argument,0,'p'},
		{"timeout",  required_argument,0,'t'},
		{"critical", required_argument,0,'c'},
		{"warning",  required_argument,0,'w'},
		{"variable", required_argument,0,'v'},
		{"hostname", required_argument,0,'H'},
		{"version",  no_argument,      0,'V'},
		{"help",     no_argument,      0,'h'},
		{0,0,0,0}
	};
#endif

	/* no options were supplied */
	if(argc<2) return ERROR;

	/* backwards compatibility */
	if (! is_option(argv[1])) {
		server_address=argv[1];
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

	while (1){
#ifdef HAVE_GETOPT_H
		c = getopt_long(argc,argv,"+hVH:t:c:w:p:v:l:s:d:",long_options,&option_index);
#else
		c = getopt(argc,argv,"+hVH:t:c:w:p:v:l:s:d:");
#endif

		if (c==-1||c==EOF||c==1)
			break;

		switch (c)
			{
			case '?': /* print short usage statement if args not parsable */
				printf("%s: Unknown argument: %s\n\n",progname,optarg);
				print_usage();
				exit(STATE_UNKNOWN);
			case 'h': /* help */
				print_help();
				exit(STATE_OK);
			case 'V': /* version */
				print_revision(progname,"$Revision$");
				exit(STATE_OK);
			case 'H': /* hostname */
				server_address=optarg;
				break;
			case 's': /* password */
				asprintf(&req_password,optarg);
				break;
			case 'p': /* port */
				if (is_intnonneg(optarg))
					server_port=atoi(optarg);
				else
					terminate(STATE_UNKNOWN,"Server port an integer (seconds)\nType '%s -h' for additional help\n",progname);
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
				else
					return ERROR;
				break;
			case 'l': /* value list */
				asprintf(&value_list,"%s",optarg);
				check_value_list=TRUE;
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
			case 't': /* timeout */
				socket_timeout=atoi(optarg);
				if(socket_timeout<=0)
					return ERROR;
			}

	}

	if (vars_to_check==CHECK_NONE)
		return ERROR;

	return OK;
}


void print_usage(void)
{
	printf("Usage: %s -H host -v variable [-p port] [-w warning] [-c critical] [-l params] [-d SHOWALL] [-t timeout]\n",progname);
}


void print_help(void)
{
	print_revision(progname,"$Revision$");
	printf
		("Copyright (c) 2000 Yves Rubin (rubiyz@yahoo.com)\n\n"
		 "This plugin attempts to contact the NSClient service running on a Windows NT/2000/XP server to\n"
		 "gather the requested system information.\n\n");
	print_usage();
  printf
		("\nOptions:\n"
		 "-H, --hostname=HOST\n"
		 "   Name of the host to check\n"
		 "-p, --port=INTEGER\n"
		 "   Optional port number (default: %d)\n"
		 "-s <password>\n"
		 "   Password needed for the request\n"
		 "-v, --variable=STRING\n"
		 "   Variable to check.  Valid variables are:\n"
		 "      CLIENTVERSION = Get the NSClient version\n"
		 "      CPULOAD = Average CPU load on last x minutes. Request a -l parameter with the following syntax:\n"
		 "        -l <minutes range>,<warning threshold>,<critical threshold>. <minute range> should be less than 24*60.\n"
		 "        Thresholds are percentage and up to 10 requests can be done in one shot. ie: -l 60,90,95,120,90,95\n"
		 "      UPTIME = Get the uptime of the machine. No specific parameters. No warning or critical threshold\n"
		 "      USEDDISKSPACE = Size and percentage of disk use. Request a -l parameter containing the drive letter only.\n"
		 "                      Warning and critical thresholds can be specified with -w and -c.\n"
		 "      MEMUSE = Memory use. Warning and critical thresholds can be specified with -w and -c.\n" 
		 "      SERVICESTATE = Check the state of one or several services. Request a -l parameters with the following syntax:\n"
		 "        -l <service1>,<service2>,<service3>,... You can specify -d SHOWALL in case you want to see working services\n"
		 "        in the returned string.\n"
		 "      PROCSTATE = Check if one or several process are running. Same syntax as SERVICESTATE.\n"
		 "      COUNTER = Check any performance counter of Windows NT/2000. Request a -l parameters with the following syntax:\n"
		 "        -l \"\\\\<performance object>\\\\counter\",\"<description>\"  The <description> parameter is optional and \n"
		 "        is given to a printf output command which require a float parameters. Some examples:\n"
		 "          \"Paging file usage is %%.2f %%%%\" or \"%%.f %%%% paging file used.\"\n"
		 " -w, --warning=INTEGER\n"
		 "   Threshold which will result in a warning status\n"
		 " -c, --critical=INTEGER\n"
		 "   Threshold which will result in a critical status\n"
		 " -t, --timeout=INTEGER\n"
		 "   Seconds before connection attempt times out (default: %d)\n"
		 "-h, --help\n"
		 "   Print this help screen\n"
		 "-V, --version\n"
		 "   Print version information\n\n"
		 "Notes:\n"
		 " - The NSClient service should be running on the server to get any information.\n"
		 " - Critical thresholds should be lower than warning thresholds\n", PORT, DEFAULT_SOCKET_TIMEOUT);
}

int strtolarray(unsigned long *array, char *string, char *delim) {
	// split a <delim> delimited string into a long array
	int idx=0;
	char *t1;

	for (idx=0;idx<MAX_VALUE_LIST;idx++)
		array[idx]=-1;
	
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
	// Replace all , with & which is the delimiter for the request
	int i;

	for (i = 0; i < strlen(string); i++)
		if (string[i] == ',') {
			string[i]='&';
		}
}

