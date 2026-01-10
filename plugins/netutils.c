/*****************************************************************************
 *
 * Monitoring Plugins network utilities
 *
 * License: GPL
 * Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
 * Copyright (c) 2003-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains commons functions used in many of the plugins.
 *
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *****************************************************************************/

#include "common.h"
#include "output.h"
#include "states.h"
#include <sys/types.h>
#include "netutils.h"

unsigned int socket_timeout = DEFAULT_SOCKET_TIMEOUT;
mp_state_enum socket_timeout_state = STATE_CRITICAL;
mp_state_enum econn_refuse_state = STATE_CRITICAL;
bool was_refused = false;

int address_family = AF_UNSPEC;

/* handles socket timeouts */
void socket_timeout_alarm_handler(int sig) {
	mp_subcheck timeout_sc = mp_subcheck_init();
	timeout_sc = mp_set_subcheck_state(timeout_sc, socket_timeout_state);

	if (sig == SIGALRM) {
		xasprintf(&timeout_sc.output, _("Socket timeout after %d seconds\n"), socket_timeout);
	} else {
		xasprintf(&timeout_sc.output, _("Abnormal timeout after %d seconds\n"), socket_timeout);
	}

	mp_check overall = mp_check_init();
	mp_add_subcheck_to_check(&overall, timeout_sc);

	mp_exit(overall);
}

/* connects to a host on a specified tcp port, sends a string, and gets a
	 response. loops on select-recv until timeout or eof to get all of a
	 multi-packet answer */
mp_state_enum process_tcp_request2(const char *server_address, const int server_port,
								   const char *send_buffer, char *recv_buffer,
								   const int recv_size) {

	int socket;

	mp_state_enum connect_result =
		np_net_connect(server_address, server_port, &socket, IPPROTO_TCP);
	if (connect_result != STATE_OK) {
		return STATE_CRITICAL;
	}

	mp_state_enum result;
	ssize_t send_result = send(socket, send_buffer, strlen(send_buffer), 0);
	if (send_result < 0 || (size_t)send_result != strlen(send_buffer)) {
		// printf("%s\n", _("Send failed"));
		result = STATE_WARNING;
	}

	fd_set readfds;
	ssize_t recv_length = 0;
	while (true) {
		/* wait up to the number of seconds for socket timeout
		   minus one for data from the host */
		struct timeval timeout = {
			.tv_sec = socket_timeout - 1,
			.tv_usec = 0,
		};
		FD_ZERO(&readfds);
		FD_SET(socket, &readfds);
		select(socket + 1, &readfds, NULL, NULL, &timeout);

		/* make sure some data has arrived */
		if (!FD_ISSET(socket, &readfds)) { /* it hasn't */
			if (!recv_length) {
				strcpy(recv_buffer, "");
				// printf("%s\n", _("No data was received from host!"));
				result = STATE_WARNING;
			} else { /* this one failed, but previous ones worked */
				recv_buffer[recv_length] = 0;
			}
			break;
		} /* it has */

		ssize_t recv_result =
			recv(socket, recv_buffer + recv_length, (size_t)(recv_size - recv_length - 1), 0);
		if (recv_result == -1) {
			/* recv failed, bail out */
			strcpy(recv_buffer + recv_length, "");
			result = STATE_WARNING;
			break;
		}

		if (recv_result == 0) {
			/* end of file ? */
			recv_buffer[recv_length] = 0;
			break;
		}

		/* we got data! */
		recv_length += recv_result;
		if (recv_length >= recv_size - 1) {
			/* buffer full, we're done */
			recv_buffer[recv_size - 1] = 0;
			break;
		}
		/* end if(!FD_ISSET(sd,&readfds)) */
	}

	close(socket);
	return result;
}

/* connects to a host on a specified port, sends a string, and gets a
   response */
mp_state_enum process_request(const char *server_address, const int server_port, const int proto,
							  const char *send_buffer, char *recv_buffer, const int recv_size) {

	mp_state_enum result = STATE_OK;
	int socket;
	result = np_net_connect(server_address, server_port, &socket, proto);
	if (result != STATE_OK) {
		return STATE_CRITICAL;
	}

	result = send_request(socket, proto, send_buffer, recv_buffer, recv_size);

	close(socket);

	return result;
}

/* opens a tcp or udp connection to a remote host or local socket */
mp_state_enum np_net_connect(const char *host_name, int port, int *socketDescriptor,
							 const int proto) {
	/* send back STATE_UNKOWN if there's an error
	   send back STATE_OK if we connect
	   send back STATE_CRITICAL if we can't connect.
	   Let upstream figure out what to send to the user. */
	bool is_socket = (host_name[0] == '/');
	int socktype = (proto == IPPROTO_UDP) ? SOCK_DGRAM : SOCK_STREAM;

	struct addrinfo hints = {};
	struct addrinfo *res = NULL;
	int result;
	/* as long as it doesn't start with a '/', it's assumed a host or ip */
	if (!is_socket) {
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = address_family;
		hints.ai_protocol = proto;
		hints.ai_socktype = socktype;

		size_t len = strlen(host_name);
		/* check for an [IPv6] address (and strip the brackets) */
		if (len >= 2 && host_name[0] == '[' && host_name[len - 1] == ']') {
			host_name++;
			len -= 2;
		}

		char host[MAX_HOST_ADDRESS_LENGTH];

		if (len >= sizeof(host)) {
			return STATE_UNKNOWN;
		}

		memcpy(host, host_name, len);
		host[len] = '\0';

		char port_str[6];
		snprintf(port_str, sizeof(port_str), "%d", port);
		int getaddrinfo_err = getaddrinfo(host, port_str, &hints, &res);

		if (getaddrinfo_err != 0) {
			// printf("%s\n", gai_strerror(result));
			return STATE_UNKNOWN;
		}

		struct addrinfo *addressPointer = res;
		while (addressPointer) {
			/* attempt to create a socket */
			*socketDescriptor =
				socket(addressPointer->ai_family, socktype, addressPointer->ai_protocol);

			if (*socketDescriptor < 0) {
				// printf("%s\n", _("Socket creation failed"));
				freeaddrinfo(addressPointer);
				return STATE_UNKNOWN;
			}

			/* attempt to open a connection */
			result =
				connect(*socketDescriptor, addressPointer->ai_addr, addressPointer->ai_addrlen);

			if (result == 0) {
				was_refused = false;
				break;
			}

			if (result < 0) {
				switch (errno) {
				case ECONNREFUSED:
					was_refused = true;
					break;
				}
			}

			close(*socketDescriptor);
			addressPointer = addressPointer->ai_next;
		}

		freeaddrinfo(res);

	} else {
		/* else the hostname is interpreted as a path to a unix socket */
		if (strlen(host_name) >= UNIX_PATH_MAX) {
			die(STATE_UNKNOWN, _("Supplied path too long unix domain socket"));
		}

		struct sockaddr_un su = {};
		su.sun_family = AF_UNIX;
		strncpy(su.sun_path, host_name, UNIX_PATH_MAX);
		*socketDescriptor = socket(PF_UNIX, SOCK_STREAM, 0);

		if (*socketDescriptor < 0) {
			die(STATE_UNKNOWN, _("Socket creation failed"));
		}

		result = connect(*socketDescriptor, (struct sockaddr *)&su, sizeof(su));
		if (result < 0 && errno == ECONNREFUSED) {
			was_refused = true;
		}
	}

	if (result == 0) {
		return STATE_OK;
	}

	if (was_refused) {
		switch (econn_refuse_state) { /* a user-defined expected outcome */
		case STATE_OK:
		case STATE_WARNING:  /* user wants WARN or OK on refusal, or... */
		case STATE_CRITICAL: /* user did not set econn_refuse_state, or wanted critical */
			if (is_socket) {
				// printf("connect to file socket %s: %s\n", host_name, strerror(errno));
			} else {
				// printf("connect to address %s and port %d: %s\n", host_name, port,
				// strerror(errno));
			}
			return STATE_CRITICAL;
			break;
		default: /* it's a logic error if we do not end up in STATE_(OK|WARNING|CRITICAL) */
			return STATE_UNKNOWN;
			break;
		}
	} else {
		if (is_socket) {
			// printf("connect to file socket %s: %s\n", host_name, strerror(errno));
		} else {
			// printf("connect to address %s and port %d: %s\n", host_name, port, strerror(errno));
		}
		return STATE_CRITICAL;
	}
}

mp_state_enum send_request(const int socket, const int proto, const char *send_buffer,
						   char *recv_buffer, const int recv_size) {
	mp_state_enum result = STATE_OK;

	ssize_t send_result = send(socket, send_buffer, strlen(send_buffer), 0);
	if (send_result < 0 || (size_t)send_result != strlen(send_buffer)) {
		// printf("%s\n", _("Send failed"));
		result = STATE_WARNING;
	}

	/* wait up to the number of seconds for socket timeout minus one
	   for data from the host */
	struct timeval timestamp = {
		.tv_sec = socket_timeout - 1,
		.tv_usec = 0,
	};
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(socket, &readfds);
	select(socket + 1, &readfds, NULL, NULL, &timestamp);

	/* make sure some data has arrived */
	if (!FD_ISSET(socket, &readfds)) {
		strcpy(recv_buffer, "");
		// printf("%s\n", _("No data was received from host!"));
		result = STATE_WARNING;
	} else {
		ssize_t recv_result = recv(socket, recv_buffer, (size_t)(recv_size - 1), 0);
		if (recv_result == -1) {
			strcpy(recv_buffer, "");
			if (proto != IPPROTO_TCP) {
				// printf("%s\n", _("Receive failed"));
			}
			result = STATE_WARNING;
		} else {
			recv_buffer[recv_result] = 0;
		}

		/* die returned string */
		recv_buffer[recv_size - 1] = 0;
	}

	return result;
}

bool is_host(const char *address) {
	if (is_addr(address) || is_hostname(address)) {
		return (true);
	}

	return (false);
}

void host_or_die(const char *str) {
	if (!str || (!is_addr(str) && !is_hostname(str))) {
		usage_va(_("Invalid hostname/address - %s"), str);
	}
}

bool is_addr(const char *address) {
	if (address_family == AF_INET && is_inet_addr(address)) {
		return true;
	}

	if (address_family == AF_INET6 && is_inet6_addr(address)) {
		return true;
	}

	return false;
}

bool dns_lookup(const char *node_string, struct sockaddr_storage *ss, const int family) {
	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = family;

	struct addrinfo *res;
	int retval = getaddrinfo(node_string, NULL, &hints, &res);
	if (retval != 0) {
		return false;
	}

	if (ss != NULL) {
		memcpy(ss, res->ai_addr, res->ai_addrlen);
	}

	freeaddrinfo(res);

	return true;
}
