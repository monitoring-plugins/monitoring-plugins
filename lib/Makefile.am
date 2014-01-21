## Process this file with automake to produce Makefile.in

SUBDIRS = . tests

noinst_LIBRARIES = libmonitoringplug.a

AM_CPPFLAGS = -DNP_STATE_DIR_PREFIX=\"$(localstatedir)\"

libmonitoringplug_a_SOURCES = utils_base.c utils_disk.c utils_tcp.c utils_cmd.c
EXTRA_DIST = utils_base.h utils_disk.h utils_tcp.h utils_cmd.h parse_ini.h extra_opts.h

if USE_PARSE_INI
libmonitoringplug_a_SOURCES += parse_ini.c extra_opts.c
endif USE_PARSE_INI

INCLUDES = -I$(srcdir) -I$(top_srcdir)/gl -I$(top_srcdir)/intl -I$(top_srcdir)/plugins

test test-debug:
	cd tests && make $@

