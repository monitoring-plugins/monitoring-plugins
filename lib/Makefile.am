## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libnagiosplug.a

libnagiosplug_a_SOURCES = getopt.c getopt1.c fsusage.c snprintf.c mountlist.c \
		xmalloc.c xstrdup.c exitfail.c

libnagiosplug_a_LIBADD = @LIBOBJS@
libnagiosplug_a_DEPENDENCIES = $(libnagiosplug_a_LIBADD)

# Extra GNU libraries required if target is missing these functions
EXTRA_DIST = ulonglong.m4 codeset.m4 gettext.m4 glibc21.m4 iconv.m4 intdiv0.m4 \
		inttypes.m4 inttypes_h.m4 inttypes-pri.m4 isc-posix.m4 lcmessage.m4 lib-ld.m4 \
		lib-link.m4 lib-prefix.m4 progtest.m4 stdint_h.m4 uintmax_t.m4 afs.m4 \
		fstypename.m4 fsusage.m4 ls-mntd-fs.m4 getopt.h gettext.h fsusage.h mountlist.h\
		error.m4 error.h error.c getloadavg.c xalloc.h unlocked-io.h unlocked-io.m4 malloc.c \
		onceonly.m4 realloc.c strtod.c exitfail.h full-read.h xstrdup.c xalloc.m4 exit.h \
		cloexec.c cloexec.h getopt.m4 exitfail.m4

INCLUDES = -I$(srcdir) -I$(top_srcdir)/intl
