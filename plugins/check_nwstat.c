/******************************************************************************

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 $Id$
 
******************************************************************************/

const char *progname = "check_nwstat";
const char *revision = "$Revision$";
const char *copyright = "2000-2004";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"

enum checkvar {
	NONE,
	LOAD1,      /* check 1 minute CPU load */
	LOAD5,      /* check 5 minute CPU load */
	LOAD15,     /* check 15 minute CPU load */
	CONNS,      /* check number of connections */
	VPF,        /* check % free space on volume */
	VKF,        /* check KB free space on volume */
	LTCH,       /* check long-term cache hit percentage */
	CBUFF,      /* check total cache buffers */
	CDBUFF,     /* check dirty cache buffers */
	LRUM,       /* check LRU sitting time in minutes */
	DSDB,       /* check to see if DS Database is open */
	LOGINS,     /* check to see if logins are enabled */
	PUPRB,      /* check % of used packet receive buffers */
	UPRB,       /* check used packet receive buffers */
	SAPENTRIES, /* check SAP entries */
	OFILES,     /* check number of open files */
	VKP,        /* check KB purgeable space on volume */
	VPP,        /* check % purgeable space on volume */
	VKNP,       /* check KB not yet purgeable space on volume */
	VPNP,       /* check % not yet purgeable space on volume */
	ABENDS,     /* check abended thread count */
	CSPROCS,    /* check number of current service processes */
	TSYNC,      /* check timesync status 0=no 1=yes in sync to the network */
	LRUS,       /* check LRU sitting time in seconds */
	DCB,        /* check dirty cache buffers as a percentage of the total */
	TCB,        /* check total cache buffers as a percentage of the original */
	DSVER,      /* check NDS version */
	UPTIME,     /* check server uptime */
	NLM         /* check NLM loaded */
};

enum {
	PORT = 9999
};

char *server_address=NULL;
char *volume_name=NULL;
char *nlm_name=NULL;
int server_port=PORT;
unsigned long warning_value=0L;
unsigned long critical_value=0L;
int check_warning_value=FALSE;
int check_critical_value=FALSE;
int check_netware_version=FALSE;
enum checkvar vars_to_check = NONE;
int sap_number=-1;

int process_arguments(int, char **);
void print_help(void);
void print_usage(void);



int
main(int argc, char **argv) {
	int result = STATE_UNKNOWN;
	int sd;
	char *send_buffer=NULL;
	char recv_buffer[MAX_INPUT_BUFFER];
	char *output_message=NULL;
	char *temp_buffer=NULL;
	char *netware_version=NULL;

	int time_sync_status=0;
	unsigned long total_cache_buffers=0;
	unsigned long dirty_cache_buffers=0;
	unsigned long open_files=0;
	unsigned long abended_threads=0;
	unsigned long max_service_processes=0;
	unsigned long current_service_processes=0;
	unsigned long free_disk_space=0L;
	unsigned long total_disk_space=0L;
	unsigned long purgeable_disk_space=0L;
	unsigned long non_purgeable_disk_space=0L;
	unsigned long percent_free_space=0;
	unsigned long percent_purgeable_space=0;
	unsigned long percent_non_purgeable_space=0;
	unsigned long current_connections=0L;
	unsigned long utilization=0L;
	unsigned long cache_hits=0;
	unsigned long cache_buffers=0L;
	unsigned long lru_time=0L;
	unsigned long max_packet_receive_buffers=0;
	unsigned long used_packet_receive_buffers=0;
	unsigned long percent_used_packet_receive_buffers=0L;
	unsigned long sap_entries=0;
	char uptime[MAX_INPUT_BUFFER];

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	if (process_arguments(argc,argv) != TRUE)
		usage4 (_("Could not parse arguments"));

	/* initialize alarm signal handling */
	signal(SIGALRM,socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm(socket_timeout);

	/* open connection */
	my_tcp_connect (server_address, server_port, &sd);

	/* get OS version string */
	if (check_netware_version==TRUE) {
		send_buffer = strdup ("S19\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		if (!strcmp(recv_buffer,"-1\n"))
			netware_version = strdup("");
		else {
			recv_buffer[strlen(recv_buffer)-1]=0;
			asprintf (&netware_version,_("NetWare %s: "),recv_buffer);
		}
	} else
		netware_version = strdup("");


	/* check CPU load */
	if (vars_to_check==LOAD1 || vars_to_check==LOAD5 || vars_to_check==LOAD15) {
			
		switch(vars_to_check) {
		case LOAD1:
			temp_buffer = strdup ("1");
			break;
		case LOAD5:
			temp_buffer = strdup ("5");
			break;
		default:
			temp_buffer = strdup ("15");
			break;
		}

		asprintf (&send_buffer,"UTIL%s\r\n",temp_buffer);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		utilization=strtoul(recv_buffer,NULL,10);
		send_buffer = strdup ("UPTIME\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		recv_buffer[strlen(recv_buffer)-1]=0;
		sprintf(uptime,_("Up %s,"),recv_buffer);

		if (check_critical_value==TRUE && utilization >= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && utilization >= warning_value)
			result=STATE_WARNING;

		asprintf (&output_message,
		          _("Load %s - %s %s-min load average = %lu%%"),
		          state_text(result),
		          uptime,
		          temp_buffer,
		          utilization);

		/* check number of user connections */
	} else if (vars_to_check==CONNS) {

		send_buffer = strdup ("CONNECT\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		current_connections=strtoul(recv_buffer,NULL,10);

		if (check_critical_value==TRUE && current_connections >= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && current_connections >= warning_value)
			result=STATE_WARNING;

		asprintf (&output_message,
							_("Conns %s - %lu current connections"),
		          state_text(result),
		          current_connections);

		/* check % long term cache hits */
	} else if (vars_to_check==LTCH) {

		send_buffer = strdup ("S1\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		cache_hits=atoi(recv_buffer);

		if (check_critical_value==TRUE && cache_hits <= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && cache_hits <= warning_value)
			result=STATE_WARNING;

		asprintf (&output_message,
		          _("%s: Long term cache hits = %lu%%"),
		          state_text(result),
		          cache_hits);

		/* check cache buffers */
	} else if (vars_to_check==CBUFF) {

		send_buffer = strdup ("S2\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		cache_buffers=strtoul(recv_buffer,NULL,10);

		if (check_critical_value==TRUE && cache_buffers <= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && cache_buffers <= warning_value)
			result=STATE_WARNING;

		asprintf (&output_message,
		          _("%s: Total cache buffers = %lu"),
		          state_text(result),
		          cache_buffers);

		/* check dirty cache buffers */
	} else if (vars_to_check==CDBUFF) {

		send_buffer = strdup ("S3\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		cache_buffers=strtoul(recv_buffer,NULL,10);

		if (check_critical_value==TRUE && cache_buffers >= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && cache_buffers >= warning_value)
			result=STATE_WARNING;

		asprintf (&output_message,
		          _("%s: Dirty cache buffers = %lu"),
		          state_text(result),
		          cache_buffers);

		/* check LRU sitting time in minutes */
	} else if (vars_to_check==LRUM) {

		send_buffer = strdup ("S5\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		lru_time=strtoul(recv_buffer,NULL,10);

		if (check_critical_value==TRUE && lru_time <= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && lru_time <= warning_value)
			result=STATE_WARNING;

		asprintf (&output_message,
		          _("%s: LRU sitting time = %lu minutes"),
		          state_text(result),
		          lru_time);


		/* check KB free space on volume */
	} else if (vars_to_check==VKF) {

		asprintf (&send_buffer,"VKF%s\r\n",volume_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			asprintf (&output_message,_("CRITICAL - Volume '%s' does not exist!"),volume_name);
			result=STATE_CRITICAL;
		}	else {
			free_disk_space=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && free_disk_space <= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && free_disk_space <= warning_value)
				result=STATE_WARNING;
			asprintf (&output_message,
			          _("%s%lu KB free on volume %s"),
			         (result==STATE_OK)?"":_("Only "),
			         free_disk_space,
			         volume_name);
		}

		/* check % free space on volume */
	} else if (vars_to_check==VPF) {

		asprintf (&send_buffer,"VKF%s\r\n",volume_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {

			asprintf (&output_message,_("CRITICAL - Volume '%s' does not exist!"),volume_name);
			result=STATE_CRITICAL;

		} else {

			free_disk_space=strtoul(recv_buffer,NULL,10);

			asprintf (&send_buffer,"VKS%s\r\n",volume_name);
			result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
			if (result!=STATE_OK)
				return result;
			total_disk_space=strtoul(recv_buffer,NULL,10);

			percent_free_space=(int)(((double)free_disk_space/(double)total_disk_space)*100.0);

			if (check_critical_value==TRUE && percent_free_space <= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && percent_free_space <= warning_value)
				result=STATE_WARNING;
			free_disk_space/=1024;
			asprintf (&output_message,_("%lu MB (%lu%%) free on volume %s"),free_disk_space,percent_free_space,volume_name);
		}

		/* check to see if DS Database is open or closed */
	} else if (vars_to_check==DSDB) {

		send_buffer = strdup ("S11\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		if (atoi(recv_buffer)==1)
			result=STATE_OK;
		else
			result=STATE_WARNING;
 
		send_buffer = strdup ("S13\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		temp_buffer=strtok(recv_buffer,"\r\n");
 
		asprintf (&output_message,_("Directory Services Database is %s (DS version %s)"),(result==STATE_OK)?"open":"closed",temp_buffer);

		/* check to see if logins are enabled */
	} else if (vars_to_check==LOGINS) {

		send_buffer = strdup ("S12\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		if (atoi(recv_buffer)==1)
			result=STATE_OK;
		else
			result=STATE_WARNING;
 
		asprintf (&output_message,_("Logins are %s"),(result==STATE_OK)?_("enabled"):_("disabled"));

		/* check packet receive buffers */
	} else if (vars_to_check==UPRB || vars_to_check==PUPRB) {
 
		asprintf (&send_buffer,"S15\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		used_packet_receive_buffers=atoi(recv_buffer);

		asprintf (&send_buffer,"S16\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		max_packet_receive_buffers=atoi(recv_buffer);
 
		percent_used_packet_receive_buffers=(unsigned long)(((double)used_packet_receive_buffers/(double)max_packet_receive_buffers)*100.0);

		if (vars_to_check==UPRB) {
			if (check_critical_value==TRUE && used_packet_receive_buffers >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && used_packet_receive_buffers >= warning_value)
				result=STATE_WARNING;
		} else {
			if (check_critical_value==TRUE && percent_used_packet_receive_buffers >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && percent_used_packet_receive_buffers >= warning_value)
				result=STATE_WARNING;
		}
 
		asprintf (&output_message,_("%lu of %lu (%lu%%) packet receive buffers used"),used_packet_receive_buffers,max_packet_receive_buffers,percent_used_packet_receive_buffers);

		/* check SAP table entries */
	} else if (vars_to_check==SAPENTRIES) {

		if (sap_number==-1)
			asprintf (&send_buffer,"S9\r\n");
		else
			asprintf (&send_buffer,"S9.%d\r\n",sap_number);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
 
		sap_entries=atoi(recv_buffer);
 
		if (check_critical_value==TRUE && sap_entries >= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && sap_entries >= warning_value)
			result=STATE_WARNING;

		if (sap_number==-1)
			asprintf (&output_message,_("%lu entries in SAP table"),sap_entries);
		else
			asprintf (&output_message,_("%lu entries in SAP table for SAP type %d"),sap_entries,sap_number);

		/* check KB purgeable space on volume */
	} else if (vars_to_check==VKP) {

		asprintf (&send_buffer,"VKP%s\r\n",volume_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			asprintf (&output_message,_("CRITICAL - Volume '%s' does not exist!"),volume_name);
			result=STATE_CRITICAL;
		} else {
			purgeable_disk_space=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && purgeable_disk_space >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && purgeable_disk_space >= warning_value)
				result=STATE_WARNING;
			asprintf (&output_message,_("%s%lu KB purgeable on volume %s"),(result==STATE_OK)?"":_("Only "),purgeable_disk_space,volume_name);
		}

		/* check % purgeable space on volume */
	} else if (vars_to_check==VPP) {

		asprintf (&send_buffer,"VKP%s\r\n",volume_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {

			asprintf (&output_message,_("CRITICAL - Volume '%s' does not exist!"),volume_name);
			result=STATE_CRITICAL;

		} else {

			purgeable_disk_space=strtoul(recv_buffer,NULL,10);

			asprintf (&send_buffer,"VKS%s\r\n",volume_name);
			result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
			if (result!=STATE_OK)
				return result;
			total_disk_space=strtoul(recv_buffer,NULL,10);

			percent_purgeable_space=(int)(((double)purgeable_disk_space/(double)total_disk_space)*100.0);

			if (check_critical_value==TRUE && percent_purgeable_space >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && percent_purgeable_space >= warning_value)
				result=STATE_WARNING;
			purgeable_disk_space/=1024;
			asprintf (&output_message,_("%lu MB (%lu%%) purgeable on volume %s"),purgeable_disk_space,percent_purgeable_space,volume_name);
		}

		/* check KB not yet purgeable space on volume */
	} else if (vars_to_check==VKNP) {

		asprintf (&send_buffer,"VKNP%s\r\n",volume_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			asprintf (&output_message,_("CRITICAL - Volume '%s' does not exist!"),volume_name);
			result=STATE_CRITICAL;
		} else {
			non_purgeable_disk_space=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && non_purgeable_disk_space >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && non_purgeable_disk_space >= warning_value)
				result=STATE_WARNING;
			asprintf (&output_message,_("%s%lu KB not yet purgeable on volume %s"),(result==STATE_OK)?"":_("Only "),non_purgeable_disk_space,volume_name);
		}

		/* check % not yet purgeable space on volume */
	} else if (vars_to_check==VPNP) {

		asprintf (&send_buffer,"VKNP%s\r\n",volume_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {

			asprintf (&output_message,_("CRITICAL - Volume '%s' does not exist!"),volume_name);
			result=STATE_CRITICAL;

		} else {

			non_purgeable_disk_space=strtoul(recv_buffer,NULL,10);

			asprintf (&send_buffer,"VKS%s\r\n",volume_name);
			result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
			if (result!=STATE_OK)
				return result;
			total_disk_space=strtoul(recv_buffer,NULL,10);

			percent_non_purgeable_space=(int)(((double)non_purgeable_disk_space/(double)total_disk_space)*100.0);

			if (check_critical_value==TRUE && percent_non_purgeable_space >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && percent_non_purgeable_space >= warning_value)
				result=STATE_WARNING;
			purgeable_disk_space/=1024;
			asprintf (&output_message,_("%lu MB (%lu%%) not yet purgeable on volume %s"),non_purgeable_disk_space,percent_non_purgeable_space,volume_name);
		}

		/* check # of open files */
	} else if (vars_to_check==OFILES) {

		asprintf (&send_buffer,"S18\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
 
		open_files=atoi(recv_buffer);
 
		if (check_critical_value==TRUE && open_files >= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && open_files >= warning_value)
			result=STATE_WARNING;

		asprintf (&output_message,_("%lu open files"),open_files);

		/* check # of abended threads (Netware 5.x only) */
	} else if (vars_to_check==ABENDS) {

		asprintf (&send_buffer,"S17\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
 
		abended_threads=atoi(recv_buffer);
 
		if (check_critical_value==TRUE && abended_threads >= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && abended_threads >= warning_value)
			result=STATE_WARNING;

		asprintf (&output_message,_("%lu abended threads"),abended_threads);

		/* check # of current service processes (Netware 5.x only) */
	} else if (vars_to_check==CSPROCS) {

		asprintf (&send_buffer,"S20\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
 
		max_service_processes=atoi(recv_buffer);
 
		asprintf (&send_buffer,"S21\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
 
		current_service_processes=atoi(recv_buffer);
 
		if (check_critical_value==TRUE && current_service_processes >= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && current_service_processes >= warning_value)
			result=STATE_WARNING;

		asprintf (&output_message,
		          _("%lu current service processes (%lu max)"),
		          current_service_processes,
		          max_service_processes);

		/* check # Timesync Status */
	} else if (vars_to_check==TSYNC) {

		asprintf (&send_buffer,"S22\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		time_sync_status=atoi(recv_buffer);

		if (time_sync_status==0) {
			result=STATE_CRITICAL;
			asprintf (&output_message,_("CRITICAL - Time not in sync with network!"));
		}
		else {
			asprintf (&output_message,_("OK - Time in sync with network!"));
		}

		/* check LRU sitting time in secondss */
	} else if (vars_to_check==LRUS) {

		send_buffer = strdup ("S4\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		lru_time=strtoul(recv_buffer,NULL,10);

		if (check_critical_value==TRUE && lru_time <= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && lru_time <= warning_value)
			result=STATE_WARNING;
		asprintf (&output_message,_("LRU sitting time = %lu seconds"),lru_time);


		/* check % dirty cacheobuffers as a percentage of the total*/
	} else if (vars_to_check==DCB) {

		send_buffer = strdup ("S6\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		dirty_cache_buffers=atoi(recv_buffer);

		if (check_critical_value==TRUE && dirty_cache_buffers <= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && dirty_cache_buffers <= warning_value)
			result=STATE_WARNING;
		asprintf (&output_message,_("Dirty cache buffers = %lu%% of the total"),dirty_cache_buffers);

		/* check % total cache buffers as a percentage of the original*/
	} else if (vars_to_check==TCB) {

		send_buffer = strdup ("S7\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		total_cache_buffers=atoi(recv_buffer);

		if (check_critical_value==TRUE && total_cache_buffers <= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && total_cache_buffers <= warning_value)
			result=STATE_WARNING;
		asprintf (&output_message,_("Total cache buffers = %lu%% of the original"),total_cache_buffers);
		
	} else if (vars_to_check==DSVER) {
		asprintf (&send_buffer,"S13\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		recv_buffer[strlen(recv_buffer)-1]=0;

		asprintf (&output_message,_("NDS Version %s"),recv_buffer);

	} else if (vars_to_check==UPTIME) {
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
	 		return result;

		recv_buffer[strlen(recv_buffer)-1]=0;

		asprintf (&output_message,_("Up %s"),recv_buffer);

	} else if (vars_to_check==NLM) {
		asprintf (&send_buffer,"S24:%s\r\n",nlm_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		recv_buffer[strlen(recv_buffer)-1]=0;
		if (strcmp(recv_buffer,"-1")) {
			asprintf (&output_message,_("Module %s version %s is loaded"),nlm_name,recv_buffer);
		} else {
			result=STATE_CRITICAL;
			asprintf (&output_message,_("Module %s is not loaded"),nlm_name);
		}

	} else {

		output_message = strdup (_("Nothing to check!\n"));
		result=STATE_UNKNOWN;

	}

	close (sd);

	/* reset timeout */
	alarm(0);

	printf("%s%s\n",netware_version,output_message);

	return result;
}



/* process command-line arguments */
int process_arguments(int argc, char **argv) {
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
			{"osversion",no_argument,      0,'o'},
			{"version",  no_argument,      0,'V'},
			{"help",     no_argument,      0,'h'},
			{0,0,0,0}
		};

	/* no options were supplied */
	if (argc<2) return ERROR;

	/* backwards compatibility */
	if (! is_option(argv[1])) {
		server_address=argv[1];
		argv[1]=argv[0];
		argv=&argv[1];
		argc--;
	}

  for (c=1;c<argc;c++) {
    if (strcmp("-to",argv[c])==0)
      strcpy(argv[c],"-t");
    else if (strcmp("-wv",argv[c])==0)
      strcpy(argv[c],"-w");
    else if (strcmp("-cv",argv[c])==0)
      strcpy(argv[c],"-c");
	}

	while (1) {
		c = getopt_long(argc,argv,"+hoVH:t:c:w:p:v:",longopts,&option);

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
				print_revision(progname, revision);
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
					die(STATE_UNKNOWN,_("Server port an integer (seconds)\nType '%s -h' for additional help\n"),progname);
				break;
			case 'v':
				if (strlen(optarg)<3)
					return ERROR;
				if (!strcmp(optarg,"LOAD1"))
					vars_to_check=LOAD1;
				else if (!strcmp(optarg,"LOAD5"))
					vars_to_check=LOAD5;
				else if (!strcmp(optarg,"LOAD15"))
					vars_to_check=LOAD15;
				else if (!strcmp(optarg,"CONNS"))
					vars_to_check=CONNS;
				else if (!strcmp(optarg,"LTCH"))
					vars_to_check=LTCH;
				else if (!strcmp(optarg,"DCB"))
					vars_to_check=DCB;
				else if (!strcmp(optarg,"TCB"))
					vars_to_check=TCB;
				else if (!strcmp(optarg,"CBUFF"))
					vars_to_check=CBUFF;
				else if (!strcmp(optarg,"CDBUFF"))
					vars_to_check=CDBUFF;
				else if (!strcmp(optarg,"LRUM"))
					vars_to_check=LRUM;
				else if (!strcmp(optarg,"LRUS"))
					vars_to_check=LRUS;
				else if (strncmp(optarg,"VPF",3)==0) {
					vars_to_check=VPF;
					volume_name = strdup (optarg+3);
					if (!strcmp(volume_name,""))
						volume_name = strdup ("SYS");
				}
				else if (strncmp(optarg,"VKF",3)==0) {
					vars_to_check=VKF;
					volume_name = strdup (optarg+3);
					if (!strcmp(volume_name,""))
						volume_name = strdup ("SYS");
				}
				else if (!strcmp(optarg,"DSDB"))
					vars_to_check=DSDB;
				else if (!strcmp(optarg,"LOGINS"))
					vars_to_check=LOGINS;
				else if (!strcmp(optarg,"UPRB"))
					vars_to_check=UPRB;
				else if (!strcmp(optarg,"PUPRB"))
					vars_to_check=PUPRB;
				else if (!strncmp(optarg,"SAPENTRIES",10)) {
					vars_to_check=SAPENTRIES;
					if (strlen(optarg)>10)
						sap_number=atoi(optarg+10);
					else
						sap_number=-1;
				}
				else if (!strcmp(optarg,"OFILES"))
					vars_to_check=OFILES;
				else if (strncmp(optarg,"VKP",3)==0) {
					vars_to_check=VKP;
					volume_name = strdup (optarg+3);
					if (!strcmp(volume_name,""))
						volume_name = strdup ("SYS");
				}
				else if (strncmp(optarg,"VPP",3)==0) {
					vars_to_check=VPP;
					volume_name = strdup (optarg+3);
					if (!strcmp(volume_name,""))
						volume_name = strdup ("SYS");
				}
				else if (strncmp(optarg,"VKNP",4)==0) {
					vars_to_check=VKNP;
					volume_name = strdup (optarg+4);
					if (!strcmp(volume_name,""))
						volume_name = strdup ("SYS");
				}
				else if (strncmp(optarg,"VPNP",4)==0) {
					vars_to_check=VPNP;
					volume_name = strdup (optarg+4);
					if (!strcmp(volume_name,""))
						volume_name = strdup("SYS");
				}
				else if (!strcmp(optarg,"ABENDS"))
					vars_to_check=ABENDS;
				else if (!strcmp(optarg,"CSPROCS"))
					vars_to_check=CSPROCS;
				else if (!strcmp(optarg,"TSYNC"))
					vars_to_check=TSYNC;
				else if (!strcmp(optarg,"DSVER"))
					vars_to_check=DSVER;
				else if (!strcmp(optarg,"UPTIME"))
					vars_to_check=UPTIME;
				else if (strncmp(optarg,"NLM:",4)==0) {
					vars_to_check=NLM;
					nlm_name=strdup (optarg+4);
				}
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
				if (socket_timeout<=0)
					return ERROR;
			}

	}

	return OK;
}



void print_help(void)
{
	char *myport;
	asprintf (&myport, "%d", PORT);

	print_revision (progname, revision);

	printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf (COPYRIGHT, copyright, email);

	printf (_("\
Usage: %s This plugin attempts to contact the MRTGEXT NLM running\n\
on a Novell server to gather the requested system information.\n\n"),
	        progname);

	print_usage();

	printf (_(UT_HELP_VRSN));

	printf (_(UT_HOST_PORT), 'p', myport);

	printf (_("\
 -v, --variable=STRING\n\
    Variable to check.  Valid variables include:\n\
      LOAD1     = 1 minute average CPU load\n\
      LOAD5     = 5 minute average CPU load\n\
      LOAD15    = 15 minute average CPU load\n\
      CSPROCS   = number of current service processes (NW 5.x only)\n\
      ABENDS    = number of abended threads (NW 5.x only)\n\
      UPTIME    = server uptime\n"));

	printf (_("\
      LTCH      = percent long term cache hits\n\
      CBUFF     = current number of cache buffers\n\
      CDBUFF    = current number of dirty cache buffers\n\
      DCB       = dirty cache buffers as a percentage of the total\n\
      TCB       = dirty cache buffers as a percentage of the original\n"));

	printf (_("\
      OFILES    = number of open files\n\
      VPF<vol>  = percent free space on volume <vol>\n\
      VKF<vol>  = KB of free space on volume <vol>\n\
      VPP<vol>  = percent purgeable space on volume <vol>\n\
      VKP<vol>  = KB of purgeable space on volume <vol>\n\
      VPNP<vol> = percent not yet purgeable space on volume <vol>\n\
      VKNP<vol> = KB of not yet purgeable space on volume <vol>\n"));

	printf (_("\
      LRUM      = LRU sitting time in minutes\n\
      LRUS      = LRU sitting time in seconds\n\
      DSDB      = check to see if DS Database is open\n\
      DSVER     = NDS version\n\
      UPRB      = used packet receive buffers\n\
      PUPRB     = percent (of max) used packet receive buffers\n\
      SAPENTRIES = number of entries in the SAP table\n\
      SAPENTRIES<n> = number of entries in the SAP table for SAP type <n>\n"));

	printf (_("\
      TSYNC     = timesync status \n\
      LOGINS    = check to see if logins are enabled\n\
      CONNS     = number of currently licensed connections\n\
      NLM:<nlm> = check if NLM is loaded and report version\n\
                  (e.g. \"NLM:TSANDS.NLM\")\n"));

	printf (_("\
 -w, --warning=INTEGER\n\
    Threshold which will result in a warning status\n\
 -c, --critical=INTEGER\n\
    Threshold which will result in a critical status\n\
 -o, --osversion\n\
    Include server version string in results\n"));

	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);

	printf (_("\n\
Notes:\n\
- This plugin requres that the MRTGEXT.NLM file from James Drews' MRTG\n\
  extension for NetWare be loaded on the Novell servers you wish to check.\n\
  (available from http://www.engr.wisc.edu/~drews/mrtg/)\n\
- Values for critical thresholds should be lower than warning thresholds\n\
  when the following variables are checked: VPF, VKF, LTCH, CBUFF, DCB, \n\
  TCB, LRUS and LRUM.\n"));

	printf (_(UT_SUPPORT));
}



void print_usage(void)
{
	printf (_("\
Usage: %s -H host [-p port] [-v variable] [-w warning] [-c critical]\n\
  [-t timeout].\n"), progname);
	printf (_(UT_HLP_VRS), progname, progname);
}
