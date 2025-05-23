## Process this file with automake to produce Makefile.in

SUBDIRS = gl tap lib plugins plugins-scripts plugins-root @PERLMODS_DIR@

EXTRA_DIST = config.rpath \
	ABOUT-NLS ACKNOWLEDGEMENTS AUTHORS CODING ChangeLog FAQ NEWS \
	NP-VERSION-GEN REQUIREMENTS SUPPORT THANKS \
	NPTest.pm pkg \
	config_test/Makefile config_test/run_tests config_test/child_test.c \
	perlmods tools/build_perl_modules

ACLOCAL_AMFLAGS = -I gl/m4 -I m4

localedir = $(datadir)/locale
DEFS = -DLOCALEDIR=\"$(localedir)\"

#
# The ChangeLog file is auto-generated from the Git history.  We let it depend
# on NP-VERSION-GEN, as we bump our version number in that file.
#
ChangeLog: NP-VERSION-GEN
	$(top_srcdir)/tools/generate-change-log > $@

dist-hook:
	$(MAKE) THANKS
	echo ${VERSION} >$(distdir)/release

install-root:
	cd plugins-root && $(MAKE) $@

test test-debug:
	cd lib && $(MAKE) $@
	if test "$(PERLMODS_DIR)" != ""; then cd perlmods && $(MAKE) $@; fi
	cd plugins && $(MAKE) $@
	cd plugins-scripts && $(MAKE) $@
	cd plugins-root && $(MAKE) $@

# Solaris pkgmk
BUILDDIR = build-solaris
PACKDIR = build-pkg
PKGSCRIPT = pkg/solaris/solpkg

solpkg:
	if [ ! -d $(BUILDDIR) ] ; then mkdir $(BUILDDIR); fi
	if [ ! -d $(PACKDIR) ] ; then mkdir $(PACKDIR); fi
	$(MAKE) all
	$(MAKE) DESTDIR=$$PWD/$(BUILDDIR) install
	$(INSTALL) plugins-root/pst3 $(BUILDDIR)$(libexecdir)
	$(INSTALL) pkg/solaris/pkginfo $(BUILDDIR)
	$(INSTALL) pkg/solaris/preinstall $(BUILDDIR)
	cd $(BUILDDIR) && $(PERL) ../$(PKGSCRIPT) ../$(PACKDIR)

THANKS: THANKS.in
	@echo "This software is brought to you by the Monitoring Plugins Development Team. However," > $@
	@echo "there have been many contributors to this project. Everyone below has helped in " >> $@
	@echo "raising bug reports, creating patches or contributing new plugins." >> $@
	@echo "" >> $@
	perl -e ' while (<>) { print STDERR "Duplicate: $$_",$$/ if ($$a{$$_}) ; $$a{$$_}++}; print sort by_last_field (keys %a); sub by_last_field { ($$a1) = $$a =~ /\s(\w+)$$/; ($$b1) = $$b =~ /\s(\w+)$$/; $$a1 cmp $$b1}' $? >> $@

pkgclean:
	rm -rf $(BUILDDIR) $(PACKDIR)

