/******************************************************************************
*
* CHECK_INET_FPING.C
*
* Program: Fping plugin for Nagios
* License: GPL
* Copyright (c) 1999 Didi Rieder (adrieder@sbox.tu-graz.ac.at)
* $Id$
*
* Modifications:
*
* 08-24-1999 Didi Rieder (adrieder@sbox.tu-graz.ac.at)
*            Intial Coding
* 09-11-1999 Karl DeBisschop (kdebiss@alum.mit.edu)
*            Change to spopen
*            Fix so that state unknown is returned by default
*            (formerly would give state ok if no fping specified)
*            Add server_name to output
*            Reformat to 80-character standard screen
* 11-18-1999 Karl DeBisschop (kdebiss@alum.mit.edu)
*            set STATE_WARNING of stderr written or nonzero status returned
* 09-29-2000 Matthew Grant (matthewg@plain.co.nz)
*            changes for monitoring multiple hosts for checking Internet
*            reachibility
*
* Description:
*
* This plugin will use the /bin/fping command (from nagios) to ping
* the specified host for a fast check if the host is alive. Note that
* it is necessary to set the suid flag on fping.
******************************************************************************/

#include "config.h"
#include "common.h"
#include "popen.h"
#include "utils.h"

#define PROGNAME "check_fping"
#define PACKET_COUNT 15
#define PACKET_SIZE 56
#define CRITICAL_COUNT 2
#define WARNING_COUNT 1
#define UNKNOWN_PACKET_LOSS 200 /* 200% */
#define UNKNOWN_TRIP_TIME -1.0 /* -1 seconds */

#define STRSZ 100

int textscan(char *buf);
int process_arguments(int, char **);
int get_threshold (char *arg, char *rv[2]);
void print_usage(void);
void print_help(void);

char *server_names=NULL;
char *name="INTERNET";
int cthresh=CRITICAL_COUNT;
int wthresh=WARNING_COUNT;
int nnames=0;
int tpl=UNKNOWN_PACKET_LOSS;
double trta=UNKNOWN_TRIP_TIME;
int packet_size=PACKET_SIZE;
int packet_count=PACKET_COUNT;
int verbose=FALSE;
int fail = 0;
int not_found = 0;
int rta_fail = 0;
int pl_fail = 0;
int unreachable = 0;

int main(int argc, char **argv){
	int result;
	int status=STATE_UNKNOWN;
	char *servers=NULL;
	char *command_line=NULL;
	char *input_buffer=NULL;
	char *pl_buffer=NULL; 
	char *rta_buffer=NULL; 
	input_buffer=malloc(MAX_INPUT_BUFFER);
	rta_buffer = malloc(80);
	pl_buffer = malloc(80);
	memset(rta_buffer, 0, 80);
	memset(pl_buffer, 0, 80);
	
	if(process_arguments(argc,argv)==ERROR)
		usage("Could not parse arguments\n");

	servers=strscpy(servers,server_names);

	/* compose the command */
	command_line=ssprintf
		(command_line,"%s -b %d -c %d %s",
		 PATH_TO_FPING,
		 packet_size,
		 packet_count,
		 servers);

	if (verbose) printf("%s\n",command_line);

	/* run the command */
	child_process=spopen(command_line);
	if(child_process==NULL){
		printf("Unable to open pipe: %s\n",command_line);
		return STATE_UNKNOWN;
	}

	child_stderr=fdopen(child_stderr_array[fileno(child_process)],"r");
	if(child_stderr==NULL){
		printf("Could not open stderr for %s\n",command_line);
	}

	while (fgets(input_buffer,MAX_INPUT_BUFFER-1,child_process)) {
		if (verbose) printf("%s",input_buffer);
		result = textscan(input_buffer);
		status = max(status,result);
	}

	while(fgets(input_buffer,MAX_INPUT_BUFFER-1,child_stderr)) {
		if (verbose) printf("%s",input_buffer);
		result = textscan(input_buffer);
		status = max(status,result);
	}
	
	(void)fclose(child_stderr);

	/* close the pipe */
	if(spclose(child_process))
		status=max(status,STATE_WARNING);

	/* Analyse fail count and produce results */
	if (fail >= wthresh) {
		status = max(status, STATE_WARNING);
	}

	if (fail >= cthresh) {
		status = max(status, STATE_CRITICAL);
	}
	
	if( tpl != UNKNOWN_PACKET_LOSS ) {
		snprintf(pl_buffer, 80, ", %d PL", pl_fail);
	}

	if( trta != UNKNOWN_TRIP_TIME ) {
		snprintf(rta_buffer, 80, ", %d RTA", rta_fail);

	}
	
	printf("FPING %s - %s, %d of %d fail, %d NF, %d UR%s%s\n",
			state_text(status),
			(name != NULL ? name : server_names),
			fail,
			nnames,
			not_found,
			unreachable,
			pl_buffer,
			rta_buffer);
	
	return status;
}



/* analyse fping output - each event resulting in an increment of fail
 * must be mutually exclusive. packet loss and round trip time analysed 
 * together, both at once just results in one increment of fail
 */
int textscan(char *buf)
{
	char *rtastr=NULL;
	char *losstr=NULL;
	double loss;
	double rta;
	int status=STATE_OK;
	
	if (strstr(buf,"not found")) {
		fail++;
		not_found++;
	} else if(strstr(buf,"xmt/rcv/%loss") 
		&& strstr(buf,"min/avg/max")) {
		losstr = strstr(buf,"=");
		losstr = 1+strstr(losstr,"/");
		losstr = 1+strstr(losstr,"/");
		rtastr = strstr(buf,"min/avg/max");
		rtastr = strstr(rtastr,"=");
		rtastr = 1+index(rtastr,'/');
		loss = strtod(losstr,NULL);
		rta = strtod(rtastr,NULL);
		/* Increment fail counter
		 */
		if (tpl!=UNKNOWN_PACKET_LOSS && loss>tpl) {
			fail++;
		}
		else if (trta!=UNKNOWN_TRIP_TIME && rta>trta) {
			fail++;
		}
		else if (loss >= 100) {
			fail++;
		}
		/* Increment other counters 
		 */
		if (trta!=UNKNOWN_TRIP_TIME && rta>trta) 
			rta_fail++;
		if (tpl!=UNKNOWN_PACKET_LOSS && loss>tpl) 
			pl_fail++;
		if (loss >= 100) 
			unreachable++;
	} else if(strstr(buf,"xmt/rcv/%loss") ) {
		losstr = strstr(buf,"=");
		losstr = 1+strstr(losstr,"/");
		losstr = 1+strstr(losstr,"/");
		loss = strtod(losstr,NULL);
		/* Increment fail counter
		 */
		if (tpl!=UNKNOWN_PACKET_LOSS && loss>tpl) {
			fail++;
		}
		else if (loss >= 100) {
			fail++;
		}
		/* Increment other counters 
		 */
		if (tpl!=UNKNOWN_PACKET_LOSS && loss>tpl)
			pl_fail++;
		if (loss >= 100) 
			unreachable++;
	}
	
	return status;
}




/* process command-line arguments */
int process_arguments(int argc, char **argv)
{
	int c;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] =
	{ 
		{"hostname"           ,required_argument,0,'H'},
		{"critical"           ,required_argument,0,'c'},
		{"warning"            ,required_argument,0,'w'},
		{"bytes"              ,required_argument,0,'b'},
		{"number"             ,required_argument,0,'n'},
		{"pl-threshold"       ,required_argument,0,'p'},
		{"rta-threshold"      ,required_argument,0,'r'},
		{"name"               ,required_argument,0,'N'},
		{"verbose"            ,no_argument,      0,'v'},
		{"version"            ,no_argument,      0,'V'},
		{"help"               ,no_argument,      0,'h'},
		{0,0,0,0}
	};
#else

 	if(argc<2) return ERROR;
	
	if (!is_option(argv[1])){
		server_names=argv[1];
		argv[1]=argv[0];
		argv=&argv[1];
		argc--;
	}
#endif
	
	while (1){
#ifdef HAVE_GETOPT_H
		c = getopt_long(argc,argv,"+hVvH:c:w:b:n:N:p:r:",long_options,&option_index);
#else
		c = getopt(argc,argv,"+hVvH:c:w:b:n:N:p:r:");
#endif

		if (c==-1||c==EOF||c==1)
			break;

		switch (c)
			{
			case '?': /* print short usage statement if args not parsable */
				printf("%s: Unknown argument: %s\n\n",my_basename(argv[0]),optarg);
				print_usage();
				exit(STATE_UNKNOWN);
			case 'h': /* help */
				print_help();
				exit(STATE_OK);
			case 'V': /* version */
				print_revision(my_basename(argv[0]),"$Revision$");
				exit(STATE_OK);
			case 'v': /* verbose mode */
				verbose=TRUE;
				break;
			case 'H': /* hostname */
				if(is_host(optarg)==FALSE){
					printf("Invalid host name/address\n\n");
					print_usage();
					exit(STATE_UNKNOWN);
				}
				if (server_names != NULL) 
					server_names=strscat(server_names," ");
				server_names=strscat(server_names,optarg);
				nnames++;
				break;
			case 'c':
				if (is_intpos(optarg))
					cthresh = atoi(optarg);
				else
					usage("Critical threshold must be a positive integer"); 
				break;
			case 'w':
				if (is_intpos(optarg))
					wthresh = atoi(optarg);
				else
					usage("Warning threshold must be a postive integer");
				break;
			case 'r':
				if (is_intpos(optarg)) {
					trta=strtod(optarg,NULL);
				} 
				else {
					usage("RTA threshold must be a positive integer");
				}
				break;
			case 'p':
				if (is_intpos(optarg)) {
					tpl=strtod(optarg,NULL);
				} 
				else {
					usage("RTA threshold must be a positive integer");
				}
				break;
			case 'b': /* bytes per packet */
				if (is_intpos(optarg))
					packet_size=atoi(optarg);
				else
					usage("Packet size must be a positive integer");
				break;
			case 'N': /* Name of service */
				name = optarg;
				break;
			case 'n': /* number of packets */
				if (is_intpos(optarg))
					packet_count=atoi(optarg);
				else
					usage("Packet count must be a positive integer");
				break;
			}
	}

	while (optind < argc) {
		if(is_host(argv[optind])==FALSE) {
			printf("Invalid host name/address\n\n");
			print_usage();
			exit(STATE_UNKNOWN);
		}
		if (server_names != NULL)
			server_names=strscat(server_names," ");
		server_names=strscat(server_names,argv[optind]);
		nnames++;
		optind++;
	}
	
	if (server_names==NULL || nnames < 2)
		usage("At least 2 hostnames must be supplied\n\n");
	
	if (cthresh < 2) 
		usage("Critical threshold must be at least 2");
	if (cthresh > nnames)
		usage("Critical threshold cannot be greater than number of hosts tested");
	if (wthresh < 1)
		usage("Warning threshold must be at least 1");
	if (wthresh > nnames)
		usage("Warning threshold cannot be greater than number of hosts tested");
	if(wthresh >= cthresh)
		usage("Warning threshold must be less than the critical threshold"); 
	
	return OK;
}


void print_usage(void)
{
	printf("Usage: %s <host_address> <host_address> [<host_address>] ...\n",PROGNAME);
}





void print_help(void)
{

	print_revision(PROGNAME,"$Revision$");

	printf
		("Copyright (c) 1999 Didi Rieder (adrieder@sbox.tu-graz.ac.at)\n"
		 "          (c) 2000 Matthew Grant (matthewg@plain.co.nz)\n"
		 "This plugin will use the /bin/fping command (from saint) to ping the\n"
		 "specified hosts for a fast check to see if the Internet is still \n"
		 "reachable, and the results of the testing aggregated. Note that it\n"
		 "is necessary to set the suid flag on fping.\n\n");

	print_usage();

  printf
		("\nOptions:\n"
		 "-b, --bytes=INTEGER\n"
		 "   Size of ICMP packet (default: %d)\n"
		 "-c, --critical=INTEGER (default: %d)\n"
		 "   critical threshold failure count\n"
		 "-n, --number=INTEGER\n"
		 "   Number of ICMP packets to send (default: %d)\n"
		 "-H, --hostname=HOST\n"
		 "   Name or IP Address of host to ping (IP Address bypasses name lookup,\n"
		 "   reducing system load)\n"
		 "-h, --help\n"
		 "   Print this help screen\n"
		 "-N, --name\n"
		 "   Service name to print in results, defaults to INTERNET\n"
		 "-p, --pl-threshold\n"
		 "   Packet loss threshold - specify to turn on packet loss testing\n"
		 "-r, --rta-threshold\n"
		 "   Round trip average threshold - specify to turn on RTA testing\n"
		 "-V, --version\n"
		 "   Print version information\n"
		 "-v, --verbose\n"
		 "   Show details for command-line debugging (do not use with nagios server)\n"
		 "-w, --warning=INTEGER (default: %d)\n"
		 "   warning threshold failure count\n",
		 PACKET_SIZE, CRITICAL_COUNT, PACKET_COUNT, WARNING_COUNT);
}
