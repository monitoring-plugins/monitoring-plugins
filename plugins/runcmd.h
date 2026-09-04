#pragma once
/****************************************************************************
 *
 * License: GPL
 * Copyright (c) 2005 Monitoring Plugins Development Team
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

#include "utils_cmd.h" /* for the "output" type */

/** prototypes **/
int mopl_utils_runcmd(const char *cmd, output *out, output *err, int flags);
void runcmd_timeout_alarm_handler(int) __attribute__((__noreturn__));

/* only multi-threaded plugins need to bother with this */
void mopl_utils_runcmd_init(void);

/* possible flags for np_runcmd()'s fourth argument */
#define MOPL_RUNCMD_NO_ARRAYS 0x01 /* don't populate arrays at all */
#define MOPL_RUNCMD_NO_ASSOC  0x02 /* output.line won't point to buf */
