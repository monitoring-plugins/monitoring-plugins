/******************************************************************************
*
* CHECK_DHCP.C
*
* Program: DHCP plugin for Nagios
* License: GPL
* Copyright (c) 2001-2004 Ethan Galstad (nagios@nagios.org)
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

#include "common.h"
#include "netutils.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>

#if defined( __linux__ )
#include <linux/if_ether.h>
#include <features.h>
#else
#include <netinet/if_ether.h>
#include <sys/sysctl.h>
#include <net/if_dl.h>
#endif

const char *progname = "check_dhcp";

/*#define DEBUG*/
#define HAVE_GETOPT_H


/**** Common definitions ****/

#define STATE_OK          0
#define STATE_WARNING     1
#define STATE_CRITICAL    2
#define STATE_UNKNOWN     -1

#define OK                0
#define ERROR             -1

#define FALSE             0
#define TRUE              1


/**** DHCP definitions ****/

#define MAX_DHCP_CHADDR_LENGTH           16
#define MAX_DHCP_SNAME_LENGTH            64
#define MAX_DHCP_FILE_LENGTH             128
#define MAX_DHCP_OPTIONS_LENGTH          312


typedef struct dhcp_packet_struct{
        u_int8_t  op;                   /* packet type */
        u_int8_t  htype;                /* type of hardware address for this machine (Ethernet, etc) */
        u_int8_t  hlen;                 /* length of hardware address (of this machine) */
        u_int8_t  hops;                 /* hops */
        u_int32_t xid;                  /* random transaction id number - chosen by this machine */
        u_int16_t secs;                 /* seconds used in timing */
        u_int16_t flags;                /* flags */
        struct in_addr ciaddr;          /* IP address of this machine (if we already have one) */
        struct in_addr yiaddr;          /* IP address of this machine (offered by the DHCP server) */
        struct in_addr siaddr;          /* IP address of DHCP server */
        struct in_addr giaddr;          /* IP address of DHCP relay */
        unsigned char chaddr [MAX_DHCP_CHADDR_LENGTH];      /* hardware address of this machine */
        char sname [MAX_DHCP_SNAME_LENGTH];    /* name of DHCP server */
        char file [MAX_DHCP_FILE_LENGTH];      /* boot file name (used for diskless booting?) */
	char options[MAX_DHCP_OPTIONS_LENGTH];  /* options */
        }dhcp_packet;


typedef struct dhcp_offer_struct{
	struct in_addr server_address;   /* address of DHCP server that sent this offer */
	struct in_addr offered_address;  /* the IP address that was offered to us */
	u_int32_t lease_time;            /* lease time in seconds */
	u_int32_t renewal_time;          /* renewal time in seconds */
	u_int32_t rebinding_time;        /* rebinding time in seconds */
	struct dhcp_offer_struct *next;
        }dhcp_offer;


typedef struct requested_server_struct{
	struct in_addr server_address;
	struct requested_server_struct *next;
        }requested_server;


#define BOOTREQUEST     1
#define BOOTREPLY       2

#define DHCPDISCOVER    1
#define DHCPOFFER       2
#define DHCPREQUEST     3
#define DHCPDECLINE     4
#define DHCPACK         5
#define DHCPNACK        6
#define DHCPRELEASE     7

#define DHCP_OPTION_MESSAGE_TYPE        53
#define DHCP_OPTION_HOST_NAME           12
#define DHCP_OPTION_BROADCAST_ADDRESS   28
#define DHCP_OPTION_REQUESTED_ADDRESS   50
#define DHCP_OPTION_LEASE_TIME          51
#define DHCP_OPTION_RENEWAL_TIME        58
#define DHCP_OPTION_REBINDING_TIME      59

#define DHCP_INFINITE_TIME              0xFFFFFFFF

#define DHCP_BROADCAST_FLAG 32768

#define DHCP_SERVER_PORT   67
#define DHCP_CLIENT_PORT   68

#define ETHERNET_HARDWARE_ADDRESS            1     /* used in htype field of dhcp packet */
#define ETHERNET_HARDWARE_ADDRESS_LENGTH     6     /* length of Ethernet hardware addresses */

unsigned char client_hardware_address[MAX_DHCP_CHADDR_LENGTH]="";

char network_interface_name[8]="eth0";

u_int32_t packet_xid=0;

u_int32_t dhcp_lease_time=0;
u_int32_t dhcp_renewal_time=0;
u_int32_t dhcp_rebinding_time=0;

int dhcpoffer_timeout=2;

dhcp_offer *dhcp_offer_list=NULL;
requested_server *requested_server_list=NULL;

int valid_responses=0;     /* number of valid DHCPOFFERs we received */
int requested_servers=0;   
int requested_responses=0;

int request_specific_address=FALSE;
int received_requested_address=FALSE;
struct in_addr requested_address;


int process_arguments(int, char **);
int call_getopt(int, char **);
int validate_arguments(void);
void print_usage(void);
void print_help(void);

int get_hardware_address(int,char *);

int send_dhcp_discover(int);
int get_dhcp_offer(int);

int get_results(void);

int add_dhcp_offer(struct in_addr,dhcp_packet *);
int free_dhcp_offer_list(void);
int free_requested_server_list(void);

int create_dhcp_socket(void);
int close_dhcp_socket(int);
int send_dhcp_packet(void *,int,int,struct sockaddr_in *);
int receive_dhcp_packet(void *,int,int,int,struct sockaddr_in *);



int main(int argc, char **argv){
	int dhcp_socket;
	int result;

	if(process_arguments(argc,argv)!=OK){
		/*usage("Invalid command arguments supplied\n");*/
		printf("Invalid command arguments supplied\n");
		exit(STATE_UNKNOWN);
	        }


	/* create socket for DHCP communications */
	dhcp_socket=create_dhcp_socket();

	/* get hardware address of client machine */
	get_hardware_address(dhcp_socket,network_interface_name);

	/* send DHCPDISCOVER packet */
	send_dhcp_discover(dhcp_socket);

	/* wait for a DHCPOFFER packet */
	get_dhcp_offer(dhcp_socket);

	/* close socket we created */
	close_dhcp_socket(dhcp_socket);

	/* determine state/plugin output to return */
	result=get_results();

	/* free allocated memory */
	free_dhcp_offer_list();
	free_requested_server_list();

	return result;
        }



/* determines hardware address on client machine */
int get_hardware_address(int sock,char *interface_name){
#if defined(__linux__)
	struct ifreq ifr;

	strncpy((char *)&ifr.ifr_name,interface_name,sizeof(ifr.ifr_name));
	
	/* try and grab hardware address of requested interface */
	if(ioctl(sock,SIOCGIFHWADDR,&ifr)<0){
                printf("Error: Could not get hardware address of interface '%s'\n",interface_name);
		exit(STATE_UNKNOWN);
	        }

	memcpy(&client_hardware_address[0],&ifr.ifr_hwaddr.sa_data,6);
#else
	/* Code from getmac.c posted at http://lists.freebsd.org/pipermail/freebsd-hackers/2004-June/007415.html
         * by Alecs King based on Unix Network programming Ch 17
         */

        int                     mib[6], len;
        char                    *buf;
        unsigned char           *ptr;
        struct if_msghdr        *ifm;
        struct sockaddr_dl      *sdl;

        mib[0] = CTL_NET;
        mib[1] = AF_ROUTE;
        mib[2] = 0;
        mib[3] = AF_LINK;
        mib[4] = NET_RT_IFLIST;

        if ((mib[5] = if_nametoindex(interface_name)) == 0) {
                perror("if_nametoindex error");
                exit(2);
        }

        if (sysctl(mib, 6, NULL, &len, NULL, 0) < 0) {
                perror("sysctl 1 error");
                exit(3);
        }

        if ((buf = malloc(len)) == NULL) {
                perror("malloc error");
                exit(4);
        }

        if (sysctl(mib, 6, buf, &len, NULL, 0) < 0) {
                perror("sysctl 2 error");
                exit(5);
        }

        ifm = (struct if_msghdr *)buf;
        sdl = (struct sockaddr_dl *)(ifm + 1);
        ptr = (unsigned char *)LLADDR(sdl);
        memcpy(&client_hardware_address[0], ptr, 6) ;
#endif


#ifdef DEBUG
	printf("Hardware address: %02x:%02x:%02x:",client_hardware_address[0],client_hardware_address[1],client_hardware_address[2]);
	printf("%02x:",client_hardware_address[3]);
	printf("%02x:%02x\n",client_hardware_address[4],client_hardware_address[5]);
	printf("\n");
#endif

	return OK;
        }


/* sends a DHCPDISCOVER broadcast message in an attempt to find DHCP servers */
int send_dhcp_discover(int sock){
	dhcp_packet discover_packet;
	struct sockaddr_in sockaddr_broadcast;


	/* clear the packet data structure */
	bzero(&discover_packet,sizeof(discover_packet));


	/* boot request flag (backward compatible with BOOTP servers) */
	discover_packet.op=BOOTREQUEST;

	/* hardware address type */
	discover_packet.htype=ETHERNET_HARDWARE_ADDRESS;

	/* length of our hardware address */
	discover_packet.hlen=ETHERNET_HARDWARE_ADDRESS_LENGTH;

	discover_packet.hops=0;

	/* transaction id is supposed to be random */
	srand(time(NULL));
	packet_xid=random();
	discover_packet.xid=htonl(packet_xid);

	/**** WHAT THE HECK IS UP WITH THIS?!?  IF I DON'T MAKE THIS CALL, ONLY ONE SERVER RESPONSE IS PROCESSED!!!! ****/
	/* downright bizzarre... */
	ntohl(discover_packet.xid);

	/*discover_packet.secs=htons(65535);*/
	discover_packet.secs=0xFF;

	/* tell server it should broadcast its response */ 
	discover_packet.flags=htons(DHCP_BROADCAST_FLAG);

	/* our hardware address */
	memcpy(discover_packet.chaddr,client_hardware_address,ETHERNET_HARDWARE_ADDRESS_LENGTH);

	/* first four bytes of options field is magic cookie (as per RFC 2132) */
	discover_packet.options[0]='\x63';
	discover_packet.options[1]='\x82';
	discover_packet.options[2]='\x53';
	discover_packet.options[3]='\x63';

	/* DHCP message type is embedded in options field */
	discover_packet.options[4]=DHCP_OPTION_MESSAGE_TYPE;    /* DHCP message type option identifier */
	discover_packet.options[5]='\x01';               /* DHCP message option length in bytes */
	discover_packet.options[6]=DHCPDISCOVER;

	/* the IP address we're requesting */
	if(request_specific_address==TRUE){
		discover_packet.options[7]=DHCP_OPTION_REQUESTED_ADDRESS;
		discover_packet.options[8]='\x04';
		memcpy(&discover_packet.options[9],&requested_address,sizeof(requested_address));
	        }
	
	/* send the DHCPDISCOVER packet to broadcast address */
        sockaddr_broadcast.sin_family=AF_INET;
        sockaddr_broadcast.sin_port=htons(DHCP_SERVER_PORT);
        sockaddr_broadcast.sin_addr.s_addr=INADDR_BROADCAST;
	bzero(&sockaddr_broadcast.sin_zero,sizeof(sockaddr_broadcast.sin_zero));


#ifdef DEBUG
	printf("DHCPDISCOVER to %s port %d\n",inet_ntoa(sockaddr_broadcast.sin_addr),ntohs(sockaddr_broadcast.sin_port));
	printf("DHCPDISCOVER XID: %lu (0x%X)\n",ntohl(discover_packet.xid),ntohl(discover_packet.xid));
	printf("DHCDISCOVER ciaddr:  %s\n",inet_ntoa(discover_packet.ciaddr));
	printf("DHCDISCOVER yiaddr:  %s\n",inet_ntoa(discover_packet.yiaddr));
	printf("DHCDISCOVER siaddr:  %s\n",inet_ntoa(discover_packet.siaddr));
	printf("DHCDISCOVER giaddr:  %s\n",inet_ntoa(discover_packet.giaddr));
#endif

	/* send the DHCPDISCOVER packet out */
	send_dhcp_packet(&discover_packet,sizeof(discover_packet),sock,&sockaddr_broadcast);

#ifdef DEBUG
	printf("\n\n");
#endif

	return OK;
        }




/* waits for a DHCPOFFER message from one or more DHCP servers */
int get_dhcp_offer(int sock){
	dhcp_packet offer_packet;
	struct sockaddr_in source;
	int result=OK;
	int timeout=1;
	int responses=0;
	int x;
	time_t start_time;
	time_t current_time;

	time(&start_time);

	/* receive as many responses as we can */
	for(responses=0,valid_responses=0;;){

		time(&current_time);
		if((current_time-start_time)>=dhcpoffer_timeout)
			break;

#ifdef DEBUG
		printf("\n\n");
#endif

		bzero(&source,sizeof(source));
		bzero(&offer_packet,sizeof(offer_packet));

		result=OK;
		result=receive_dhcp_packet(&offer_packet,sizeof(offer_packet),sock,dhcpoffer_timeout,&source);
		
		if(result!=OK){
#ifdef DEBUG
			printf("Result=ERROR\n");
#endif
			continue;
		        }
		else{
#ifdef DEBUG
			printf("Result=OK\n");
#endif
			responses++;
		        }

#ifdef DEBUG
		printf("DHCPOFFER from IP address %s\n",inet_ntoa(source.sin_addr));
		printf("DHCPOFFER XID: %lu (0x%X)\n",ntohl(offer_packet.xid),ntohl(offer_packet.xid));
#endif

		/* check packet xid to see if its the same as the one we used in the discover packet */
		if(ntohl(offer_packet.xid)!=packet_xid){
#ifdef DEBUG
			printf("DHCPOFFER XID (%lu) did not match DHCPDISCOVER XID (%lu) - ignoring packet\n",ntohl(offer_packet.xid),packet_xid);
#endif
			continue;
		        }

		/* check hardware address */
		result=OK;
#ifdef DEBUG
		printf("DHCPOFFER chaddr: ");
#endif
		for(x=0;x<ETHERNET_HARDWARE_ADDRESS_LENGTH;x++){
#ifdef DEBUG
			printf("%02X",(unsigned char)offer_packet.chaddr[x]);
#endif
			if(offer_packet.chaddr[x]!=client_hardware_address[x]){
				result=ERROR;
			        }
		        }
#ifdef DEBUG
		printf("\n");
#endif
		if(result==ERROR){
#ifdef DEBUG
			printf("DHCPOFFER hardware address did not match our own - ignoring packet\n");
#endif
			continue;
		        }

#ifdef DEBUG
		printf("DHCPOFFER ciaddr: %s\n",inet_ntoa(offer_packet.ciaddr));
		printf("DHCPOFFER yiaddr: %s\n",inet_ntoa(offer_packet.yiaddr));
		printf("DHCPOFFER siaddr: %s\n",inet_ntoa(offer_packet.siaddr));
		printf("DHCPOFFER giaddr: %s\n",inet_ntoa(offer_packet.giaddr));
#endif

		add_dhcp_offer(source.sin_addr,&offer_packet);

		valid_responses++;
	        }

#ifdef DEBUG
	printf("Total responses seen on the wire: %d\n",responses);
	printf("Valid responses for this machine: %d\n",valid_responses);
#endif

	return OK;
        }



/* sends a DHCP packet */
int send_dhcp_packet(void *buffer, int buffer_size, int sock, struct sockaddr_in *dest){
	struct sockaddr_in myname;
	int result;

	result=sendto(sock,(char *)buffer,buffer_size,0,(struct sockaddr *)dest,sizeof(*dest));

#ifdef DEBUG
	printf("send_dhcp_packet result: %d\n",result);
#endif

	if(result<0)
		return ERROR;

	return OK;
        }



/* receives a DHCP packet */
int receive_dhcp_packet(void *buffer, int buffer_size, int sock, int timeout, struct sockaddr_in *address){
        struct timeval tv;
        fd_set readfds;
	int recv_result;
	socklen_t address_size;
	struct sockaddr_in source_address;


        /* wait for data to arrive (up time timeout) */
        tv.tv_sec=timeout;
        tv.tv_usec=0;
        FD_ZERO(&readfds);
        FD_SET(sock,&readfds);
        select(sock+1,&readfds,NULL,NULL,&tv);

        /* make sure some data has arrived */
        if(!FD_ISSET(sock,&readfds)){
#ifdef DEBUG
                printf("No (more) data received\n");
#endif
                return ERROR;
                }

        else{

		/* why do we need to peek first?  i don't know, its a hack.  without it, the source address of the first packet received was
		   not being interpreted correctly.  sigh... */
		bzero(&source_address,sizeof(source_address));
		address_size=sizeof(source_address);
                recv_result=recvfrom(sock,(char *)buffer,buffer_size,MSG_PEEK,(struct sockaddr *)&source_address,&address_size);
#ifdef DEBUG
		printf("recv_result_1: %d\n",recv_result);
#endif
                recv_result=recvfrom(sock,(char *)buffer,buffer_size,0,(struct sockaddr *)&source_address,&address_size);
#ifdef DEBUG
		printf("recv_result_2: %d\n",recv_result);
#endif

                if(recv_result==-1){
#ifdef DEBUG
			printf("recvfrom() failed, ");
			printf("errno: (%d) -> %s\n",errno,strerror(errno));
#endif
                        return ERROR;
                        }
		else{
#ifdef DEBUG
			printf("receive_dhcp_packet() result: %d\n",recv_result);
			printf("receive_dhcp_packet() source: %s\n",inet_ntoa(source_address.sin_addr));
#endif

			memcpy(address,&source_address,sizeof(source_address));
			return OK;
		        }
                }

	return OK;
        }



/* creates a socket for DHCP communication */
int create_dhcp_socket(void){
        struct sockaddr_in myname;
	struct ifreq interface;
        int sock;
        int flag=1;

        /* Set up the address we're going to bind to. */
	bzero(&myname,sizeof(myname));
        myname.sin_family=AF_INET;
        myname.sin_port=htons(DHCP_CLIENT_PORT);
        myname.sin_addr.s_addr=INADDR_ANY;                 /* listen on any address */
        bzero(&myname.sin_zero,sizeof(myname.sin_zero));

        /* create a socket for DHCP communications */
	sock=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if(sock<0){
		printf("Error: Could not create socket!\n");
		exit(STATE_UNKNOWN);
	        }

#ifdef DEBUG
	printf("DHCP socket: %d\n",sock);
#endif

        /* set the reuse address flag so we don't get errors when restarting */
        flag=1;
        if(setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,(char *)&flag,sizeof(flag))<0){
		printf("Error: Could not set reuse address option on DHCP socket!\n");
		exit(STATE_UNKNOWN);
	        }

        /* set the broadcast option - we need this to listen to DHCP broadcast messages */
        if(setsockopt(sock,SOL_SOCKET,SO_BROADCAST,(char *)&flag,sizeof flag)<0){
		printf("Error: Could not set broadcast option on DHCP socket!\n");
		exit(STATE_UNKNOWN);
	        }

	/* bind socket to interface */
#if defined(__linux__)
	strncpy(interface.ifr_ifrn.ifrn_name,network_interface_name,IFNAMSIZ);
	if(setsockopt(sock,SOL_SOCKET,SO_BINDTODEVICE,(char *)&interface,sizeof(interface))<0){
		printf("Error: Could not bind socket to interface %s.  Check your privileges...\n",network_interface_name);
		exit(STATE_UNKNOWN);
	        }

#else
	strncpy(interface.ifr_name,network_interface_name,IFNAMSIZ);
#endif

        /* bind the socket */
        if(bind(sock,(struct sockaddr *)&myname,sizeof(myname))<0){
		printf("Error: Could not bind to DHCP socket (port %d)!  Check your privileges...\n",DHCP_CLIENT_PORT);
		exit(STATE_UNKNOWN);
	        }

        return sock;
        }





/* closes DHCP socket */
int close_dhcp_socket(int sock){

	close(sock);

	return OK;
        }




/* adds a requested server address to list in memory */
int add_requested_server(struct in_addr server_address){
	requested_server *new_server;

	new_server=(requested_server *)malloc(sizeof(requested_server));
	if(new_server==NULL)
		return ERROR;

	new_server->server_address=server_address;

	new_server->next=requested_server_list;
	requested_server_list=new_server;

	requested_servers++;

#ifdef DEBUG
	printf("Requested server address: %s\n",inet_ntoa(new_server->server_address));
#endif

	return OK;
        }




/* adds a DHCP OFFER to list in memory */
int add_dhcp_offer(struct in_addr source,dhcp_packet *offer_packet){
	dhcp_offer *new_offer;
	int x;
	int y;
	unsigned option_type;
	unsigned option_length;

	if(offer_packet==NULL)
		return ERROR;

	/* process all DHCP options present in the packet */
	for(x=4;x<MAX_DHCP_OPTIONS_LENGTH;){

		/* end of options (0 is really just a pad, but bail out anyway) */
		if((int)offer_packet->options[x]==-1 || (int)offer_packet->options[x]==0)
			break;

		/* get option type */
		option_type=offer_packet->options[x++];

		/* get option length */
		option_length=offer_packet->options[x++];

#ifdef DEBUG
		printf("Option: %d (0x%02X)\n",option_type,option_length);
#endif

		/* get option data */
		if(option_type==DHCP_OPTION_LEASE_TIME)
			dhcp_lease_time=ntohl(*((u_int32_t *)&offer_packet->options[x]));
		if(option_type==DHCP_OPTION_RENEWAL_TIME)
			dhcp_renewal_time=ntohl(*((u_int32_t *)&offer_packet->options[x]));
		if(option_type==DHCP_OPTION_REBINDING_TIME)
			dhcp_rebinding_time=ntohl(*((u_int32_t *)&offer_packet->options[x]));

		/* skip option data we're ignoring */
		else
			for(y=0;y<option_length;y++,x++);
	        }

#ifdef DEBUG
	if(dhcp_lease_time==DHCP_INFINITE_TIME)
		printf("Lease Time: Infinite\n");
	else
		printf("Lease Time: %lu seconds\n",(unsigned long)dhcp_lease_time);
	if(dhcp_renewal_time==DHCP_INFINITE_TIME)
		printf("Renewal Time: Infinite\n");
	else
		printf("Renewal Time: %lu seconds\n",(unsigned long)dhcp_renewal_time);
	if(dhcp_rebinding_time==DHCP_INFINITE_TIME)
		printf("Rebinding Time: Infinite\n");
	printf("Rebinding Time: %lu seconds\n",(unsigned long)dhcp_rebinding_time);
#endif

	new_offer=(dhcp_offer *)malloc(sizeof(dhcp_offer));

	if(new_offer==NULL)
		return ERROR;


	new_offer->server_address=source;
	new_offer->offered_address=offer_packet->yiaddr;
	new_offer->lease_time=dhcp_lease_time;
	new_offer->renewal_time=dhcp_renewal_time;
	new_offer->rebinding_time=dhcp_rebinding_time;


#ifdef DEBUG
	printf("Added offer from server @ %s",inet_ntoa(new_offer->server_address));
	printf(" of IP address %s\n",inet_ntoa(new_offer->offered_address));
#endif

	/* add new offer to head of list */
	new_offer->next=dhcp_offer_list;
	dhcp_offer_list=new_offer;

	return OK;
        }




/* frees memory allocated to DHCP OFFER list */
int free_dhcp_offer_list(void){
	dhcp_offer *this_offer;
	dhcp_offer *next_offer;

	for(this_offer=dhcp_offer_list;this_offer!=NULL;this_offer=next_offer){
		next_offer=this_offer->next;
		free(this_offer);
	        }

	return OK;
        }




/* frees memory allocated to requested server list */
int free_requested_server_list(void){
	requested_server *this_server;
	requested_server *next_server;

	for(this_server=requested_server_list;this_server!=NULL;this_server=next_server){
		next_server=this_server->next;
		free(this_server);
	        }
	
	return OK;
        }


/* gets state and plugin output to return */
int get_results(void){
	dhcp_offer *temp_offer;
	requested_server *temp_server;
	int result;
	u_int32_t max_lease_time=0;

	received_requested_address=FALSE;

	/* checks responses from requested servers */
	requested_responses=0;
	if(requested_servers>0){

		for(temp_server=requested_server_list;temp_server!=NULL;temp_server=temp_server->next){

			for(temp_offer=dhcp_offer_list;temp_offer!=NULL;temp_offer=temp_offer->next){

				/* get max lease time we were offered */
				if(temp_offer->lease_time>max_lease_time || temp_offer->lease_time==DHCP_INFINITE_TIME)
					max_lease_time=temp_offer->lease_time;
				
				/* see if we got the address we requested */
				if(!memcmp(&requested_address,&temp_offer->offered_address,sizeof(requested_address)))
					received_requested_address=TRUE;

				/* see if the servers we wanted a response from talked to us or not */
				if(!memcmp(&temp_offer->server_address,&temp_server->server_address,sizeof(temp_server->server_address))){
#ifdef DEBUG
					printf("DHCP Server Match: Offerer=%s",inet_ntoa(temp_offer->server_address));
					printf(" Requested=%s\n",inet_ntoa(temp_server->server_address));
#endif				       
					requested_responses++;
				        }
		                }
		        }

	        }

	/* else check and see if we got our requested address from any server */
	else{

		for(temp_offer=dhcp_offer_list;temp_offer!=NULL;temp_offer=temp_offer->next){

			/* get max lease time we were offered */
			if(temp_offer->lease_time>max_lease_time || temp_offer->lease_time==DHCP_INFINITE_TIME)
				max_lease_time=temp_offer->lease_time;
				
			/* see if we got the address we requested */
			if(!memcmp(&requested_address,&temp_offer->offered_address,sizeof(requested_address)))
				received_requested_address=TRUE;
	                }
	        }

	result=STATE_OK;
	if(valid_responses==0)
		result=STATE_CRITICAL;
	else if(requested_servers>0 && requested_responses==0)
		result=STATE_CRITICAL;
	else if(requested_responses<requested_servers)
		result=STATE_WARNING;
	else if(request_specific_address==TRUE && received_requested_address==FALSE)
		result=STATE_WARNING;


	printf("DHCP %s: ",(result==STATE_OK)?"ok":"problem");

	/* we didn't receive any DHCPOFFERs */
	if(dhcp_offer_list==NULL){
		printf("No DHCPOFFERs were received.\n");
		return result;
	        }

	printf("Received %d DHCPOFFER(s)",valid_responses);

	if(requested_servers>0)
		printf(", %s%d of %d requested servers responded",((requested_responses<requested_servers) && requested_responses>0)?"only ":"",requested_responses,requested_servers);

	if(request_specific_address==TRUE)
		printf(", requested address (%s) was %soffered",inet_ntoa(requested_address),(received_requested_address==TRUE)?"":"not ");

	printf(", max lease time = ");
	if(max_lease_time==DHCP_INFINITE_TIME)
		printf("Infinity");
	else
		printf("%lu sec",(unsigned long)max_lease_time);

	printf(".\n");

	return result;
        }






/* print usage help */
void print_help(void){

	/*print_revision(progname,"$Revision$");*/

	printf("Copyright (c) 2001-2004 Ethan Galstad (nagios@nagios.org)\n\n");
	printf("This plugin tests the availability of DHCP servers on a network.\n\n");

	print_usage();

	printf
		("\nOptions:\n"
		 " -s, --serverip=IPADDRESS\n"
		 "   IP address of DHCP server that we must hear from\n"
		 " -r, --requestedip=IPADDRESS\n"
		 "   IP address that should be offered by at least one DHCP server\n"
		 " -t, --timeout=INTEGER\n"
		 "   Seconds to wait for DHCPOFFER before timeout occurs\n"
		 " -i, --interface=STRING\n"
		 "   Interface to to use for listening (i.e. eth0)\n"
		 " -v, --verbose\n"
		 "   Print extra information (command-line use only)\n"
		 " -h, --help\n"
		 "   Print detailed help screen\n"
		 " -V, --version\n"
		 "   Print version information\n\n"
		 );

	/*support();*/

	return;
        }


/* prints usage information */
void print_usage(void){

	printf("Usage: %s [-s serverip] [-r requestedip] [-t timeout] [-i interface]\n",progname);
	printf("       %s --help\n",progname);
	printf("       %s --version\n",progname);

	return;
        }




/* process command-line arguments */
int process_arguments(int argc, char **argv){
	int c;

	if(argc<1)
		return ERROR;

	c=0;
	while((c+=(call_getopt(argc-c,&argv[c])))<argc){

		/*
		if(is_option(argv[c]))
			continue;
		*/
		}

	return validate_arguments();
        }



int call_getopt(int argc, char **argv){
	int c=0;
	int i=0;
	struct in_addr ipaddress;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] =
	{ 
		{"serverip",       required_argument,0,'s'},
		{"requestedip",    required_argument,0,'r'},
		{"timeout",        required_argument,0,'t'},
		{"interface",      required_argument,0,'i'},
		{"verbose",        no_argument,      0,'v'},
		{"version",        no_argument,      0,'V'},
		{"help",           no_argument,      0,'h'},
		{0,0,0,0}
	};
#endif

	while(1){
#ifdef HAVE_GETOPT_H
		c=getopt_long(argc,argv,"+hVvt:s:r:t:i:",long_options,&option_index);
#else
		c=getopt(argc,argv,"+?hVvt:s:r:t:i:");
#endif

		i++;

		if(c==-1||c==EOF||c==1)
			break;

		switch(c){
		case 'w':
		case 'r':
		case 't':
		case 'i':
			i++;
			break;
		default:
			break;
		        }

		switch(c){

		case 's': /* DHCP server address */
			if(inet_aton(optarg,&ipaddress))
				add_requested_server(ipaddress);
			/*
			else
				usage("Invalid server IP address\n");
			*/
			break;

		case 'r': /* address we are requested from DHCP servers */
			if(inet_aton(optarg,&ipaddress)){
				requested_address=ipaddress;
				request_specific_address=TRUE;
			        }
			/*
			else
				usage("Invalid requested IP address\n");
			*/
			break;

		case 't': /* timeout */

			/*
			if(is_intnonneg(optarg))
			*/
			if(atoi(optarg)>0)
				dhcpoffer_timeout=atoi(optarg);
			/*
			else
				usage("Time interval must be a nonnegative integer\n");
			*/
			break;

		case 'i': /* interface name */

			strncpy(network_interface_name,optarg,sizeof(network_interface_name)-1);
			network_interface_name[sizeof(network_interface_name)-1]='\x0';

			break;

		case 'V': /* version */

			/*print_revision(progname,"$Revision$");*/
			exit(STATE_OK);

		case 'h': /* help */

			print_help();
			exit(STATE_OK);

		case '?': /* help */

			/*usage("Invalid argument\n");*/
			break;

		default:
			break;
		        }
	        }

	return i;
        }



int validate_arguments(void){

	return OK;
        }

