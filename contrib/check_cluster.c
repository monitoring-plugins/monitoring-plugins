/*****************************************************************************
 *
 * CHECK_CLUSTER.C - Host and Service Cluster Plugin for NetSaint
 *
 * Copyright (c) 2000 Ethan Galstad (netsaint@netsaint.org)
 * License: GPL
 * Last Modified:   07-08-2000
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

typedef struct clustermember_struct{
	char *host_name;
	char *svc_description;
	struct clustermember_struct *next;
        }clustermember;


int check_cluster_status(void);
int add_clustermember(char *,char *);
void free_memory(void);

clustermember *clustermember_list=NULL;

int total_services_ok=0;
int total_services_warning=0;
int total_services_unknown=0;
int total_services_critical=0;

int total_hosts_up=0;
int total_hosts_down=0;
int total_hosts_unreachable=0;

char status_log[MAX_INPUT_BUFFER]="";
int warning_threshold=0;
int critical_threshold=0;

int check_type=CHECK_SERVICES;


int main(int argc, char **argv){
	char input_buffer[MAX_INPUT_BUFFER];
	char *host_name;
	char *svc_description;
	int return_code=STATE_OK;
	int error=FALSE;

	if(argc!=5){

		printf("Invalid arguments supplied\n");
		printf("\n");

		printf("Host/Service Cluster Plugin for NetSaint\n");
		printf("Copyright (c) 2000 Ethan Galstad (netsaint@netsaint.org)\n");
		printf("Last Modified: 07-08-2000\n");
		printf("License: GPL\n");
		printf("\n");
		printf("Usage: %s <--service | --host> <status_log> <warn_threshold> <crit_threshold>\n",argv[0]);
		printf("\n");
		printf("Options:\n");
		printf("   --service        = Check service cluster status\n");
		printf("   --host           = Check host cluster status\n");
		printf("   <status_log>     = This is the location of the NetSaint status log\n");
		printf("   <warn_threshold> = This is the number of hosts or services in\n");
		printf("                      the cluster that must be in a non-OK state\n");
		printf("                      in order to result in a warning status level\n");
		printf("   <crit_threshold> = This is the number of hosts or services in\n");
		printf("                      the cluster that must be in a non-OK state\n");
		printf("                      in order to result in a critical status level\n");
		printf("\n");
		printf("Notes:\n");
		printf("Members of the host or service cluster are read from STDIN.\n");
		printf("One host or service can be specified per line, services must\n");
		printf("be in the format of <host_name>;<svc_description>\n");
		printf("\n");

		return STATE_UNKNOWN;
	        }

	/* see if we're checking a host or service clust */
	if(!strcmp(argv[1],"--host"))
		check_type=CHECK_HOSTS;
	else
		check_type=CHECK_SERVICES;

	/* get the status log */
	strncpy(status_log,argv[2],sizeof(status_log)-1);
	status_log[sizeof(status_log)-1]='\x0';

	/* get the warning and critical thresholds */
	warning_threshold=atoi(argv[3]);
	critical_threshold=atoi(argv[4]);


	/* read all data from STDIN until there isn't anymore */
	while(fgets(input_buffer,sizeof(input_buffer)-1,stdin)){

		if(feof(stdin))
			break;

		/*strip(input_buffer);*/

		if(!strcmp(input_buffer,""))
			continue;

		if(!strcmp(input_buffer,"\n"))
			continue;

		/* get the host name */
		if(check_type==CHECK_SERVICES)
			host_name=(char *)strtok(input_buffer,";");
		else
			host_name=(char *)strtok(input_buffer,"\n");
		if(host_name==NULL || !strcmp(host_name,"")){
			printf("Error: Host name is NULL!\n");
			continue;
		        }

		if(check_type==CHECK_SERVICES){

			/* get the service description */
			svc_description=(char *)strtok(NULL,"\n");
			if(svc_description==NULL || !strcmp(svc_description,"")){
				printf("Error: Service description is NULL!\n");
				continue;
			        }
		        }

		/* add the cluster member to the list in memory */
		if(add_clustermember(host_name,svc_description)!=OK)
			printf("Error: Could not add cluster member\n");
#ifdef DEBUG
		else
			printf("Added cluster member\n");
#endif
	        }


	/* check the status of the cluster */
	if(check_cluster_status()==OK){
	
		if(check_type==CHECK_SERVICES){
			if((total_services_warning+total_services_unknown+total_services_critical) >= critical_threshold)
				return_code=STATE_CRITICAL;
			else if((total_services_warning+total_services_unknown+total_services_critical) >= warning_threshold)
				return_code=STATE_WARNING;
			else
				return_code=STATE_OK;
		
			printf("Service cluster %s: %d ok, %d warning, %d unknown, %d critical\n",(return_code==STATE_OK)?"ok":"problem",total_services_ok,total_services_warning,total_services_unknown,total_services_critical);
	                }
		else{
			if((total_hosts_down+total_hosts_unreachable) >= critical_threshold)
				return_code=STATE_CRITICAL;
			else if((total_hosts_down+total_hosts_unreachable) >= warning_threshold)
				return_code=STATE_WARNING;
			else
				return_code=STATE_OK;

			printf("Host cluster %s: %d up, %d down, %d unreachable\n",(return_code==STATE_OK)?"ok":"problem",total_hosts_up,total_hosts_down,total_hosts_unreachable);
	                }
	        }
	else
		return_code=STATE_UNKNOWN;

	free_memory();

	return return_code;
        }



int add_clustermember(char *hst,char *svc){
	clustermember *new_clustermember;

	new_clustermember=(clustermember *)malloc(sizeof(clustermember));
	if(new_clustermember==NULL)
		return ERROR;

	new_clustermember->host_name=NULL;
	new_clustermember->svc_description=NULL;

	if(hst!=NULL){
		new_clustermember->host_name=(char *)malloc(strlen(hst)+1);
		if(new_clustermember->host_name==NULL){
			free(new_clustermember);
			return ERROR;
		        }
		strcpy(new_clustermember->host_name,hst);
	        }

	if(svc!=NULL){
		new_clustermember->svc_description=(char *)malloc(strlen(svc)+1);
		if(new_clustermember->svc_description==NULL){
			if(new_clustermember->host_name!=NULL)
				free(new_clustermember->host_name);
			free(new_clustermember);
			return ERROR;
		        }
		strcpy(new_clustermember->svc_description,svc);
	        }

	new_clustermember->next=clustermember_list;
	clustermember_list=new_clustermember;

	return OK;
        }


void free_memory(void){
	clustermember *this_clustermember;
	clustermember *next_clustermember;

	for(this_clustermember=clustermember_list;this_clustermember!=NULL;this_clustermember=next_clustermember){
		next_clustermember=this_clustermember->next;
		if(this_clustermember->host_name!=NULL)
			free(this_clustermember->host_name);		
		if(this_clustermember->svc_description!=NULL)
			free(this_clustermember->svc_description);
		free(this_clustermember);
	        }

	return;
        }



int check_cluster_status(void){
	FILE *fp;
	clustermember *temp_clustermember;
	char input_buffer[MAX_INPUT_BUFFER];
	char matching_entry[MAX_INPUT_BUFFER];

	fp=fopen(status_log,"r");
	if(fp==NULL){
		printf("Error: Could not open status log '%s' for reading\n",status_log);
		return ERROR;
	        }

#ifdef DEBUG
	for(temp_clustermember=clustermember_list;temp_clustermember!=NULL;temp_clustermember=temp_clustermember->next){
		if(check_type==CHECK_HOSTS)
			printf("Cluster member: '%s'\n",temp_clustermember->host_name);
		else
			printf("Cluster member: '%s'/'%s'\n",temp_clustermember->host_name,temp_clustermember->svc_description);
	        }
#endif

	for(fgets(input_buffer,MAX_INPUT_BUFFER-1,fp);!feof(fp);fgets(input_buffer,MAX_INPUT_BUFFER-1,fp)){

		/* this is a host entry */
		if(strstr(input_buffer,"] HOST;") && check_type==CHECK_HOSTS){

			/* this this a match? */
			for(temp_clustermember=clustermember_list;temp_clustermember!=NULL;temp_clustermember=temp_clustermember->next){

				snprintf(matching_entry,sizeof(matching_entry)-1,";%s;",temp_clustermember->host_name);

				if(strstr(input_buffer,matching_entry)){
					if(strstr(input_buffer,";DOWN;"))
						total_hosts_down++;
					else if(strstr(input_buffer,";UNREACHABLE;"))
						total_hosts_unreachable++;
					else if(strstr(input_buffer,";UP;"))
						total_hosts_up++;
				        }
			        }
			
		        }

		/* this is a service entry */
		else if(strstr(input_buffer,"] SERVICE;") && check_type==CHECK_SERVICES){

			/* this this a match? */
			for(temp_clustermember=clustermember_list;temp_clustermember!=NULL;temp_clustermember=temp_clustermember->next){

				snprintf(matching_entry,sizeof(matching_entry)-1,";%s;%s;",temp_clustermember->host_name,temp_clustermember->svc_description);

				if(strstr(input_buffer,matching_entry)){
					if(strstr(input_buffer,";HOST DOWN;") || strstr(input_buffer,";UNREACHABLE;") || strstr(input_buffer,";CRITICAL;"))
						total_services_critical++;
					else if(strstr(input_buffer,";WARNING;"))
						total_services_warning++;
					else if(strstr(input_buffer,";UNKNOWN;"))
						total_services_unknown++;
					else if(strstr(input_buffer,";OK;") || strstr(input_buffer,";RECOVERY;"))
						total_services_ok++;
				        }
			        }
		
		        }
	        }

	fclose(fp);

	return OK;
        }
