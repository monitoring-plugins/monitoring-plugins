## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libcoreutils.a libnagiosplug.a

# Will auto pick up fsusage.c mountlist.c
libcoreutils_a_SOURCES = \
  cloexec.c cloexec.h \
  exit.h \
  exitfail.c exitfail.h \
  fsusage.h \
  full-read.c full-read.h \
  full-write.c full-write.h \
  getopt.c getopt.h getopt1.c \
  gettext.h \
  mountlist.h \
  safe-read.c safe-read.h \
  safe-write.c safe-write.h \
  unlocked-io.h \
  xalloc.h \
  xmalloc.c \
  xstrdup.c

other_coreutils_files = \
  error.c error.h \
  fsusage.c \
  getloadaavg.c \
  malloc.c \
  mountlist.c \
  realloc.c \
  strtod.c

libcoreutils_a_LIBADD = $(LIBOBJS)
libcoreutils_a_DEPENDENCIES = $(libcoreutils_a_LIBADD)

libnagiosplug_a_SOURCES = snprintf.c

INCLUDES = -I$(srcdir) -I$(top_srcdir)/intl

