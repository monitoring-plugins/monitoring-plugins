## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libnagiosplug.a

noinst_HEADERS = getopt.h getaddrinfo.h gethostbyname.h

libnagiosplug_a_SOURCES = getopt.c getopt1.c getloadavg.c snprintf.c 

libnagiosplug_a_DEPENDENCIES = @LIBOBJS@

INCLUDES = -I$(srcdir)

EXTRA_DIST = getaddrinfo.c gethostbyname.c
