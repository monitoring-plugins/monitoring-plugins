/*****************************************************************************
 *
 * CHECK_TIMEOUT.C
 *
 * Program: Plugin timeout tester for Nagios
 * License: GPL
 * Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
 *
 * Last Modified: 01-10-2000
 *
 * Command line: CHECK_TIMEOUT <something..>
 *
 * Description:
 * This 'plugin' - if you want to call it that - doesn't do anything.  It
 * just stays in a loop forever and never exits, and is therefore useful for
 * testing service and host check timeouts in Nagios.  You must supply at
 * least one argument on the command line in order to activate the loop.
 *
 ****************************************************************************/

#include <stdio.h>
#include <unistd.h>


int main(int argc, char **argv){

	if(argc==1){
		printf("Incorrect arguments supplied\n");
		printf("\n");
		printf("Plugin timeout tester for Nagios\n");
		printf("Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)\n");
		printf("Last Modified: 01-10-2000\n");
		printf("License: GPL\n");
		printf("\n");
		printf("Usage: %s <something>\n",argv[0]);
		printf("\n");
		printf("Options:\n");
		printf(" <something> = Anything at all...\n");
		printf("\n");
		printf("Notes:\n");
		printf("This 'plugin' doesn't do anything.  It is designed to never exit and therefore\n");
		printf("provides an easy way of testing service and host check timeouts in Nagios.\n");
		printf("\n");
		return 0;
                }

	/* let's never leave here, okay? */
	while(1)
		sleep(1);

	return 0;
        }



