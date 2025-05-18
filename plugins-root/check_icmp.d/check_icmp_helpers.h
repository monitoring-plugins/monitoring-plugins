#pragma once

#include "../../lib/states.h"
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <arpa/inet.h>

typedef struct ping_target {
	unsigned short id; /* id in **table, and icmp pkts */
	char *msg;         /* icmp error message, if any */

	struct sockaddr_storage address;              /* the address of this host */
	struct sockaddr_storage error_addr;           /* stores address of error replies */
	time_t time_waited;                           /* total time waited, in usecs */
	unsigned int icmp_sent, icmp_recv, icmp_lost; /* counters */
	unsigned char icmp_type, icmp_code;           /* type and code from errors */
	unsigned short flags;                         /* control/status flags */

	double rtmax; /* max rtt */
	double rtmin; /* min rtt */

	double jitter;     /* measured jitter */
	double jitter_max; /* jitter rtt maximum */
	double jitter_min; /* jitter rtt minimum */

	time_t last_tdiff;
	unsigned int last_icmp_seq; /* Last ICMP_SEQ to check out of order pkts */

	bool found_out_of_order_packets;

	struct ping_target *next;
} ping_target;

ping_target ping_target_init();

typedef struct {
	char *name;
	ping_target *target_list;
	unsigned int number_of_targets;
} check_icmp_target_container;

check_icmp_target_container check_icmp_target_container_init();

typedef struct {
	unsigned int icmp_sent;
	unsigned int icmp_recv;
	unsigned int icmp_lost;
	unsigned short targets_down;
} check_icmp_state;

check_icmp_state check_icmp_state_init();

typedef struct {
	int errorcode;
	ping_target host;
} ping_target_create_wrapper;

typedef struct {
	int socket4;
	int socket6;
} check_icmp_socket_set;

ping_target_create_wrapper ping_target_create(struct sockaddr_storage address);
unsigned int ping_target_list_append(ping_target *list, ping_target *elem);

void check_icmp_timeout_handler(int, siginfo_t *, void *);
