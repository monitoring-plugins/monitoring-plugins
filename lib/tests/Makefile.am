
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

test_utils_SOURCES = test_utils.c
test_utils_CFLAGS = -g -I..
test_utils_LDFLAGS = -L/usr/local/lib -ltap
test_utils_LDADD = ../utils_base.o

test_disk_SOURCES = test_disk.c
test_disk_CFLAGS = -g -I..
test_disk_LDFLAGS = -L/usr/local/lib -ltap
test_disk_LDADD = ../utils_disk.o $(top_srcdir)/gl/libgnu.a

test_tcp_SOURCES = test_tcp.c
test_tcp_CFLAGS = -g -I..
test_tcp_LDFLAGS = -L/usr/local/lib -ltap
test_tcp_LDADD = ../utils_tcp.o 

test_cmd_SOURCES = test_cmd.c
test_cmd_CFLAGS = -g -I..
test_cmd_LDFLAGS = -L/usr/local/lib -ltap
test_cmd_LDADD = ../utils_cmd.o ../utils_base.o

test_base64_SOURCES = test_base64.c
test_base64_CFLAGS = -g -I..
test_base64_LDFLAGS = -L/usr/local/lib -ltap
test_base64_LDADD = $(top_srcdir)/gl/base64.o 

test_ini_SOURCES = test_ini.c
test_ini_CFLAGS = -g -I..
test_ini_LDFLAGS = -L/usr/local/lib -ltap
test_ini_LDADD = ../utils_base.o ../parse_ini.o

test_opts_SOURCES = test_opts.c
test_opts_CFLAGS = -g -I..
test_opts_LDFLAGS = -L/usr/local/lib -ltap
test_opts_LDADD = ../utils_base.o ../parse_ini.o ../extra_opts.o

test: ${noinst_PROGRAMS}
	perl -MTest::Harness -e '$$Test::Harness::switches=""; runtests(map {$$_ .= ".t"} @ARGV)' $(EXTRA_PROGRAMS)

test-debug: ${noinst_PROGRAMS}
	perl -MTest::Harness -e '$$Test::Harness::verbose=1; $$Test::Harness::switches=""; runtests(map {$$_ .= ".t"} @ARGV)' $(EXTRA_PROGRAMS)

