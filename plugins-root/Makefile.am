## Process this file with automake to produce Makefile.in

VPATH = $(top_srcdir) $(top_srcdir)/lib $(top_srcdir)/plugins $(top_srcdir)/plugins/t $(srcdir)

INCLUDES = -I.. -I$(top_srcdir)/lib -I$(top_srcdir)/gl -I$(top_srcdir)/intl -I$(top_srcdir)/plugins @SSLINCLUDE@

datadir = @datadir@
localedir = $(datadir)/locale
DEFS = -DLOCALEDIR=\"$(localedir)\" @DEFS@
LIBS = @LIBINTL@ @LIBS@ @SSLLIBS@

noinst_PROGRAMS = check_dhcp check_icmp @EXTRAS_ROOT@

EXTRA_PROGRAMS = pst3

EXTRA_DIST = pst3.c

BASEOBJS = ../plugins/utils.o ../lib/libnagiosplug.a ../gl/libgnu.a
NETOBJS = ../plugins/netutils.o $(BASEOBJS) $(EXTRA_NETOBJS)
NETLIBS = $(NETOBJS) $(SOCKETLIBS)

TESTS_ENVIRONMENT = perl -I $(top_builddir) -I $(top_srcdir)

TESTS = @PLUGIN_TEST@

test:
	perl -I $(top_builddir) -I $(top_srcdir) ../test.pl

setuid_root_mode = ug=rx,u+s

# /* Author Coreutils team - see ACKNOWLEDGEMENTS */

INSTALL_SUID = \
	for f in $(noinst_PROGRAMS) ; do \
	p=$$f; \
	echo " $(INSTALL_PROGRAM) $$p $(DESTDIR)$(libexecdir)/$$p"; \
	$(INSTALL_PROGRAM) $$p $(DESTDIR)$(libexecdir)/$$p; \
	echo " chown root $(DESTDIR)$(libexecdir)/$$p"; \
	chown root $(DESTDIR)$(libexecdir)/$$p; \
	echo " chmod $(setuid_root_mode) $(DESTDIR)$(libexecdir)/$$p"; \
	chmod $(setuid_root_mode) $(DESTDIR)$(libexecdir)/$$p; \
	done

install-root: $(noinst_PROGRAMS)
	@$(INSTALL_SUID)

install-exec-local: $(noinst_PROGRAMS)
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
check_icmp_LDADD = $(NETLIBS) $(SOCKETLIBS)
pst3_LDADD = -lkvm

check_dhcp_DEPENDENCIES = check_dhcp.c $(NETOBJS) $(DEPLIBS) 
check_icmp_DEPENDENCIES = check_icmp.c $(NETOBJS)
