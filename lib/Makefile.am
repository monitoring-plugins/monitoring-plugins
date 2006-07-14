## Process this file with automake to produce Makefile.in

SUBDIRS = tests

noinst_LIBRARIES = libcoreutils.a libnagiosplug.a

# Will auto pick up fsusage.c mountlist.c
libcoreutils_a_SOURCES = \
  cloexec.c cloexec.h \
  exit.h \
  full-read.c full-read.h \
  full-write.c full-write.h \
  gettext.h \
  safe-read.c safe-read.h \
  safe-write.c safe-write.h strcase.h xalloc-die.c

# intprops.h required for getloadavg.c
# This needs to be an EXTRA_DIST because the m4s
# that have AC_LIBSOURCES for intprops.h are not necessary and
# the getloadavg m4 is an autoconf one, so doesn't
# know about intprops.h. Confused? You will be!
EXTRA_DIST = intprops.h

other_coreutils_files = \
  error.c error.h \
  fsusage.c \
  getloadavg.c \
  malloc.c \
  mountlist.c \
  realloc.c \
  strtod.c \
  exitfail.c exitfail.h \
  fsusage.h \
  getopt.c getopt1.c \
  mountlist.h \
  unlocked-io.h \
  xalloc.h \
  xmalloc.c


libcoreutils_a_LIBADD = $(LIBOBJS)
libcoreutils_a_DEPENDENCIES = $(libcoreutils_a_LIBADD)

libnagiosplug_a_SOURCES = snprintf.c utils_base.c utils_disk.c
EXTRA_DIST += utils_base.h utils_disk.h

INCLUDES = -I$(srcdir) -I$(top_srcdir)/intl -I$(top_srcdir)/plugins

print_coreutil_files:
	@echo $(libcoreutils_a_SOURCES) $(other_coreutils_files) $(EXTRA_DIST)

test:
	cd tests && make test

# Below are from coreutil's lib/Makefile.am
BUILT_SOURCES = $(STDBOOL_H)
EXTRA_DIST += stdbool_.h
MOSTLYCLEANFILES = stdbool.h stdbool.ht
# Create stdbool.h on systems that lack a working one.
stdbool.h: stdbool_.h
	sed -e 's/@''HAVE__BOOL''@/$(HAVE__BOOL)/g' $(srcdir)/stdbool_.h > $@t
	mv $@t $@

BUILT_SOURCES += $(GETOPT_H)
EXTRA_DIST += getopt_.h getopt_int.h

# We need the following in order to create an <getopt.h> when the system
# doesn't have one that works with the given compiler.
all-local $(lib_OBJECTS): $(GETOPT_H)
getopt.h: getopt_.h
	cp $(srcdir)/getopt_.h $@-t 
	mv $@-t $@ 
MOSTLYCLEANFILES += getopt.h getopt.h-t

