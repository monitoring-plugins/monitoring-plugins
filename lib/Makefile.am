## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libnagiosplug.a

noinst_HEADERS = getopt.h fsusage.h mountlist.h

libnagiosplug_a_SOURCES = getopt.c getopt1.c getloadavg.c snprintf.c fsusage.c mountlist.c

EXTRA_DIST = afs.m4 fstypename.m4 fsusage.m4 ls-mntd-fs.m4

INCLUDES = -I$(srcdir)
