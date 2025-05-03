#pragma once

#include "../../config.h"
#include "../../lib/states.h"
#include <stddef.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <arpa/inet.h>
#include "./check_icmp_helpers.h"

/* threshold structure. all values are maximum allowed, exclusive */
typedef struct threshold {
	unsigned char pl; /* max allowed packet loss in percent */
	unsigned int rta; /* roundtrip time average, microseconds */
	double jitter;    /* jitter time average, microseconds */
	double mos;       /* MOS */
	double score;     /* Score */
} threshold;

typedef struct {
	char *source_ip;

	bool order_mode;
	bool mos_mode;
	bool rta_mode;
	bool pl_mode;
	bool jitter_mode;
	bool score_mode;

	int min_hosts_alive;
	unsigned short icmp_data_size;
	unsigned short icmp_pkt_size;
	unsigned int pkt_interval;
	unsigned int target_interval;
	threshold crit;
	threshold warn;
	pid_t pid;

	int mode;
	unsigned long ttl;

	unsigned short packets;

	unsigned short number_of_targets;
	ping_target *targets;

	unsigned short number_of_hosts;
	check_icmp_target_container *hosts;
} check_icmp_config;

check_icmp_config check_icmp_config_init();

/* the data structure */
typedef struct icmp_ping_data {
	struct timeval stime; /* timestamp (saved in protocol struct as well) */
	unsigned short ping_id;
} icmp_ping_data;

#define MAX_IP_PKT_SIZE        65536 /* (theoretical) max IP packet size */
#define IP_HDR_SIZE            20
#define MAX_PING_DATA          (MAX_IP_PKT_SIZE - IP_HDR_SIZE - ICMP_MINLEN)
#define MIN_PING_DATA_SIZE     sizeof(struct icmp_ping_data)
#define DEFAULT_PING_DATA_SIZE (MIN_PING_DATA_SIZE + 44)

/* 80 msec packet interval by default */
#define DEFAULT_PKT_INTERVAL    80000
#define DEFAULT_TARGET_INTERVAL 0

#define DEFAULT_WARN_RTA 200000
#define DEFAULT_CRIT_RTA 500000
#define DEFAULT_WARN_PL  40
#define DEFAULT_CRIT_PL  80

#define DEFAULT_TIMEOUT 10
#define DEFAULT_TTL     64

/* the different modes of this program are as follows:
 * MODE_RTA: send all packets no matter what (mimic check_icmp and check_ping)
 * MODE_HOSTCHECK: Return immediately upon any sign of life
 *                 In addition, sends packets to ALL addresses assigned
 *                 to this host (as returned by gethostbyname() or
 *                 gethostbyaddr() and expects one host only to be checked at
 *                 a time.  Therefore, any packet response what so ever will
 *                 count as a sign of life, even when received outside
 *                 crit.rta limit. Do not misspell any additional IP's.
 * MODE_ALL:  Requires packets from ALL requested IP to return OK (default).
 * MODE_ICMP: implement something similar to check_icmp (MODE_RTA without
 *            tcp and udp args does this)
 */
#define MODE_RTA       0
#define MODE_HOSTCHECK 1
#define MODE_ALL       2
#define MODE_ICMP      3

#define DEFAULT_NUMBER_OF_PACKETS 5

#define PACKET_BACKOFF_FACTOR 1.5
#define TARGET_BACKOFF_FACTOR 1.5
