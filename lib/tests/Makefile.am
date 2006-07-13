
noinst_PROGRAMS = @EXTRA_TEST@

# These two lines support "make check", but we use "make test"
TESTS = @EXTRA_TEST@
check_PROGRAMS = @EXTRA_TEST@

INCLUDES = -I$(top_srcdir)/lib -I$(top_srcdir)/intl -I$(top_srcdir)/plugins

EXTRA_PROGRAMS = test_utils test_disk

EXTRA_DIST = test_utils.t test_disk.t

LIBS = @LIBINTL@

test_utils_SOURCES = test_utils.c
test_utils_CFLAGS = -g -I..
test_utils_LDFLAGS = -L/usr/local/lib -ltap
test_utils_LDADD = ../utils_base.o

test_disk_SOURCES = test_disk.c
test_disk_CFLAGS = -g -I..
test_disk_LDFLAGS = -L/usr/local/lib -ltap
test_disk_LDADD = ../utils_disk.o

test: ${noinst_PROGRAMS}
	perl -MTest::Harness -e '$$Test::Harness::switches=""; runtests(map {$$_ .= ".t"} @ARGV)' $(EXTRA_PROGRAMS)

