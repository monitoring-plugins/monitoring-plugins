#pragma once

#include "../../config.h"
#include "../lib/states.h"
#include <stdbool.h>
#include <netinet/in.h>
#include "net/if.h"
#include "output.h"

typedef struct requested_server_struct {
	struct in_addr server_address;
	bool answered;
	struct requested_server_struct *next;
} requested_server;

typedef struct check_dhcp_config {
	bool unicast_mode;   /* unicast mode: mimic a DHCP relay */
	bool exclusive_mode; /* exclusive mode aka "rogue DHCP server detection" */
	int num_of_requested_servers;
	struct in_addr dhcp_ip; /* server to query (if in unicast mode) */
	struct in_addr requested_address;
	bool request_specific_address;

	int dhcpoffer_timeout;
	unsigned char *user_specified_mac;
	char network_interface_name[IFNAMSIZ];
	requested_server *requested_server_list;

	mp_output_format output_format;
	bool output_format_is_set;
} check_dhcp_config;

check_dhcp_config check_dhcp_config_init(void) {
	check_dhcp_config tmp = {
		.unicast_mode = false,
		.exclusive_mode = false,
		.num_of_requested_servers = 0,
		.dhcp_ip = {0},
		.requested_address = {0},
		.request_specific_address = false,

		.dhcpoffer_timeout = 2,
		.user_specified_mac = NULL,
		.network_interface_name = "eth0",
		.requested_server_list = NULL,

		.output_format_is_set = false,
	};
	return tmp;
}
