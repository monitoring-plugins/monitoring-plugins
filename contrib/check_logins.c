/*=================================
 *  check_logins - Nagios plugin
 *  Copyright (C) 2003  Dag Robøle
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Authors email: drobole@broadpark.no
 */
//=================================
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "config.h"
#include "common.h"
#include "utils.h"
#include "popen.h"
//=================================
#define REVISION		"$Revision$"
#define COPYRIGHT		"2003"
#define AUTHOR			"Dag Robole"
#define EMAIL 			"drobole@broadpark.no"
#define SUMMARY			"Check for multiple user logins"

#define check(func, errmsg)	{ if((func) == -1) die(STATE_UNKNOWN, errmsg); }
#define checkz(func, errmsg)	{ if(!(func)) die(STATE_UNKNOWN, errmsg); }
//=================================
typedef struct USERNODE_TYP {
	char *name;
	char *host;
	struct USERNODE_TYP* next;
} USERNODE;
//=================================
char *progname = NULL;
USERNODE *userlist = NULL, *adminlist = NULL;
int warning_limit = 0, critical_limit = 0;

void print_usage();
void print_help();
void process_arguments(int argc, char* *argv);
void parse_wholine(const char *line, char *name, char *host);
void node_create(USERNODE* *node, const char *name, const char *host);
void node_free(USERNODE* *node);
USERNODE* list_insert_sort_uniq(USERNODE* *list, USERNODE* *node);
void list_free(USERNODE* *list);
void cleanup();
//=================================
int main(int argc, char* *argv)
{
	FILE *who;
	USERNODE *newnode, *nptra, *nptrb;
	char buffer[BUFSIZ], username[BUFSIZ], hostname[BUFSIZ], *cptra, *cptrb;
	char max_login_name[BUFSIZ], currname[BUFSIZ];
	int max_login_count = 0, counter = 0, skip;
	void (*old_sig_alrm)();

	progname = argv[0];
	if(atexit(cleanup))
		die(STATE_UNKNOWN, "atexit failed\n");
	
	if((old_sig_alrm = signal((int)SIGALRM, timeout_alarm_handler)) == SIG_ERR)
		die(STATE_UNKNOWN, "signal failed\n");
	alarm(timeout_interval);

	process_arguments(argc, argv);
	
	checkz(who = spopen(PATH_TO_WHO), "spopen failed\n");
	
	while(fgets(buffer, sizeof(buffer), who) != NULL) {	
		parse_wholine(buffer, username, hostname);
		skip = 0;
		nptra = adminlist;
		
		while(nptra != NULL) {
			if(!strcmp(nptra->name, username)) {
				skip = 1;
				break;
			}
			nptra = nptra->next;
		}		
		if(!skip) {
			node_create(&newnode, username, hostname);
			if(!list_insert_sort_uniq(&userlist, &newnode))
				node_free(&newnode);
		}
	}
	
	check(spclose(who), "spclose failed\n");

	if(userlist != NULL) {
		nptra = userlist;
		strcpy(currname, nptra->name);
		strcpy(max_login_name, nptra->name);
		max_login_count = 1;
		while(nptra != NULL) {
			if(!strcmp(currname, nptra->name))
				++counter;
			else {
				if(counter > max_login_count) {
					max_login_count = counter;
					strcpy(max_login_name, currname);
				}
				strcpy(currname, nptra->name);
				counter = 1;
			}
			nptra = nptra->next;
		}
		
		if(counter > max_login_count) {
			max_login_count = counter;
			strcpy(max_login_name, currname);
		}
	}

	if(signal((int)SIGALRM, old_sig_alrm) == SIG_ERR)
		die(STATE_UNKNOWN, "signal failed\n");
		
	if(max_login_count) {
		if(critical_limit && max_login_count >= critical_limit) {
			printf("CRITICAL - User %s has logged in from %d different hosts\n", max_login_name, max_login_count);
			return STATE_CRITICAL;
		}
		else if(warning_limit && max_login_count >= warning_limit) {
			printf("WARNING - User %s has logged in from %d different hosts\n", max_login_name, max_login_count);
			return STATE_WARNING;
		}	
	}

	printf("OK - No users has exceeded the login limits\n");	
	return STATE_OK;
}
//=================================
void print_usage()
{
	fprintf(stderr, "Usage: %s [ -hV ] [ -w limit ] [ -c limit ] [ -u username1, ... ,usernameN ]\n", progname);
}
//=================================
void print_help()
{
	print_revision(progname, REVISION);
	printf("Copyright (c) %s %s <%s>\n\n%s\n\n", COPYRIGHT, AUTHOR, EMAIL, SUMMARY);
	print_usage();
	printf("\nDescription:\n"
	       "\tThis plugin supports the w (warning) and c (critical) options indicating the upper limits\n"
	       "\tof logins allowed before a warning is given.\n"
	       "\tThe output from %s is the username and number of login sessions for the user\n"
	       "\twho has the most login sessions (from different hosts) running at a given point in time.\n"
	       "\tThe u (users) option takes a comma separated list of usernames that will be ignored\n"
	       "\twhile scannig users.\n"
	       "\nOptions:\n"
	       "\t-h | --help\n\t\tShow this help message and exit\n"
	       "\t-V | --version\n\t\tShow version description\n"
	       "\t-w | --warning=INTEGER\n\t\tSet warning limit for logins (minimum value is 2)\n"
	       "\t-c | --critical=INTEGER\n\t\tSet critical limit for logins (minimum value is 2)\n"
	       "\t-u | --users=STRING\n\t\tSet usernames to be ignored\n"
	       "\nExamples:\n\t%s -w 3 -c 5\n"
	       "\t%s -w 3 -c 5 -u root,guest,jcarmack\n\n", progname, progname, progname);
}
//=================================
void process_arguments(int argc, char* *argv)
{	
	USERNODE *newnode;
	int optch;
	char buffer[BUFSIZ], *cptra;
	static struct option long_opts[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"users", required_argument, 0, 'u'},
		{0, 0, 0, 0},
	};
	
	while((optch = getopt_long(argc, argv, "hVw:c:u:", long_opts, NULL)) != -1) {
		switch(optch) {
			case 'h':
				print_help();
				exit(STATE_OK);
				break;
			case 'V':
				print_revision(progname, REVISION);
				exit(STATE_OK);
				break;
			case 'w':
				if(!is_numeric(optarg)) {
					print_usage();
					die(STATE_UNKNOWN, "invalid options\n");
				}
				warning_limit = atoi(optarg) > 2 ? atoi(optarg) : 2;
				break;
			case 'c':
				if(!is_numeric(optarg)) {
					print_usage();
					die(STATE_UNKNOWN, "invalid options\n");
				}
				critical_limit = atoi(optarg) > 2 ? atoi(optarg) : 2;
				break;
			case 'u':
				strcpy(buffer, optarg);
				cptra = strtok(buffer, ",");
				while(cptra != NULL) {
					node_create(&newnode, cptra, "(adminhost)");
					list_insert_sort_uniq(&adminlist, &newnode);
					cptra = strtok(NULL, ",");
				}
				break;
			default:
				print_usage();
				exit(STATE_UNKNOWN);
				break;
		}
	}

	if(argc > optind) {
		print_usage();
		die(STATE_UNKNOWN, "invalid options\n");
	}

	if(!warning_limit && !critical_limit) {
		print_usage();
		die(STATE_UNKNOWN, "you must provide a limit for this plugin\n");
	}
	
	if(critical_limit && warning_limit > critical_limit) {
		print_usage();
		die(STATE_UNKNOWN, "warning limit must be less or equal critical limit\n");
	}
}
//=================================
void parse_wholine(const char *line, char *name, char *host)
{	
	char buffer[BUFSIZ], *cptra, *cptrb, *display;
	strcpy(buffer, line);

	cptra = buffer;
	checkz(cptrb = (char*)strchr(buffer, ' '), "strchr failed\n");
	strncpy(name, cptra, cptrb-cptra);
	name[cptrb-cptra] = '\0';

	if((cptra = strchr(buffer, '(')) != NULL) // hostname found in source arg...
	{
		if(cptra[1] == ':') // local host
			strcpy(host, "(localhost)");
		else // extern host
		{
			checkz(cptrb = strchr(cptra, ')'), "strchr failed\n");
			cptrb++;
			strncpy(host, cptra, cptrb-cptra);
			host[cptrb-cptra] = '\0';
		}
	}
	else // no hostname in source arg, look in line arg...
	{
		checkz(cptra = strtok(buffer, " \t\r\n"), "strtok failed\n"); 
		checkz(cptra = strtok(NULL, " \t\r\n"), "strtok failed\n");
		if(cptra[0] == ':') // local host
			strcpy(host, "(localhost)");
		else // extern host
			sprintf(host, "(%s)", cptra);
	}
	
	if((cptra = strchr(host, ':')) != NULL) // remove display if any...
		strcpy(cptra, ")");
}
//================================= 
void node_create(USERNODE* *node, const char *name, const char *host)
{	
	checkz(*node = (USERNODE*)malloc(sizeof(USERNODE)), "malloc failed\n");
	checkz((*node)->name = (char*)malloc(strlen(name)+1), "malloc failed\n");
	checkz((*node)->host = (char*)malloc(strlen(host)+1), "malloc failed\n");
	(*node)->next = NULL;
	strcpy((*node)->name, name);
	strcpy((*node)->host, host);
}
//================================= 
void node_free(USERNODE* *node)
{	
	free((*node)->name);
	free((*node)->host);
	free(*node);
	*node = NULL;
}
//================================= 
USERNODE* list_insert_sort_uniq(USERNODE* *list, USERNODE* *node)
{
	char n1[BUFSIZ], n2[BUFSIZ];
	USERNODE *last_nptr = NULL, *nptr = *list;
	
	if(*list == NULL)
		return(*list = *node);
	else {
		sprintf(n1, "%s %s", (*node)->name, (*node)->host);
		while(nptr != NULL) {
			sprintf(n2, "%s %s", nptr->name, nptr->host);
			if(!strcmp(n1, n2))
				return NULL;
			else if(strcmp(n1, n2) < 0) {
				if(last_nptr) {
					last_nptr->next = *node;
					(*node)->next = nptr;
				}
				else {
					(*node)->next = *list;
					*list = *node;
				}
				break;
			}
			else {
				last_nptr = nptr;
				nptr = nptr->next;
			}
		}
		if(nptr == NULL)
			last_nptr->next = *node;
	}
	return *node;
}
//================================= 
void list_free(USERNODE* *list)
{	
	USERNODE *doe, *nptr = *list;
	while(nptr != NULL) {
		doe = nptr;
		nptr = nptr->next;
		node_free(&doe);
	}
	*list = NULL;
}
//=================================
void cleanup()
{
	list_free(&userlist);
	list_free(&adminlist);
}
//=================================
