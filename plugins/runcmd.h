/*
 * $Id$
 * 
 * Author: Andreas Ericsson <ae@op5.se>
 *
 * Copyright: GNU GPL v2 or any later version.
 * 
 */

#ifndef NAGIOSPLUG_RUNCMD_H
#define NAGIOSPLUG_RUNCMD_H

#include "common.h"

/** types **/
struct output {
	char *buf;     /* output buffer */
	size_t buflen; /* output buffer content length */
	char **line;   /* array of lines (points to buf) */
	size_t *lens;  /* string lengths */
	size_t lines;  /* lines of output */
};

typedef struct output output;

/** prototypes **/
int np_runcmd(const char *, output *, output *, int);
void popen_timeout_alarm_handler(int)
	__attribute__((__noreturn__));

/* only multi-threaded plugins need to bother with this */
void np_runcmd_init(void);
#define NP_RUNCMD_INIT np_runcmd_init()

/* possible flags for np_runcmd()'s fourth argument */
#define RUNCMD_NO_ARRAYS 0x01 /* don't populate arrays at all */
#define RUNCMD_NO_ASSOC 0x02  /* output.line won't point to buf */

#endif /* NAGIOSPLUG_RUNCMD_H */
