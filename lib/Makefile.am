## Process this file with automake to produce Makefile.in

SUBDIRS = tests

noinst_LIBRARIES = libnagiosplug.a


libnagiosplug_a_SOURCES = utils_base.c utils_disk.c utils_tcp.c utils_cmd.c base64.c
EXTRA_DIST = utils_base.h utils_disk.h utils_tcp.h utils_cmd.h base64.h

INCLUDES = -I$(srcdir) -I$(top_srcdir)/gl -I$(top_srcdir)/intl -I$(top_srcdir)/plugins

test test-debug:
	cd tests && make $@

