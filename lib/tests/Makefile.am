
noinst_PROGRAMS = @EXTRA_TEST@

# These two lines support "make check", but we use "make test"
TESTS = @EXTRA_TEST@
check_PROGRAMS = @EXTRA_TEST@

TAP_LIB_DIR = $(top_srcdir)/@TAP_DIR@/src
TAP_O = $(TAP_LIB_DIR)/tap.o $(TAP_LIB_DIR)/../lib/libgnu.la
INCLUDES = -I$(top_srcdir)/lib -I$(top_srcdir)/gl -I$(top_srcdir)/intl -I$(top_srcdir)/plugins -I$(TAP_LIB_DIR)

EXTRA_PROGRAMS = test_utils test_disk test_tcp test_cmd test_base64 test_ini test_opts

np_test_files = config-dos.ini config-opts.ini config-tiny.ini plugin.ini plugins.ini
EXTRA_DIST = test_all.t $(np_test_files)

AM_CFLAGS = -g -I..

LIBS = @LIBINTL@

test_utils_LDADD = ../utils_base.o $(TAP_O)

test_disk_LDADD = ../utils_disk.o $(top_srcdir)/gl/libgnu.a $(TAP_O)

test_tcp_LDADD = ../utils_tcp.o $(TAP_O)

test_cmd_LDADD = ../utils_cmd.o ../utils_base.o $(TAP_O)

test_base64_LDADD = $(top_srcdir)/gl/base64.o $(TAP_O)

test_ini_LDADD = ../utils_base.o ../parse_ini.o $(TAP_O)

test_opts_LDADD = ../utils_base.o ../parse_ini.o ../extra_opts.o $(TAP_O)

test: ${noinst_PROGRAMS}
	perl ./test_all.t $(EXTRA_PROGRAMS)

test-debug: ${noinst_PROGRAMS}
	perl ./test_all.t -v $(EXTRA_PROGRAMS)

