/******************************************************************************
*
* Nagios plugins net utilities include file
*
* License: GPL
* Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
*
* Last Modified: $Date$
*
* Description:
*
* This file contains common include files and function definitions
* used in many of the plugins.
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
* $Id$
*
******************************************************************************/

#include "config.h"
#include <netinet/in.h>
#include <arpa/inet.h>

RETSIGTYPE socket_timeout_alarm_handler (int) __attribute__((noreturn));

int process_tcp_request2 (const char *address, int port,
  const char *sbuffer, char *rbuffer, int rsize);
int process_tcp_request (const char *address, int port,
  const char *sbuffer, char *rbuffer, int rsize);
int process_udp_request (const char *address, int port,
  const char *sbuffer, char *rbuffer, int rsize);
int process_request (const char *address, int port, int proto,
  const char *sbuffer, char *rbuffer, int rsize);

int my_tcp_connect (const char *address, int port, int *sd);
int my_udp_connect (const char *address, int port, int *sd);

int send_tcp_request (int sd, const char *send_buffer, char *recv_buffer, int recv_size);
int send_udp_request (int sd, const char *send_buffer, char *recv_buffer, int recv_size);
int send_request (int sd, int proto, const char *send_buffer, char *recv_buffer, int recv_size);

int is_host (const char *);
int is_addr (const char *);
int resolve_host_or_addr (const char *, int);
int is_inet_addr (const char *);
#ifdef USE_IPV6
int is_inet6_addr (const char *);
#endif
int is_hostname (const char *);

extern unsigned int socket_timeout;
extern int econn_refuse_state;
extern int was_refused;
extern int address_family;
