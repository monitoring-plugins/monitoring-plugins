
noinst_PROGRAMS = @EXTRA_TEST@

# These two lines support "make check", but we use "make test"
TESTS = @EXTRA_TEST@
check_PROGRAMS = @EXTRA_TEST@

INCLUDES = -I$(top_srcdir)/lib -I$(top_srcdir)/gl -I$(top_srcdir)/intl -I$(top_srcdir)/plugins

EXTRA_PROGRAMS = test_utils test_disk test_tcp test_cmd test_base64 test_ini test_opts

np_test_scripts = test_base64.t test_cmd.t test_disk.t test_ini.t test_opts.t test_tcp.t test_utils.t
np_test_files = config-dos.ini config-opts.ini config-tiny.ini plugin.ini plugins.ini
EXTRA_DIST = $(np_test_scripts) $(np_test_files)

LIBS = @LIBINTL@

if USE_LIBTAP_LOCAL
tap_cflags = -I$(top_srcdir)/tap
tap_ldflags = -L$(top_srcdir)/tap
tap_ldadd = $(top_srcdir)/gl/libgnu.a
else
## Shouldn't we detect that??
tap_ldflags = -L/usr/local/lib
endif

AM_CFLAGS = -g -I$(top_srcdir)/lib -I$(top_srcdir)/gl $(tap_cflags)
AM_LDFLAGS = $(tap_ldflags) -ltap
LDADD = $(top_srcdir)/lib/libnagiosplug.a $(top_srcdir)/gl/libgnu.a

SOURCES = test_utils.c test_disk.c test_tcp.c test_cmd.c test_base64.c test_ini.c test_opts.c

test: ${noinst_PROGRAMS}
	perl -MTest::Harness -e '$$Test::Harness::switches=""; runtests(map {$$_ .= ".t"} @ARGV)' $(EXTRA_PROGRAMS)

test-debug: ${noinst_PROGRAMS}
	perl -MTest::Harness -e '$$Test::Harness::verbose=1; $$Test::Harness::switches=""; runtests(map {$$_ .= ".t"} @ARGV)' $(EXTRA_PROGRAMS)

