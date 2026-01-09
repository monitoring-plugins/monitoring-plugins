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
#include <stdint.h>
#include "./check_icmp_helpers.h"
#include "output.h"

/* threshold structure. all values are maximum allowed, exclusive */
typedef struct {
	unsigned char pl; /* max allowed packet loss in percent */
	time_t rta;       /* roundtrip time average, microseconds */
	double jitter;    /* jitter time average, microseconds */
	double mos;       /* MOS */
	double score;     /* Score */
} check_icmp_threshold;

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
 * MODE_ICMP: Default Mode
 */
typedef enum {
	MODE_RTA,
	MODE_HOSTCHECK,
	MODE_ALL,
	MODE_ICMP,
} check_icmp_execution_mode;

typedef struct {
	bool order_mode;
	bool mos_mode;
	bool rta_mode;
	bool pl_mode;
	bool jitter_mode;
	bool score_mode;
} check_icmp_mode_switches;

typedef struct {
	check_icmp_mode_switches modes;

	int min_hosts_alive;
	check_icmp_threshold crit;
	check_icmp_threshold warn;

	unsigned long ttl;
	unsigned short icmp_data_size;
	time_t target_interval;
	unsigned short number_of_packets;

	char *source_ip;
	bool need_v4;
	bool need_v6;

	uint16_t sender_id; // PID of the main process, which is used as an ID in packets

	check_icmp_execution_mode mode;

	unsigned short number_of_targets;
	ping_target *targets;

	unsigned short number_of_hosts;
	check_icmp_target_container *hosts;

	mp_output_format output_format;
	bool output_format_is_set;
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
// DEPRECATED, remove when removing the option
#define DEFAULT_PKT_INTERVAL 80000

#define DEFAULT_TARGET_INTERVAL 0

#define DEFAULT_WARN_RTA 200000
#define DEFAULT_CRIT_RTA 500000
#define DEFAULT_WARN_PL  40
#define DEFAULT_CRIT_PL  80

#define DEFAULT_TIMEOUT 10
#define DEFAULT_TTL     64

#define DEFAULT_NUMBER_OF_PACKETS 5

#define PACKET_BACKOFF_FACTOR 1.5
#define TARGET_BACKOFF_FACTOR 1.5
