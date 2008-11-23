/******************************************************************************
 *
 * CHECK_UPTIME.C
 *
 * Program: Uptime plugin for Nagios
 * License: GPL
 * Copyright (c) 2000 Teresa Ramanan (teresa@redowl.org)
 *
 * Based on CHECK_LOAD.C
 * Copyright (c) 1999 Felipe Gustavo de Almeida <galmeida@linux.ime.usp.br>
 *
 * Last Modified: $Date: 2002-02-28 21:42:56 -0500 (Thu, 28 Feb 2002) $
 *
 * Command line: CHECK_UPTIME <host_address>
 * 
 * Description:
 *
 * This plugin parses the output from "uptime", tokenizing it with ',' as the
 * delimiter. Returning only the number of days and/or the hours and minutes
 * a machine has been up and running.
 *
 *****************************************************************************/

#include "config.h"
#include "common.h"
#include "utils.h"
#include "popen.h"

int main(int argc, char **argv)
{

  int result;
  char input_buffer[MAX_INPUT_BUFFER];
  int ct;
  int i;
  char *tok1 = NULL;
  char *daytok = NULL;
  char *hrmintok = NULL;
  char *runstr = NULL;
  char tempp;
  char ch;
  char delim[] = ",";

  if(argc != 2){
    printf("Incorrect number of arguments supplied\n");
    printf("\n");
    print_revision(argv[0],"$Revision: 6 $");
    printf("Copyright (c) 2000 Teresa Ramanan (tlr@redowl.org)\n");
    printf("\n");
    printf("Usage: %s <host_address>\n",argv[0]);
    printf("\n");
    return STATE_UNKNOWN;
  }

  child_process = spopen(PATH_TO_UPTIME);
  if(child_process==NULL){
      printf("Error opening %s\n",PATH_TO_UPTIME);
      return STATE_UNKNOWN;
  }
  child_stderr=fdopen(child_stderr_array[fileno(child_process)],"r");
  if(child_stderr==NULL){
    printf("Could not open stderr for %s\n",PATH_TO_UPTIME);
  }
  fgets(input_buffer,MAX_INPUT_BUFFER-1,child_process);
  i = 0;
  ct = 0;

  /* Let's mark the end of this string for parsing purposes */
  input_buffer[strlen(input_buffer)-1]='\0';

  tempp = input_buffer[0];
  while(ch != '\0'){
    ch = (&tempp)[i];
    if (ch == ',') { ct++; }
    i++;
  }
  runstr = input_buffer;
  tok1 = strsep(&runstr, delim);
  if (ct > 4) {
    hrmintok = strsep(&runstr, delim);
    hrmintok++;
    daytok = strstr(tok1,"up");
  }
  else {
    hrmintok = strstr(tok1, "up");
  }

  result = spclose(child_process);
  if(result){
    printf("Error code %d returned in %s\n",result,PATH_TO_UPTIME);
    return STATE_UNKNOWN;
  }
  if (hrmintok == NULL) {
    printf("Problem - unexpected data returned\n");
    return STATE_UNKNOWN;
  }
  printf("%s%s%s\n",(daytok == NULL)?"":daytok,(daytok == NULL)?"":",",hrmintok);
  return STATE_OK;
}
