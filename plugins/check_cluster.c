/*****************************************************************************
 *
 * CHECK_CLUSTER2.C - Host and Service Cluster Plugin for Nagios 2.x
 *
 * Copyright (c) 2000-2004 Ethan Galstad (nagios@nagios.org)
 * License: GPL
 * Last Modified:   03-11-2004
 *
 * License:
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#define OK		0
#define ERROR		-1

#define TRUE		1
#define FALSE		0

#define CHECK_SERVICES	1
#define CHECK_HOSTS	2

#define MAX_INPUT_BUFFER	1024

#define STATE_OK	0
#define STATE_WARNING	1
#define STATE_CRITICAL	2
#define STATE_UNKNOWN	3

int total_services_ok=0;
int total_services_warning=0;
int total_services_unknown=0;
int total_services_critical=0;

int total_hosts_up=0;
int total_hosts_down=0;
int total_hosts_unreachable=0;

int warning_threshold=1;
int critical_threshold=1;

int check_type=CHECK_SERVICES;

char *data_vals=NULL;
char *label=NULL;


int process_arguments(int,char **);



int main(int argc, char **argv){
	char input_buffer[MAX_INPUT_BUFFER];
	char *ptr;
	int data_val;
	int return_code=STATE_OK;
	int error=FALSE;

	if(process_arguments(argc,argv)==ERROR){

		printf("Invalid arguments supplied\n");
		printf("\n");

		printf("Host/Service Cluster Plugin for Nagios 2\n");
		printf("Copyright (c) 2000-2004 Ethan Galstad (nagios@nagios.org)\n");
		printf("Last Modified: 03-11-2004\n");
		printf("License: GPL\n");
		printf("\n");
		printf("Usage: %s (-s | -h) [-l label] [-w threshold] [-c threshold] [-d val1,val2,...,valn]\n",argv[0]);
		printf("\n");
		printf("Options:\n");
		printf("   -s, --service  = Check service cluster status\n");
		printf("   -h, --host     = Check host cluster status\n");
		printf("   -l, --label    = Optional prepended text output (i.e. \"Host cluster\")\n");
		printf("   -w, --warning  = Specifies the number of hosts or services in cluster that must be in\n");
		printf("                    a non-OK state in order to return a WARNING status level\n");
		printf("   -c, --critical = Specifies the number of hosts or services in cluster that must be in\n");
		printf("                    a non-OK state in order to return a CRITICAL status level\n");
		printf("   -d, --data     = The status codes of the hosts or services in the cluster, separated\n");
		printf("                    by commas\n");
		printf("\n");

		return STATE_UNKNOWN;
	        }

	/* check the data values */
	for(ptr=strtok(data_vals,",");ptr!=NULL;ptr=strtok(NULL,",")){

		data_val=atoi(ptr);

		if(check_type==CHECK_SERVICES){
			switch(data_val){
			case 0:
				total_services_ok++;
				break;
			case 1:
				total_services_warning++;
				break;
			case 2:
				total_services_critical++;
				break;
			case 3:
				total_services_unknown++;
				break;
			default:
				break;
			        }
		        }
		else{
			switch(data_val){
			case 0:
				total_hosts_up++;
				break;
			case 1:
				total_hosts_down++;
				break;
			case 2:
				total_hosts_unreachable++;
				break;
			default:
				break;
			        }
		        }
	        }
	

	/* return the status of the cluster */
	if(check_type==CHECK_SERVICES){
		if((total_services_warning+total_services_unknown+total_services_critical) >= critical_threshold)
			return_code=STATE_CRITICAL;
		else if((total_services_warning+total_services_unknown+total_services_critical) >= warning_threshold)
			return_code=STATE_WARNING;
		else
			return_code=STATE_OK;
		printf("%s %s: %d ok, %d warning, %d unknown, %d critical\n",(label==NULL)?"Service cluster":label,(return_code==STATE_OK)?"ok":"problem",total_services_ok,total_services_warning,total_services_unknown,total_services_critical);
                }
	else{
		if((total_hosts_down+total_hosts_unreachable) >= critical_threshold)
			return_code=STATE_CRITICAL;
		else if((total_hosts_down+total_hosts_unreachable) >= warning_threshold)
			return_code=STATE_WARNING;
		else
			return_code=STATE_OK;
		printf("%s %s: %d up, %d down, %d unreachable\n",(label==NULL)?"Host cluster":label,(return_code==STATE_OK)?"ok":"problem",total_hosts_up,total_hosts_down,total_hosts_unreachable);
                }

	return return_code;
        }



int process_arguments(int argc, char **argv){
	int c;
	int option=0;
	static struct option longopts[]={ 
		{"data",     required_argument,0,'d'},
		{"warning",  required_argument,0,'w'},
		{"critical", required_argument,0,'c'},
		{"label",    required_argument,0,'l'},
		{"host",     no_argument,      0,'h'},
		{"service",  no_argument,      0,'s'},
		{0,0,0,0}
		};

	/* no options were supplied */
	if(argc<2)
		return ERROR;

	while(1){

		c=getopt_long(argc,argv,"hsw:c:d:l:",longopts,&option);

		if(c==-1 || c==EOF || c==1)
			break;

		switch(c){

		case 'h': /* host cluster */
			check_type=CHECK_HOSTS;
			break;

		case 's': /* service cluster */
			check_type=CHECK_SERVICES;
			break;

		case 'w': /* warning threshold */
			warning_threshold=atoi(optarg);
			break;

		case 'c': /* warning threshold */
			critical_threshold=atoi(optarg);
			break;

		case 'd': /* data values */
			data_vals=(char *)strdup(optarg);
			break;

		case 'l': /* text label */
			label=(char *)strdup(optarg);
			break;

		default:
			return ERROR;
			break;
		        }
		}

	if(data_vals==NULL)
		return ERROR;

	return OK;
        }
