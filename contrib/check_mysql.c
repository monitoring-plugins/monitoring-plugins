/*****************************************************************
 *
 * Program: check_mysql.c
 * License: GPL
 *
 * Written by Tim Weippert
 *  (based on plugins by Ethan Galstad and MySQL example code)
 *
 * Command line: check_mysql <host> [user] [passwd]
 *      <host> can be the FQDN or the IP-Adress
 *	[user] and [passwd] are optional
 *	
 * Description:
 * 
 * This plugin attempts to connect to an MySQL Server
 * with the optional specified parameters user and passwd.
 * Normaly the host and a user HAVE to assigned.
 *
 * The plugin returns 
 * STATE_OK and the Version Number of the Server when all is fine
 * STATE_CRITICAL if the Connection can't be esablished
 * STATE_WARNING if the connection was established but the 
 * program can't get the Versoin Number
 * STATE_UNKNOWN if to many parameters are given
 *
 * Copyright (c) 1999 by Tim Weippert
 *
 * Changes:
 *   16.12.1999: Changed the return codes from numbers to statements
 *   		 
 *******************************************************************/

#include "config.h"
#include "common.h"
#include "mysql.h"

MYSQL mysql;

int main(int argc, char **argv)
{
  uint i = 0;
  char *host;
  char *user;
  char *passwd;
  
  char *status;
  char *version;
  
  if ( argc > 4 ) {
    printf("Too many Arguments supplied - %i .\n", argc);
    printf("Usage: %s <host> [user] [passwd]\n", argv[0]);
    return STATE_UNKNOWN;
  }

  (host = argv[1]) || (host = NULL);
  (user = argv[2]) || (user = NULL);
  (passwd = argv[3]) || (passwd = NULL);
  		  
  if (!(mysql_connect(&mysql,host,user,passwd))) {
    printf("Can't connect to Mysql on Host: %s\n", host);
    return STATE_CRITICAL;
  }
  
  if ( !(version = mysql_get_server_info(&mysql)) ) {
    printf("Connect OK, but can't get Serverinfo ... something wrong !\n");
    return STATE_WARNING;
  }
  
  printf("Mysql ok - Running Version: %s\n", version);
  
  mysql_close(&mysql);
  return STATE_OK;
}
													    

