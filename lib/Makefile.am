## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libnagiosplug.a

libnagiosplug_a_SOURCES = getopt.c getopt1.c snprintf.c fsusage.c mountlist.c xmalloc.c

libnagiosplug_a_LIBADD = @LIBOBJS@
libnagiosplug_a_DEPENDENCIES = $(libnagiosplug_a_LIBADD)

# Extra GNU libraries required if target is missing these functions
EXTRA_DIST = ulonglong.m4 codeset.m4 getloadavg.m4 gettext.m4 glibc21.m4 iconv.m4 intdiv0.m4 \
		inttypes.m4 inttypes_h.m4 inttypes-pri.m4 isc-posix.m4 lcmessage.m4 lib-ld.m4 \
		lib-link.m4 lib-prefix.m4 progtest.m4 stdint_h.m4 uintmax_t.m4 afs.m4 \
		fstypename.m4 fsusage.m4 ls-mntd-fs.m4 getopt.h gettext.h fsusage.h mountlist.h\
		error.m4 error.h error.c xalloc.h unlocked-io.h malloc.c realloc.c strtod.c

INCLUDES = -I$(srcdir) -I$(top_srcdir)/intl
