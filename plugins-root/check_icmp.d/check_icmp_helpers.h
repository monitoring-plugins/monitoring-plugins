#pragma once

#include "../../lib/states.h"
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <arpa/inet.h>

typedef struct rta_host {
	unsigned short id;                            /* id in **table, and icmp pkts */
	char *name;                                   /* arg used for adding this host */
	char *msg;                                    /* icmp error message, if any */
	struct sockaddr_storage saddr_in;             /* the address of this host */
	struct sockaddr_storage error_addr;           /* stores address of error replies */
	unsigned long long time_waited;               /* total time waited, in usecs */
	unsigned int icmp_sent, icmp_recv, icmp_lost; /* counters */
	unsigned char icmp_type, icmp_code;           /* type and code from errors */
	unsigned short flags;                         /* control/status flags */

	double rta;   /* measured RTA */
	double rtmax; /* max rtt */
	double rtmin; /* min rtt */

	double jitter;     /* measured jitter */
	double jitter_max; /* jitter rtt maximum */
	double jitter_min; /* jitter rtt minimum */

	double EffectiveLatency;
	double mos;   /* Mean opinion score */
	double score; /* score */

	unsigned int last_tdiff;
	unsigned int last_icmp_seq; /* Last ICMP_SEQ to check out of order pkts */
	unsigned char pl;           /* measured packet loss */

	mp_state_enum rta_status;    // check result for RTA checks
	mp_state_enum jitter_status; // check result for Jitter checks
	mp_state_enum mos_status;    // check result for MOS checks
	mp_state_enum score_status;  // check result for score checks
	mp_state_enum pl_status;     // check result for packet loss checks
	mp_state_enum order_status;  // check result for packet order checks

	struct rta_host *next;
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
} rta_host_create_wrapper;

rta_host_create_wrapper rta_host_create(char *name, struct sockaddr_storage *address);
