/******************************************************************************************
 *
 * CHECK_HLTHERM.C
 *
 * Program: Hot Little Therm temperature plugin for Nagios
 * License: GPL
 * Copyright (c) 1999-2002 Ethan Galstad (nagios@nagios.org)
 *
 * Last Modified: 02-28-2002
 *
 * Command line: check_hltherm <probe> <wtemp> <ctemp> [-l label] [-s scale] [-lower]
 *
 * Description:
 *
 * This plugin checks the temperature of a given temperature probe on a
 * Hot Little Therm digital thermometer.  The plugin uses the 'therm' utility
 * that is included with the HLT software to check the probe temperature.  Both
 * the HLT digital thermometer and software are produced by Spiderplant. See
 * their website at http://www.spiderplant.com/hlt for more information.
 *
 *****************************************************************************************/

#include "config.h"
#include "common.h"
#include "popen.h"

#define DEFAULT_TIMEOUT         10	/* default timeout in seconds */

#define HLTHERM_COMMAND          "/usr/local/bin/therm"     /* this should be moved out to the configure script */


static void timeout_alarm_handler(int); /* author must provide */
int process_arguments(int, char **);

int timeout_interval=DEFAULT_TIMEOUT;

double wtemp=0.0L;
double ctemp=0.0L;

int check_lower_temps=FALSE;

char probe[MAX_INPUT_BUFFER]="";
char label[MAX_INPUT_BUFFER]="Temperature";
char scale[MAX_INPUT_BUFFER]="Degrees";

FILE *fp;


int main(int argc, char **argv){
	int result=STATE_OK;
	char command[MAX_INPUT_BUFFER];
	double temp=0.0L;
	char input_buffer[MAX_INPUT_BUFFER];
	int found=0;

	/* process command line arguments */
	result=process_arguments(argc,argv);

	/* display usage if there was a problem */
	if(result==ERROR){
		printf("Incorrect arguments supplied\n");
		printf("\n");
		printf("Hot Little Therm temperature plugin for Nagios\n");
		printf("Copyright (c) 1999-2002 Ethan Galstad (nagios@nagios.org)\n");
		printf("Last Modified: 02-28-2002\n");
		printf("License: GPL\n");
		printf("\n");
		printf("Usage: %s <probe> <wtemp> <ctemp> [-l label] [-s scale] [-lower]\n",argv[0]);
		printf("\n");
		printf("Options:\n");
		printf(" <wtemp>  = Temperature necessary to result in a WARNING state\n");
		printf(" <ctemp>  = Temperature necessary to result in a CRITICAL state\n");
		printf(" [label]  = A descriptive label for the probe.  Example: \"Outside Temp\"\n");
		printf(" [scale]  = A descriptive label for the temperature scale.  Example: \"Celsius\"\n");
		printf(" [-lower] = Evaluate temperatures with lower values being more critical\n");
		printf("\n");
		printf("This plugin checks the temperature of a given temperature probe on a\n");
		printf("Hot Little Therm digital thermometer.  The plugin uses the 'therm' utility\n");
		printf("included with the HLT software to check the probe temperature.  Both the\n");
		printf("HLT digital thermometer and software are produced by Spiderplant. See\n");
		printf("their website at http://www.spiderplant.com/hlt for more information.\n");
		printf("\n");
		return STATE_UNKNOWN;
	        }


	result=STATE_OK;

	/* Set signal handling and alarm */
	if(signal(SIGALRM,timeout_alarm_handler)==SIG_ERR){
		printf("Cannot catch SIGALRM");
		return STATE_UNKNOWN;
	        }

	/* handle timeouts gracefully */
	alarm(timeout_interval);

	/* create the command line we're going to use */
	snprintf(command,sizeof(command),"%s %s",HLTHERM_COMMAND,probe);
	command[sizeof(command)-1]='\x0';

	/* run the command to check the temperature on the probe */
	fp=spopen(command);
	if(fp==NULL){
		printf("Could not open pipe: %s\n",command);
		return STATE_UNKNOWN;
	        }

	if(fgets(input_buffer,MAX_INPUT_BUFFER-1,fp)){
		found=1;
		temp=(double)atof(input_buffer);
	        }

	/* close the pipe */
	spclose(fp);

	if(result==STATE_OK){

		if(found==0){
			printf("Therm problem - Could not read program output\n");
			result=STATE_CRITICAL;
		        }
		else{
			if(check_lower_temps==TRUE){
				if(temp<=ctemp)
					result=STATE_CRITICAL;
				else if(temp<=wtemp)
				       result=STATE_WARNING;
			        }
			else{
				if(temp>=ctemp)
					result=STATE_CRITICAL;
				else if(temp>=wtemp)
					result=STATE_WARNING;
			        }

			printf("Therm %s: %s = %2.1f %s\n",(result==STATE_OK)?"ok":"problem",label,temp,scale);
		        }
	        }

	return result;
        }


/* process command-line arguments */
int process_arguments(int argc, char **argv){
	int x;

	/* not enough options were supplied */
	if(argc<4)
		return ERROR;

	/* first option is always the probe name */
	strncpy(probe,argv[1],sizeof(probe)-1);
	probe[sizeof(probe)-1]='\x0';

	/* 2nd and 3rd options are temperature thresholds */
	wtemp=(double)atof(argv[2]);
	ctemp=(double)atof(argv[3]);

	/* process all remaining arguments */
	for(x=5;x<=argc;x++){

		/* we got the lower temperature option */
		if(!strcmp(argv[x-1],"-lower"))
			check_lower_temps=TRUE;

		/* we got the label */
		else if(!strcmp(argv[x-1],"-l")){
			if(x<argc){
				strncpy(label,argv[x],sizeof(label));
				label[sizeof(label)-1]='\x0';
				x++;
			        }
			else
				return ERROR;
		        }

		/* we got the scale */
		else if(!strcmp(argv[x-1],"-s")){
			if(x<argc){
				strncpy(scale,argv[x],sizeof(scale));
				scale[sizeof(scale)-1]='\x0';
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



/* handle timeouts gracefully... */
static void timeout_alarm_handler(int signo){

	if(signo==SIGALRM){
    
		kill(childpid[fileno(fp)],SIGKILL);
		printf("Therm problem - Check timed out after %d seconds\n",timeout_interval);
		exit(STATE_CRITICAL);
	        }
        }
