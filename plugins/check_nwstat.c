/******************************************************************************
 *
 * Program: NetWare statistics plugin for Nagios
 * License: GPL
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
 * $Id$
 *
 *****************************************************************************/

const char *progname = "check_nwstat";
#define REVISION "$Revision$"
#define COPYRIGHT "Copyright (c) 1999-2001 Ethan Galstad"

#define SUMMARY "\
This plugin attempts to contact the MRTGEXT NLM running on a Novell server\n\
to gather the requested system information.\n"

#define OPTIONS "\
-H host [-v variable] [-w warning] [-c critical]\n\
              [-p port] [-t timeout]"

#define LONGOPTIONS "\
-H, --hostname=HOST\n\
  Name of the host to check\n\
-v, --variable=STRING\n\
  Variable to check.  Valid variables include:\n\
     LOAD1    = 1 minute average CPU load\n\
     LOAD5    = 5 minute average CPU load\n\
     LOAD15   = 15 minute average CPU load\n\
     CONNS    = number of currently licensed connections\n\
     VPF<vol> = percent free space on volume <vol>\n\
     VKF<vol> = KB of free space on volume <vol>\n\
     LTCH     = percent long term cache hits\n\
     CBUFF    = current number of cache buffers\n\
     CDBUFF   = current number of dirty cache buffers\n\
     LRUM     = LRU sitting time in minutes\n\
     DSDB     = check to see if DS Database is open\n\
     LOGINS   = check to see if logins are enabled\n\
     UPRB     = used packet receive buffers\n\
     PUPRB    = percent (of max) used packet receive buffers\n\
     SAPENTRIES = number of entries in the SAP table\n\
     SAPENTRIES<n> = number of entries in the SAP table for SAP type <n>\n\
     OFILES   = number of open files\n\
     VPP<vol> = percent purgeable space on volume <vol>\n\
     VKP<vol> = KB of purgeable space on volume <vol>\n\
     VPNP<vol> = percent not yet purgeable space on volume <vol>\n\
     VKNP<vol> = KB of not yet purgeable space on volume <vol>\n\
     ABENDS   = number of abended threads (NW 5.x only)\n\
     CSPROCS  = number of current service processes (NW 5.x only)\n\
-w, --warning=INTEGER\n\
  Threshold which will result in a warning status\n\
-c, --critical=INTEGER\n\
  Threshold which will result in a critical status\n\
-p, --port=INTEGER\n\
  Optional port number (default: %d)\n\
-t, --timeout=INTEGER\n\
  Seconds before connection attempt times out (default: %d)\n\
-o, --osversion\n\
  Include server version string in results\n\
-h, --help\n\
  Print this help screen\n\
-V, --version\n\
  Print version information\n"

#define DESCRIPTION "\
Notes:\n\
- This plugin requres that the MRTGEXT.NLM file from James Drews' MRTG\n\
  extension for NetWare be loaded on the Novell servers you wish to check.\n\
  (available from http://www.engr.wisc.edu/~drews/mrtg/)\n\
- Values for critical thresholds should be lower than warning thresholds\n\
  when the following variables are checked: VPF, VKF, LTCH, CBUFF, and LRUM.\n"

#include "config.h"
#include "common.h"
#include "netutils.h"
#include "utils.h"

#define CHECK_NONE           0
#define CHECK_LOAD1          1 /* check 1 minute CPU load */
#define CHECK_LOAD5          2 /* check 5 minute CPU load */
#define CHECK_LOAD15         3 /* check 15 minute CPU load */
#define CHECK_CONNS          4 /* check number of connections */
#define CHECK_VPF            5 /* check % free space on volume */
#define CHECK_VKF            6 /* check KB free space on volume */
#define CHECK_LTCH           7 /* check long-term cache hit percentage */
#define CHECK_CBUFF          8 /* check total cache buffers */
#define CHECK_CDBUFF         9 /* check dirty cache buffers */
#define CHECK_LRUM          10 /* check LRU sitting time in minutes */
#define CHECK_DSDB          11 /* check to see if DS Database is open */
#define CHECK_LOGINS        12 /* check to see if logins are enabled */
#define CHECK_PUPRB         13 /* check % of used packet receive buffers */
#define CHECK_UPRB          14 /* check used packet receive buffers */
#define CHECK_SAPENTRIES    15 /* check SAP entries */
#define CHECK_OFILES        16 /* check number of open files */
#define CHECK_VKP           17 /* check KB purgeable space on volume */
#define CHECK_VPP           18 /* check % purgeable space on volume */
#define CHECK_VKNP          19 /* check KB not yet purgeable space on volume */
#define CHECK_VPNP          20 /* check % not yet purgeable space on volume */
#define CHECK_ABENDS        21 /* check abended thread count */
#define CHECK_CSPROCS       22 /* check number of current service processes */

#define PORT 9999

char *server_address=NULL;
char *volume_name=NULL;
int server_port=PORT;
unsigned long warning_value=0L;
unsigned long critical_value=0L;
int check_warning_value=FALSE;
int check_critical_value=FALSE;
int check_netware_version=FALSE;
unsigned long vars_to_check=CHECK_NONE;
int sap_number=-1;

int process_arguments(int, char **);
void print_usage(void);
void print_help(void);

int main(int argc, char **argv){
	int result;
	char *send_buffer=NULL;
	char recv_buffer[MAX_INPUT_BUFFER];
	char *output_message=NULL;
	char *temp_buffer=NULL;
	char *netware_version=NULL;

	int open_files=0;
	int abended_threads=0;
	int max_service_processes=0;
	int current_service_processes=0;
	unsigned long free_disk_space=0L;
	unsigned long total_disk_space=0L;
	unsigned long purgeable_disk_space=0L;
	unsigned long non_purgeable_disk_space=0L;
	int percent_free_space=0;
	int percent_purgeable_space=0;
	int percent_non_purgeable_space=0;
	unsigned long current_connections=0L;
	unsigned long utilization=0L;
	int cache_hits=0;
	unsigned long cache_buffers=0L;
	unsigned long lru_time=0L;
	char uptime[MAX_INPUT_BUFFER];
	int max_packet_receive_buffers=0;
	int used_packet_receive_buffers=0;
	unsigned long percent_used_packet_receive_buffers=0L;
	int sap_entries=0;

	if(process_arguments(argc,argv)==ERROR)
		usage("Could not parse arguments\n");

	/* initialize alarm signal handling */
	signal(SIGALRM,socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm(socket_timeout);
	
	/* get OS version string */
	if (check_netware_version==TRUE) {
		send_buffer = strscpy(send_buffer,"S19\r\n");
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;
		if(!strcmp(recv_buffer,"-1\n"))
			asprintf(&netware_version,"");
		else {
			recv_buffer[strlen(recv_buffer)-1]=0;
			asprintf(&netware_version,"NetWare %s: ",recv_buffer);
		}
	} else
		asprintf(&netware_version,"");


	/* check CPU load */
	if (vars_to_check==CHECK_LOAD1 || vars_to_check==CHECK_LOAD5 || vars_to_check==CHECK_LOAD15) {
			
		switch(vars_to_check){
		case CHECK_LOAD1:
			temp_buffer = strscpy(temp_buffer,"1");
			break;
		case CHECK_LOAD5:
			temp_buffer = strscpy(temp_buffer,"5");
			break;
		default:
			temp_buffer = strscpy(temp_buffer,"15");
			break;
		}

		asprintf(&send_buffer,"UTIL%s\r\n",temp_buffer);
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;
		utilization=strtoul(recv_buffer,NULL,10);
		send_buffer = strscpy(send_buffer,"UPTIME\r\n");
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;
		recv_buffer[strlen(recv_buffer)-1]=0;
		sprintf(uptime,"Up %s,",recv_buffer);

		if(check_critical_value==TRUE && utilization >= critical_value)
			result=STATE_CRITICAL;
		else if(check_warning_value==TRUE && utilization >= warning_value)
			result=STATE_WARNING;

		asprintf(&output_message,"Load %s - %s %s-min load average = %lu%%",(result==STATE_OK)?"ok":"problem",uptime,temp_buffer,utilization);

	/* check number of user connections */
	} else if (vars_to_check==CHECK_CONNS) {

		send_buffer = strscpy(send_buffer,"CONNECT\r\n");
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;
		current_connections=strtoul(recv_buffer,NULL,10);

		if(check_critical_value==TRUE && current_connections >= critical_value)
			result=STATE_CRITICAL;
		else if(check_warning_value==TRUE && current_connections >= warning_value)
			result=STATE_WARNING;
		asprintf(&output_message,"Conns %s - %lu current connections",(result==STATE_OK)?"ok":"problem",current_connections);

	/* check % long term cache hits */
	} else if (vars_to_check==CHECK_LTCH) {

		send_buffer = strscpy(send_buffer,"S1\r\n");
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;
		cache_hits=atoi(recv_buffer);

		if(check_critical_value==TRUE && cache_hits <= critical_value)
			result=STATE_CRITICAL;
		else if(check_warning_value==TRUE && cache_hits <= warning_value)
			result=STATE_WARNING;
		asprintf(&output_message,"Long term cache hits = %d%%",cache_hits);

	/* check cache buffers */
	} else if (vars_to_check==CHECK_CBUFF) {

		send_buffer = strscpy(send_buffer,"S2\r\n");
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;
		cache_buffers=strtoul(recv_buffer,NULL,10);

		if(check_critical_value==TRUE && cache_buffers <= critical_value)
			result=STATE_CRITICAL;
		else if(check_warning_value==TRUE && cache_buffers <= warning_value)
			result=STATE_WARNING;
		asprintf(&output_message,"Total cache buffers = %lu",cache_buffers);

	/* check dirty cache buffers */
	} else if (vars_to_check==CHECK_CDBUFF) {

		send_buffer = strscpy(send_buffer,"S3\r\n");
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;
		cache_buffers=strtoul(recv_buffer,NULL,10);

		if(check_critical_value==TRUE && cache_buffers >= critical_value)
			result=STATE_CRITICAL;
		else if(check_warning_value==TRUE && cache_buffers >= warning_value)
			result=STATE_WARNING;
		asprintf(&output_message,"Dirty cache buffers = %lu",cache_buffers);

	/* check LRU sitting time in minutes */
	} else if (vars_to_check==CHECK_LRUM) {

		send_buffer = strscpy(send_buffer,"S5\r\n");
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;
		lru_time=strtoul(recv_buffer,NULL,10);

		if(check_critical_value==TRUE && lru_time <= critical_value)
			result=STATE_CRITICAL;
		else if(check_warning_value==TRUE && lru_time <= warning_value)
			result=STATE_WARNING;
		asprintf(&output_message,"LRU sitting time = %lu minutes",lru_time);


	/* check KB free space on volume */
	} else if (vars_to_check==CHECK_VKF) {

		asprintf(&send_buffer,"VKF%s\r\n",volume_name);
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			asprintf(&output_message,"Error: Volume '%s' does not exist!",volume_name);
			result=STATE_CRITICAL;
		}	else {
			free_disk_space=strtoul(recv_buffer,NULL,10);
			if(check_critical_value==TRUE && free_disk_space <= critical_value)
				result=STATE_CRITICAL;
			else if(check_warning_value==TRUE && free_disk_space <= warning_value)
				result=STATE_WARNING;
			asprintf(&output_message,"%s%lu KB free on volume %s",(result==STATE_OK)?"":"Only ",free_disk_space,volume_name);
		}

	/* check % free space on volume */
	} else if (vars_to_check==CHECK_VPF) {

		asprintf(&send_buffer,"VKF%s\r\n",volume_name);
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;

		if(!strcmp(recv_buffer,"-1\n")){

			asprintf(&output_message,"Error: Volume '%s' does not exist!",volume_name);
			result=STATE_CRITICAL;

		} else {

			free_disk_space=strtoul(recv_buffer,NULL,10);

			asprintf(&send_buffer,"VKS%s\r\n",volume_name);
			result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
			if(result!=STATE_OK)
				return result;
			total_disk_space=strtoul(recv_buffer,NULL,10);

			percent_free_space=(int)(((double)free_disk_space/(double)total_disk_space)*100.0);

			if(check_critical_value==TRUE && percent_free_space <= critical_value)
				result=STATE_CRITICAL;
			else if(check_warning_value==TRUE && percent_free_space <= warning_value)
				result=STATE_WARNING;
			free_disk_space/=1024;
			asprintf(&output_message,"%lu MB (%d%%) free on volume %s",free_disk_space,percent_free_space,volume_name);
		}

	/* check to see if DS Database is open or closed */
	} else if(vars_to_check==CHECK_DSDB) {

		send_buffer = strscpy(send_buffer,"S11\r\n");
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;
		if(atoi(recv_buffer)==1)
			result=STATE_OK;
		else
			result=STATE_WARNING;
 
		send_buffer = strscpy(send_buffer,"S13\r\n");
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		temp_buffer=strtok(recv_buffer,"\r\n");
 
		asprintf(&output_message,"Directory Services Database is %s (DS version %s)",(result==STATE_OK)?"open":"closed",temp_buffer);

	/* check to see if logins are enabled */
	} else if (vars_to_check==CHECK_LOGINS) {

		send_buffer = strscpy(send_buffer,"S12\r\n");
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;
		if(atoi(recv_buffer)==1)
			result=STATE_OK;
		else
			result=STATE_WARNING;
 
		asprintf(&output_message,"Logins are %s",(result==STATE_OK)?"enabled":"disabled");

	/* check packet receive buffers */
	} else if (vars_to_check==CHECK_UPRB || vars_to_check==CHECK_PUPRB) {
 
		asprintf(&send_buffer,"S15\r\n",volume_name);
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;

		used_packet_receive_buffers=atoi(recv_buffer);

		asprintf(&send_buffer,"S16\r\n",volume_name);
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;

		max_packet_receive_buffers=atoi(recv_buffer);
 
		percent_used_packet_receive_buffers=(unsigned long)(((double)used_packet_receive_buffers/(double)max_packet_receive_buffers)*100.0);

		if(vars_to_check==CHECK_UPRB){
			if(check_critical_value==TRUE && used_packet_receive_buffers >= critical_value)
				result=STATE_CRITICAL;
			else if(check_warning_value==TRUE && used_packet_receive_buffers >= warning_value)
				result=STATE_WARNING;
		} else {
			if(check_critical_value==TRUE && percent_used_packet_receive_buffers >= critical_value)
				result=STATE_CRITICAL;
			else if(check_warning_value==TRUE && percent_used_packet_receive_buffers >= warning_value)
				result=STATE_WARNING;
		}
 
		asprintf(&output_message,"%d of %d (%lu%%) packet receive buffers used",used_packet_receive_buffers,max_packet_receive_buffers,percent_used_packet_receive_buffers);

	/* check SAP table entries */
	} else if (vars_to_check==CHECK_SAPENTRIES) {

		if(sap_number==-1)
			asprintf(&send_buffer,"S9\r\n");
		else
			asprintf(&send_buffer,"S9.%d\r\n",sap_number);
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;
 
		sap_entries=atoi(recv_buffer);
 
		if(check_critical_value==TRUE && sap_entries >= critical_value)
			result=STATE_CRITICAL;
		else if(check_warning_value==TRUE && sap_entries >= warning_value)
			result=STATE_WARNING;

		if(sap_number==-1)
			asprintf(&output_message,"%d entries in SAP table",sap_entries);
		else
			asprintf(&output_message,"%d entries in SAP table for SAP type %d",sap_entries,sap_number);

	/* check KB purgeable space on volume */
	} else if (vars_to_check==CHECK_VKP) {

		asprintf(&send_buffer,"VKP%s\r\n",volume_name);
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			asprintf(&output_message,"Error: Volume '%s' does not exist!",volume_name);
			result=STATE_CRITICAL;
		} else {
			purgeable_disk_space=strtoul(recv_buffer,NULL,10);
			if(check_critical_value==TRUE && purgeable_disk_space >= critical_value)
				result=STATE_CRITICAL;
			else if(check_warning_value==TRUE && purgeable_disk_space >= warning_value)
				result=STATE_WARNING;
			asprintf(&output_message,"%s%lu KB purgeable on volume %s",(result==STATE_OK)?"":"Only ",purgeable_disk_space,volume_name);
		}

	/* check % purgeable space on volume */
	} else if (vars_to_check==CHECK_VPP) {

		asprintf(&send_buffer,"VKP%s\r\n",volume_name);
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;

		if(!strcmp(recv_buffer,"-1\n")){

			asprintf(&output_message,"Error: Volume '%s' does not exist!",volume_name);
			result=STATE_CRITICAL;

		} else {

			purgeable_disk_space=strtoul(recv_buffer,NULL,10);

			asprintf(&send_buffer,"VKS%s\r\n",volume_name);
			result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
			if(result!=STATE_OK)
				return result;
			total_disk_space=strtoul(recv_buffer,NULL,10);

			percent_purgeable_space=(int)(((double)purgeable_disk_space/(double)total_disk_space)*100.0);

			if(check_critical_value==TRUE && percent_purgeable_space >= critical_value)
				result=STATE_CRITICAL;
			else if(check_warning_value==TRUE && percent_purgeable_space >= warning_value)
				result=STATE_WARNING;
			purgeable_disk_space/=1024;
			asprintf(&output_message,"%lu MB (%d%%) purgeable on volume %s",purgeable_disk_space,percent_purgeable_space,volume_name);
		}

	/* check KB not yet purgeable space on volume */
	} else if (vars_to_check==CHECK_VKNP) {

		asprintf(&send_buffer,"VKNP%s\r\n",volume_name);
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			asprintf(&output_message,"Error: Volume '%s' does not exist!",volume_name);
			result=STATE_CRITICAL;
		} else {
			non_purgeable_disk_space=strtoul(recv_buffer,NULL,10);
			if(check_critical_value==TRUE && non_purgeable_disk_space >= critical_value)
				result=STATE_CRITICAL;
			else if(check_warning_value==TRUE && non_purgeable_disk_space >= warning_value)
				result=STATE_WARNING;
			asprintf(&output_message,"%s%lu KB not yet purgeable on volume %s",(result==STATE_OK)?"":"Only ",non_purgeable_disk_space,volume_name);
		}

	/* check % not yet purgeable space on volume */
	} else if (vars_to_check==CHECK_VPNP) {

		asprintf(&send_buffer,"VKNP%s\r\n",volume_name);
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;

		if(!strcmp(recv_buffer,"-1\n")){

			asprintf(&output_message,"Error: Volume '%s' does not exist!",volume_name);
			result=STATE_CRITICAL;

		} else {

			non_purgeable_disk_space=strtoul(recv_buffer,NULL,10);

			asprintf(&send_buffer,"VKS%s\r\n",volume_name);
			result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
			if(result!=STATE_OK)
				return result;
			total_disk_space=strtoul(recv_buffer,NULL,10);

			percent_non_purgeable_space=(int)(((double)non_purgeable_disk_space/(double)total_disk_space)*100.0);

			if(check_critical_value==TRUE && percent_non_purgeable_space >= critical_value)
				result=STATE_CRITICAL;
			else if(check_warning_value==TRUE && percent_non_purgeable_space >= warning_value)
				result=STATE_WARNING;
			purgeable_disk_space/=1024;
			asprintf(&output_message,"%lu MB (%d%%) not yet purgeable on volume %s",non_purgeable_disk_space,percent_non_purgeable_space,volume_name);
		}

	/* check # of open files */
	} else if (vars_to_check==CHECK_OFILES) {

		asprintf(&send_buffer,"S18\r\n");
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;
 
		open_files=atoi(recv_buffer);
 
		if(check_critical_value==TRUE && open_files >= critical_value)
			result=STATE_CRITICAL;
		else if(check_warning_value==TRUE && open_files >= warning_value)
			result=STATE_WARNING;

		asprintf(&output_message,"%d open files",open_files);

	/* check # of abended threads (Netware 5.x only) */
	} else if (vars_to_check==CHECK_ABENDS) {

		asprintf(&send_buffer,"S17\r\n");
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;
 
		abended_threads=atoi(recv_buffer);
 
		if(check_critical_value==TRUE && abended_threads >= critical_value)
			result=STATE_CRITICAL;
		else if(check_warning_value==TRUE && abended_threads >= warning_value)
			result=STATE_WARNING;

		asprintf(&output_message,"%d abended threads",abended_threads);

	/* check # of current service processes (Netware 5.x only) */
	} else if (vars_to_check==CHECK_CSPROCS) {

		asprintf(&send_buffer,"S20\r\n");
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;
 
		max_service_processes=atoi(recv_buffer);
 
		asprintf(&send_buffer,"S21\r\n");
		result=process_tcp_request(server_address,server_port,send_buffer,recv_buffer,sizeof(recv_buffer));
		if(result!=STATE_OK)
			return result;
 
		current_service_processes=atoi(recv_buffer);
 
		if(check_critical_value==TRUE && current_service_processes >= critical_value)
			result=STATE_CRITICAL;
		else if(check_warning_value==TRUE && current_service_processes >= warning_value)
			result=STATE_WARNING;

		asprintf(&output_message,"%d current service processes (%d max)",current_service_processes,max_service_processes);

	} else {

		output_message = strscpy(output_message,"Nothing to check!\n");
		result=STATE_UNKNOWN;

	}

	/* reset timeout */
	alarm(0);

	printf("%s%s\n",netware_version,output_message);

	return result;
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
		{"osversion",no_argument,      0,'o'},
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
		c = getopt_long(argc,argv,"+hoVH:t:c:w:p:v:",long_options,&option_index);
#else
		c = getopt(argc,argv,"+hoVH:t:c:w:p:v:");
#endif

		if (c==-1||c==EOF||c==1)
			break;

		switch (c)
			{
			case '?': /* print short usage statement if args not parsable */
				printf ("%s: Unknown argument: %s\n\n", progname, optarg);
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
			case 'o': /* display nos version */
				check_netware_version=TRUE;
				break;
			case 'p': /* port */
				if (is_intnonneg(optarg))
					server_port=atoi(optarg);
				else
					terminate(STATE_UNKNOWN,"Server port an integer (seconds)\nType '%s -h' for additional help\n",progname);
				break;
			case 'v':
				if(strlen(optarg)<3)
					return ERROR;
				if(!strcmp(optarg,"LOAD1"))
					vars_to_check=CHECK_LOAD1;
				else if(!strcmp(optarg,"LOAD5"))
					vars_to_check=CHECK_LOAD5;
				else if(!strcmp(optarg,"LOAD15"))
					vars_to_check=CHECK_LOAD15;
				else if(!strcmp(optarg,"CONNS"))
					vars_to_check=CHECK_CONNS;
				else if(!strcmp(optarg,"LTCH"))
					vars_to_check=CHECK_LTCH;
				else if(!strcmp(optarg,"CBUFF"))
					vars_to_check=CHECK_CBUFF;
				else if(!strcmp(optarg,"CDBUFF"))
					vars_to_check=CHECK_CDBUFF;
				else if(!strcmp(optarg,"LRUM"))
					vars_to_check=CHECK_LRUM;
				else if(strncmp(optarg,"VPF",3)==0){
					vars_to_check=CHECK_VPF;
					volume_name = strscpy(volume_name,optarg+3);
					if(!strcmp(volume_name,""))
						volume_name = strscpy(volume_name,"SYS");
				}
				else if(strncmp(optarg,"VKF",3)==0){
					vars_to_check=CHECK_VKF;
					volume_name = strscpy(volume_name,optarg+3);
					if(!strcmp(volume_name,""))
						volume_name = strscpy(volume_name,"SYS");
				}
				else if(!strcmp(optarg,"DSDB"))
					vars_to_check=CHECK_DSDB;
				else if(!strcmp(optarg,"LOGINS"))
					vars_to_check=CHECK_LOGINS;
				else if(!strcmp(optarg,"UPRB"))
					vars_to_check=CHECK_UPRB;
				else if(!strcmp(optarg,"PUPRB"))
					vars_to_check=CHECK_PUPRB;
				else if(!strncmp(optarg,"SAPENTRIES",10)){
					vars_to_check=CHECK_SAPENTRIES;
					if(strlen(optarg)>10)
						sap_number=atoi(optarg+10);
					else
						sap_number=-1;
				}
				else if(!strcmp(optarg,"OFILES"))
					vars_to_check=CHECK_OFILES;
				else if(strncmp(optarg,"VKP",3)==0){
					vars_to_check=CHECK_VKP;
					volume_name = strscpy(volume_name,optarg+3);
					if(!strcmp(volume_name,""))
						volume_name = strscpy(volume_name,"SYS");
				}
				else if(strncmp(optarg,"VPP",3)==0){
					vars_to_check=CHECK_VPP;
					volume_name = strscpy(volume_name,optarg+3);
					if(!strcmp(volume_name,""))
						volume_name = strscpy(volume_name,"SYS");
				}
				else if(strncmp(optarg,"VKNP",4)==0){
					vars_to_check=CHECK_VKNP;
					volume_name = strscpy(volume_name,optarg+4);
					if(!strcmp(volume_name,""))
						volume_name = strscpy(volume_name,"SYS");
				}
				else if(strncmp(optarg,"VPNP",4)==0){
					vars_to_check=CHECK_VPNP;
					volume_name = strscpy(volume_name,optarg+4);
					if(!strcmp(volume_name,""))
						volume_name = strscpy(volume_name,"SYS");
				}
				else if(!strcmp(optarg,"ABENDS"))
					vars_to_check=CHECK_ABENDS;
				else if(!strcmp(optarg,"CSPROCS"))
					vars_to_check=CHECK_CSPROCS;
				else
					return ERROR;
				break;
			case 'w': /* warning threshold */
				warning_value=strtoul(optarg,NULL,10);
				check_warning_value=TRUE;
				break;
			case 'c': /* critical threshold */
				critical_value=strtoul(optarg,NULL,10);
				check_critical_value=TRUE;
				break;
			case 't': /* timeout */
				socket_timeout=atoi(optarg);
				if(socket_timeout<=0)
					return ERROR;
			}

	}

	return OK;
}


void print_usage(void)
{
	printf
		("Usage:\n"
		 " %s %s\n"
#ifdef HAVE_GETOPT_H
		 " %s (-h | --help) for detailed help\n"
		 " %s (-V | --version) for version information\n",
#else
		 " %s -h for detailed help\n"
		 " %s -V for version information\n",
#endif
		 progname, OPTIONS, progname, progname);
}

void print_help(void)
{
	print_revision (progname, REVISION);
	printf ("%s\n\n%s\n", COPYRIGHT, SUMMARY);
	print_usage();
	printf
		("\nOptions:\n" LONGOPTIONS "\n" DESCRIPTION "\n",
		 PORT, DEFAULT_SOCKET_TIMEOUT);
	support ();
}
