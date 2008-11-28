## Process this file with automake to produce Makefile.in

SUBDIRS = gl tap lib plugins plugins-scripts plugins-root po @PERLMODS_DIR@

EXTRA_DIST = config.rpath \
	ABOUT-NLS ACKNOWLEDGEMENTS AUTHORS BUGS CODING FAQ LEGAL NEWS \
	NP-VERSION-GEN REQUIREMENTS SUPPORT THANKS \
	NPTest.pm contrib pkg nagios-plugins.spec \
	config_test/Makefile config_test/run_tests config_test/child_test.c \
	perlmods tools/build_perl_modules \
	tools/tinderbox_build

ACLOCAL_AMFLAGS = -I gl/m4 -I m4

localedir = $(datadir)/locale
DEFS = -DLOCALEDIR=\"$(localedir)\"

dist-hook:
	make THANKS nagios-plugins.spec pkg/fedora/nagios-plugins.spec
	echo ${VERSION} >$(distdir)/release

install-root:
	cd plugins-root && $(MAKE) $@

test test-debug:
	cd lib && $(MAKE) $@
	if test "$(PERLMODS_DIR)" != ""; then cd perlmods && $(MAKE) $@; fi
	cd plugins && $(MAKE) $@
	cd plugins-scripts && $(MAKE) $@

nagios-plugins.spec: nagios-plugins.spec.in
	sed "s/%%{VERSION}/${VERSION}/;s/%%{RELEASE}/${RELEASE}/;s/^%%{requires}$$//" $? > $@

pkg/fedora/nagios-plugins.spec: nagios-plugins.spec.in
	$(top_srcdir)/mkinstalldirs pkg/fedora
	sed "s/%%{VERSION}/${VERSION}/;s/%%{RELEASE}/${RELEASE}/;/^# Requires$$/r $(top_srcdir)/pkg/fedora/requires" $? > $@

# Solaris pkgmk
BUILDDIR = build-solaris
PACKDIR = build-pkg
PKGSCRIPT = pkg/solaris/solpkg

solpkg:
	if [ ! -d $(BUILDDIR) ] ; then mkdir $(BUILDDIR); fi
	if [ ! -d $(PACKDIR) ] ; then mkdir $(PACKDIR); fi
	$(MAKE) all
	$(MAKE) DESTDIR=$$PWD/$(BUILDDIR) install
	$(INSTALL) pkg/solaris/pkginfo $(BUILDDIR)
	cd $(BUILDDIR) && $(PERL) ../$(PKGSCRIPT) ../$(PACKDIR)

THANKS: THANKS.in
	@echo "This software is brought to you by the Nagios Plugins Development Team. However," > $@
	@echo "there have been many contributors to this project. Everyone below has helped in " >> $@
	@echo "raising bug reports, creating patches or contributing new plugins." >> $@
	@echo "" >> $@
	perl -e ' while (<>) { print STDERR "Duplicate: $$_",$$/ if ($$a{$$_}) ; $$a{$$_}++}; print sort by_last_field (keys %a); sub by_last_field { ($$a1) = $$a =~ /\s(\w+)$$/; ($$b1) = $$b =~ /\s(\w+)$$/; $$a1 cmp $$b1}' $? >> $@

pkgclean:
	rm -rf $(BUILDDIR) $(PACKDIR)

