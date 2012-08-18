/****************************************************************************
* 
* License: GPL
* Copyright (c) 2005 Nagios Plugins Development Team
* Author: Andreas Ericsson <ae@op5.se>
* 
* 
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
* 
* 
*****************************************************************************/

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
void runcmd_timeout_alarm_handler(int)
	__attribute__((__noreturn__));

/* only multi-threaded plugins need to bother with this */
void np_runcmd_init(void);
#define NP_RUNCMD_INIT np_runcmd_init()

/* possible flags for np_runcmd()'s fourth argument */
#define RUNCMD_NO_ARRAYS 0x01 /* don't populate arrays at all */
#define RUNCMD_NO_ASSOC 0x02  /* output.line won't point to buf */

#endif /* NAGIOSPLUG_RUNCMD_H */
