/******************************************************************************
 *
 * Nagios plugins common include file
 *
 * License: GPL
 * Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
 *
 * Last Modified: 11-05-1999
 *
 * Description:
 *
 * This file contains common include files and defines used in many of
 * the plugins.
 *
 * License Information:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/

#include "config.h"

#ifdef HAVE_FEATURES_H
#include <features.h>
#endif

#include <stdio.h>							/* obligatory includes */
#include <stdlib.h>
#include <errno.h>

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

/* TODO: define can be removed when all ifdef in each plugin has been removed */
#define HAVE_GETOPT_H
#include <getopt.h>

#include <ctype.h>

#if HAVE_LWRES_NETDB_H
#include <lwres/netdb.h>
#else
# if !HAVE_GETADDRINFO
#  include "getaddrinfo.h"
# else
#  include <netdb.h>
# endif
#endif

/*
 *
 * Missing Functions
 *
 */

#ifndef HAVE_STRTOL
# define strtol(a,b,c) atol((a))
#endif

#ifndef HAVE_STRTOUL
# define strtoul(a,b,c) (unsigned long)atol((a))
#endif

#ifndef HAVE_ASPRINTF
int asprintf(char **strp, const char *fmt, ...);
#endif

#ifndef HAVE_VASPRINTF
/* int vasprintf(char **strp, const char *fmt, va_list ap); */
#endif

#ifndef HAVE_SNPRINTF
int snprintf(char *str, size_t size, const  char  *format, ...);
#endif

#ifndef HAVE_VSNPRINTF
int vsnprintf(char *str, size_t size, const char  *format, va_list ap);
#endif

/*
 *
 * Standard Values
 *
 */

enum {
	OK = 0,
	ERROR = -1
};

enum {
	FALSE,
	TRUE
};

enum {
	STATE_OK,
	STATE_WARNING,
	STATE_CRITICAL,
	STATE_UNKNOWN,
	STATE_DEPENDENT
};

enum {
	DEFAULT_SOCKET_TIMEOUT = 10,	 /* timeout after 10 seconds */
	MAX_INPUT_BUFFER = 1024,	     /* max size of most buffers we use */
	MAX_HOST_ADDRESS_LENGTH = 256	 /* max size of a host address */
};

/*
 *
 * Internationalization
 *
 */

#if ENABLE_NLS
#  include "gettext.h"
#  define _(String) gettext (String)
#  define S_(String) gettext (String)
#  define gettext_noop(String) String
#  define N_(String) gettext_noop String
#else
#  define _(String) (String)
#  define S_(String) (String)
#  define N_(String) String
#  define textdomain(Domain)
#  define bindtextdomain(Package, Directory)
#endif
