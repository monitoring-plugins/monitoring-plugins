## Process this file with automake to produce Makefile.in

SUBDIRS = lib plugins plugins-scripts

EXTRA_DIST = CODING FAQ LEGAL REQUIREMENTS SUPPORT \
	Helper.pm contrib pkg nagios-plugins.spec.in

ACLOCAL_AMFLAGS = -I lib

dist-hook:
	sed "s/%%{VER}/${VER}/;s/%%{REL}/${REL}/;" $(srcdir)/nagios-plugins.spec.in > $(distdir)/nagios-plugins.spec

test:
	cd plugins; $(MAKE) test
	cd plugins-scripts; $(MAKE) test

nagios-plugins.spec: nagios-plugins.spec.in
	sed "s/%%{VER}/${VER}/;s/%%{REL}/${REL}/;" $? > $@

# Solaris pkgmk
BUILDDIR=`pwd`/build-solaris
PACKDIR=`pwd`/build-pkg
VERSION=@PACKAGE_VERSION@

solpkg:
	if [ ! -d $(BUILDDIR) ] ; then mkdir $(BUILDDIR); fi
	if [ ! -d $(PACKDIR) ] ; then mkdir $(PACKDIR); fi
	$(MAKE) all
	$(MAKE) DESTDIR=$(BUILDDIR) install
	$(INSTALL) pkg/solaris/pkginfo $(BUILDDIR)
	$(cd $(BUILDDIR) && `pwd`/pkg/solaris/solpkg $(PACKDIR))

pkgclean:
	rm -rf $(BUILDDIR) $(PACKDIR)
