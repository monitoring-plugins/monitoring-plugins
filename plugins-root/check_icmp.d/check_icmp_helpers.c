#include "./config.h"
#include "states.h"
#include <math.h>
#include <netinet/in.h>
#include "./check_icmp_helpers.h"
#include "../../plugins/netutils.h"

check_icmp_config check_icmp_config_init() {
	check_icmp_config tmp = {
		.source_ip = NULL,

		.order_mode = false,
		.mos_mode = false,
		.rta_mode = false,
		.pl_mode = false,
		.jitter_mode = false,
		.score_mode = false,

		.min_hosts_alive = -1,
		.icmp_data_size = DEFAULT_PING_DATA_SIZE,
		.icmp_pkt_size = DEFAULT_PING_DATA_SIZE + ICMP_MINLEN,
		.pkt_interval = DEFAULT_PKT_INTERVAL,
		.target_interval = 0,
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
		.pid = {},
		.mode = MODE_RTA,
		.timeout = DEFAULT_TIMEOUT,
		.ttl = DEFAULT_TTL,

		.packets = DEFAULT_NUMBER_OF_PACKETS,
		.number_of_targets = 0,
		.hosts = NULL,
	};
	return tmp;
}

ping_target ping_target_init() {
	ping_target tmp = {
		.rtmin = INFINITY,

		.jitter_min = INFINITY,

		.rta_status = STATE_OK,
		.jitter_status = STATE_OK,
		.mos_status = STATE_OK,
		.score_status = STATE_OK,
		.pl_status = STATE_OK,
		.order_status = STATE_OK,
	};

	return tmp;
}

check_icmp_state check_icmp_state_init() {
	check_icmp_state tmp = {.icmp_sent = 0, .icmp_lost = 0, .icmp_recv = 0, .targets_down = 0};

	return tmp;
}

rta_host_create_wrapper rta_host_create(char *name, struct sockaddr_storage *address) {
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	if (address_family == AF_INET) {
		sin = (struct sockaddr_in *)address;
	} else {
		sin6 = (struct sockaddr_in6 *)address;
	}

	rta_host_create_wrapper result = {
		.errorcode = OK,
	};

	/* disregard obviously stupid addresses
	 * (I didn't find an ipv6 equivalent to INADDR_NONE) */
	if (((address_family == AF_INET &&
		  (sin->sin_addr.s_addr == INADDR_NONE || sin->sin_addr.s_addr == INADDR_ANY))) ||
		(address_family == AF_INET6 && (sin6->sin6_addr.s6_addr == in6addr_any.s6_addr))) {
		result.errorcode = ERROR;
		return result;
	}

	// TODO: Maybe add the following back in as a sanity check for the config
	// /* no point in adding two identical IP's, so don't. ;) */
	// struct sockaddr_in *host_sin;
	// struct sockaddr_in6 *host_sin6;
	// struct rta_host *host = host_list;

	// while (host) {
	// 	host_sin = (struct sockaddr_in *)&host->saddr_in;
	// 	host_sin6 = (struct sockaddr_in6 *)&host->saddr_in;

	// 	if ((address_family == AF_INET && host_sin->sin_addr.s_addr == sin->sin_addr.s_addr) ||
	// 		(address_family == AF_INET6 &&
	// 		 host_sin6->sin6_addr.s6_addr == sin6->sin6_addr.s6_addr)) {
	// 		if (debug) {
	// 			printf("Identical IP already exists. Not adding %s\n", name);
	// 		}
	// 		return -1;
	// 	}
	// 	host = host->next;
	// }

	/* add the fresh ip */
	ping_target host = ping_target_init();

	/* set the values. use calling name for output */
	host.name = strdup(name);

	/* fill out the sockaddr_storage struct */
	if (address_family == AF_INET) {
		struct sockaddr_in *host_sin = (struct sockaddr_in *)&host.saddr_in;
		host_sin->sin_family = AF_INET;
		host_sin->sin_addr.s_addr = sin->sin_addr.s_addr;
	} else {
		struct sockaddr_in6 *host_sin6 = (struct sockaddr_in6 *)&host.saddr_in;
		host_sin6->sin6_family = AF_INET6;
		memcpy(host_sin6->sin6_addr.s6_addr, sin6->sin6_addr.s6_addr,
			   sizeof host_sin6->sin6_addr.s6_addr);
	}

	result.host = host;

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
