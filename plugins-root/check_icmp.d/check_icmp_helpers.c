#include "./config.h"
#include <math.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "./check_icmp_helpers.h"
#include "../../plugins/netutils.h"

// timeout as a global variable to make it available to the timeout handler
unsigned int timeout = DEFAULT_TIMEOUT;

check_icmp_config check_icmp_config_init() {
	check_icmp_config tmp = {
		.modes =
			{
				.order_mode = false,
				.mos_mode = false,
				.rta_mode = false,
				.pl_mode = false,
				.jitter_mode = false,
				.score_mode = false,
			},

		.min_hosts_alive = -1,
		.crit = {.pl = DEFAULT_CRIT_PL,
				 .rta = DEFAULT_CRIT_RTA,
				 .jitter = 50.0,
				 .mos = 3.0,
				 .score = 70.0},
		.warn = {.pl = DEFAULT_WARN_PL,
				 .rta = DEFAULT_WARN_RTA,
				 .jitter = 40.0,
				 .mos = 3.5,
				 .score = 80.0},

		.ttl = DEFAULT_TTL,
		.icmp_data_size = DEFAULT_PING_DATA_SIZE,
		.icmp_pkt_size = DEFAULT_PING_DATA_SIZE + ICMP_MINLEN,
		.target_interval = 0,
		.number_of_packets = DEFAULT_NUMBER_OF_PACKETS,

		.source_ip = NULL,
		.need_v4 = false,
		.need_v6 = false,

		.sender_id = 0,

		.mode = MODE_RTA,

		.number_of_targets = 0,
		.targets = NULL,

		.number_of_hosts = 0,
		.hosts = NULL,

		.output_format_is_set = false,
	};
	return tmp;
}

ping_target ping_target_init() {
	ping_target tmp = {
		.rtmin = INFINITY,

		.jitter_min = INFINITY,

		.found_out_of_order_packets = false,
	};

	return tmp;
}

check_icmp_state check_icmp_state_init() {
	check_icmp_state tmp = {.icmp_sent = 0, .icmp_lost = 0, .icmp_recv = 0, .targets_down = 0};

	return tmp;
}

ping_target_create_wrapper ping_target_create(struct sockaddr_storage address) {
	ping_target_create_wrapper result = {
		.errorcode = OK,
	};

	struct sockaddr_storage *tmp_addr = &address;

	/* disregard obviously stupid addresses
	 * (I didn't find an ipv6 equivalent to INADDR_NONE) */
	if (((tmp_addr->ss_family == AF_INET &&
		  (((struct sockaddr_in *)tmp_addr)->sin_addr.s_addr == INADDR_NONE ||
		   ((struct sockaddr_in *)tmp_addr)->sin_addr.s_addr == INADDR_ANY))) ||
		(tmp_addr->ss_family == AF_INET6 &&
		 (((struct sockaddr_in6 *)tmp_addr)->sin6_addr.s6_addr == in6addr_any.s6_addr))) {
		result.errorcode = ERROR;
		return result;
	}

	/* add the fresh ip */
	ping_target target = ping_target_init();

	/* fill out the sockaddr_storage struct */
	target.address = address;

	result.host = target;

	return result;
}

check_icmp_target_container check_icmp_target_container_init() {
	check_icmp_target_container tmp = {
		.name = NULL,
		.number_of_targets = 0,
		.target_list = NULL,
	};
	return tmp;
}

unsigned int ping_target_list_append(ping_target *list, ping_target *elem) {
	if (elem == NULL || list == NULL) {
		return 0;
	}

	while (list->next != NULL) {
		list = list->next;
	}

	list->next = elem;

	unsigned int result = 1;

	while (elem->next != NULL) {
		result++;
		elem = elem->next;
	}

	return result;
}
