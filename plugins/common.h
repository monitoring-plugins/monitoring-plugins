/*****************************************************************************
* 
* Nagios plugins common include file
* 
* License: GPL
* Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
* Copyright (c) 2003-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains common include files and defines used in many of
* the plugins.
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

#ifndef _COMMON_H_
#define _COMMON_H_

#include "config.h"

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

#ifdef _AIX
#ifdef HAVE_MP_H
#include <mp.h>
#endif
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

/* GET_NUMBER_OF_CPUS is a macro to return 
   number of CPUs, if we can get that data.
   Use configure.in to test for various OS ways of
   getting that data
   Will return -1 if cannot get data
*/
#if defined(HAVE_SYSCONF__SC_NPROCESSORS_ONLN)
# define GET_NUMBER_OF_CPUS() sysconf(_SC_NPROCESSORS_ONLN)
#elif defined (HAVE_SYSCONF__SC_NPROCESSORS_CONF)
# define GET_NUMBER_OF_CPUS() sysconf(_SC_NPROCESSORS_CONF)
#else
# define GET_NUMBER_OF_CPUS() -1
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

/* GNU Libraries */
#include <getopt.h>
#include "dirname.h"

#include <locale.h>

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

/* SSL implementations */
#ifdef HAVE_GNUTLS_OPENSSL_H
#  include <gnutls/openssl.h>
#else
#  define OPENSSL_LOAD_CONF /* See the OPENSSL_config(3) man page. */
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

enum {
	STATE_OK,
	STATE_WARNING,
	STATE_CRITICAL,
	STATE_UNKNOWN,
	STATE_DEPENDENT
};

enum {
	DEFAULT_SOCKET_TIMEOUT = 10,	 /* timeout after 10 seconds */
	MAX_INPUT_BUFFER = 8192,	     /* max size of most buffers we use */
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
