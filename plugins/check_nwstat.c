/*****************************************************************************
* 
* Nagios check_nwstat plugin
* 
* License: GPL
* Copyright (c) 2000-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_nwstat plugin
* 
* This plugin attempts to contact the MRTGEXT NLM running on a
* Novell server to gather the requested system information.
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

const char *progname = "check_nwstat";
const char *copyright = "2000-2007";
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
	VMF,	    /* check MB free space on volume */
	VMU,	    /* check MB used space on volume */
	VMP,	    /* check MB purgeable space on volume */
	VKF,        /* check KB free space on volume */
	LTCH,       /* check long-term cache hit percentage */
	CBUFF,      /* check total cache buffers */
	CDBUFF,     /* check dirty cache buffers */
	LRUM,       /* check LRU sitting time in minutes */
	DSDB,       /* check to see if DS Database is open */
	LOGINS,     /* check to see if logins are enabled */
	NRMH,	    /* check to see NRM Health Status */
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
	NLM,        /* check NLM loaded */
	NRMP,	    /* check NRM Process Values */
	NRMM,	    /* check NRM Memory Values */
	NRMS,       /* check NRM Values */
	NSS1,       /* check Statistics from _Admin:Manage_NSS\GeneralStats.xml */
	NSS2,       /* check Statistics from _Admin:Manage_NSS\BufferCache.xml */
	NSS3,       /* check statistics from _Admin:Manage_NSS\NameCache.xml */
	NSS4,       /* check statistics from _Admin:Manage_NSS\FileStats.xml */
	NSS5,       /* check statistics from _Admin:Manage_NSS\ObjectCache.xml */
	NSS6,       /* check statistics from _Admin:Manage_NSS\Thread.xml */
	NSS7        /* check statistics from _Admin:Manage_NSS\AuthorizationCache.xml */
};

enum {
	PORT = 9999
};

char *server_address=NULL;
char *volume_name=NULL;
char *nlm_name=NULL;
char *nrmp_name=NULL;
char *nrmm_name=NULL;
char *nrms_name=NULL;
char *nss1_name=NULL;
char *nss2_name=NULL;
char *nss3_name=NULL;
char *nss4_name=NULL;
char *nss5_name=NULL;
char *nss6_name=NULL;
char *nss7_name=NULL;
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
	int nrm_health_status=0;
	unsigned long total_cache_buffers=0;
	unsigned long dirty_cache_buffers=0;
	unsigned long open_files=0;
	unsigned long abended_threads=0;
	unsigned long max_service_processes=0;
	unsigned long current_service_processes=0;
	unsigned long free_disk_space=0L;
	unsigned long nrmp_value=0L;
	unsigned long nrmm_value=0L;
	unsigned long nrms_value=0L;
	unsigned long nss1_value=0L;
	unsigned long nss2_value=0L;
	unsigned long nss3_value=0L;
	unsigned long nss4_value=0L;
	unsigned long nss5_value=0L;
	unsigned long nss6_value=0L;
	unsigned long nss7_value=0L;
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

	/* Parse extra opts if any */
	argv=np_extra_opts(&argc, argv, progname);

	if (process_arguments(argc,argv) == ERROR)
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
			xasprintf (&netware_version,_("NetWare %s: "),recv_buffer);
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

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		xasprintf (&send_buffer,"UTIL%s\r\n",temp_buffer);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		utilization=strtoul(recv_buffer,NULL,10);

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

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

		xasprintf (&output_message,
		          _("Load %s - %s %s-min load average = %lu%%|load%s=%lu;%lu;%lu;0;100"),
		          state_text(result),
		          uptime,
		          temp_buffer,
		          utilization,
			  temp_buffer,
			  utilization,
			  warning_value,
			  critical_value);

		/* check number of user connections */
	} else if (vars_to_check==CONNS) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		send_buffer = strdup ("CONNECT\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		current_connections=strtoul(recv_buffer,NULL,10);

		if (check_critical_value==TRUE && current_connections >= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && current_connections >= warning_value)
			result=STATE_WARNING;

		xasprintf (&output_message,
			_("Conns %s - %lu current connections|Conns=%lu;%lu;%lu;;"),
		          state_text(result),
		          current_connections,
		          current_connections,
			  warning_value,
			  critical_value);

		/* check % long term cache hits */
	} else if (vars_to_check==LTCH) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		send_buffer = strdup ("S1\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		cache_hits=atoi(recv_buffer);

		if (check_critical_value==TRUE && cache_hits <= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && cache_hits <= warning_value)
			result=STATE_WARNING;

		xasprintf (&output_message,
		          _("%s: Long term cache hits = %lu%%"),
		          state_text(result),
		          cache_hits);

		/* check cache buffers */
	} else if (vars_to_check==CBUFF) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		send_buffer = strdup ("S2\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		cache_buffers=strtoul(recv_buffer,NULL,10);

		if (check_critical_value==TRUE && cache_buffers <= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && cache_buffers <= warning_value)
			result=STATE_WARNING;

		xasprintf (&output_message,
		          _("%s: Total cache buffers = %lu|Cachebuffers=%lu;%lu;%lu;;"),
		          state_text(result),
		          cache_buffers,
			  cache_buffers,
			  warning_value,
		  	  critical_value);

		/* check dirty cache buffers */
	} else if (vars_to_check==CDBUFF) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		send_buffer = strdup ("S3\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		cache_buffers=strtoul(recv_buffer,NULL,10);

		if (check_critical_value==TRUE && cache_buffers >= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && cache_buffers >= warning_value)
			result=STATE_WARNING;

		xasprintf (&output_message,
		          _("%s: Dirty cache buffers = %lu|Dirty-Cache-Buffers=%lu;%lu;%lu;;"),
		          state_text(result),
		          cache_buffers,
			  cache_buffers,
			  warning_value,
			  critical_value);

		/* check LRU sitting time in minutes */
	} else if (vars_to_check==LRUM) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		send_buffer = strdup ("S5\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		lru_time=strtoul(recv_buffer,NULL,10);

		if (check_critical_value==TRUE && lru_time <= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && lru_time <= warning_value)
			result=STATE_WARNING;

		xasprintf (&output_message,
		          _("%s: LRU sitting time = %lu minutes"),
		          state_text(result),
		          lru_time);


		/* check KB free space on volume */
	} else if (vars_to_check==VKF) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		xasprintf (&send_buffer,"VKF%s\r\n",volume_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			xasprintf (&output_message,_("CRITICAL - Volume '%s' does not exist!"),volume_name);
			result=STATE_CRITICAL;
		}	else {
			free_disk_space=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && free_disk_space <= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && free_disk_space <= warning_value)
				result=STATE_WARNING;
			xasprintf (&output_message,
			          _("%s%lu KB free on volume %s|KBFree%s=%lu;%lu;%lu;;"),
			         (result==STATE_OK)?"":_("Only "),
			         free_disk_space,
			         volume_name,
				 volume_name,
				 free_disk_space,
				 warning_value,
				 critical_value);
		}

		/* check MB free space on volume */
	} else if (vars_to_check==VMF) {

		xasprintf (&send_buffer,"VMF%s\r\n",volume_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			xasprintf (&output_message,_("CRITICAL - Volume '%s' does not exist!"),volume_name);
			result=STATE_CRITICAL;
		}	else {
			free_disk_space=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && free_disk_space <= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && free_disk_space <= warning_value)
				result=STATE_WARNING;
			xasprintf (&output_message,
			          _("%s%lu MB free on volume %s|MBFree%s=%lu;%lu;%lu;;"),
			         (result==STATE_OK)?"":_("Only "),
			         free_disk_space,
			         volume_name,
				 volume_name,
				 free_disk_space,
				 warning_value,
				 critical_value);
		}
		/* check MB used space on volume */
	} else if (vars_to_check==VMU) {

		xasprintf (&send_buffer,"VMU%s\r\n",volume_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			xasprintf (&output_message,_("CRITICAL - Volume '%s' does not exist!"),volume_name);
			result=STATE_CRITICAL;
		}	else {
			free_disk_space=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && free_disk_space <= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && free_disk_space <= warning_value)
				result=STATE_WARNING;
			xasprintf (&output_message,
			          _("%s%lu MB used on volume %s|MBUsed%s=%lu;%lu;%lu;;"),
			         (result==STATE_OK)?"":_("Only "),
			         free_disk_space,
			         volume_name,
				 volume_name,
				 free_disk_space,
				 warning_value,
				 critical_value);
		}


		/* check % free space on volume */
	} else if (vars_to_check==VPF) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		xasprintf (&send_buffer,"VKF%s\r\n",volume_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {

			xasprintf (&output_message,_("CRITICAL - Volume '%s' does not exist!"),volume_name);
			result=STATE_CRITICAL;

		} else {

			free_disk_space=strtoul(recv_buffer,NULL,10);

			close(sd);
			my_tcp_connect (server_address, server_port, &sd);

			xasprintf (&send_buffer,"VKS%s\r\n",volume_name);
			result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
			if (result!=STATE_OK)
				return result;
			total_disk_space=strtoul(recv_buffer,NULL,10);

			percent_free_space=(unsigned long)(((double)free_disk_space/(double)total_disk_space)*100.0);

			if (check_critical_value==TRUE && percent_free_space <= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && percent_free_space <= warning_value)
				result=STATE_WARNING;
			free_disk_space/=1024;
			total_disk_space/=1024;
			xasprintf (&output_message,_("%lu MB (%lu%%) free on volume %s - total %lu MB|FreeMB%s=%lu;%lu;%lu;0;100"),
				free_disk_space,
				percent_free_space,
				volume_name,
				total_disk_space,
				volume_name,
                                percent_free_space,
                                warning_value,
                                critical_value
                                );
		}

		/* check to see if DS Database is open or closed */
	} else if (vars_to_check==DSDB) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		send_buffer = strdup ("S11\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		if (atoi(recv_buffer)==1)
			result=STATE_OK;
		else
			result=STATE_WARNING;

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		send_buffer = strdup ("S13\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		temp_buffer=strtok(recv_buffer,"\r\n");

		xasprintf (&output_message,_("Directory Services Database is %s (DS version %s)"),(result==STATE_OK)?"open":"closed",temp_buffer);

		/* check to see if logins are enabled */
	} else if (vars_to_check==LOGINS) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		send_buffer = strdup ("S12\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		if (atoi(recv_buffer)==1)
			result=STATE_OK;
		else
			result=STATE_WARNING;

		xasprintf (&output_message,_("Logins are %s"),(result==STATE_OK)?_("enabled"):_("disabled"));


		/* check NRM Health Status Summary*/
	} else if (vars_to_check==NRMH) {

		xasprintf (&send_buffer,"NRMH\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		nrm_health_status=atoi(recv_buffer);

		if (nrm_health_status==2) {
			result=STATE_OK;
			xasprintf (&output_message,_("CRITICAL - NRM Status is bad!"));
		}
		else {
			if (nrm_health_status==1) {
				result=STATE_WARNING;
				xasprintf (&output_message,_("Warning - NRM Status is suspect!"));
			}

			xasprintf (&output_message,_("OK - NRM Status is good!"));
		}



		/* check packet receive buffers */
	} else if (vars_to_check==UPRB || vars_to_check==PUPRB) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		xasprintf (&send_buffer,"S15\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		used_packet_receive_buffers=atoi(recv_buffer);

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		xasprintf (&send_buffer,"S16\r\n");
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

		xasprintf (&output_message,_("%lu of %lu (%lu%%) packet receive buffers used"),used_packet_receive_buffers,max_packet_receive_buffers,percent_used_packet_receive_buffers);

		/* check SAP table entries */
	} else if (vars_to_check==SAPENTRIES) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		if (sap_number==-1)
			xasprintf (&send_buffer,"S9\r\n");
		else
			xasprintf (&send_buffer,"S9.%d\r\n",sap_number);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		sap_entries=atoi(recv_buffer);

		if (check_critical_value==TRUE && sap_entries >= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && sap_entries >= warning_value)
			result=STATE_WARNING;

		if (sap_number==-1)
			xasprintf (&output_message,_("%lu entries in SAP table"),sap_entries);
		else
			xasprintf (&output_message,_("%lu entries in SAP table for SAP type %d"),sap_entries,sap_number);

		/* check KB purgeable space on volume */
	} else if (vars_to_check==VKP) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		xasprintf (&send_buffer,"VKP%s\r\n",volume_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			xasprintf (&output_message,_("CRITICAL - Volume '%s' does not exist!"),volume_name);
			result=STATE_CRITICAL;
		} else {
			purgeable_disk_space=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && purgeable_disk_space >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && purgeable_disk_space >= warning_value)
				result=STATE_WARNING;
			xasprintf (&output_message,_("%s%lu KB purgeable on volume %s|Purge%s=%lu;%lu;%lu;;"),
				 (result==STATE_OK)?"":_("Only "),
				 purgeable_disk_space,
				 volume_name,
				 volume_name,
				 purgeable_disk_space,
				 warning_value,
				 critical_value);
		}
		/* check MB purgeable space on volume */
	} else if (vars_to_check==VMP) {

		xasprintf (&send_buffer,"VMP%s\r\n",volume_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			xasprintf (&output_message,_("CRITICAL - Volume '%s' does not exist!"),volume_name);
			result=STATE_CRITICAL;
		} else {
			purgeable_disk_space=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && purgeable_disk_space >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && purgeable_disk_space >= warning_value)
				result=STATE_WARNING;
			xasprintf (&output_message,_("%s%lu MB purgeable on volume %s|Purge%s=%lu;%lu;%lu;;"),
				 (result==STATE_OK)?"":_("Only "),
				 purgeable_disk_space,
				 volume_name,
				 volume_name,
				 purgeable_disk_space,
				 warning_value,
				 critical_value);
		}

		/* check % purgeable space on volume */
	} else if (vars_to_check==VPP) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		xasprintf (&send_buffer,"VKP%s\r\n",volume_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {

			xasprintf (&output_message,_("CRITICAL - Volume '%s' does not exist!"),volume_name);
			result=STATE_CRITICAL;

		} else {

			purgeable_disk_space=strtoul(recv_buffer,NULL,10);

			close(sd);
			my_tcp_connect (server_address, server_port, &sd);

			xasprintf (&send_buffer,"VKS%s\r\n",volume_name);
			result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
			if (result!=STATE_OK)
				return result;
			total_disk_space=strtoul(recv_buffer,NULL,10);

			percent_purgeable_space=(unsigned long)(((double)purgeable_disk_space/(double)total_disk_space)*100.0);

			if (check_critical_value==TRUE && percent_purgeable_space >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && percent_purgeable_space >= warning_value)
				result=STATE_WARNING;
			purgeable_disk_space/=1024;
			xasprintf (&output_message,_("%lu MB (%lu%%) purgeable on volume %s|Purgeable%s=%lu;%lu;%lu;0;100"),
					purgeable_disk_space,
					percent_purgeable_space,
					volume_name,
					volume_name,
					percent_purgeable_space,
					warning_value,
					critical_value
					);
		}

		/* check KB not yet purgeable space on volume */
	} else if (vars_to_check==VKNP) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		xasprintf (&send_buffer,"VKNP%s\r\n",volume_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			xasprintf (&output_message,_("CRITICAL - Volume '%s' does not exist!"),volume_name);
			result=STATE_CRITICAL;
		} else {
			non_purgeable_disk_space=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && non_purgeable_disk_space >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && non_purgeable_disk_space >= warning_value)
				result=STATE_WARNING;
			xasprintf (&output_message,_("%s%lu KB not yet purgeable on volume %s"),(result==STATE_OK)?"":_("Only "),non_purgeable_disk_space,volume_name);
		}

		/* check % not yet purgeable space on volume */
	} else if (vars_to_check==VPNP) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		xasprintf (&send_buffer,"VKNP%s\r\n",volume_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {

			xasprintf (&output_message,_("CRITICAL - Volume '%s' does not exist!"),volume_name);
			result=STATE_CRITICAL;

		} else {

			non_purgeable_disk_space=strtoul(recv_buffer,NULL,10);

			close(sd);
			my_tcp_connect (server_address, server_port, &sd);

			xasprintf (&send_buffer,"VKS%s\r\n",volume_name);
			result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
			if (result!=STATE_OK)
				return result;
			total_disk_space=strtoul(recv_buffer,NULL,10);

			percent_non_purgeable_space=(unsigned long)(((double)non_purgeable_disk_space/(double)total_disk_space)*100.0);

			if (check_critical_value==TRUE && percent_non_purgeable_space >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && percent_non_purgeable_space >= warning_value)
				result=STATE_WARNING;
			purgeable_disk_space/=1024;
			xasprintf (&output_message,_("%lu MB (%lu%%) not yet purgeable on volume %s"),non_purgeable_disk_space,percent_non_purgeable_space,volume_name);
		}

		/* check # of open files */
	} else if (vars_to_check==OFILES) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		xasprintf (&send_buffer,"S18\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		open_files=atoi(recv_buffer);

		if (check_critical_value==TRUE && open_files >= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && open_files >= warning_value)
			result=STATE_WARNING;

		xasprintf (&output_message,_("%lu open files|Openfiles=%lu;%lu;%lu;0,0"),
				open_files,
				open_files,
				warning_value,
				critical_value);


		/* check # of abended threads (Netware > 5.x only) */
	} else if (vars_to_check==ABENDS) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		xasprintf (&send_buffer,"S17\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		abended_threads=atoi(recv_buffer);

		if (check_critical_value==TRUE && abended_threads >= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && abended_threads >= warning_value)
			result=STATE_WARNING;

		xasprintf (&output_message,_("%lu abended threads|Abends=%lu;%lu;%lu;;"),
				abended_threads,
				abended_threads,
				warning_value,
				critical_value);

		/* check # of current service processes (Netware 5.x only) */
	} else if (vars_to_check==CSPROCS) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		xasprintf (&send_buffer,"S20\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		max_service_processes=atoi(recv_buffer);

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		xasprintf (&send_buffer,"S21\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		current_service_processes=atoi(recv_buffer);

		if (check_critical_value==TRUE && current_service_processes >= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && current_service_processes >= warning_value)
			result=STATE_WARNING;

		xasprintf (&output_message,
		          _("%lu current service processes (%lu max)|Processes=%lu;%lu;%lu;0;%lu"),
		          current_service_processes,
		          max_service_processes,
			  current_service_processes,
			  warning_value,
			  critical_value,
			  max_service_processes);

		/* check # Timesync Status */
	} else if (vars_to_check==TSYNC) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		xasprintf (&send_buffer,"S22\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		time_sync_status=atoi(recv_buffer);

		if (time_sync_status==0) {
			result=STATE_CRITICAL;
			xasprintf (&output_message,_("CRITICAL - Time not in sync with network!"));
		}
		else {
			xasprintf (&output_message,_("OK - Time in sync with network!"));
		}





		/* check LRU sitting time in secondss */
	} else if (vars_to_check==LRUS) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		send_buffer = strdup ("S4\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		lru_time=strtoul(recv_buffer,NULL,10);

		if (check_critical_value==TRUE && lru_time <= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && lru_time <= warning_value)
			result=STATE_WARNING;
		xasprintf (&output_message,_("LRU sitting time = %lu seconds"),lru_time);


		/* check % dirty cacheobuffers as a percentage of the total*/
	} else if (vars_to_check==DCB) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		send_buffer = strdup ("S6\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		dirty_cache_buffers=atoi(recv_buffer);

		if (check_critical_value==TRUE && dirty_cache_buffers <= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && dirty_cache_buffers <= warning_value)
			result=STATE_WARNING;
		xasprintf (&output_message,_("Dirty cache buffers = %lu%% of the total|DCB=%lu;%lu;%lu;0;100"),
				dirty_cache_buffers,
				dirty_cache_buffers,
				warning_value,
				critical_value);

		/* check % total cache buffers as a percentage of the original*/
	} else if (vars_to_check==TCB) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		send_buffer = strdup ("S7\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;
		total_cache_buffers=atoi(recv_buffer);

		if (check_critical_value==TRUE && total_cache_buffers <= critical_value)
			result=STATE_CRITICAL;
		else if (check_warning_value==TRUE && total_cache_buffers <= warning_value)
			result=STATE_WARNING;
		xasprintf (&output_message,_("Total cache buffers = %lu%% of the original|TCB=%lu;%lu;%lu;0;100"),
				total_cache_buffers,
				total_cache_buffers,
				warning_value,
				critical_value);

	} else if (vars_to_check==DSVER) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		xasprintf (&send_buffer,"S13\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		recv_buffer[strlen(recv_buffer)-1]=0;

		xasprintf (&output_message,_("NDS Version %s"),recv_buffer);

	} else if (vars_to_check==UPTIME) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		xasprintf (&send_buffer,"UPTIME\r\n");
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
	 		return result;


		recv_buffer[sizeof(recv_buffer)-1]=0;
		recv_buffer[strlen(recv_buffer)-1]=0;

		xasprintf (&output_message,_("Up %s"),recv_buffer);

	} else if (vars_to_check==NLM) {

		close(sd);
		my_tcp_connect (server_address, server_port, &sd);

		xasprintf (&send_buffer,"S24:%s\r\n",nlm_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		recv_buffer[strlen(recv_buffer)-1]=0;
		if (strcmp(recv_buffer,"-1")) {
			xasprintf (&output_message,_("Module %s version %s is loaded"),nlm_name,recv_buffer);
		} else {
			result=STATE_CRITICAL;
			xasprintf (&output_message,_("Module %s is not loaded"),nlm_name);

			}
	} else if (vars_to_check==NRMP) {

		xasprintf (&send_buffer,"NRMP:%s\r\n",nrmp_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			xasprintf (&output_message,_("CRITICAL - Value '%s' does not exist!"),nrmp_name);
			result=STATE_CRITICAL;
		}	else {
			nrmp_value=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && nrmp_value <= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && nrmp_value <= warning_value)
				result=STATE_WARNING;
			xasprintf (&output_message,
			          _("%s is  %lu|%s=%lu;%lu;%lu;;"),
			         nrmp_name,
			         nrmp_value,
				 nrmp_name,
				 nrmp_value,
				 warning_value,
				 critical_value);
		}

	} else if (vars_to_check==NRMM) {

		xasprintf (&send_buffer,"NRMM:%s\r\n",nrmm_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			xasprintf (&output_message,_("CRITICAL - Value '%s' does not exist!"),nrmm_name);
			result=STATE_CRITICAL;
		}	else {
			nrmm_value=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && nrmm_value <= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && nrmm_value <= warning_value)
				result=STATE_WARNING;
			xasprintf (&output_message,
			          _("%s is  %lu|%s=%lu;%lu;%lu;;"),
			         nrmm_name,
			         nrmm_value,
				 nrmm_name,
				 nrmm_value,
				 warning_value,
				 critical_value);
		}

	} else if (vars_to_check==NRMS) {

		xasprintf (&send_buffer,"NRMS:%s\r\n",nrms_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			xasprintf (&output_message,_("CRITICAL - Value '%s' does not exist!"),nrms_name);
			result=STATE_CRITICAL;
		}	else {
			nrms_value=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && nrms_value >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && nrms_value >= warning_value)
				result=STATE_WARNING;
			xasprintf (&output_message,
			          _("%s is  %lu|%s=%lu;%lu;%lu;;"),
			         nrms_name,
			         nrms_value,
				 nrms_name,
				 nrms_value,
				 warning_value,
				 critical_value);
		}

	} else if (vars_to_check==NSS1) {

		xasprintf (&send_buffer,"NSS1:%s\r\n",nss1_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			xasprintf (&output_message,_("CRITICAL - Value '%s' does not exist!"),nss1_name);
			result=STATE_CRITICAL;
		}	else {
			nss1_value=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && nss1_value >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && nss1_value >= warning_value)
				result=STATE_WARNING;
			xasprintf (&output_message,
			          _("%s is  %lu|%s=%lu;%lu;%lu;;"),
			         nss1_name,
			         nss1_value,
				 nss1_name,
				 nss1_value,
				 warning_value,
				 critical_value);
		}

	} else if (vars_to_check==NSS2) {

		xasprintf (&send_buffer,"NSS2:%s\r\n",nss2_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			xasprintf (&output_message,_("CRITICAL - Value '%s' does not exist!"),nss2_name);
			result=STATE_CRITICAL;
		}	else {
			nss2_value=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && nss2_value >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && nss2_value >= warning_value)
				result=STATE_WARNING;
			xasprintf (&output_message,
			          _("%s is  %lu|%s=%lu;%lu;%lu;;"),
			         nss2_name,
			         nss2_value,
				 nss2_name,
				 nss2_value,
				 warning_value,
				 critical_value);
		}

	} else if (vars_to_check==NSS3) {

		xasprintf (&send_buffer,"NSS3:%s\r\n",nss3_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			xasprintf (&output_message,_("CRITICAL - Value '%s' does not exist!"),nss3_name);
			result=STATE_CRITICAL;
		}	else {
			nss3_value=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && nss3_value >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && nss3_value >= warning_value)
				result=STATE_WARNING;
			xasprintf (&output_message,
			          _("%s is  %lu|%s=%lu;%lu;%lu;;"),
			         nss3_name,
			         nss3_value,
				 nss3_name,
				 nss3_value,
				 warning_value,
				 critical_value);
		}

	} else if (vars_to_check==NSS4) {

		xasprintf (&send_buffer,"NSS4:%s\r\n",nss4_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			xasprintf (&output_message,_("CRITICAL - Value '%s' does not exist!"),nss4_name);
			result=STATE_CRITICAL;
		}	else {
			nss4_value=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && nss4_value >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && nss4_value >= warning_value)
				result=STATE_WARNING;
			xasprintf (&output_message,
			          _("%s is  %lu|%s=%lu;%lu;%lu;;"),
			         nss4_name,
			         nss4_value,
				 nss4_name,
				 nss4_value,
				 warning_value,
				 critical_value);
		}

	} else if (vars_to_check==NSS5) {

		xasprintf (&send_buffer,"NSS5:%s\r\n",nss5_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			xasprintf (&output_message,_("CRITICAL - Value '%s' does not exist!"),nss5_name);
			result=STATE_CRITICAL;
		}	else {
			nss5_value=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && nss5_value >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && nss5_value >= warning_value)
				result=STATE_WARNING;
			xasprintf (&output_message,
			          _("%s is  %lu|%s=%lu;%lu;%lu;;"),
			         nss5_name,
			         nss5_value,
				 nss5_name,
				 nss5_value,
				 warning_value,
				 critical_value);
		}

	} else if (vars_to_check==NSS6) {

		xasprintf (&send_buffer,"NSS6:%s\r\n",nss6_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			xasprintf (&output_message,_("CRITICAL - Value '%s' does not exist!"),nss6_name);
			result=STATE_CRITICAL;
		}	else {
			nss6_value=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && nss6_value >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && nss6_value >= warning_value)
				result=STATE_WARNING;
			xasprintf (&output_message,
			          _("%s is  %lu|%s=%lu;%lu;%lu;;"),
			         nss6_name,
			         nss6_value,
				 nss6_name,
				 nss6_value,
				 warning_value,
				 critical_value);
		}

	} else if (vars_to_check==NSS7) {

		xasprintf (&send_buffer,"NSS7:%s\r\n",nss7_name);
		result=send_tcp_request(sd,send_buffer,recv_buffer,sizeof(recv_buffer));
		if (result!=STATE_OK)
			return result;

		if (!strcmp(recv_buffer,"-1\n")) {
			xasprintf (&output_message,_("CRITICAL - Value '%s' does not exist!"),nss7_name);
			result=STATE_CRITICAL;
		}	else {
			nss7_value=strtoul(recv_buffer,NULL,10);
			if (check_critical_value==TRUE && nss7_value >= critical_value)
				result=STATE_CRITICAL;
			else if (check_warning_value==TRUE && nss7_value >= warning_value)
				result=STATE_WARNING;
			xasprintf (&output_message,
			          _("%s is  %lu|%s=%lu;%lu;%lu;;"),
			         nss7_name,
			         nss7_value,
				 nss7_name,
				 nss7_value,
				 warning_value,
				 critical_value);
		}


}
	else {

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
			usage5 ();
			case 'h': /* help */
				print_help();
				exit(STATE_OK);
			case 'V': /* version */
				print_revision(progname, NP_VERSION);
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
					die(STATE_UNKNOWN,_("Server port an integer\n"));
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
				else if (strncmp(optarg,"VMF",3)==0) {
					vars_to_check=VMF;
					volume_name = strdup (optarg+3);
					if (!strcmp(volume_name,""))
						volume_name = strdup ("SYS");
				}
				else if (!strcmp(optarg,"DSDB"))
					vars_to_check=DSDB;
				else if (!strcmp(optarg,"LOGINS"))
					vars_to_check=LOGINS;
				else if (!strcmp(optarg,"NRMH"))
					vars_to_check=NRMH;
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
				else if (strncmp(optarg,"VMP",3)==0) {
					vars_to_check=VMP;
					volume_name = strdup (optarg+3);
					if (!strcmp(volume_name,""))
						volume_name = strdup ("SYS");
				}
				else if (strncmp(optarg,"VMU",3)==0) {
					vars_to_check=VMU;
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
				else if (!strcmp(optarg,"UPTIME")) {
					vars_to_check=UPTIME;
				}
				else if (strncmp(optarg,"NLM:",4)==0) {
					vars_to_check=NLM;
					nlm_name=strdup (optarg+4);
				}
				else if (strncmp(optarg,"NRMP",4)==0) {
					vars_to_check=NRMP;
					nrmp_name = strdup (optarg+4);
					if (!strcmp(nrmp_name,""))
						nrmp_name = strdup ("AVAILABLE_MEMORY");
				}
				else if (strncmp(optarg,"NRMM",4)==0) {
					vars_to_check=NRMM;
					nrmm_name = strdup (optarg+4);
					if (!strcmp(nrmm_name,""))
						nrmm_name = strdup ("AVAILABLE_CACHE_MEMORY");

				}

				else if (strncmp(optarg,"NRMS",4)==0) {
					vars_to_check=NRMS;
					nrms_name = strdup (optarg+4);
					if (!strcmp(nrms_name,""))
						nrms_name = strdup ("USED_SWAP_SPACE");

				}

				else if (strncmp(optarg,"NSS1",4)==0) {
					vars_to_check=NSS1;
					nss1_name = strdup (optarg+4);
					if (!strcmp(nss1_name,""))
						nss1_name = strdup ("CURRENTBUFFERCACHESIZE");

				}

				else if (strncmp(optarg,"NSS2",4)==0) {
					vars_to_check=NSS2;
					nss2_name = strdup (optarg+4);
					if (!strcmp(nss2_name,""))
						nss2_name = strdup ("CACHEHITS");

				}

				else if (strncmp(optarg,"NSS3",4)==0) {
					vars_to_check=NSS3;
					nss3_name = strdup (optarg+4);
					if (!strcmp(nss3_name,""))
						nss3_name = strdup ("CACHEGITPERCENT");

				}

				else if (strncmp(optarg,"NSS4",4)==0) {
					vars_to_check=NSS4;
					nss4_name = strdup (optarg+4);
					if (!strcmp(nss4_name,""))
						nss4_name = strdup ("CURRENTOPENCOUNT");

				}

				else if (strncmp(optarg,"NSS5",4)==0) {
					vars_to_check=NSS5;
					nss5_name = strdup (optarg+4);
					if (!strcmp(nss5_name,""))
						nss5_name = strdup ("CACHEMISSES");

				}


				else if (strncmp(optarg,"NSS6",4)==0) {
					vars_to_check=NSS6;
					nss6_name = strdup (optarg+4);
					if (!strcmp(nss6_name,""))
						nss6_name = strdup ("PENDINGWORKSCOUNT");

				}


				else if (strncmp(optarg,"NSS7",4)==0) {
					vars_to_check=NSS7;
					nss7_name = strdup (optarg+4);
					if (!strcmp(nss7_name,""))
						nss7_name = strdup ("CACHESIZE");

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
	xasprintf (&myport, "%d", PORT);

	print_revision (progname, NP_VERSION);

	printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("This plugin attempts to contact the MRTGEXT NLM running on a"));
  printf ("%s\n", _("Novell server to gather the requested system information."));

  printf ("\n\n");

	print_usage();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (UT_HOST_PORT, 'p', myport);

	printf (" %s\n", "-v, --variable=STRING");
  printf ("   %s\n", _("Variable to check.  Valid variables include:"));
  printf ("    %s\n", _("LOAD1     = 1 minute average CPU load"));
  printf ("    %s\n", _("LOAD5     = 5 minute average CPU load"));
  printf ("    %s\n", _("LOAD15    = 15 minute average CPU load"));
  printf ("    %s\n", _("CSPROCS   = number of current service processes (NW 5.x only)"));
  printf ("    %s\n", _("ABENDS    = number of abended threads (NW 5.x only)"));
  printf ("    %s\n", _("UPTIME    = server uptime"));
	printf ("    %s\n", _("LTCH      = percent long term cache hits"));
  printf ("    %s\n", _("CBUFF     = current number of cache buffers"));
  printf ("    %s\n", _("CDBUFF    = current number of dirty cache buffers"));
  printf ("    %s\n", _("DCB       = dirty cache buffers as a percentage of the total"));
  printf ("    %s\n", _("TCB       = dirty cache buffers as a percentage of the original"));
	printf ("    %s\n", _("OFILES    = number of open files"));
  printf ("    %s\n", _("    VMF<vol>  = MB of free space on Volume <vol>"));
  printf ("    %s\n", _("    VMU<vol>  = MB used space on Volume <vol>"));
  printf ("    %s\n", _("    VMP<vol>  = MB of purgeable space on Volume <vol>"));
  printf ("    %s\n", _("    VPF<vol>  = percent free space on volume <vol>"));
  printf ("    %s\n", _("    VKF<vol>  = KB of free space on volume <vol>"));
  printf ("    %s\n", _("    VPP<vol>  = percent purgeable space on volume <vol>"));
  printf ("    %s\n", _("    VKP<vol>  = KB of purgeable space on volume <vol>"));
  printf ("    %s\n", _("    VPNP<vol> = percent not yet purgeable space on volume <vol>"));
  printf ("    %s\n", _("    VKNP<vol> = KB of not yet purgeable space on volume <vol>"));
  printf ("    %s\n", _("    LRUM      = LRU sitting time in minutes"));
  printf ("    %s\n", _("    LRUS      = LRU sitting time in seconds"));
  printf ("    %s\n", _("    DSDB      = check to see if DS Database is open"));
  printf ("    %s\n", _("    DSVER     = NDS version"));
  printf ("    %s\n", _("    UPRB      = used packet receive buffers"));
  printf ("    %s\n", _("    PUPRB     = percent (of max) used packet receive buffers"));
  printf ("    %s\n", _("    SAPENTRIES = number of entries in the SAP table"));
  printf ("    %s\n", _("    SAPENTRIES<n> = number of entries in the SAP table for SAP type <n>"));
  printf ("    %s\n", _("    TSYNC     = timesync status"));
  printf ("    %s\n", _("    LOGINS    = check to see if logins are enabled"));
  printf ("    %s\n", _("    CONNS     = number of currently licensed connections"));
  printf ("    %s\n", _("    NRMH	= NRM Summary Status"));
  printf ("    %s\n", _("    NRMP<stat> = Returns the current value for a NRM health item"));
  printf ("    %s\n", _("    NRMM<stat> = Returns the current memory stats from NRM"));
  printf ("    %s\n", _("    NRMS<stat> = Returns the current Swapfile stats from NRM"));
  printf ("    %s\n", _("    NSS1<stat> = Statistics from _Admin:Manage_NSS\\GeneralStats.xml"));
  printf ("    %s\n", _("    NSS3<stat> = Statistics from _Admin:Manage_NSS\\NameCache.xml"));
  printf ("    %s\n", _("    NSS4<stat> = Statistics from _Admin:Manage_NSS\\FileStats.xml"));
  printf ("    %s\n", _("    NSS5<stat> = Statistics from _Admin:Manage_NSS\\ObjectCache.xml"));
  printf ("    %s\n", _("    NSS6<stat> = Statistics from _Admin:Manage_NSS\\Thread.xml"));
  printf ("    %s\n", _("    NSS7<stat> = Statistics from _Admin:Manage_NSS\\AuthorizationCache.xml"));
  printf ("    %s\n", _("    NLM:<nlm> = check if NLM is loaded and report version"));
  printf ("    %s\n", _("                (e.g. NLM:TSANDS.NLM)"));
  printf ("\n");
	printf (" %s\n", "-w, --warning=INTEGER");
  printf ("    %s\n", _("Threshold which will result in a warning status"));
  printf (" %s\n", "-c, --critical=INTEGER");
  printf ("    %s\n", _("Threshold which will result in a critical status"));
  printf (" %s\n", "-o, --osversion");
  printf ("    %s\n", _("Include server version string in results"));

	printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

  printf ("\n");
  printf ("%s\n", _("Notes:"));
	printf (" %s\n", _("- This plugin requres that the MRTGEXT.NLM file from James Drews' MRTG"));
  printf (" %s\n", _("  extension for NetWare be loaded on the Novell servers you wish to check."));
  printf (" %s\n", _("  (available from http://www.engr.wisc.edu/~drews/mrtg/)"));
  printf (" %s\n", _("- Values for critical thresholds should be lower than warning thresholds"));
  printf (" %s\n", _("  when the following variables are checked: VPF, VKF, LTCH, CBUFF, DCB, "));
  printf (" %s\n", _("  TCB, LRUS and LRUM."));

	printf (UT_SUPPORT);
}



void print_usage(void)
{
  printf ("%s\n", _("Usage:"));
	printf ("%s -H host [-p port] [-v variable] [-w warning] [-c critical] [-t timeout]\n",progname);
}
