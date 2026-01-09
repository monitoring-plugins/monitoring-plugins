/*****************************************************************************
 *
 * Monitoring check_dhcp plugin
 *
 * License: GPL
 * Copyright (c) 2001-2004 Ethan Galstad (nagios@nagios.org)
 * Copyright (c) 2001-2025 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_dhcp plugin
 *
 * This plugin tests the availability of DHCP servers on a network.
 *
 * Unicast mode was originally implemented by Heiti of Boras Kommun with
 * general improvements as well as usability fixes and "forward"-porting by
 * Andreas Ericsson of OP5 AB.
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

const char *progname = "check_dhcp";
const char *copyright = "2001-2025";
const char *email = "devel@monitoring-plugins.org";

#include "../plugins/common.h"
#include "../plugins/utils.h"
#include "./check_dhcp.d/config.h"
#include "../lib/output.h"
#include "../lib/utils_base.h"

#include "states.h"
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>

#if HAVE_SYS_SOCKIO_H
#	include <sys/sockio.h>
#endif // HAVE_SYS_SOCKIO_H

#if defined(__linux__)

#	include <linux/if_ether.h>
#	include <features.h>

#elif defined(__bsd__)

#	include <netinet/if_ether.h>
#	include <sys/param.h>
#	include <sys/sysctl.h>
#	include <net/if_dl.h>

#elif defined(__sun__) || defined(__solaris__) || defined(__hpux__)

#	define INSAP  22
#	define OUTSAP 24

#	include <signal.h>
#	include <ctype.h>
#	include <sys/stropts.h>
#	include <sys/poll.h>
#	include <sys/dlpi.h>

#	define bcopy(source, destination, length) memcpy(destination, source, length)

#	define AREA_SZ 5000 /* buffer length in bytes */
static u_long ctl_area[AREA_SZ];
static u_long dat_area[AREA_SZ];
static struct strbuf ctl = {AREA_SZ, 0, (char *)ctl_area};
static struct strbuf dat = {AREA_SZ, 0, (char *)dat_area};

#	define GOT_CTRL 1
#	define GOT_DATA 2
#	define GOT_BOTH 3
#	define GOT_INTR 4
#	define GOT_ERR  128

static int get_msg(int);
static int check_ctrl(int);
static int put_ctrl(int, int, int);
static int put_both(int, int, int, int);
static int dl_open(const char *, int, int *);
static int dl_bind(int, int, u_char *);
static long mac_addr_dlpi(const char *, int, u_char *);

#endif // __sun__ || __solaris__ || __hpux

/**** Common definitions ****/

#define OK           0
#define ERROR        -1
#define MAC_ADDR_LEN 6

/**** DHCP definitions ****/

#define MAX_DHCP_CHADDR_LENGTH  16
#define MAX_DHCP_SNAME_LENGTH   64
#define MAX_DHCP_FILE_LENGTH    128
#define MAX_DHCP_OPTIONS_LENGTH 312

typedef struct dhcp_packet_struct {
	uint8_t op;            /* packet type */
	uint8_t htype;         /* type of hardware address for this machine (Ethernet, etc) */
	uint8_t hlen;          /* length of hardware address (of this machine) */
	uint8_t hops;          /* hops */
	uint32_t xid;          /* random transaction id number - chosen by this machine */
	uint16_t secs;         /* seconds used in timing */
	uint16_t flags;        /* flags */
	struct in_addr ciaddr; /* IP address of this machine (if we already have one) */
	struct in_addr yiaddr; /* IP address of this machine (offered by the DHCP server) */
	struct in_addr siaddr; /* IP address of next server */
	struct in_addr giaddr; /* IP address of DHCP relay */
	unsigned char chaddr[MAX_DHCP_CHADDR_LENGTH]; /* hardware address of this machine */
	char sname[MAX_DHCP_SNAME_LENGTH];            /* name of DHCP server */
	char file[MAX_DHCP_FILE_LENGTH];              /* boot file name (used for diskless booting?) */
	char options[MAX_DHCP_OPTIONS_LENGTH];        /* options */
} dhcp_packet;

typedef struct dhcp_offer_struct {
	struct in_addr server_address;  /* address of DHCP server that sent this offer */
	struct in_addr offered_address; /* the IP address that was offered to us */
	uint32_t lease_time;            /* lease time in seconds */
	uint32_t renewal_time;          /* renewal time in seconds */
	uint32_t rebinding_time;        /* rebinding time in seconds */
	bool desired;                   /* is this offer desired (necessary in exclusive mode) */
	struct dhcp_offer_struct *next;
} dhcp_offer;

#define BOOTREQUEST 1
#define BOOTREPLY   2

#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPDECLINE  4
#define DHCPACK      5
#define DHCPNACK     6
#define DHCPRELEASE  7

#define DHCP_OPTION_MESSAGE_TYPE      53
#define DHCP_OPTION_HOST_NAME         12
#define DHCP_OPTION_BROADCAST_ADDRESS 28
#define DHCP_OPTION_REQUESTED_ADDRESS 50
#define DHCP_OPTION_LEASE_TIME        51
#define DHCP_OPTION_SERVER_IDENTIFIER 54
#define DHCP_OPTION_RENEWAL_TIME      58
#define DHCP_OPTION_REBINDING_TIME    59
#define DHCP_OPTION_END               255

#define DHCP_INFINITE_TIME 0xFFFFFFFF

#define DHCP_BROADCAST_FLAG 32768

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

#define ETHERNET_HARDWARE_ADDRESS        1 /* used in htype field of dhcp packet */
#define ETHERNET_HARDWARE_ADDRESS_LENGTH 6 /* length of Ethernet hardware addresses */

static int verbose = 0;

typedef struct process_arguments_wrapper {
	int error;
	check_dhcp_config config;
} process_arguments_wrapper;

static process_arguments_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
void print_usage(void);
static void print_help(void);

static void resolve_host(const char * /*in*/, struct in_addr * /*out*/);
static unsigned char *mac_aton(const char * /*string*/);
static void print_hardware_address(const unsigned char * /*address*/);
static int get_hardware_address(int /*sock*/, char * /*interface_name*/,
								unsigned char *client_hardware_address);

typedef struct get_ip_address_wrapper {
	int error;
	struct in_addr my_ip;
} get_ip_address_wrapper;
static get_ip_address_wrapper get_ip_address(int /*sock*/, char * /*interface_name*/);

typedef struct send_dhcp_discover_wrapper {
	int error;
	uint32_t packet_xid;
} send_dhcp_discover_wrapper;
static send_dhcp_discover_wrapper
send_dhcp_discover(int socket, bool unicast, struct in_addr dhcp_ip,
				   struct in_addr requested_address, bool request_specific_address,
				   struct in_addr my_ip, unsigned char *client_hardware_address);
typedef struct get_dhcp_offer_wrapper {
	int error;
	int valid_responses;
	dhcp_offer *dhcp_offer_list;
} get_dhcp_offer_wrapper;
static get_dhcp_offer_wrapper get_dhcp_offer(int /*sock*/, int dhcpoffer_timeout,
											 uint32_t packet_xid, dhcp_offer *dhcp_offer_list,
											 const unsigned char *client_hardware_address);

static mp_subcheck get_results(bool exclusive, int requested_servers,
							   struct in_addr requested_address, bool request_specific_address,
							   requested_server *requested_server_list, int valid_responses,
							   dhcp_offer *dhcp_offer_list);

typedef struct add_dhcp_offer_wrapper {
	int error;
	dhcp_offer *dhcp_offer_list;
} add_dhcp_offer_wrapper;
static add_dhcp_offer_wrapper add_dhcp_offer(struct in_addr /*source*/,
											 dhcp_packet * /*offer_packet*/,
											 dhcp_offer *dhcp_offer_list);
static int free_dhcp_offer_list(dhcp_offer *dhcp_offer_list);
static int free_requested_server_list(requested_server *requested_server_list);

static int create_dhcp_socket(bool /*unicast*/, char *network_interface_name);
static int close_dhcp_socket(int /*sock*/);
static int send_dhcp_packet(void * /*buffer*/, int /*buffer_size*/, int /*sock*/,
							struct sockaddr_in * /*dest*/);
static int receive_dhcp_packet(void * /*buffer*/, int /*buffer_size*/, int /*sock*/,
							   int /*timeout*/, struct sockaddr_in * /*address*/);

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	process_arguments_wrapper tmp = process_arguments(argc, argv);

	if (tmp.error != OK) {
		usage4(_("Could not parse arguments"));
	}

	check_dhcp_config config = tmp.config;
	if (config.output_format_is_set) {
		mp_set_format(config.output_format);
	}

	/* create socket for DHCP communications */
	int dhcp_socket = create_dhcp_socket(config.unicast_mode, config.network_interface_name);

	/* get hardware address of client machine */
	unsigned char client_hardware_address[MAX_DHCP_CHADDR_LENGTH] = "";
	if (config.user_specified_mac != NULL) {
		memcpy(client_hardware_address, config.user_specified_mac, MAC_ADDR_LEN);
	} else {
		get_hardware_address(dhcp_socket, config.network_interface_name, client_hardware_address);
	}

	struct in_addr my_ip = {0};

	if (config.unicast_mode) { /* get IP address of client machine */
		get_ip_address_wrapper tmp_get_ip =
			get_ip_address(dhcp_socket, config.network_interface_name);
		if (tmp_get_ip.error == OK) {
			my_ip = tmp_get_ip.my_ip;
		} else {
			// TODO failed to get own IP
			die(STATE_UNKNOWN, "Failed to retrieve my own IP address in unicast mode");
		}
	}

	/* send DHCPDISCOVER packet */
	send_dhcp_discover_wrapper disco_res = send_dhcp_discover(
		dhcp_socket, config.unicast_mode, config.dhcp_ip, config.requested_address,
		config.request_specific_address, my_ip, client_hardware_address);

	if (disco_res.error != OK) {
		// DO something?
		die(STATE_UNKNOWN, "Failed to send DHCP discover");
	}

	/* wait for a DHCPOFFER packet */
	get_dhcp_offer_wrapper offer_res = get_dhcp_offer(
		dhcp_socket, config.dhcpoffer_timeout, disco_res.packet_xid, NULL, client_hardware_address);

	int valid_responses = 0;
	dhcp_offer *dhcp_offer_list = NULL;
	if (offer_res.error == OK) {
		valid_responses = offer_res.valid_responses;
		dhcp_offer_list = offer_res.dhcp_offer_list;
	} else {
		die(STATE_UNKNOWN, "Failed to get DHCP offers");
	}

	/* close socket we created */
	close_dhcp_socket(dhcp_socket);

	mp_check overall = mp_check_init();

	/* determine state/plugin output to return */
	mp_subcheck sc_res =
		get_results(config.exclusive_mode, config.num_of_requested_servers,
					config.requested_address, config.request_specific_address,
					config.requested_server_list, valid_responses, dhcp_offer_list);
	mp_add_subcheck_to_check(&overall, sc_res);
	/* free allocated memory */
	free_dhcp_offer_list(dhcp_offer_list);
	free_requested_server_list(config.requested_server_list);

	mp_exit(overall);
}

/* determines hardware address on client machine */
int get_hardware_address(int sock, char *interface_name, unsigned char *client_hardware_address) {

#if defined(__linux__)
	struct ifreq ifr;

	strncpy((char *)&ifr.ifr_name, interface_name, sizeof(ifr.ifr_name) - 1);
	ifr.ifr_name[sizeof(ifr.ifr_name) - 1] = '\0';

	/* try and grab hardware address of requested interface */
	if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
		printf(_("Error: Could not get hardware address of interface '%s'\n"), interface_name);
		exit(STATE_UNKNOWN);
	}

	memcpy(&client_hardware_address[0], &ifr.ifr_hwaddr.sa_data, MAC_ADDR_LEN);

#elif defined(__bsd__)
	/* King 2004	see ACKNOWLEDGEMENTS */

	size_t len;
	int mib[6];
	char *buf;
	unsigned char *ptr;
	struct if_msghdr *ifm;
	struct sockaddr_dl *sdl;

	mib[0] = CTL_NET;
	mib[1] = AF_ROUTE;
	mib[2] = 0;
	mib[3] = AF_LINK;
	mib[4] = NET_RT_IFLIST;

	if ((mib[5] = if_nametoindex(interface_name)) == 0) {
		printf(_("Error: if_nametoindex error - %s.\n"), strerror(errno));
		exit(STATE_UNKNOWN);
	}

	if (sysctl(mib, 6, NULL, &len, NULL, 0) < 0) {
		printf(_("Error: Couldn't get hardware address from %s. sysctl 1 error - %s.\n"),
			   interface_name, strerror(errno));
		exit(STATE_UNKNOWN);
	}

	if ((buf = malloc(len)) == NULL) {
		printf(_("Error: Couldn't get hardware address from interface %s. malloc error - %s.\n"),
			   interface_name, strerror(errno));
		exit(4);
	}

	if (sysctl(mib, 6, buf, &len, NULL, 0) < 0) {
		printf(_("Error: Couldn't get hardware address from %s. sysctl 2 error - %s.\n"),
			   interface_name, strerror(errno));
		exit(STATE_UNKNOWN);
	}

	ifm = (struct if_msghdr *)buf;
	sdl = (struct sockaddr_dl *)(ifm + 1);
	ptr = (unsigned char *)LLADDR(sdl);
	memcpy(&client_hardware_address[0], ptr, 6);
	/* King 2004 */

#elif defined(__sun__) || defined(__solaris__)

	/* Kompf 2000-2003	see ACKNOWLEDGEMENTS */
	long stat;
	char dev[20] = "/dev/";
	char *p;
	int unit;

	/* get last number from interfacename, eg lnc0, e1000g0*/
	int i;
	p = interface_name + strlen(interface_name) - 1;
	for (i = strlen(interface_name) - 1; i > 0; p--) {
		if (isalpha(*p)) {
			break;
		}
	}
	p++;
	if (p != interface_name) {
		unit = atoi(p);
		strncat(dev, interface_name, 6);
	} else {
		printf(_("Error: can't find unit number in interface_name (%s) - expecting TypeNumber eg "
				 "lnc0.\n"),
			   interface_name);
		exit(STATE_UNKNOWN);
	}
	stat = mac_addr_dlpi(dev, unit, client_hardware_address);
	if (stat != 0) {
		printf(
			_("Error: can't read MAC address from DLPI streams interface for device %s unit %d.\n"),
			dev, unit);
		exit(STATE_UNKNOWN);
	}

#elif defined(__hpux__)

	long stat;
	char dev[20] = "/dev/dlpi";
	int unit = 0;

	stat = mac_addr_dlpi(dev, unit, client_hardware_address);
	if (stat != 0) {
		printf(
			_("Error: can't read MAC address from DLPI streams interface for device %s unit %d.\n"),
			dev, unit);
		exit(STATE_UNKNOWN);
	}
	/* Kompf 2000-2003 */

#else
	printf(_("Error: can't get MAC address for this architecture.  Use the --mac option.\n"));
	exit(STATE_UNKNOWN);
#endif

	if (verbose) {
		print_hardware_address(client_hardware_address);
	}

	return OK;
}

/* determines IP address of the client interface */
get_ip_address_wrapper get_ip_address(int sock, char *interface_name) {
#if defined(SIOCGIFADDR)
	struct ifreq ifr;

	strncpy((char *)&ifr.ifr_name, interface_name, sizeof(ifr.ifr_name) - 1);
	ifr.ifr_name[sizeof(ifr.ifr_name) - 1] = '\0';

	if (ioctl(sock, SIOCGIFADDR, &ifr) < 0) {
		printf(_("Error: Cannot determine IP address of interface %s\n"), interface_name);
		exit(STATE_UNKNOWN);
	}

#else
	printf(_("Error: Cannot get interface IP address on this platform.\n"));
	exit(STATE_UNKNOWN);
#endif

	get_ip_address_wrapper result = {
		.error = OK,
		.my_ip = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr,
	};

	if (verbose) {
		printf(_("Pretending to be relay client %s\n"), inet_ntoa(result.my_ip));
	}

	return result;
}

/* sends a DHCPDISCOVER broadcast message in an attempt to find DHCP servers */
static send_dhcp_discover_wrapper send_dhcp_discover(int sock, bool unicast, struct in_addr dhcp_ip,
													 struct in_addr requested_address,
													 bool request_specific_address,
													 struct in_addr my_ip,
													 unsigned char *client_hardware_address) {
	dhcp_packet discover_packet = {0};
	/* boot request flag (backward compatible with BOOTP servers) */
	discover_packet.op = BOOTREQUEST;

	/* hardware address type */
	discover_packet.htype = ETHERNET_HARDWARE_ADDRESS;

	/* length of our hardware address */
	discover_packet.hlen = ETHERNET_HARDWARE_ADDRESS_LENGTH;

	send_dhcp_discover_wrapper result = {
		.error = OK,
	};
	/*
	 * transaction ID is supposed to be random.
	 */
	srand(time(NULL) ^ getpid());
	result.packet_xid = random();
	discover_packet.xid = htonl(result.packet_xid);

	/*discover_packet.secs=htons(65535);*/
	discover_packet.secs = 0xFF;

	/*
	 * server needs to know if it should broadcast or unicast its response:
	 * 0x8000L == 32768 == 1 << 15 == broadcast, 0 == unicast
	 */
	discover_packet.flags = unicast ? 0 : htons(DHCP_BROADCAST_FLAG);

	/* our hardware address */
	memcpy(discover_packet.chaddr, client_hardware_address, ETHERNET_HARDWARE_ADDRESS_LENGTH);

	/* first four bytes of options field is magic cookie (as per RFC 2132) */
	discover_packet.options[0] = '\x63';
	discover_packet.options[1] = '\x82';
	discover_packet.options[2] = '\x53';
	discover_packet.options[3] = '\x63';

	unsigned short opts = 4;
	/* DHCP message type is embedded in options field */
	discover_packet.options[opts++] =
		DHCP_OPTION_MESSAGE_TYPE;             /* DHCP message type option identifier */
	discover_packet.options[opts++] = '\x01'; /* DHCP message option length in bytes */
	discover_packet.options[opts++] = DHCPDISCOVER;

	/* the IP address we're requesting */
	if (request_specific_address) {
		discover_packet.options[opts++] = DHCP_OPTION_REQUESTED_ADDRESS;
		discover_packet.options[opts++] = '\x04';
		memcpy(&discover_packet.options[opts], &requested_address, sizeof(requested_address));
		opts += sizeof(requested_address);
	}
	discover_packet.options[opts++] = (char)DHCP_OPTION_END;

	/* unicast fields */
	if (unicast) {
		discover_packet.giaddr.s_addr = my_ip.s_addr;
	}

	/* see RFC 1542, 4.1.1 */
	discover_packet.hops = unicast ? 1 : 0;

	/* send the DHCPDISCOVER packet to broadcast address */
	struct sockaddr_in sockaddr_broadcast = {
		.sin_family = AF_INET,
		.sin_port = htons(DHCP_SERVER_PORT),
		.sin_addr.s_addr = unicast ? dhcp_ip.s_addr : INADDR_BROADCAST,
	};

	if (verbose) {
		printf(_("DHCPDISCOVER to %s port %d\n"), inet_ntoa(sockaddr_broadcast.sin_addr),
			   ntohs(sockaddr_broadcast.sin_port));
		printf("DHCPDISCOVER XID: %u (0x%X)\n", ntohl(discover_packet.xid),
			   ntohl(discover_packet.xid));
		printf("DHCDISCOVER ciaddr:  %s\n", inet_ntoa(discover_packet.ciaddr));
		printf("DHCDISCOVER yiaddr:  %s\n", inet_ntoa(discover_packet.yiaddr));
		printf("DHCDISCOVER siaddr:  %s\n", inet_ntoa(discover_packet.siaddr));
		printf("DHCDISCOVER giaddr:  %s\n", inet_ntoa(discover_packet.giaddr));
	}

	/* send the DHCPDISCOVER packet out */
	send_dhcp_packet(&discover_packet, sizeof(discover_packet), sock, &sockaddr_broadcast);

	if (verbose) {
		printf("\n\n");
	}

	return result;
}

/* waits for a DHCPOFFER message from one or more DHCP servers */
get_dhcp_offer_wrapper get_dhcp_offer(int sock, int dhcpoffer_timeout, uint32_t packet_xid,
									  dhcp_offer *dhcp_offer_list,
									  const unsigned char *client_hardware_address) {
	time_t start_time;
	time(&start_time);

	int result = OK;
	int responses = 0;
	int valid_responses = 0;
	/* receive as many responses as we can */
	for (;;) {
		time_t current_time;
		time(&current_time);
		if ((current_time - start_time) >= dhcpoffer_timeout) {
			break;
		}

		if (verbose) {
			printf("\n\n");
		}

		struct sockaddr_in source = {0};
		dhcp_packet offer_packet = {0};

		result = OK;
		result = receive_dhcp_packet(&offer_packet, sizeof(offer_packet), sock, dhcpoffer_timeout,
									 &source);

		if (result != OK) {
			if (verbose) {
				printf(_("Result=ERROR\n"));
			}

			continue;
		}
		if (verbose) {
			printf(_("Result=OK\n"));
		}

		responses++;

		/* The "source" is either a server or a relay. */
		/* Save a copy of "source" into "via" even if it's via itself */
		struct sockaddr_in via = {0};
		memcpy(&via, &source, sizeof(source));

		if (verbose) {
			printf(_("DHCPOFFER from IP address %s"), inet_ntoa(source.sin_addr));
			printf(_(" via %s\n"), inet_ntoa(via.sin_addr));
			printf("DHCPOFFER XID: %u (0x%X)\n", ntohl(offer_packet.xid), ntohl(offer_packet.xid));
		}

		/* check packet xid to see if its the same as the one we used in the discover packet */
		if (ntohl(offer_packet.xid) != packet_xid) {
			if (verbose) {
				printf(
					_("DHCPOFFER XID (%u) did not match DHCPDISCOVER XID (%u) - ignoring packet\n"),
					ntohl(offer_packet.xid), packet_xid);
			}

			continue;
		}

		/* check hardware address */
		result = OK;
		if (verbose) {
			printf("DHCPOFFER chaddr: ");
		}

		for (int i = 0; i < ETHERNET_HARDWARE_ADDRESS_LENGTH; i++) {
			if (verbose) {
				printf("%02X", offer_packet.chaddr[i]);
			}

			if (offer_packet.chaddr[i] != client_hardware_address[i]) {
				result = ERROR;
			}
		}
		if (verbose) {
			printf("\n");
		}

		if (result == ERROR) {
			if (verbose) {
				printf(_("DHCPOFFER hardware address did not match our own - ignoring packet\n"));
			}

			continue;
		}

		if (verbose) {
			printf("DHCPOFFER ciaddr: %s\n", inet_ntoa(offer_packet.ciaddr));
			printf("DHCPOFFER yiaddr: %s\n", inet_ntoa(offer_packet.yiaddr));
			printf("DHCPOFFER siaddr: %s\n", inet_ntoa(offer_packet.siaddr));
			printf("DHCPOFFER giaddr: %s\n", inet_ntoa(offer_packet.giaddr));
		}

		add_dhcp_offer_wrapper add_res =
			add_dhcp_offer(source.sin_addr, &offer_packet, dhcp_offer_list);
		if (add_res.error != OK) {
			// TODO
		} else {
			dhcp_offer_list = add_res.dhcp_offer_list;
		}

		valid_responses++;
	}

	if (verbose) {
		printf(_("Total responses seen on the wire: %d\n"), responses);
		printf(_("Valid responses for this machine: %d\n"), valid_responses);
	}

	get_dhcp_offer_wrapper ret_val = {
		.error = OK,
		.valid_responses = valid_responses,
		.dhcp_offer_list = dhcp_offer_list,
	};
	return ret_val;
}

/* sends a DHCP packet */
int send_dhcp_packet(void *buffer, int buffer_size, int sock, struct sockaddr_in *dest) {
	int result =
		sendto(sock, (char *)buffer, buffer_size, 0, (struct sockaddr *)dest, sizeof(*dest));

	if (verbose) {
		printf(_("send_dhcp_packet result: %d\n"), result);
	}

	if (result < 0) {
		return ERROR;
	}

	return OK;
}

/* receives a DHCP packet */
int receive_dhcp_packet(void *buffer, int buffer_size, int sock, int timeout,
						struct sockaddr_in *address) {
	/* wait for data to arrive (up time timeout) */
	struct timeval timeout_val = {
		.tv_sec = timeout,
		.tv_usec = 0,
	};
	fd_set readfds;
	FD_ZERO(&readfds);
	fd_set oobfds;
	FD_ZERO(&oobfds);
	FD_SET(sock, &readfds);
	FD_SET(sock, &oobfds);
	int nfound = select(sock + 1, &readfds, NULL, &oobfds, &timeout_val);

	/* make sure some data has arrived */
	if (!FD_ISSET(sock, &readfds)) {
		if (verbose) {
			printf(_("No (more) data received (nfound: %d)\n"), nfound);
		}
		return ERROR;
	}

	struct sockaddr_in source_address = {0};
	socklen_t address_size = sizeof(source_address);
	int recv_result = recvfrom(sock, (char *)buffer, buffer_size, 0,
							   (struct sockaddr *)&source_address, &address_size);
	if (verbose) {
		printf("recv_result: %d\n", recv_result);
	}

	if (recv_result == -1) {
		if (verbose) {
			printf(_("recvfrom() failed, "));
			printf("errno: (%d) -> %s\n", errno, strerror(errno));
		}
		return ERROR;
	}
	if (verbose) {
		printf(_("receive_dhcp_packet() result: %d\n"), recv_result);
		printf(_("receive_dhcp_packet() source: %s\n"), inet_ntoa(source_address.sin_addr));
	}

	memcpy(address, &source_address, sizeof(source_address));
	return OK;
}

/* creates a socket for DHCP communication */
int create_dhcp_socket(bool unicast, char *network_interface_name) {
	/* Set up the address we're going to bind to. */
	/* listen to DHCP server port if we're in unicast mode */
	struct sockaddr_in myname = {
		.sin_family = AF_INET,
		.sin_port = htons(unicast ? DHCP_SERVER_PORT : DHCP_CLIENT_PORT),
		// TODO previously the next line was trying to use our own IP, we was not set
		// until some point later, so it was removed. Recheck whether it is actually
		// necessary/useful
		.sin_addr.s_addr = INADDR_ANY,
	};

	/* create a socket for DHCP communications */
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		printf(_("Error: Could not create socket!\n"));
		exit(STATE_UNKNOWN);
	}

	if (verbose) {
		printf("DHCP socket: %d\n", sock);
	}

	/* set the reuse address flag so we don't get errors when restarting */
	int flag = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof(flag)) < 0) {
		printf(_("Error: Could not set reuse address option on DHCP socket!\n"));
		exit(STATE_UNKNOWN);
	}

	/* set the broadcast option - we need this to listen to DHCP broadcast messages */
	if (!unicast && setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char *)&flag, sizeof flag) < 0) {
		printf(_("Error: Could not set broadcast option on DHCP socket!\n"));
		exit(STATE_UNKNOWN);
	}

	struct ifreq interface;
	/* bind socket to interface */
#if defined(__linux__)
	strncpy(interface.ifr_ifrn.ifrn_name, network_interface_name, IFNAMSIZ - 1);
	interface.ifr_ifrn.ifrn_name[IFNAMSIZ - 1] = '\0';
	if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, (char *)&interface, sizeof(interface)) < 0) {
		printf(_("Error: Could not bind socket to interface %s.  Check your privileges...\n"),
			   network_interface_name);
		exit(STATE_UNKNOWN);
	}

#else
	strncpy(interface.ifr_name, network_interface_name, IFNAMSIZ - 1);
	interface.ifr_name[IFNAMSIZ - 1] = '\0';
#endif

	/* bind the socket */
	if (bind(sock, (struct sockaddr *)&myname, sizeof(myname)) < 0) {
		printf(_("Error: Could not bind to DHCP socket (port %d)!  Check your privileges...\n"),
			   DHCP_CLIENT_PORT);
		exit(STATE_UNKNOWN);
	}

	return sock;
}

/* closes DHCP socket */
int close_dhcp_socket(int sock) {
	close(sock);
	return OK;
}

/* adds a requested server address to list in memory */
int add_requested_server(struct in_addr server_address, int *requested_servers,
						 requested_server **requested_server_list) {
	requested_server *new_server = (requested_server *)malloc(sizeof(requested_server));
	if (new_server == NULL) {
		return ERROR;
	}

	new_server->server_address = server_address;
	new_server->answered = false;

	new_server->next = *requested_server_list;
	*requested_server_list = new_server;

	*requested_servers += 1;

	if (verbose) {
		printf(_("Requested server address: %s\n"), inet_ntoa(new_server->server_address));
	}

	return OK;
}

/* adds a DHCP OFFER to list in memory */
add_dhcp_offer_wrapper add_dhcp_offer(struct in_addr source, dhcp_packet *offer_packet,
									  dhcp_offer *dhcp_offer_list) {
	if (offer_packet == NULL) {
		add_dhcp_offer_wrapper tmp = {
			.error = ERROR,
		};
		return tmp;
	}

	uint32_t dhcp_lease_time = 0;
	uint32_t dhcp_renewal_time = 0;
	uint32_t dhcp_rebinding_time = 0;
	dhcp_offer *new_offer;
	struct in_addr serv_ident = {0};
	/* process all DHCP options present in the packet */
	for (int dchp_opt_idx = 4; dchp_opt_idx < MAX_DHCP_OPTIONS_LENGTH - 1;) {

		if ((int)offer_packet->options[dchp_opt_idx] == -1) {
			break;
		}

		/* get option type */
		unsigned option_type = offer_packet->options[dchp_opt_idx++];

		/* get option length */
		unsigned option_length = offer_packet->options[dchp_opt_idx++];

		if (verbose) {
			printf("Option: %d (0x%02X)\n", option_type, option_length);
		}

		/* get option data */
		switch (option_type) {
		case DHCP_OPTION_LEASE_TIME:
			memcpy(&dhcp_lease_time, &offer_packet->options[dchp_opt_idx], sizeof(dhcp_lease_time));
			dhcp_lease_time = ntohl(dhcp_lease_time);
			break;
		case DHCP_OPTION_RENEWAL_TIME:
			memcpy(&dhcp_renewal_time, &offer_packet->options[dchp_opt_idx],
				   sizeof(dhcp_renewal_time));
			dhcp_renewal_time = ntohl(dhcp_renewal_time);
			break;
		case DHCP_OPTION_REBINDING_TIME:
			memcpy(&dhcp_rebinding_time, &offer_packet->options[dchp_opt_idx],
				   sizeof(dhcp_rebinding_time));
			dhcp_rebinding_time = ntohl(dhcp_rebinding_time);
			break;
		case DHCP_OPTION_SERVER_IDENTIFIER:
			memcpy(&serv_ident.s_addr, &offer_packet->options[dchp_opt_idx],
				   sizeof(serv_ident.s_addr));
			break;
		}

		/* skip option data we're ignoring */
		if (option_type == 0) { /* "pad" option, see RFC 2132 (3.1) */
			dchp_opt_idx += 1;
		} else {
			dchp_opt_idx += option_length;
		}
	}

	if (verbose) {
		if (dhcp_lease_time == DHCP_INFINITE_TIME) {
			printf(_("Lease Time: Infinite\n"));
		} else {
			printf(_("Lease Time: %lu seconds\n"), (unsigned long)dhcp_lease_time);
		}
		if (dhcp_renewal_time == DHCP_INFINITE_TIME) {
			printf(_("Renewal Time: Infinite\n"));
		} else {
			printf(_("Renewal Time: %lu seconds\n"), (unsigned long)dhcp_renewal_time);
		}
		if (dhcp_rebinding_time == DHCP_INFINITE_TIME) {
			printf(_("Rebinding Time: Infinite\n"));
		}
		printf(_("Rebinding Time: %lu seconds\n"), (unsigned long)dhcp_rebinding_time);
	}

	new_offer = (dhcp_offer *)malloc(sizeof(dhcp_offer));

	if (new_offer == NULL) {
		add_dhcp_offer_wrapper tmp = {
			.error = ERROR,
		};
		return tmp;
	}

	/*
	 * RFC 2131 (2.) says: "DHCP clarifies the interpretation of the
	 * 'siaddr' field as the address of the server to use in the next step
	 * of the client's bootstrap process.  A DHCP server may return its own
	 * address in the 'siaddr' field, if the server is prepared to supply
	 * the next bootstrap service (e.g., delivery of an operating system
	 * executable image).  A DHCP server always returns its own address in
	 * the 'server identifier' option."  'serv_ident' is the 'server
	 * identifier' option, 'source' is the IP address we received the
	 * DHCPOFFER from.  If 'serv_ident' isn't available for some reason, we
	 * use 'source'.
	 */
	new_offer->server_address = serv_ident.s_addr ? serv_ident : source;
	new_offer->offered_address = offer_packet->yiaddr;
	new_offer->lease_time = dhcp_lease_time;
	new_offer->renewal_time = dhcp_renewal_time;
	new_offer->rebinding_time = dhcp_rebinding_time;
	new_offer->desired = false; /* exclusive mode: we'll check that in get_results */

	if (verbose) {
		printf(_("Added offer from server @ %s"), inet_ntoa(new_offer->server_address));
		printf(_(" of IP address %s\n"), inet_ntoa(new_offer->offered_address));
	}

	/* add new offer to head of list */
	new_offer->next = dhcp_offer_list;
	dhcp_offer_list = new_offer;

	add_dhcp_offer_wrapper result = {
		.error = OK,
		.dhcp_offer_list = dhcp_offer_list,
	};

	return result;
}

/* frees memory allocated to DHCP OFFER list */
int free_dhcp_offer_list(dhcp_offer *dhcp_offer_list) {
	dhcp_offer *next_offer;
	for (dhcp_offer *this_offer = dhcp_offer_list; this_offer != NULL; this_offer = next_offer) {
		next_offer = this_offer->next;
		free(this_offer);
	}

	return OK;
}

/* frees memory allocated to requested server list */
int free_requested_server_list(requested_server *requested_server_list) {
	requested_server *next_server;
	for (requested_server *this_server = requested_server_list; this_server != NULL;
		 this_server = next_server) {
		next_server = this_server->next;
		free(this_server);
	}

	return OK;
}

/* gets state and plugin output to return */
mp_subcheck get_results(bool exclusive, const int requested_servers,
						const struct in_addr requested_address, bool request_specific_address,
						requested_server *requested_server_list, int valid_responses,
						dhcp_offer *dhcp_offer_list) {
	mp_subcheck sc_dhcp_results = mp_subcheck_init();
	sc_dhcp_results = mp_set_subcheck_default_state(sc_dhcp_results, STATE_OK);

	/* we didn't receive any DHCPOFFERs */
	if (dhcp_offer_list == NULL) {
		sc_dhcp_results = mp_set_subcheck_state(sc_dhcp_results, STATE_CRITICAL);
		xasprintf(&sc_dhcp_results.output, "%s", "No DHCPOFFERs were received");
		return sc_dhcp_results;
	}

	if (valid_responses == 0) {
		// No valid responses at all, so early exit here
		sc_dhcp_results = mp_set_subcheck_state(sc_dhcp_results, STATE_CRITICAL);
		xasprintf(&sc_dhcp_results.output, "No valid responses received");
		return sc_dhcp_results;
	}

	if (valid_responses == 1) {
		xasprintf(&sc_dhcp_results.output, "Received %d DHCPOFFER", valid_responses);
	} else {
		xasprintf(&sc_dhcp_results.output, "Received %d DHCPOFFERs", valid_responses);
	}

	bool received_requested_address = false;
	dhcp_offer *undesired_offer = NULL;
	uint32_t max_lease_time = 0;
	/* checks responses from requested servers */
	int requested_responses = 0;
	if (requested_servers > 0) {
		for (requested_server *temp_server = requested_server_list; temp_server != NULL;
			 temp_server = temp_server->next) {
			for (dhcp_offer *temp_offer = dhcp_offer_list; temp_offer != NULL;
				 temp_offer = temp_offer->next) {
				/* get max lease time we were offered */
				if (temp_offer->lease_time > max_lease_time ||
					temp_offer->lease_time == DHCP_INFINITE_TIME) {
					max_lease_time = temp_offer->lease_time;
				}

				/* see if we got the address we requested */
				if (!memcmp(&requested_address, &temp_offer->offered_address,
							sizeof(requested_address))) {
					received_requested_address = true;
				}

				/* see if the servers we wanted a response from, talked to us or not */
				if (!memcmp(&temp_offer->server_address, &temp_server->server_address,
							sizeof(temp_server->server_address))) {
					if (verbose) {
						printf(_("DHCP Server Match: Offerer=%s"),
							   inet_ntoa(temp_offer->server_address));
						printf(_(" Requested=%s"), inet_ntoa(temp_server->server_address));
						if (temp_server->answered) {
							printf(_(" (duplicate)"));
						}
						printf(_("\n"));
					}

					if (!temp_server->answered) {
						requested_responses++;
						temp_server->answered = true;
						temp_offer->desired = true;
					}
				}
			}
		}

		/* exclusive mode: check for undesired offers */
		for (dhcp_offer *temp_offer = dhcp_offer_list; temp_offer != NULL;
			 temp_offer = temp_offer->next) {
			if (!temp_offer->desired) {
				undesired_offer = temp_offer; /* Checks only for the first undesired offer */
				break;                        /* no further checks needed */
			}
		}

		mp_subcheck sc_rqust_srvs = mp_subcheck_init();
		xasprintf(&sc_rqust_srvs.output, "%d of %d requested servers responded",
				  requested_responses, requested_servers);

		if (requested_responses == requested_servers) {
			sc_rqust_srvs = mp_set_subcheck_state(sc_rqust_srvs, STATE_OK);
		} else if (requested_responses == 0) {
			sc_rqust_srvs = mp_set_subcheck_state(sc_rqust_srvs, STATE_CRITICAL);
		} else if (requested_responses < requested_servers) {
			sc_rqust_srvs = mp_set_subcheck_state(sc_rqust_srvs, STATE_WARNING);
		} else {
			// We received more(!) responses than we asked for?
			// This case shouldn't happen, but is here for completion
			sc_rqust_srvs = mp_set_subcheck_state(sc_rqust_srvs, STATE_WARNING);
		}
		mp_add_subcheck_to_subcheck(&sc_dhcp_results, sc_rqust_srvs);

	} else {
		/* else check and see if we got our requested address from any server */
		for (dhcp_offer *temp_offer = dhcp_offer_list; temp_offer != NULL;
			 temp_offer = temp_offer->next) {
			/* get max lease time we were offered */
			if (temp_offer->lease_time > max_lease_time ||
				temp_offer->lease_time == DHCP_INFINITE_TIME) {
				max_lease_time = temp_offer->lease_time;
			}

			/* see if we got the address we requested */
			if (!memcmp(&requested_address, &temp_offer->offered_address,
						sizeof(requested_address))) {
				received_requested_address = true;
			}
		}
	}

	if (max_lease_time == DHCP_INFINITE_TIME) {
		xasprintf(&sc_dhcp_results.output, "%s, max lease time = Infinity", sc_dhcp_results.output);
	} else {
		xasprintf(&sc_dhcp_results.output, "%s, max lease time = %" PRIu32 " seconds",
				  sc_dhcp_results.output, max_lease_time);
	}

	if (exclusive) {
		mp_subcheck sc_rogue_server = mp_subcheck_init();

		if (undesired_offer != NULL) {
			// We wanted to get a DHCPOFFER exclusively from one machine, but another one
			// sent one (too)
			sc_rogue_server = mp_set_subcheck_state(sc_rogue_server, STATE_CRITICAL);

			// Get the addresses for printout
			// 1.address of the sending server
			char server_address[INET_ADDRSTRLEN];
			const char *server_address_transformed = inet_ntop(
				AF_INET, &undesired_offer->server_address, server_address, sizeof(server_address));

			if (server_address != server_address_transformed) {
				die(STATE_UNKNOWN, "inet_ntop failed");
			}

			// 2.address offered
			char offered_address[INET_ADDRSTRLEN];
			const char *offered_address_transformed =
				inet_ntop(AF_INET, &undesired_offer->offered_address, offered_address,
						  sizeof(offered_address));

			if (offered_address != offered_address_transformed) {
				die(STATE_UNKNOWN, "inet_ntop failed");
			}

			xasprintf(&sc_rogue_server.output, "Rogue DHCP Server detected! Server %s offered %s",
					  server_address, offered_address);
		} else {
			sc_rogue_server = mp_set_subcheck_state(sc_rogue_server, STATE_OK);
			xasprintf(&sc_rogue_server.output, "No Rogue DHCP Server detected");
		}
		mp_add_subcheck_to_subcheck(&sc_dhcp_results, sc_rogue_server);
	}

	if (request_specific_address) {
		mp_subcheck sc_rqustd_addr = mp_subcheck_init();

		if (received_requested_address) {
			sc_rqustd_addr = mp_set_subcheck_state(sc_rqustd_addr, STATE_OK);
			xasprintf(&sc_rqustd_addr.output, "Requested address (%s) was offered",
					  inet_ntoa(requested_address));
		} else {
			sc_rqustd_addr = mp_set_subcheck_state(sc_rqustd_addr, STATE_WARNING);
			xasprintf(&sc_rqustd_addr.output, "Requested address (%s) was NOT offered",
					  inet_ntoa(requested_address));
		}

		mp_add_subcheck_to_subcheck(&sc_dhcp_results, sc_rqustd_addr);
	}

	return sc_dhcp_results;
}

/* process command-line arguments */
process_arguments_wrapper process_arguments(int argc, char **argv) {
	if (argc < 1) {
		process_arguments_wrapper tmp = {
			.error = ERROR,
		};
		return tmp;
	}

	enum {
		output_format_index = CHAR_MAX + 1,
	};

	int option_index = 0;
	static struct option long_options[] = {
		{"serverip", required_argument, 0, 's'},
		{"requestedip", required_argument, 0, 'r'},
		{"timeout", required_argument, 0, 't'},
		{"interface", required_argument, 0, 'i'},
		{"mac", required_argument, 0, 'm'},
		{"unicast", no_argument, 0, 'u'},
		{"exclusive", no_argument, 0, 'x'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{"output-format", required_argument, 0, output_format_index},
		{0, 0, 0, 0}};

	check_dhcp_config config = check_dhcp_config_init();
	int option_char = 0;
	while (true) {
		option_char = getopt_long(argc, argv, "+hVvxt:s:r:t:i:m:u", long_options, &option_index);

		if (option_char == -1 || option_char == EOF || option_char == 1) {
			break;
		}

		switch (option_char) {
		case 's': /* DHCP server address */
			resolve_host(optarg, &config.dhcp_ip);
			add_requested_server(config.dhcp_ip, &config.num_of_requested_servers,
								 &config.requested_server_list);
			break;

		case 'r': /* address we are requested from DHCP servers */
			resolve_host(optarg, &config.requested_address);
			config.request_specific_address = true;
			break;

		case 't': /* timeout */
			if (atoi(optarg) > 0) {
				config.dhcpoffer_timeout = atoi(optarg);
			}
			break;

		case 'm': /* MAC address */
			if ((config.user_specified_mac = mac_aton(optarg)) == NULL) {
				usage("Cannot parse MAC address.\n");
			}
			if (verbose) {
				print_hardware_address(config.user_specified_mac);
			}
			break;

		case 'i': /* interface name */
			strncpy(config.network_interface_name, optarg,
					sizeof(config.network_interface_name) - 1);
			config.network_interface_name[sizeof(config.network_interface_name) - 1] = '\x0';
			break;

		case 'u': /* unicast testing */
			config.unicast_mode = true;
			break;

		case 'x': /* exclusive testing aka "rogue DHCP server detection" */
			config.exclusive_mode = true;
			break;

		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);

		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);

		case 'v': /* verbose */
			verbose = 1;
			break;
		case output_format_index: {
			parsed_output_format parser = mp_parse_output_format(optarg);
			if (!parser.parsing_success) {
				// TODO List all available formats here, maybe add anothoer usage function
				printf("Invalid output format: %s\n", optarg);
				exit(STATE_UNKNOWN);
			}

			config.output_format_is_set = true;
			config.output_format = parser.output_format;
			break;
		}
		case '?': /* help */
			usage5();
			break;

		default:
			break;
		}
	}

	if (argc - optind > 0) {
		usage(_("Got unexpected non-option argument"));
	}

	process_arguments_wrapper result = {
		.config = config,
		.error = OK,
	};
	return result;
}

#if defined(__sun__) || defined(__solaris__) || defined(__hpux__)
/* Kompf 2000-2003	see ACKNOWLEDGEMENTS */

/* get a message from a stream; return type of message */
static int get_msg(int fd) {
	int flags = 0;
	int res, ret;
	ctl_area[0] = 0;
	dat_area[0] = 0;
	ret = 0;
	res = getmsg(fd, &ctl, &dat, &flags);

	if (res < 0) {
		if (errno == EINTR) {
			return (GOT_INTR);
		} else {
			printf("%s\n", "get_msg FAILED.");
			return (GOT_ERR);
		}
	}
	if (ctl.len > 0) {
		ret |= GOT_CTRL;
	}
	if (dat.len > 0) {
		ret |= GOT_DATA;
	}

	return (ret);
}

/* verify that dl_primitive in ctl_area = prim */
static int check_ctrl(int prim) {
	dl_error_ack_t *err_ack = (dl_error_ack_t *)ctl_area;

	if (err_ack->dl_primitive != prim) {
		printf(_("Error: DLPI stream API failed to get MAC in check_ctrl: %s.\n"), strerror(errno));
		exit(STATE_UNKNOWN);
	}

	return 0;
}

/* put a control message on a stream */
static int put_ctrl(int fd, int len, int pri) {

	ctl.len = len;
	if (putmsg(fd, &ctl, 0, pri) < 0) {
		printf(_("Error: DLPI stream API failed to get MAC in put_ctrl/putmsg(): %s.\n"),
			   strerror(errno));
		exit(STATE_UNKNOWN);
	}

	return 0;
}

/* put a control + data message on a stream */
static int put_both(int fd, int clen, int dlen, int pri) {

	ctl.len = clen;
	dat.len = dlen;
	if (putmsg(fd, &ctl, &dat, pri) < 0) {
		printf(_("Error: DLPI stream API failed to get MAC in put_both/putmsg().\n"),
			   strerror(errno));
		exit(STATE_UNKNOWN);
	}

	return 0;
}

/* open file descriptor and attach */
static int dl_open(const char *dev, int unit, int *fd) {
	dl_attach_req_t *attach_req = (dl_attach_req_t *)ctl_area;

	if ((*fd = open(dev, O_RDWR)) == -1) {
		printf(_("Error: DLPI stream API failed to get MAC in dl_attach_req/open(%s..): %s.\n"),
			   dev, strerror(errno));
		exit(STATE_UNKNOWN);
	}
	attach_req->dl_primitive = DL_ATTACH_REQ;
	attach_req->dl_ppa = unit;
	put_ctrl(*fd, sizeof(dl_attach_req_t), 0);
	get_msg(*fd);
	return check_ctrl(DL_OK_ACK);
}

/* send DL_BIND_REQ */
static int dl_bind(int fd, int sap, u_char *addr) {
	dl_bind_req_t *bind_req = (dl_bind_req_t *)ctl_area;
	dl_bind_ack_t *bind_ack = (dl_bind_ack_t *)ctl_area;

	bind_req->dl_primitive = DL_BIND_REQ;
	bind_req->dl_sap = sap;
	bind_req->dl_max_conind = 1;
	bind_req->dl_service_mode = DL_CLDLS;
	bind_req->dl_conn_mgmt = 0;
	bind_req->dl_xidtest_flg = 0;
	put_ctrl(fd, sizeof(dl_bind_req_t), 0);
	get_msg(fd);
	if (GOT_ERR == check_ctrl(DL_BIND_ACK)) {
		printf(_("Error: DLPI stream API failed to get MAC in dl_bind/check_ctrl(): %s.\n"),
			   strerror(errno));
		exit(STATE_UNKNOWN);
	}
	bcopy((u_char *)bind_ack + bind_ack->dl_addr_offset, addr, bind_ack->dl_addr_length);

	return 0;
}

/***********************************************************************
 * interface:
 * function mac_addr_dlpi - get the mac address of the interface with
 *                          type dev (eg lnc, hme) and unit (0, 1 ..)
 *
 * parameter: addr: an array of six bytes, has to be allocated by the caller
 *
 * return: 0 if OK, -1 if the address could not be determined
 *
 *
 ***********************************************************************/

long mac_addr_dlpi(const char *dev, int unit, u_char *addr) {
	int fd;
	u_char mac_addr[25];

	if (GOT_ERR != dl_open(dev, unit, &fd)) {
		if (GOT_ERR != dl_bind(fd, INSAP, mac_addr)) {
			bcopy(mac_addr, addr, 6);
			return 0;
		}
	}
	close(fd);

	return -1;
}

/* Kompf 2000-2003 */
#endif

/* resolve host name or die (TODO: move this to netutils.c!) */
void resolve_host(const char *name, struct in_addr *out) {
	struct addrinfo hints = {
		.ai_family = PF_INET,
	};
	struct addrinfo *addr_info;

	if (getaddrinfo(name, NULL, &hints, &addr_info) != 0) {
		usage_va(_("Invalid hostname/address - %s"), optarg);
	}

	memcpy(out, &((struct sockaddr_in *)addr_info->ai_addr)->sin_addr, sizeof(*out));
	freeaddrinfo(addr_info);
}

/* parse MAC address string, return 6 bytes (unterminated) or NULL */
unsigned char *mac_aton(const char *string) {
	static unsigned char result[MAC_ADDR_LEN];
	char tmp[3];
	unsigned byte_counter = 0;

	for (int i = 0; string[i] != '\0' && byte_counter < sizeof(result); i++) {
		/* ignore ':' and any other non-hex character */
		if (!isxdigit(string[i]) || !isxdigit(string[i + 1])) {
			continue;
		}
		tmp[0] = string[i];
		tmp[1] = string[i + 1];
		tmp[2] = '\0';
		result[byte_counter] = strtol(tmp, (char **)NULL, 16);
		i++;
		byte_counter++;
	}

	return (byte_counter == MAC_ADDR_LEN) ? result : NULL;
}

void print_hardware_address(const unsigned char *address) {

	printf(_("Hardware address: "));
	for (int addr_idx = 0; addr_idx < MAC_ADDR_LEN; addr_idx++) {
		printf("%2.2x:", address[addr_idx]);
	}
	putchar('\n');
}

/* print usage help */
void print_help(void) {

	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 2001-2004 Ethan Galstad (nagios@nagios.org)\n");
	printf(COPYRIGHT, copyright, email);

	printf("%s\n", _("This plugin tests the availability of DHCP servers on a network."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(UT_OUTPUT_FORMAT);
	printf(UT_VERBOSE);

	printf(" %s\n", "-s, --serverip=IPADDRESS");
	printf("    %s\n", _("IP address of DHCP server that we must hear from"));
	printf(" %s\n", "-r, --requestedip=IPADDRESS");
	printf("    %s\n", _("IP address that should be offered by at least one DHCP server"));
	printf(" %s\n", "-t, --timeout=INTEGER");
	printf("    %s\n", _("Seconds to wait for DHCPOFFER before timeout occurs"));
	printf(" %s\n", "-i, --interface=STRING");
	printf("    %s\n", _("Interface to to use for listening (i.e. eth0)"));
	printf(" %s\n", "-m, --mac=STRING");
	printf("    %s\n", _("MAC address to use in the DHCP request"));
	printf(" %s\n", "-u, --unicast");
	printf("    %s\n", _("Unicast testing: mimic a DHCP relay, requires -s"));
	printf(" %s\n", "-x, --exclusive");
	printf("    %s\n",
		   _("Only requested DHCP server may response (rogue DHCP server detection), requires -s"));

	printf(UT_SUPPORT);
}

void print_usage(void) {

	printf("%s\n", _("Usage:"));
	printf(" %s [-v] [-u] [-x] [-s serverip] [-r requestedip] [-t timeout]\n", progname);
	printf("                  [-i interface] [-m mac]\n");
}
