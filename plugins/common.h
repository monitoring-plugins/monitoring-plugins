/*****************************************************************************
 *
 * Monitoring Plugins common include file
 *
 * License: GPL
 * Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
 * Copyright (c) 2003-2007 Monitoring Plugins Development Team
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

#include "../config.h"
#include "../lib/monitoringplug.h"

#ifdef HAVE_FEATURES_H
#	include <features.h>
#endif

#include <stdio.h> /* obligatory includes */
#include <stdlib.h>
#include <errno.h>

/* This block provides uintmax_t - should be reported to coreutils that this should be added to
 * fsuage.h */
#if HAVE_INTTYPES_H
#	include <inttypes.h>
#endif
#if HAVE_STDINT_H
#	include <stdint.h>
#endif
#include <unistd.h>
#ifndef UINTMAX_MAX
#	define UINTMAX_MAX ((uintmax_t) - 1)
#endif

#include <limits.h> /* This is assumed true, because coreutils assume it too */

#ifdef HAVE_MATH_H
#	include <math.h>
#endif

#ifdef _AIX
#	ifdef HAVE_MP_H
#		include <mp.h>
#	endif
#endif

#ifdef HAVE_STRINGS_H
#	include <strings.h>
#endif
#ifdef HAVE_STRING_H
#	include <string.h>
#endif

#ifdef HAVE_UNISTD_H
#	include <unistd.h>
#endif

/* GET_NUMBER_OF_CPUS is a macro to return
   number of CPUs, if we can get that data.
   Use configure.in to test for various OS ways of
   getting that data
   Will return -1 if cannot get data
*/
#if defined(HAVE_SYSCONF__SC_NPROCESSORS_ONLN)
#	define GET_NUMBER_OF_CPUS() sysconf(_SC_NPROCESSORS_ONLN)
#elif defined(HAVE_SYSCONF__SC_NPROCESSORS_CONF)
#	define GET_NUMBER_OF_CPUS() sysconf(_SC_NPROCESSORS_CONF)
#else
#	define GET_NUMBER_OF_CPUS() -1
#endif

#ifdef HAVE_SYS_TIME_H
#	include <sys/time.h>
#endif
#include <time.h>

#ifdef HAVE_SYS_TYPES_H
#	include <sys/types.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#	include <sys/socket.h>
#endif

#ifdef HAVE_SIGNAL_H
#	include <signal.h>
#endif

/* GNU Libraries */
#include <getopt.h>
#include "../gl/dirname.h"

#include <locale.h>

#ifdef HAVE_SYS_POLL_H
#	include "sys/poll.h"
#endif

/*
 *
 * Missing Functions
 *
 */

#ifndef HAVE_STRTOL
#	define strtol(a, b, c) atol((a))
#endif

#ifndef HAVE_STRTOUL
#	define strtoul(a, b, c) (unsigned long)atol((a))
#endif

/* SSL implementations */
#ifdef HAVE_GNUTLS_OPENSSL_H
#	include <gnutls/openssl.h>
#else
#	define OPENSSL_LOAD_CONF /* See the OPENSSL_config(3) man page. */
#	ifdef HAVE_SSL_H
#		include <rsa.h>
#		include <crypto.h>
#		include <x509.h>
#		include <pem.h>
#		include <ssl.h>
#		include <err.h>
#	else
#		ifdef HAVE_OPENSSL_SSL_H
#			include <openssl/rsa.h>
#			include <openssl/crypto.h>
#			include <openssl/x509.h>
#			include <openssl/pem.h>
#			include <openssl/ssl.h>
#			include <openssl/err.h>
#		endif
#	endif
#endif

/* openssl 1.1 does not set OPENSSL_NO_SSL2 by default but ships without ssl2 */
#ifdef OPENSSL_VERSION_NUMBER
#	if OPENSSL_VERSION_NUMBER >= 0x10100000
#		define OPENSSL_NO_SSL2
#	endif
#endif

/*
 *
 * Standard Values
 *
 */

/* MariaDB 10.2 client does not set MYSQL_PORT */
#ifndef MYSQL_PORT
#	define MYSQL_PORT 3306
#endif

enum {
	OK = 0,
	ERROR = -1
};

enum {
	DEFAULT_SOCKET_TIMEOUT = 10,  /* timeout after 10 seconds */
	MAX_INPUT_BUFFER = 8192,      /* max size of most buffers we use */
	MAX_HOST_ADDRESS_LENGTH = 256 /* max size of a host address */
};

/*
 *
 * Internationalization
 *
 */
#include "../gl/gettext.h"
#define _(String) gettext(String)
#if !defined(ENABLE_NLS) || !ENABLE_NLS
#	undef textdomain
#	define textdomain(Domainname) /* empty */
#	undef bindtextdomain
#	define bindtextdomain(Domainname, Dirname) /* empty */
#endif

/* For non-GNU compilers to ignore __attribute__ */
#ifndef __GNUC__
#	define __attribute__(x) /* do nothing */
#endif

/* for checking the result of getopt_long */
#if EOF == -1
#define CHECK_EOF(c) ((c) == EOF)
#else
#define CHECK_EOF(c) ((c) == -1 || (c) == EOF)
#endif

#endif /* _COMMON_H_ */
