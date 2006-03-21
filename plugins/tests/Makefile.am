
noinst_PROGRAMS = @EXTRA_TEST@

# These two lines support "make check", but we use "make test"
TESTS = @EXTRA_TEST@
check_PROGRAMS = @EXTRA_TEST@

INCLUDES = -I$(top_srcdir)/lib -I$(top_srcdir)/intl 

EXTRA_PROGRAMS = test_utils

EXTRA_DIST = test_utils.t

LIBS = @LIBINTL@

test_utils_SOURCES = test_utils.c
test_utils_CFLAGS = -g -I..
test_utils_LDFLAGS = -L/usr/local/lib -ltap
test_utils_LDADD = ../utils.o

test: ${noinst_PROGRAMS}
	perl -MTest::Harness -e '$$Test::Harness::switches=""; runtests(map {$$_ .= ".t"} @ARGV)' $(EXTRA_PROGRAMS)

