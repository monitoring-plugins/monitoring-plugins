/******************************************************************************************
 *
 * CHECK_IPXPING.C
 *
 * Program: IPX ping plugin for Nagios
 * License: GPL
 * Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
 *
 * Last Modified: 09-24-1999
 *
 * Command line: CHECK_IPXPING <dest_network> <dest_address> <wrtt> <crtt>
 *
 * Description:
 *
 * This plugin will use the /usr/bin/ipxping command to ping the specified host using the
 * IPX protocol.  Note: Linux users must have IPX support compiled into the kernerl and
 * must have IPX configured correctly in order for this plugin to work.
 * If the round trip time value is above the <wrtt> level, a STATE_WARNING is
 * returned.  If it exceeds the <crtt> level, a STATE_CRITICAL is returned.
 *
 *
 *
 * IMPORTANT!!
 *
 * This plugin will only work with the ipxping command that has been ported to Linux.
 * The version for Sun takes different command line arguments and differs in its output.
 *
 *****************************************************************************************/

#include "config.h"
#include "common.h"
#include "netutils.h"
#include "popen.h"

/* this should be moved out to the configure script! */
#define IPXPING_COMMAND	"/tmp/ipxping/ipxping"

/* these should be moved to the common header file */
#define MAX_IPXNET_ADDRESS_LENGTH	12
#define MAX_IPXHOST_ADDRESS_LENGTH	18

int socket_timeout=DEFAULT_SOCKET_TIMEOUT;
char dest_network[MAX_IPXNET_ADDRESS_LENGTH];
char dest_address[MAX_IPXHOST_ADDRESS_LENGTH];
int wrtt;
int crtt;

int process_arguments(int,char **);

FILE * spopen(const char *);
int spclose(FILE *);

int main(int argc, char **argv){
	char command_line[MAX_INPUT_BUFFER];
	int rtt;
	int bytes_returned;
	int result=STATE_OK;
	FILE *fp;
	char input_buffer[MAX_INPUT_BUFFER];
	char *substr;
	int current_line;

	if(process_arguments(argc,argv)!=OK){
		printf("Incorrect arguments supplied\n");
		printf("\n");
		printf("IPX ping plugin for Nagios\n");
		printf("Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)\n");
		printf("Last Modified: 09-24-1999\n");
		printf("License: GPL\n");
		printf("\n");
		printf("Usage: %s <dest_network> <dest_address> <wrtt> <crtt> [-to to_sec]\n",argv[0]);
		printf("\n");
		printf("Options:\n");
		printf(" <dest_network> = IPX network that the remote host lies on.  (Hex Format - 00:00:00:00)\n");
		printf(" <dest_address> = MAC address of the remote host.  (Hex Format - 00:00:00:00:00:00)\n");
		printf(" <wrtt>         = Round trip time in milliseconds necessary to result in a WARNING state\n");
		printf(" <crtt>         = Round trip time in milliseconds necessary to result in a CRITICAL state\n");
		printf(" [to_sec]	= Seconds before we should timeout waiting for ping result.  Default = %d sec\n",DEFAULT_SOCKET_TIMEOUT);
		printf("\n");
		printf("Notes:\n");
		printf("This plugin will use the /usr/bin/ipxping command to ping the specified host using\n");
		printf("the IPX protocol.  IPX support must be compiled into the kernel and your host must\n");
		printf("be correctly configured to use IPX before this plugin will work! An RPM package of\n");
		printf("the ipxping binary can be found at...\n");
		printf("http://www.rpmfind.net/linux/RPM/contrib/libc5/i386/ipxping-0.0-2.i386.shtml\n");
		printf("\n");
		return STATE_UNKNOWN;
	        }
  
	/* create the command line to use... */
	sprintf(command_line,"%s %s %s",IPXPING_COMMAND,dest_network,dest_address);

	/* initialize alarm signal handling */
	signal(SIGALRM,socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm(socket_timeout);

	/* run the command */
	fp = spopen(command_line);
	if(fp==NULL){
		printf("Unable to open pipe: %s",command_line);
		return STATE_UNKNOWN;
	        }

	current_line=0;
	while(fgets(input_buffer,MAX_INPUT_BUFFER-1,fp)){

		current_line++;

		/* skip the first line of the output */
		if(current_line==1)
			continue;

		/* we didn't get the "is alive" */
		if(current_line==2 && !strstr(input_buffer,"is alive"))
			result=STATE_CRITICAL;

		/* get the round trip time */
		if(current_line==3){
			substr=strtok(input_buffer,":");
			substr=strtok(NULL,"\n");
			rtt=atoi(substr);
		        }

		/* get the number of bytes returned */
		if(current_line==4 && strstr(input_buffer,"bytes returned")){
			bytes_returned=atoi(input_buffer);
		        }
	        }

	/* close the pipe */
	spclose(fp);

	/* reset the alarm */
	alarm(0);

	if(current_line==1 || result==STATE_CRITICAL)
		printf("IPX Ping problem - No response from host\n");
	else{

		if(rtt>crtt)
			result=STATE_CRITICAL;
		else if(rtt>wrtt)
			result=STATE_WARNING;

		printf("IPX Ping %s - RTT = %d ms, %d bytes returned from %s %s\n",(result==STATE_OK)?"ok":"problem",rtt,bytes_returned,dest_network,dest_address);
	        }
	

	return result;
        }



/* process all arguments passed on the command line */
int process_arguments(int argc, char **argv){
	int x;

	/* no options were supplied */
	if(argc<5)
		return ERROR;

	/* get the destination network address */
	strncpy(dest_network,argv[1],sizeof(dest_network)-1);
	dest_network[sizeof(dest_network)-1]='\x0';

	/* get the destination host address */
	strncpy(dest_address,argv[2],sizeof(dest_address)-1);
	dest_address[sizeof(dest_address)-1]='\x0';

	/* get the round trip time variables */
	wrtt=atoi(argv[3]);
	crtt=atoi(argv[4]);

	/* process remaining arguments */
	for(x=6;x<=argc;x++){

		/* we got the timeout to use */
		if(!strcmp(argv[x-1],"-to")){
			if(x<argc){
				socket_timeout=atoi(argv[x]);
				if(socket_timeout<=0)
					return ERROR;
				x++;
			        }
			else
				return ERROR;
		        }

		/* else we got something else... */
		else
			return ERROR;
	        }

	return OK;
        }




