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
 * $Id$
 *
 *****************************************************************************/

#ifndef _COMMON_H_
#define _COMMON_H_

#include "config.h"
/* This needs to be removed for Solaris servers, where 64 bit files, but 32 bit architecture
   This needs to be done early on because subsequent system includes use _FILE_OFFSET_BITS
   Cannot remove from config.h because is included by regex.c from lib/ */
#if __sun__ && !defined(_LP64) && _FILE_OFFSET_BITS == 64
#undef _FILE_OFFSET_BITS
#endif

#ifdef HAVE_FEATURES_H
#include <features.h>
#endif

#include <stdio.h>							/* obligatory includes */
#include <stdlib.h>
#include <errno.h>

/* This block provides uintmax_t - should be reported to coreutils that this should be added to fsuage.h */
#if HAVE_INTTYPES_H
# include <inttypes.h>
#endif
#if HAVE_STDINT_H
# include <stdint.h>
#endif
#include <unistd.h>
#ifndef UINTMAX_MAX
# define UINTMAX_MAX ((uintmax_t) -1)
#endif

#include <limits.h>	/* This is assumed true, because coreutils assume it too */

#ifdef HAVE_MATH_H
#include <math.h>
#endif

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

#include <getopt.h>
#include <ctype.h>

#ifdef HAVE_LWRES_NETDB_H
#include <lwres/netdb.h>
#else
# if !HAVE_GETADDRINFO
#  include "getaddrinfo.h"
# else
#  include <netdb.h>
# endif
#endif

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#ifdef HAVE_SYS_POLL_H
# include "sys/poll.h"
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

/* SSL implementations */
#ifdef HAVE_GNUTLS_OPENSSL_H
#  include <gnutls/openssl.h>
#else
#  ifdef HAVE_SSL_H
#    include <rsa.h>
#    include <crypto.h>
#    include <x509.h>
#    include <pem.h>
#    include <ssl.h>
#    include <err.h>
#  else
#    ifdef HAVE_OPENSSL_SSL_H
#      include <openssl/rsa.h>
#      include <openssl/crypto.h>
#      include <openssl/x509.h>
#      include <openssl/pem.h>
#      include <openssl/ssl.h>
#      include <openssl/err.h>
#    endif
#  endif
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

/* AIX seems to have this defined somewhere else */
#ifndef FALSE
enum {
	FALSE,
	TRUE
};
#endif

/* Solaris does not have floorf, but floor works. Should probably be in configure */
#if defined(__sun) || defined(__sun__)
static inline float floorf (float x) { return floor(x); }
#endif

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
#include "gettext.h"
#define _(String) gettext (String)
#if ! ENABLE_NLS
# undef textdomain
# define textdomain(Domainname) /* empty */
# undef bindtextdomain
# define bindtextdomain(Domainname, Dirname) /* empty */
#endif

/* For non-GNU compilers to ignore __attribute__ */
#ifndef __GNUC__
# define __attribute__(x) /* do nothing */
#endif

#endif /* _COMMON_H_ */
