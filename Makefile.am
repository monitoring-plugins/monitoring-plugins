## Process this file with automake to produce Makefile.in

SUBDIRS = intl lib plugins plugins-scripts po

EXTRA_DIST = config.rpath \
	ABOUT-NLS CODING FAQ LEGAL REQUIREMENTS SUPPORT THANKS \
	Helper.pm contrib pkg nagios-plugins.spec.in

ACLOCAL_AMFLAGS = -I lib

localedir = $(datadir)/locale
DEFS = -DLOCALEDIR=\"$(localedir)\"

dist-hook:
	sed "s/%%{VERSION}/${VERSION}/;s/%%{RELEASE}/${RELEASE}/;" $(srcdir)/nagios-plugins.spec.in > $(distdir)/nagios-plugins.spec

test:
	cd plugins; $(MAKE) test
	cd plugins-scripts; $(MAKE) test

nagios-plugins.spec: nagios-plugins.spec.in
	sed "s/%%{VERSION}/${VERSION}/;s/%%{RELEASE}/${RELEASE}/;" $? > $@

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

THANKS: THANKS.in AUTHORS
	perl -e ' @a=<>; print sort by_last_field @a; sub by_last_field { ($$a1) = $$a =~ /\s(\w+)$$/; ($$b1) = $$b =~ /\s(\w+)$$/; $$a1 cmp $$b1}' AUTHORS | cat THANKS.in - > $@

pkgclean:
	rm -rf $(BUILDDIR) $(PACKDIR)
