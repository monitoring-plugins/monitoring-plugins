/******************************************************************************
*
* check_rbl.c
*
* Modified by Tim Bell <bhat@trinity.unimelb.edu.au> 2002-06-05
* based on:
*
* * check_dig.c
* *
* * Program: dig plugin for NetSaint
* * License: GPL
* * Copyright (c) 2000
* * 
* * $Id: check_rbl.c 970 2004-12-02 00:30:32Z opensides $
*
*****************************************************************************/

#include "config.h"
#include "common.h"
#include "utils.h"
#include "popen.h"
#include "string.h"

const char progname = "check_rbl";
const char *revision = "$Revision: 970 $";
//const char *copyright = "2000-2003";
//const char *email = "nagiosplug-devel@lists.sourceforge.net";

int process_arguments(int, char **);
int call_getopt(int, char **);
int validate_arguments(void);
int check_disk(int usp,int free_disk);
void print_help(void);
void print_usage(void);
char *reverse_ipaddr(char *ipaddr);

char *query_address=NULL;
char *query_address_rev=NULL;
char *dns_server=NULL;
char *rbl_name=NULL;
int verbose=FALSE;

int main(int argc, char **argv){
	char input_buffer[MAX_INPUT_BUFFER];
	char *command_line=NULL;
	char *output=NULL;
	int result=STATE_OK;

	/* Set signal handling and alarm */
	if (signal(SIGALRM,popen_timeout_alarm_handler)==SIG_ERR)
		usage("Cannot catch SIGALRM\n");

	if (process_arguments(argc,argv)!=OK)
		usage	(_("check_rbl: could not parse arguments\n"));

	/* reverse the octets in the IP address */
	query_address_rev = reverse_ipaddr(query_address);

	/* build the command to run */
	if (dns_server) {
	  command_line=ssprintf(command_line,"%s @%s %s.%s",
				PATH_TO_DIG,dns_server,
				query_address_rev, rbl_name);
	} else {
	  command_line=ssprintf(command_line,"%s %s.%s",
				PATH_TO_DIG,
				query_address_rev, rbl_name);
	}
	alarm(timeout_interval);
	time(&start_time);

	if (verbose)
		printf("%s\n",command_line);
	/* run the command */
	child_process=spopen(command_line);
	if (child_process==NULL) {
		printf("Could not open pipe: %s\n",command_line);
		return STATE_UNKNOWN;
	}

	child_stderr=fdopen(child_stderr_array[fileno(child_process)],"r");
	if(child_stderr==NULL)
		printf("Could not open stderr for %s\n",command_line);

	output=strscpy(output,"");

	while (fgets(input_buffer,MAX_INPUT_BUFFER-1,child_process)) {

		/* the server is responding, we just got the host name... */
		if (strstr(input_buffer,";; ANSWER SECTION:")) {

			/* get the host address */
			if (!fgets(input_buffer,MAX_INPUT_BUFFER-1,child_process))
				break;

			if (strpbrk(input_buffer,"\r\n"))
				input_buffer[strcspn(input_buffer,"\r\n")] = '\0';

			if (strstr(input_buffer,query_address_rev)==input_buffer) {
				output=strscpy(output,input_buffer);
				/* we found it, which means it's listed! */
				result=STATE_CRITICAL;
			} else {
				strcpy(output,"Server not RBL listed.");
				result=STATE_OK;
			}

			continue;
		}

	}
	
	/*
	if (result!=STATE_OK) {
		strcpy(output,"No ANSWER SECTION found");
	}
	*/

	while (fgets(input_buffer,MAX_INPUT_BUFFER-1,child_stderr)) {
		/* If we get anything on STDERR, at least set warning */
		result=error_set(result,STATE_WARNING);
		printf("%s",input_buffer);
		if (!strcmp(output,""))
			strcpy(output,1+index(input_buffer,':'));
	}

	(void)fclose(child_stderr);

	/* close the pipe */
	if (spclose(child_process)) {
		result=error_set(result,STATE_WARNING);
		if (!strcmp(output,""))
			strcpy(output,"nslookup returned an error status");
	}
	
	(void)time(&end_time);

	if (result==STATE_OK)
		printf("RBL check okay - not listed.\n");
	else if (result==STATE_WARNING)
		printf("RBL WARNING - %s\n",!strcmp(output,"")?" Probably a non-existent host/domain":output);
	else if (result==STATE_CRITICAL)
		printf("RBL CRITICAL - %s is listed on %s\n",query_address, rbl_name);
	else
		printf("DNS problem - %s\n",!strcmp(output,"")?" Probably a non-existent host/domain":output);

	return result;
}



/* reverse the ipaddr */
char *reverse_ipaddr(char *ipaddr)
{
  static char revip[MAX_HOST_ADDRESS_LENGTH];
  int a, b, c, d;

  if (strlen(ipaddr) >= MAX_HOST_ADDRESS_LENGTH ||
      sscanf(ipaddr, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
    usage("IP address invalid or too long");
  }
  sprintf(revip, "%d.%d.%d.%d", d, c, b, a);

  return revip;
}



/* process command-line arguments */
int process_arguments(int argc, char **argv)
{
  int c;

  if(argc<2)
    return ERROR;


  c=0;
  while((c+=(call_getopt(argc-c,&argv[c])))<argc){

		if (is_option(argv[c]))
			continue;

    if (query_address==NULL) {
			if (is_host(argv[c])) {
				query_address=argv[c];
			} else {
				usage("Invalid host name");
			}
		}
  }

  return validate_arguments();
}



int call_getopt(int argc, char **argv)
{
  int c,i=0;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] =
	{ 
		{"hostname",       required_argument,0,'H'},
		{"server",         required_argument,0,'s'},
		{"rblname",        required_argument,0,'r'},
		{"verbose",        no_argument,      0,'v'},
		{"version",        no_argument,      0,'V'},
		{"help",           no_argument,      0,'h'},
		{0,0,0,0}
	};
#endif

  while (1){
#ifdef HAVE_GETOPT_H
    c = getopt_long(argc,argv,"+hVvt:s:H:r:",long_options,&option_index);
#else
    c = getopt(argc,argv,"+?hVvt:s:H:r:");
#endif

    i++;

    if(c==-1||c==EOF||c==1)
      break;

    switch (c)
      {
      case 't':
      case 'l':
      case 'H':
				i++;
      }

    switch (c)
      {
      case 'H': /* hostname */
				if (is_host(optarg)) {
					query_address=optarg;
				} else {
					usage("Invalid host name (-H)\n");
				}
				break;
      case 's': /* server */
				if (is_host(optarg)) {
					dns_server=optarg;
				} else {
					usage("Invalid host name (-s)\n");
				}
				break;
      case 'r': /* rblname */
				rbl_name=optarg;
				break;
      case 'v': /* verbose */
				verbose=TRUE;
				break;
      case 't': /* timeout */
				if (is_intnonneg(optarg)) {
					timeout_interval=atoi(optarg);
				} else {
					usage("Time interval must be a nonnegative integer\n");
				}
				break;
      case 'V': /* version */
				print_revision(progname,"$Revision: 970 $");
				exit(STATE_OK);
      case 'h': /* help */
				print_help();
				exit(STATE_OK);
      case '?': /* help */
			printf (_("%s: Unknown argument: %s\n\n"), progname, optarg);
			print_usage ();
			exit (STATE_UNKNOWN);
			}
  }
  return i;
}



int validate_arguments(void)
{
  if (query_address == NULL || rbl_name == NULL)
    return ERROR;
  else
    return OK;
}



void print_help(void)
{
	print_revision(progname,"$Revision: 970 $");
	printf
		("Copyright (c) 2000 Karl DeBisschop\n\n"
		 "This plugin uses dig to test whether the specified host is on any RBL lists.\n\n");
	print_usage();
	printf
		("\nOptions:\n"
		 " -H, --hostname=IPADDRESS\n"
		 "   Check status of indicated host\n"
		 " -s, --server=STRING or IPADDRESS\n"
		 "   DNS server to use\n"
		 " -r, --rblname=STRING\n"
		 "   RBL domain name to use (e.g. relays.ordb.org)\n"
		 " -t, --timeout=INTEGER\n"
		 "   Seconds before connection attempt times out (default: %d)\n"
		 " -v, --verbose\n"
		 "   Print extra information (command-line use only)\n"
		 " -h, --help\n"
		 "   Print detailed help screen\n"
		 " -V, --version\n"
		 "   Print version information\n\n",
		 DEFAULT_SOCKET_TIMEOUT);
		support();
}



void print_usage(void)
{
	printf
		("Usage: %s -H hostip -r rblname [-s server] [-t timeout] [-v]\n"
		 "       %s --help\n"
		 "       %s --version\n",
		 progname, progname, progname);
}
