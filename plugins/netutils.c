/****************************************************************************
*
* Nagios plugins network utilities
*
* License: GPL
* Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
*
* Last Modified: $Date$
*
* Description:
*
* This file contains commons functions used in many of the plugins.
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
****************************************************************************/

#include "netutils.h"

int socket_timeout = DEFAULT_SOCKET_TIMEOUT; 

/* handles socket timeouts */
void
socket_timeout_alarm_handler (int sig)
{

	printf ("CRITICAL - Socket timeout after %d seconds\n", socket_timeout);

	exit (STATE_CRITICAL);
}


/* connects to a host on a specified TCP port, sends a string,
   and gets a response */
int
process_tcp_request (char *server_address, int server_port,
	char *send_buffer, char *recv_buffer, int recv_size)
{
	int result;

	result = process_request (server_address, server_port,
			IPPROTO_TCP, send_buffer, recv_buffer, recv_size);

	return result;
}


/* connects to a host on a specified UDP port, sends a string, and gets a
    response */
int
process_udp_request (char *server_address, int server_port,
	char *send_buffer, char *recv_buffer, int recv_size)
{
	int result;

	result = process_request (server_address, server_port,
			IPPROTO_UDP, send_buffer, recv_buffer, recv_size);

	return result;
}



/* connects to a host on a specified tcp port, sends a string, and gets a 
	 response. loops on select-recv until timeout or eof to get all of a 
	 multi-packet answer */
int
process_tcp_request2 (char *server_address, int server_port,
	char *send_buffer, char *recv_buffer, int recv_size)
{

	int result;
	int send_result;
	int recv_result;
	int sd;
	struct timeval tv;
	fd_set readfds;
	int recv_length = 0;

	result = my_connect (server_address, server_port, &sd, IPPROTO_TCP);
	if (result != STATE_OK)
		return STATE_CRITICAL;

	send_result = send (sd, send_buffer, strlen (send_buffer), 0);
	if (send_result != strlen (send_buffer)) {
		printf ("send() failed\n");
		result = STATE_WARNING;
	}

	while (1) {
		/* wait up to the number of seconds for socket timeout
		   minus one for data from the host */
		tv.tv_sec = socket_timeout - 1;
		tv.tv_usec = 0;
		FD_ZERO (&readfds);
		FD_SET (sd, &readfds);
		select (sd + 1, &readfds, NULL, NULL, &tv);

		/* make sure some data has arrived */
		if (!FD_ISSET (sd, &readfds)) {	/* it hasn't */
			if (!recv_length) {
				strcpy (recv_buffer, "");
				printf ("No data was recieved from host!\n");
				result = STATE_WARNING;
			}
			else {										/* this one failed, but previous ones worked */
				recv_buffer[recv_length] = 0;
			}
			break;
		}
		else {											/* it has */
			recv_result =
				recv (sd, recv_buffer + recv_length, 
					recv_size - recv_length - 1, 0);
			if (recv_result == -1) {
				/* recv failed, bail out */
				strcpy (recv_buffer + recv_length, "");
				result = STATE_WARNING;
				break;
			}
			else if (recv_result == 0) {
				/* end of file ? */
				recv_buffer[recv_length] = 0;
				break;
			}
			else {										/* we got data! */
				recv_length += recv_result;
				if (recv_length >= recv_size - 1) {
					/* buffer full, we're done */
					recv_buffer[recv_size - 1] = 0;
					break;
				}
			}
		}
		/* end if(!FD_ISSET(sd,&readfds)) */
	}
	/* end while(1) */

	close (sd);
	return result;
}

/* connects to a host on a specified port, sends a string, and gets a 
   response */
int
process_request (char *server_address, int server_port, int proto,
	char *send_buffer, char *recv_buffer, int recv_size)
{
	int result;
	int send_result;
	int recv_result;
	int sd;
	struct timeval tv;
	fd_set readfds;

	result = STATE_OK;

	result = my_connect (server_address, server_port, &sd, proto);
	if (result != STATE_OK)
		return STATE_CRITICAL;

	send_result = send (sd, send_buffer, strlen (send_buffer), 0);
	if (send_result != strlen (send_buffer)) {
		printf ("send() failed\n");
		result = STATE_WARNING;
	}

	/* wait up to the number of seconds for socket timeout minus one 
	   for data from the host */
	tv.tv_sec = socket_timeout - 1;
	tv.tv_usec = 0;
	FD_ZERO (&readfds);
	FD_SET (sd, &readfds);
	select (sd + 1, &readfds, NULL, NULL, &tv);

	/* make sure some data has arrived */
	if (!FD_ISSET (sd, &readfds)) {
		strcpy (recv_buffer, "");
		printf ("No data was recieved from host!\n");
		result = STATE_WARNING;
	}

	else {
		recv_result = recv (sd, recv_buffer, recv_size - 1, 0);
		if (recv_result == -1) {
			strcpy (recv_buffer, "");
			if (proto != IPPROTO_TCP)
				printf ("recv() failed\n");
			result = STATE_WARNING;
		}
		else
			recv_buffer[recv_result] = 0;

		/* terminate returned string */
		recv_buffer[recv_size - 1] = 0;
	}

	close (sd);

	return result;
}


/* opens a connection to a remote host/tcp port */
int
my_tcp_connect (char *host_name, int port, int *sd)
{
	int result;

	result = my_connect (host_name, port, sd, IPPROTO_TCP);

	return result;
}


/* opens a connection to a remote host/udp port */
int
my_udp_connect (char *host_name, int port, int *sd)
{
	int result;

	result = my_connect (host_name, port, sd, IPPROTO_UDP);

	return result;
}


/* opens a tcp or udp connection to a remote host */
int
my_connect (char *host_name, int port, int *sd, int proto)
{
	struct addrinfo hints;
	struct addrinfo *res;
	struct addrinfo *ptrp;
	char port_str[6];
	int result;

	memset (&hints, 0, sizeof (hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_protocol = proto;

	snprintf (port_str, sizeof (port_str), "%d", port);
	result = getaddrinfo (host_name, port_str, &hints, &res);

	if (result != 0) {
		printf ("%s\n", gai_strerror (result));
		return STATE_UNKNOWN;
	}
	else {
		while (res) {
			/* attempt to create a socket */
			*sd = socket (res->ai_family, (proto == IPPROTO_UDP) ?
				SOCK_DGRAM : SOCK_STREAM, res->ai_protocol);

			if (*sd < 0) {
				printf ("Socket creation failed\n");
				freeaddrinfo (res);
				return STATE_UNKNOWN;
			}

			/* attempt to open a connection */
			result = connect (*sd, res->ai_addr, res->ai_addrlen);

			if (result == 0)
				break;

			close (*sd);
			res = res->ai_next;
		}
		freeaddrinfo (res);
	}

	if (result == 0)
		return STATE_OK;
	else {
		printf ("%s\n", strerror(errno));
		return STATE_CRITICAL;
	}
}

int
is_host (char *address)
{
        if (is_addr (address) || is_hostname (address))
                return (TRUE);

        return (FALSE);
}

int
is_addr (char *address)
{
#ifdef USE_IPV6
        if (is_inet_addr (address) || is_inet6_addr (address))
#else
        if (is_inet_addr (address))
#endif
                return (TRUE);

        return (FALSE);
}

int
resolve_host_or_addr (char *address, int family)
{
        struct addrinfo hints;
        struct addrinfo *res;
        int retval;

        memset (&hints, 0, sizeof (hints));
        hints.ai_family = family;
        retval = getaddrinfo (address, NULL, &hints, &res);

        if (retval != 0)
                return FALSE;
        else {
                freeaddrinfo (res);
                return TRUE;
        }
}

int
is_inet_addr (char *address)
{
        return resolve_host_or_addr (address, AF_INET);
}

#ifdef USE_IPV6
int
is_inet6_addr (char *address)
{
        return resolve_host_or_addr (address, AF_INET6);
}
#endif

int
is_hostname (char *s1)
{
#ifdef USE_IPV6
        return resolve_host_or_addr (s1, AF_UNSPEC);
#else
        return resolve_host_or_addr (s1, AF_INET);
#endif
}

