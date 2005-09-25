## Process this file with automake to produce Makefile.in

VPATH = $(top_srcdir) $(top_srcdir)/lib $(top_srcdir)/plugins $(top_srcdir)/plugins/t 

INCLUDES = -I.. -I$(top_srcdir)/lib -I$(top_srcdir)/intl -I$(top_srcdir)/plugins

datadir = @datadir@
localedir = $(datadir)/locale
DEFS = -DLOCALEDIR=\"$(localedir)\" @DEFS@
LIBS = @LIBINTL@ @LIBS@ @SSLINCLUDE@

EXTRA_PROGRAMS = check_dhcp check_icmp pst3

BASEOBJS = ../plugins/utils.o ../lib/libnagiosplug.a ../lib/libcoreutils.a
NETOBJS = ../plugins/netutils.o $(BASEOBJS) $(EXTRA_NETOBJS)
NETLIBS = $(NETOBJS) $(SOCKETLIBS)

TESTS_ENVIRONMENT = perl -I $(top_builddir) -I $(top_srcdir)

TESTS = @PLUGIN_TEST@

test:
	perl -I $(top_builddir) -I $(top_srcdir) ../test.pl

all-local: $(EXTRA_PROGRAMS)

setuid_root_mode = 4550

group = nagios

# /* Author Coreutils team - see ACKNOWLEDGEMENTS */

INSTALL_SUID = \
	for f in $(EXTRA_PROGRAMS) ; do \
	p=$$f; \
	echo " $(INSTALL_PROGRAM) $$p $(DESTDIR)$(libexecdir)/$$p"; \
	$(INSTALL_PROGRAM) $$p $(DESTDIR)$(libexecdir)/$$p; \
	echo " chown root $(DESTDIR)$(libexecdir)/$$p"; \
	chown root $(DESTDIR)$(libexecdir)/$$p; \
	echo " chmod $(setuid_root_mode) $(DESTDIR)$(libexecdir)/$$p"; \
	chmod $(setuid_root_mode) $(DESTDIR)$(libexecdir)/$$p; \
	done

install-root: $(EXTRA_PROGRAMS)
	@$(INSTALL_SUID)

install-exec-local: $(EXTRA_PROGRAMS)
	@TMPFILE=$(DESTDIR)$(libexecdir)/.setuid-$$$$; \
	rm -f $$TMPFILE; \
	echo > $$TMPFILE; \
	## See if we can create a setuid root executable in $(libexecdir).
	## If not, then don't even try to install setuid plugins.
	can_create_suid_root_executable=no; \
	chown root $$TMPFILE > /dev/null 2>&1 \
	  && chmod $(setuid_root_mode) $$TMPFILE > /dev/null 2>&1 \
	  && can_create_suid_root_executable=yes; \
	rm -f $$TMPFILE; \
	if test $$can_create_suid_root_executable = yes; then \
	  $(INSTALL_SUID); \
	else \
	  echo "WARNING: insufficient access; not installing setuid plugins"; \
	  echo "NOTE: to install setuid plugins, run 'make install-root' as root"; \
	fi

# /* Author Coreutils team sub-citation */

##############################################################################
# the actual targets
check_dhcp_LDADD = $(NETLIBS)
check_icmp_LDADD = $(SOCKETLIBS)

check_dhcp_DEPENDENCIES = check_dhcp.c $(NETOBJS) $(DEPLIBS) 
check_icmp_DEPENDENCIES = check_icmp.c
pst3_DEPENDENCIES = pst3.c
