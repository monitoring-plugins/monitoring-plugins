/*
 * $Id$
 *
 * This is a hack of fping2 made to work with nagios.
 * It's fast and removes the necessity of parsing another programs output.
 *
 * VIEWING NOTES:
 * This file was formatted with tab indents at a tab stop of 4.
 *
 * It is highly recommended that your editor is set to this
 * tab stop setting for viewing and editing.
 *
 * COPYLEFT;
 * This programs copyright status is currently undetermined. Much of
 * the code in it comes from the fping2 program which used to be licensed
 * under the Stanford General Software License (available at
 * http://graphics.stanford.edu/software/license.html). It is presently
 * unclear what license (if any) applies to the original code at the
 * moment.
 *
 * The fping website can be found at http://www.fping.com
 */

#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#include <unistd.h>

#include <stdlib.h>

#include <string.h>
#include <stddef.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>

#include <sys/file.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>

#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#include <arpa/inet.h>
#include <netdb.h>

/* RS6000 has sys/select.h */
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif							/* HAVE_SYS_SELECT_H */

/* rta threshold values can't be larger than MAXTTL seconds */
#ifndef MAXTTL
#  define MAXTTL	255
#endif
#ifndef IPDEFTTL
#  define IPDEFTTL	64
#endif

/*** externals ***/
extern char *optarg;
extern int optind, opterr;

/*** Constants ***/
#define EMAIL		"ae@op5.se"
#define VERSION		"0.8.1"

#ifndef INADDR_NONE
# define INADDR_NONE 0xffffffU
#endif

/*** Ping packet defines ***/
/* data added after ICMP header for our nefarious purposes */
typedef struct ping_data {
	unsigned int ping_count;	/* counts up to -[n|p] count or 1 */
	struct timeval ping_ts;		/* time sent */
} PING_DATA;

#define MIN_PING_DATA	sizeof(PING_DATA)
#define	MAX_IP_PACKET	65536	/* (theoretical) max IP packet size */
#define SIZE_IP_HDR		20
#define SIZE_ICMP_HDR	ICMP_MINLEN	/* from ip_icmp.h */
#define MAX_PING_DATA	(MAX_IP_PACKET - SIZE_IP_HDR - SIZE_ICMP_HDR)

/*
 *  Interval is the minimum amount of time between sending a ping packet to
 *  any host.
 *
 *  Perhost_interval is the minimum amount of time between sending a ping
 *  packet to a particular responding host
 *
 *  Timeout  is the initial amount of time between sending a ping packet to
 *  a particular non-responding host.
 *
 *  Retry is the number of ping packets to send to a non-responding host
 *  before giving up (in is-it-alive mode).
 *
 *  Backoff factor is how much longer to wait on successive retries.
 */
#ifndef DEFAULT_INTERVAL
#define DEFAULT_INTERVAL 25		/* default time between packets (msec) */
#endif

#ifndef DEFAULT_RETRY
#define DEFAULT_RETRY	1			/* number of times to retry a host */
#endif

#ifndef DEFAULT_TIMEOUT
# define DEFAULT_TIMEOUT 1000
#endif

#ifndef DEFAULT_BACKOFF_FACTOR
#define DEFAULT_BACKOFF_FACTOR 1.5	/* exponential timeout factor */
#endif
#define MIN_BACKOFF_FACTOR     1.0	/* exponential timeout factor */
#define MAX_BACKOFF_FACTOR     5.0	/* exponential timeout factor */

#ifndef DNS_TIMEOUT
#define DNS_TIMEOUT 1000		/* time in usec for dns retry */
#endif

#ifndef MAX_RTA_THRESHOLD_VALUE
# define MAX_RTA_THRESHOLD_VALUE 120*1000000 /* 2 minutes should be enough */
#endif
#ifndef MIN_RTA_THRESHOLD_VALUE
# define MIN_RTA_THRESHOLD_VALUE 10000	/* minimum RTA threshold value */
#endif

/* sized so as to be like traditional ping */
#define DEFAULT_PING_DATA_SIZE	(MIN_PING_DATA + 44)

/* maxima and minima */
#define MAX_COUNT				50		/* max count even if we're root */
#define MAX_RETRY				5
#define MIN_INTERVAL			25	/* msecs */
#define MIN_TIMEOUT				50	/* msecs */

/* response time array flags */
#define RESP_WAITING	-1
#define RESP_UNUSED		-2

#define	ICMP_UNREACH_MAXTYPE	15

/* entry used to keep track of each host we are pinging */
struct host_entry {
	int i;						/* index into array */
	char *name;					/* name as given by user */
	char *host;					/* text description of host */
	struct sockaddr_in saddr;	/* internet address */
	unsigned short **pr;		/* TCP port range to check for connectivity */
	struct timeval last_send_time;	/* time of last packet sent */
	unsigned int num_sent;		/* number of ping packets sent */
	unsigned int num_recv;		/* number of pings received */
	unsigned int total_time;	/* sum of response times */
	unsigned int status;		/* this hosts status */
	unsigned int running;		/* unset when through sending */
	unsigned int waiting;		/* waiting for response */
	int *resp_times;	/* individual response times */
	struct host_entry *prev, *next;	/* doubly linked list */
};

typedef struct host_entry HOST_ENTRY;

struct host_name_list {
	char *entry;
	struct host_name_list *next;
};

/* threshold structure */
struct threshold {
	unsigned int pl;	/* packet loss */
	unsigned int rta;	/* roundtrip time average */
};
typedef struct threshold threshold;

/*****************************************************************************
 *                             Global Variables                              *
 *****************************************************************************/

HOST_ENTRY *rrlist = NULL;		/* linked list of hosts be pinged */
HOST_ENTRY **table = NULL;		/* array of pointers to items in the list */
HOST_ENTRY *cursor;

char *prog;						/* our name */
int ident;						/* our pid, for marking icmp packets */
int sock;						/* socket */
u_int debug = 0;

/* threshold value defaults;
 * WARNING;  60% packetloss or 200 msecs round trip average
 * CRITICAL; 80% packetloss or 500 msecs round trip average */
threshold warn = {60, 200 * 1000};
threshold crit = {80, 500 * 1000};

/* times get *100 because all times are calculated in 10 usec units, not ms */
unsigned int retry = DEFAULT_RETRY;
u_int timeout = DEFAULT_TIMEOUT * 100;
u_int interval = DEFAULT_INTERVAL * 100;
float backoff = DEFAULT_BACKOFF_FACTOR;
u_int select_time;	/* calculated using maximum threshold rta value */
u_int ping_data_size = DEFAULT_PING_DATA_SIZE;
u_int ping_pkt_size;
unsigned int count = 5;
unsigned int trials = 1;

/* global stats */
int total_replies = 0;
int num_jobs = 0;				/* number of hosts still to do */
int num_hosts = 0;				/* total number of hosts */
int num_alive = 0;				/* total number alive */
int num_unreachable = 0;		/* total number unreachable */
int num_noaddress = 0;			/* total number of addresses not found */
int num_timeout = 0;			/* number of timed out packets */
int num_pingsent = 0;			/* total pings sent */
int num_pingreceived = 0;		/* total pings received */
int num_othericmprcvd = 0;		/* total non-echo-reply ICMP received */

struct timeval current_time;	/* current time (pseudo) */
struct timeval start_time;
struct timeval end_time;
struct timeval last_send_time;	/* time last ping was sent */
struct timezone tz;

/* switches */
int generate_flag = 0;			/* flag for IP list generation */
int stats_flag, unreachable_flag, alive_flag;
int elapsed_flag, version_flag, count_flag;
int name_flag, addr_flag, backoff_flag;
int multif_flag;

/*** prototypes ***/
void add_name(char *);
void add_addr(char *, char *, struct in_addr);
char *na_cat(char *, struct in_addr);
char *cpystr(char *);
void crash(char *);
char *get_host_by_address(struct in_addr);
int in_cksum(u_short *, int);
void u_sleep(int);
int recvfrom_wto(int, char *, int, struct sockaddr *, int);
void remove_job(HOST_ENTRY *);
void send_ping(int, HOST_ENTRY *);
long timeval_diff(struct timeval *, struct timeval *);
void usage(void);
int wait_for_reply(int);
void finish(void);
int handle_random_icmp(struct icmp *, struct sockaddr_in *);
char *sprint_tm(int);
int get_threshold(char *, threshold *);

/*** the various exit-states */
enum {
	STATE_OK = 0,
	STATE_WARNING,
	STATE_CRITICAL,
	STATE_UNKNOWN,
	STATE_DEPENDANT,
	STATE_OOB
};
/* the strings that correspond to them */
char *status_string[STATE_OOB] = {
	"OK",
	"WARNING",
	"CRITICAL",
	"UNKNOWN",
	"DEPENDANT"
};

int status = STATE_OK;
int fin_stat = STATE_OK;

/*****************************************************************************
 *                           Code block start                                *
 *****************************************************************************/
int main(int argc, char **argv)
{
	int c;
	u_int lt, ht;
	int advance;
	struct protoent *proto;
	uid_t uid;
	struct host_name_list *host_ptr, *host_base_ptr;

	if(strchr(argv[0], '/')) prog = strrchr(argv[0], '/') + 1;
	else prog = argv[0];

	/* check if we are root */
	if(geteuid()) {
		printf("Root access needed (for raw sockets)\n");
		exit(STATE_UNKNOWN);
	}

	/* confirm that ICMP is available on this machine */
	if((proto = getprotobyname("icmp")) == NULL)
		crash("icmp: unknown protocol");

	/* create raw socket for ICMP calls (ping) */
	sock = socket(AF_INET, SOCK_RAW, proto->p_proto);

	if(sock < 0)
		crash("can't create raw socket");

	/* drop privileges now that we have the socket */
	if((uid = getuid())) {
		seteuid(uid);
	}
	
	if(argc < 2) usage();

	ident = getpid() & 0xFFFF;

	if(!(host_base_ptr = malloc(sizeof(struct host_name_list)))) {
		crash("Unable to allocate memory for host name list\n");
	}
	host_ptr = host_base_ptr;

	backoff_flag = 0;
	opterr = 1;

	/* get command line options
	 * -H denotes a host (actually ignored and picked up later)
	 * -h for help
	 * -V or -v for version
	 * -d to display hostnames rather than addresses
	 * -t sets timeout for packets and tcp connects
	 * -r defines retries (persistence)
	 * -p or -n sets packet count (5)
	 * -b sets packet size (56)
	 * -w sets warning threshhold (200,40%)
	 * -c sets critical threshhold (500,80%)
	 * -i sets interval for both packet transmissions and connect attempts
	 */
#define OPT_STR "amH:hvVDdAp:n:b:r:t:i:w:c:"
	while((c = getopt(argc, argv, OPT_STR)) != EOF) {
		switch (c) {
			case 'H':
				if(!(host_ptr->entry = malloc(strlen(optarg) + 1))) {
					crash("Failed to allocate memory for hostname");
				}
				memset(host_ptr->entry, 0, strlen(optarg) + 1);
				host_ptr->entry = memcpy(host_ptr->entry, optarg, strlen(optarg));
				if(!(host_ptr->next = malloc(sizeof(struct host_name_list))))
					crash("Failed to allocate memory for hostname");
				host_ptr = host_ptr->next;
				host_ptr->next = NULL;
//				add_name(optarg);
				break;
				/* this is recognized, but silently ignored.
				 * host(s) are added later on */

				break;
			case 'w':
				if(get_threshold(optarg, &warn)) {
					printf("Illegal threshold pair specified for -%c", c);
					usage();
				}
				break;

			case 'c':
				if(get_threshold(optarg, &crit)) {
					printf("Illegal threshold pair specified for -%c", c);
					usage();
				}
				break;

			case 't':
				if(!(timeout = (u_int) strtoul(optarg, NULL, 0) * 100)) {
					printf("option -%c requires integer argument\n", c);
					usage();
				}
				break;

			case 'r':
				if(!(retry = (u_int) strtoul(optarg, NULL, 0))) {
					printf("option -%c requires integer argument\n", c);
					usage();
				}
				break;

			case 'i':
				if(!(interval = (u_int) strtoul(optarg, NULL, 0) * 100)) {
					printf("option -%c requires positive non-zero integer argument\n", c);
					usage();
				}
				break;

			case 'p':
			case 'n':
				if(!(count = (u_int) strtoul(optarg, NULL, 0))) {
					printf("option -%c requires positive non-zero integer argument\n", c);
					usage();
				}
				break;

			case 'b':
				if(!(ping_data_size = (u_int) strtoul(optarg, NULL, 0))) {
					printf("option -%c requires integer argument\n", c);
					usage();
				}
				break;

			case 'h':
				usage();
				break;

			case 'e':
				elapsed_flag = 1;
				break;
				
			case 'm':
				multif_flag = 1;
				break;
				
			case 'd':
				name_flag = 1;
				break;

			case 'A':
				addr_flag = 1;
				break;
				
			case 's':
				stats_flag = 1;
				break;

			case 'u':
				unreachable_flag = 1;
				break;

			case 'a':
				alive_flag = 1;
				break;

			case 'v':
				printf("%s: Version %s $Date$\n", prog, VERSION);
				printf("%s: comments to %s\n", prog, EMAIL);
				exit(STATE_OK);

			case 'g':
				/* use IP list generation */
				/* mutex with file input or command line targets */
				generate_flag = 1;
				break;

			default:
				printf("option flag -%c specified, but not recognized\n", c);
				usage();
				break;
		}
	}

	/* arguments are parsed, so now we validate them */

	if(count > 1) count_flag = 1;

	/* set threshold values to 10usec units (inherited from fping.c) */
	crit.rta = crit.rta / 10;
	warn.rta = warn.rta / 10;
	select_time = crit.rta;
	/* this isn't critical, but will most likely not be what the user expects
	 * so we tell him/her about it, but keep running anyways */
	if(warn.pl > crit.pl || warn.rta > crit.rta) {
		select_time = warn.rta;
		printf("(WARNING threshold > CRITICAL threshold) :: ");
		fflush(stdout);
	}

	/* A timeout smaller than maximum rta threshold makes no sense */
	if(timeout < crit.rta) timeout = crit.rta;
	else if(timeout < warn.rta) timeout = warn.rta;

	if((interval < MIN_INTERVAL * 100 || retry > MAX_RETRY) && getuid()) {
		printf("%s: these options are too risky for mere mortals.\n", prog);
		printf("%s: You need i >= %u and r < %u\n",
				prog, MIN_INTERVAL, MAX_RETRY);
		printf("Current settings; i = %d, r = %d\n",
			   interval / 100, retry);
		usage();
	}

	if((ping_data_size > MAX_PING_DATA) || (ping_data_size < MIN_PING_DATA)) {
		printf("%s: data size %u not valid, must be between %u and %u\n",
				prog, ping_data_size, MIN_PING_DATA, MAX_PING_DATA);
		usage();

	}

	if((backoff > MAX_BACKOFF_FACTOR) || (backoff < MIN_BACKOFF_FACTOR)) {
		printf("%s: backoff factor %.1f not valid, must be between %.1f and %.1f\n",
				prog, backoff, MIN_BACKOFF_FACTOR, MAX_BACKOFF_FACTOR);
		usage();

	}

	if(count > MAX_COUNT) {
		printf("%s: count %u not valid, must be less than %u\n",
				prog, count, MAX_COUNT);
		usage();
	}

	if(count_flag) {
		alive_flag = unreachable_flag = 0;
	}

	trials = (count > retry + 1) ? count : retry + 1;

	/* handle host names supplied on command line or in a file */
	/* if the generate_flag is on, then generate the IP list */
	argv = &argv[optind];

	/* cover allowable conditions */

	/* generate requires command line parameters beyond the switches */
	if(generate_flag && !*argv) {
		printf("generate flag requires command line parameters beyond switches\n");
		usage();
	}

	if(*argv && !generate_flag) {
		while(*argv) {
			if(!(host_ptr->entry = malloc(strlen(*argv) + 1))) {
				crash("Failed to allocate memory for hostname");
			}
			memset(host_ptr->entry, 0, strlen(*argv) + 1);
			host_ptr->entry = memcpy(host_ptr->entry, *argv, strlen(*argv));
			if(!(host_ptr->next = malloc(sizeof(struct host_name_list))))
				crash("Failed to allocate memory for hostname");
			host_ptr = host_ptr->next;
			host_ptr->next = NULL;

//			add_name(*argv);
			argv++;
		}
	}

	// now add all the hosts
	host_ptr = host_base_ptr;
	while(host_ptr->next) {
		add_name(host_ptr->entry);
		host_ptr = host_ptr->next;
	}

	if(!num_hosts) {
		printf("No hosts to work with!\n\n");
		usage();
	}

	/* allocate array to hold outstanding ping requests */
	table = (HOST_ENTRY **) malloc(sizeof(HOST_ENTRY *) * num_hosts);
	if(!table) crash("Can't malloc array of hosts");

	cursor = rrlist;

	for(num_jobs = 0; num_jobs < num_hosts; num_jobs++) {
		table[num_jobs] = cursor;
		cursor->i = num_jobs;

		cursor = cursor->next;
	}							/* FOR */

	ping_pkt_size = ping_data_size + SIZE_ICMP_HDR;

	signal(SIGINT, (void *)finish);

	gettimeofday(&start_time, &tz);
	current_time = start_time;

	last_send_time.tv_sec = current_time.tv_sec - 10000;

	cursor = rrlist;
	advance = 0;

	/* main loop */
	while(num_jobs) {
		/* fetch all packets that receive within time boundaries */
		while(num_pingsent &&
			  cursor &&
			  cursor->num_sent > cursor->num_recv &&
			  wait_for_reply(sock)) ;

		if(cursor && advance) {
			cursor = cursor->next;
		}

		gettimeofday(&current_time, &tz);
		lt = timeval_diff(&current_time, &last_send_time);
		ht = timeval_diff(&current_time, &cursor->last_send_time);

		advance = 1;

		/* if it's OK to send while counting or looping or starting */
		if(lt > interval) {
			/* send if starting or looping */
			if((cursor->num_sent == 0)) {
				send_ping(sock, cursor);
				continue;
			}					/* IF */

			/* send if counting and count not exceeded */
			if(count_flag) {
				if(cursor->num_sent < count) {
					send_ping(sock, cursor);
					continue;
				}				/* IF */
			}					/* IF */
		}						/* IF */

		/* is-it-alive mode, and timeout exceeded while waiting for a reply */
		/*   and we haven't exceeded our retries                            */
		if((lt > interval) && !count_flag && !cursor->num_recv &&
		   (ht > timeout) && (cursor->waiting < retry + 1)) {
			num_timeout++;

			/* try again */
			send_ping(sock, cursor);
			continue;
		}						/* IF */

		/* didn't send, can we remove? */

		/* remove if counting and count exceeded */
		if(count_flag) {
			if((cursor->num_sent >= count)) {
				remove_job(cursor);
				continue;
			}					/* IF */
		}						/* IF */
		else {
			/* normal mode, and we got one */
			if(cursor->num_recv) {
				remove_job(cursor);
				continue;
			}					/* IF */

			/* normal mode, and timeout exceeded while waiting for a reply */
			/* and we've run out of retries, so node is unreachable */
			if((ht > timeout) && (cursor->waiting >= retry + 1)) {
				num_timeout++;
				remove_job(cursor);
				continue;

			}					/* IF */
		}						/* ELSE */

		/* could send to this host, so keep considering it */
		if(ht > interval) {
			advance = 0;
		}
	}							/* WHILE */

	finish();
	return 0;
}								/* main() */

/************************************************************
 * Description:
 *
 * Main program clean up and exit point
 ************************************************************/
void finish()
{
	int i;
	HOST_ENTRY *h;

	gettimeofday(&end_time, &tz);

	/* tot up unreachables */
	for(i=0; i<num_hosts; i++) {
		h = table[i];

		if(!h->num_recv) {
			num_unreachable++;
			status = fin_stat = STATE_CRITICAL;
			if(num_hosts == 1) {
				printf("CRITICAL - %s is down (lost 100%%)|"
					   "rta=;%d;%d;; pl=100%%;%d;%d;;\n",
					   h->host,
					   warn.rta / 100, crit.rta / 100,
					   warn.pl, crit.pl);
			}
			else {
				printf("%s is down (lost 100%%)", h->host);
			}
		}
		else {
			/* reset the status */
			status = STATE_OK;

			/* check for warning before critical, for debugging purposes */
			if(warn.rta <= h->total_time / h->num_recv) {
/*				printf("warn.rta exceeded\n");
*/				status = STATE_WARNING;
			}
			if(warn.pl <= ((h->num_sent - h->num_recv) * 100) / h->num_sent) {
/*				printf("warn.pl exceeded (pl=%d)\n",
					   ((h->num_sent - h->num_recv) * 100) / h->num_sent);
*/				status = STATE_WARNING;
			}
			if(crit.rta <= h->total_time / h->num_recv) {
/*				printf("crit.rta exceeded\n");
*/				status = STATE_CRITICAL;
			}
			if(crit.pl <= ((h->num_sent - h->num_recv) * 100) / h->num_sent) {
/*				printf("crit.pl exceeded (pl=%d)\n",
					   ((h->num_sent - h->num_recv) * 100) / h->num_sent);
*/				status = STATE_CRITICAL;
			}

			if(num_hosts == 1 || status != STATE_OK) {
				printf("%s - %s: rta %s ms, lost %d%%",
					   status_string[status], h->host,
					   sprint_tm(h->total_time / h->num_recv),
					   h->num_sent > 0 ? ((h->num_sent - h->num_recv) * 100) / h->num_sent : 0
					   );
				/* perfdata only available for single-host stuff */
				if(num_hosts == 1) {
					printf("|rta=%sms;%d;%d;; pl=%d%%;%d;%d;;\n",
						   sprint_tm(h->total_time / h->num_recv), warn.rta / 100, crit.rta / 100,
						   h->num_sent > 0 ? ((h->num_sent - h->num_recv) * 100) / h->num_sent : 0, warn.pl, crit.pl
						   );
				}
				else printf(" :: ");
			}

			/* fin_stat should always hold the WORST state */
			if(fin_stat != STATE_CRITICAL && status != STATE_OK) {
				fin_stat = status;
			}
		}
	}

	if(num_noaddress) {
		printf("No hostaddress specified.\n");
		usage();
	}
	else if(num_alive != num_hosts) {
		/* for future multi-check support */
		/*printf("num_alive != num_hosts (%d : %d)\n", num_alive, num_hosts);*/
		fin_stat = STATE_CRITICAL;
	}

	if(num_hosts > 1) {
		if(num_alive == num_hosts) {
			printf("OK - All %d hosts are alive\n", num_hosts);
		}
		else {
			printf("CRITICAL - %d of %d hosts are alive\n", num_alive, num_hosts);
		}
	}
	exit(fin_stat);
}


void send_ping(int lsock, HOST_ENTRY *h)
{
	char *buffer;
	struct icmp *icp;
	PING_DATA *pdp;
	int n;

	buffer = (char *)malloc((size_t) ping_pkt_size);
	if(!buffer)
		crash("can't malloc ping packet");

	memset(buffer, 0, ping_pkt_size * sizeof(char));
	icp = (struct icmp *)buffer;

	gettimeofday(&h->last_send_time, &tz);

	icp->icmp_type = ICMP_ECHO;
	icp->icmp_code = 0;
	icp->icmp_cksum = 0;
	icp->icmp_seq = h->i;
	icp->icmp_id = ident;

	pdp = (PING_DATA *) (buffer + SIZE_ICMP_HDR);
	pdp->ping_ts = h->last_send_time;
	pdp->ping_count = h->num_sent;

	icp->icmp_cksum = in_cksum((u_short *) icp, ping_pkt_size);

	n = sendto(lsock, buffer, ping_pkt_size, 0,
			   (struct sockaddr *)&h->saddr, sizeof(struct sockaddr_in));

	if(n < 0 || (unsigned int)n != ping_pkt_size) {
		if(unreachable_flag) {
			printf("%s error while sending ping: %s\n",
				   h->host, strerror(errno));
		}			/* IF */

		num_unreachable++;
		remove_job(h);
	}				/* IF */
	else {
		/* mark this trial as outstanding */
		h->resp_times[h->num_sent] = RESP_WAITING;

		h->num_sent++;
		h->waiting++;
		num_pingsent++;
		last_send_time = h->last_send_time;
	}				/* ELSE */

	free(buffer);
}					/* send_ping() */

int wait_for_reply(int lsock)
{
	int result;
	static char buffer[4096];
	struct sockaddr_in response_addr;
	struct ip *ip;
	int hlen;
	struct icmp *icp;
	int n;
	HOST_ENTRY *h = NULL;
	long this_reply;
	int this_count;
	struct timeval sent_time;

	result = recvfrom_wto(lsock, buffer, sizeof(buffer),
						  (struct sockaddr *)&response_addr, select_time);

	if(result < 0) return 0;		/* timeout */

	ip = (struct ip *)buffer;

#if defined( __alpha__ ) && __STDC__ && !defined( __GLIBC__ )
	/* The alpha headers are decidedly broken.
	 * Using an ANSI compiler, it provides ip_vhl instead of ip_hl and
	 * ip_v.  So, to get ip_hl, we mask off the bottom four bits.
	 */
	hlen = (ip->ip_vhl & 0x0F) << 2;
#else
	hlen = ip->ip_hl << 2;
#endif /* defined(__alpha__) && __STDC__ */

	if(result < hlen + ICMP_MINLEN) {
		printf("received packet too short for ICMP (%d bytes from %s)\n", result,
			   inet_ntoa(response_addr.sin_addr));

		return (1);				/* too short */
	}							/* IF */

	icp = (struct icmp *)(buffer + hlen);
	if(icp->icmp_type != ICMP_ECHOREPLY) {
		/* handle some problem */
		if(handle_random_icmp(icp, &response_addr))
			num_othericmprcvd++;

		return 1;
	}							/* IF */

	if(icp->icmp_id != ident)
		return 1;				/* packet received, but not the one we are looking for! */

	num_pingreceived++;

	if(icp->icmp_seq >= (n_short) num_hosts)
		return(1);				/* packet received, don't worry about it anymore */

	n = icp->icmp_seq;
	h = table[n];
	
	/* received ping is cool, so process it */

	gettimeofday(&current_time, &tz);
	h->waiting = 0;
	h->num_recv++;

	memcpy(&sent_time, icp->icmp_data + offsetof(PING_DATA, ping_ts),
		   sizeof(sent_time));
	memcpy(&this_count, icp->icmp_data, sizeof(this_count));

	this_reply = timeval_diff(&current_time, &sent_time);
	h->total_time += this_reply;
	total_replies++;

	/* note reply time in array, probably */
	if((this_count >= 0) && ((unsigned int)this_count < trials)) {
		if(h->resp_times[this_count] != RESP_WAITING) {
			printf("%s : duplicate for [%d], %d bytes, %s ms",
				   h->host, this_count, result, sprint_tm(this_reply));

			if(response_addr.sin_addr.s_addr != h->saddr.sin_addr.s_addr)
				printf(" [<- %s]\n", inet_ntoa(response_addr.sin_addr));
		}					/* IF */
		else h->resp_times[this_count] = this_reply;
	}						/* IF */
	else {
		/* count is out of bounds?? */
		printf("%s : duplicate for [%d], %d bytes, %s ms\n",
			   h->host, this_count, result, sprint_tm(this_reply));
		}						/* ELSE */
	
	if(h->num_recv == 1) {
		num_alive++;
	}							/* IF */

	return num_jobs;
}								/* wait_for_reply() */

int handle_random_icmp(struct icmp *p, struct sockaddr_in *addr)
{
	struct icmp *sent_icmp;
	u_char *c;
	HOST_ENTRY *h;

	c = (u_char *) p;
	switch (p->icmp_type) {
	case ICMP_UNREACH:
		sent_icmp = (struct icmp *)(c + 28);

		if((sent_icmp->icmp_type == ICMP_ECHO) &&
		   (sent_icmp->icmp_id == ident) &&
		   (sent_icmp->icmp_seq < (n_short) num_hosts)) {
			/* this is a response to a ping we sent */
			h = table[sent_icmp->icmp_seq];

			if(p->icmp_code > ICMP_UNREACH_MAXTYPE) {
				printf("ICMP Unreachable (Invalid Code) from %s for ICMP Echo sent to %s",
						inet_ntoa(addr->sin_addr), h->host);

			}					/* IF */
			else {
				printf("ICMP Unreachable from %s for ICMP Echo sent to %s",
					   inet_ntoa(addr->sin_addr), h->host);

			}					/* ELSE */

			if(inet_addr(h->host) == INADDR_NONE)
				printf(" (%s)", inet_ntoa(h->saddr.sin_addr));

			printf("\n");

		}						/* IF */

		return 1;

	case ICMP_SOURCEQUENCH:
	case ICMP_REDIRECT:
	case ICMP_TIMXCEED:
	case ICMP_PARAMPROB:
		sent_icmp = (struct icmp *)(c + 28);
		if((sent_icmp->icmp_type = ICMP_ECHO) &&
		   (sent_icmp->icmp_id = ident) &&
		   (sent_icmp->icmp_seq < (n_short) num_hosts)) {
			/* this is a response to a ping we sent */
			h = table[sent_icmp->icmp_seq];
			printf("ICMP Unreachable from %s for ICMP Echo sent to %s",
					inet_ntoa(addr->sin_addr), h->host);

			if(inet_addr(h->host) == INADDR_NONE)
				printf(" (%s)", inet_ntoa(h->saddr.sin_addr));

			printf("\n");
		}						/* IF */

		return 2;

		/* no way to tell whether any of these are sent due to our ping */
		/* or not (shouldn't be, of course), so just discard            */
	case ICMP_TSTAMP:
	case ICMP_TSTAMPREPLY:
	case ICMP_IREQ:
	case ICMP_IREQREPLY:
	case ICMP_MASKREQ:
	case ICMP_MASKREPLY:
	default:
		return 0;

	}							/* SWITCH */

}								/* handle_random_icmp() */

int in_cksum(u_short * p, int n)
{
	register u_short answer;
	register long sum = 0;
	u_short odd_byte = 0;

	while(n > 1) {
		sum += *p++;
		n -= 2;
	}							/* WHILE */

	/* mop up an odd byte, if necessary */
	if(n == 1) {
		*(u_char *) (&odd_byte) = *(u_char *) p;
		sum += odd_byte;
	}							/* IF */

	sum = (sum >> 16) + (sum & 0xffff);	/* add hi 16 to low 16 */
	sum += (sum >> 16);			/* add carry */
	answer = ~sum;				/* ones-complement, truncate */

	return (answer);

}								/* in_cksum() */

void add_name(char *name)
{
	struct hostent *host_ent;
	int ipaddress;
	struct in_addr *ipa = (struct in_addr *)&ipaddress;
	struct in_addr *host_add;
	char *nm;
	int i = 0;
	
	if((ipaddress = inet_addr(name)) != -1) {
		/* input name is an IP addr, go with it */
		if(name_flag) {
			if(addr_flag)
				add_addr(name, na_cat(get_host_by_address(*ipa), *ipa), *ipa);
			else {
				nm = cpystr(get_host_by_address(*ipa));
				add_addr(name, nm, *ipa);

			}					/* ELSE */
		}						/* IF */
		else add_addr(name, name, *ipa);

		return;
	}							/* IF */

	/* input name is not an IP addr, maybe it's a host name */
	host_ent = gethostbyname(name);
	if(host_ent == NULL) {
		if(h_errno == TRY_AGAIN) {
			u_sleep(DNS_TIMEOUT);
			host_ent = gethostbyname(name);
		}						/* IF */

		if(host_ent == NULL) {
			printf("%s address not found\n", name);
			num_noaddress++;
			return;
		}						/* IF */
	}							/* IF */

	host_add = (struct in_addr *)*(host_ent->h_addr_list);
	if(host_add == NULL) {
		printf("%s has no address data\n", name);
		num_noaddress++;
		return;
	}							/* IF */
	else {
		/* it is indeed a hostname with a real address */
		while(host_add) {
			if(name_flag && addr_flag)
				add_addr(name, na_cat(name, *host_add), *host_add);
			else if(addr_flag) {
				nm = cpystr(inet_ntoa(*host_add));
				add_addr(name, nm, *host_add);
			}					/* ELSE IF */
			else {
				add_addr(name, name, *host_add);
			}

			if(!multif_flag) break;

			host_add = (struct in_addr *)(host_ent->h_addr_list[++i]);
		}						/* WHILE */
	}							/* ELSE */
}								/* add_name() */


char *na_cat(char *name, struct in_addr ipaddr)
{
	char *nm, *as;

	as = inet_ntoa(ipaddr);
	nm = (char *)malloc(strlen(name) + strlen(as) + 4);

	if(!nm)
		crash("can't allocate some space for a string");

	strcpy(nm, name);
	strcat(nm, " (");
	strcat(nm, as);
	strcat(nm, ")");

	return (nm);

}								/* na_cat() */


void add_addr(char *name, char *host, struct in_addr ipaddr)
{
	HOST_ENTRY *p;
	unsigned int n;
	int *i;

	if(!(p = (HOST_ENTRY *) malloc(sizeof(HOST_ENTRY)))) {
		crash("can't allocate HOST_ENTRY");
	}

	memset((char *)p, 0, sizeof(HOST_ENTRY));

	p->name = name;
	p->host = host;
	p->saddr.sin_family = AF_INET;
	p->saddr.sin_addr = ipaddr;
	p->running = 1;

	/* array for response time results */
	if(!(i = (int *)malloc(trials * sizeof(int)))) {
		crash("can't allocate resp_times array");
	}

	for(n = 1; n < trials; n++)
		i[n] = RESP_UNUSED;

		p->resp_times = i;

	if(!rrlist) {
		rrlist = p;
		p->next = p;
		p->prev = p;
	}							/* IF */
	else {
		p->next = rrlist;
		p->prev = rrlist->prev;
		p->prev->next = p;
		p->next->prev = p;
	}							/* ELSE */

	num_hosts++;
}								/* add_addr() */


void remove_job(HOST_ENTRY * h)
{
	h->running = 0;
	h->waiting = 0;
	num_jobs--;

	
	if(num_jobs) {
		/* remove us from list of active jobs */
		h->prev->next = h->next;
		h->next->prev = h->prev;
		if(h == cursor) cursor = h->next;
	}							/* IF */
	else {
		cursor = NULL;
		rrlist = NULL;
	}							/* ELSE */

}								/* remove_job() */


char *get_host_by_address(struct in_addr in)
{
	struct hostent *h;
	h = gethostbyaddr((char *)&in, sizeof(struct in_addr), AF_INET);

	if(h == NULL || h->h_name == NULL)
		return inet_ntoa(in);
	else
		return (char *)h->h_name;

}								/* get_host_by_address() */


char *cpystr(char *string)
{
	char *dst;

	if(string) {
		dst = (char *)malloc(1 + strlen(string));
		if(!dst) crash("malloc() failed!");

		strcpy(dst, string);
		return dst;

	}							/* IF */
	else return NULL;

}								/* cpystr() */


void crash(char *msg)
{
	if(errno || h_errno) {
		if(errno)
			printf("%s: %s : %s\n", prog, msg, strerror(errno));
		if(h_errno)
			printf("%s: %s : A network error occurred\n", prog, msg);
	}
	else printf("%s: %s\n", prog, msg);

	exit(STATE_UNKNOWN);
}								/* crash() */


long timeval_diff(struct timeval *a, struct timeval *b)
{
	double temp;

	temp = (((a->tv_sec * 1000000) + a->tv_usec) -
			((b->tv_sec * 1000000) + b->tv_usec)) / 10;

	return (long)temp;

}								/* timeval_diff() */


char *sprint_tm(int t)
{
	static char buf[10];

	/* <= 0.99 ms */
	if(t < 100) {
		sprintf(buf, "0.%02d", t);
		return (buf);
	}							/* IF */

	/* 1.00 - 9.99 ms */
	if(t < 1000) {
		sprintf(buf, "%d.%02d", t / 100, t % 100);
		return (buf);
	}							/* IF */

	/* 10.0 - 99.9 ms */
	if(t < 10000) {
		sprintf(buf, "%d.%d", t / 100, (t % 100) / 10);
		return (buf);
	}							/* IF */

	/* >= 100 ms */
	sprintf(buf, "%d", t / 100);
	return (buf);
}								/* sprint_tm() */


/*
 * select() is posix, so we expect it to be around
 */
void u_sleep(int u_sec)
{
	int nfound;
	struct timeval to;
	fd_set readset, writeset;
	
	to.tv_sec = u_sec / 1000000;
	to.tv_usec = u_sec - (to.tv_sec * 1000000);
/*	printf("u_sleep :: to.tv_sec: %d, to_tv_usec: %d\n",
		   (int)to.tv_sec, (int)to.tv_usec);
*/	
	FD_ZERO(&writeset);
	FD_ZERO(&readset);
	nfound = select(0, &readset, &writeset, NULL, &to);
	if(nfound < 0)
		crash("select() in u_sleep:");

	return;
}								/* u_sleep() */


/************************************************************
 * Description:
 *
 * receive with timeout
 * returns length of data read or -1 if timeout
 * crash on any other errrors
 ************************************************************/
/* TODO: add MSG_DONTWAIT to recvfrom flags (currently 0) */
int recvfrom_wto(int lsock, char *buf, int len, struct sockaddr *saddr, int timo)
{
	int nfound = 0, slen, n;
	struct timeval to;
	fd_set readset, writeset;

	to.tv_sec = timo / 1000000;
	to.tv_usec = (timo - (to.tv_sec * 1000000)) * 10;

/*	printf("to.tv_sec: %d, to.tv_usec: %d\n", (int)to.tv_sec, (int)to.tv_usec);
*/

	FD_ZERO(&readset);
	FD_ZERO(&writeset);
	FD_SET(lsock, &readset);
	nfound = select(lsock + 1, &readset, &writeset, NULL, &to);
	if(nfound < 0) crash("select() in recvfrom_wto");

	if(nfound == 0) return -1;				/* timeout */

	if(nfound) {
		slen = sizeof(struct sockaddr);
		n = recvfrom(sock, buf, len, 0, saddr, &slen);
		if(n < 0) crash("recvfrom");
		return(n);
	}

	return(0);	/* 0 bytes read, so return it */
}								/* recvfrom_wto() */


/*
 * u = micro
 * m = milli
 * s = seconds
 */
int get_threshold(char *str, threshold *th)
{
	unsigned int i, factor = 0;
	char *p = NULL;

	if(!str || !strlen(str) || !th) return -1;

	for(i=0; i<strlen(str); i++) {
		/* we happily accept decimal points in round trip time thresholds,
		 * but we ignore them quite blandly. The new way of specifying higher
		 * precision is to specify 'u' (for microseconds),
		 * 'm' (for millisecs - default) or 's' for seconds. */
		if(!p && !factor) {
			if(str[i] == 's') factor = 1000000;		/* seconds */
			else if(str[i] == 'm') factor = 1000;	/* milliseconds */
			else if(str[i] == 'u') factor = 1;		/* microseconds */
		}

		if(str[i] == '%') str[i] = '\0';
		else if(str[i] == ',' && !p && i != (strlen(str) - 1)) {
			p = &str[i+1];
			str[i] = '\0';
		}
	}

	/* default to milliseconds */
	if(!factor) factor = 1000;

	if(!p || !strlen(p)) return -1;
	th->rta = (unsigned int)strtoul(str, NULL, 0) * factor;
	th->pl = (unsigned int)strtoul(p, NULL, 0);
	return 0;
}

/* make a blahblah */
void usage(void)
{
	printf("\nUsage: %s [options] [targets]\n", prog);
	printf("  -H host  target host\n");
	printf("  -b n     ping packet size in bytes (default %d)\n", ping_data_size);
	printf("  -n|p n   number of pings to send to each target (default %d)\n", count);
	printf("  -r n     number of retries (default %d)\n", retry);
	printf("  -t n     timeout value (in msec) (default %d)\n", timeout / 100);
	printf("  -i n     packet interval (in msec) (default %d)\n", DEFAULT_INTERVAL);
/* XXX - possibly on todo-list
	printf("  -m       ping multiple interfaces on target host\n");
	printf("  -a       show targets that are alive\n");
	printf("  -d       show dead targets\n");
*/	printf("  -v       show version\n");
	printf("  -D       increase debug output level\n");
	printf("  -w       warning threshold pair, given as RTA[ums],PL[%%]\n");
	printf("  -c       critical threshold pair, given as RTA[ums],PL[%%]\n");
	printf("\n");
	printf("Note:\n");
	printf("* This program requires root privileges to run properly.\n");
	printf("  If it is run as setuid root it will halt with an error if;\n");
	printf("    interval < 25 || retries > 5\n\n");
	printf("* Threshold pairs are given as such;\n");
	printf("    100,40%%\n");
	printf("  to set a threshold value pair of 100 milliseconds and 40%% packetloss\n");
	printf("  The '%%' sign is optional, and if rta value is suffixed by;\n");
	printf("    s, rta time is set in seconds\n");
	printf("    m, rta time will be set in milliseconds (this is default)\n");
	printf("    u, rta time will be set in microseconds\n");
	printf("  Decimal points are silently ignored for sideways compatibility.\n");
	printf("\n");
	exit(3);
}								/* usage() */
