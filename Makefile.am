## Process this file with automake to produce Makefile.in

SUBDIRS = lib plugins plugins-scripts

EXTRA_DIST = CODING FAQ LEGAL REQUIREMENTS SUPPORT \
	Helper.pm	contrib nagios-plugins.spec.in

ACLOCAL_AMFLAGS = -I lib

dist-hook:
	sed "s/%%{VER}/${VER}/;s/%%{REL}/${REL}/;" $(srcdir)/nagios-plugins.spec.in > $(distdir)/nagios-plugins.spec

test:
	cd plugins; $(MAKE) test
	cd plugins-scripts; $(MAKE) test

nagios-plugins.spec: nagios-plugins.spec.in
	sed "s/%%{VER}/${VER}/;s/%%{REL}/${REL}/;" $? > $@

# Solaris pkgmk
PACKDIR=build-pkg
VERSION=@PACKAGE_VERSION@

Prototype:
	if [ ! -d $(PACKDIR) ] ; then mkdir $(PACKDIR); fi
	cd build-solaris
	$(MAKE) all
	$(MAKE) DESTDIR=../$(PACKDIR) install
	cd ..
	echo i pkginfo> Prototype
	if [ -f checkinstall ] ; then echo i checkinstall>> Prototype; fi
	if [ -f preinstall ] ; then echo i preinstall>> Prototype; fi
	if [ -f postinstall ] ; then echo i postinstall>> Prototype; fi
	pkgproto $(PACKDIR)=/ | sed -e "s|$(LOGNAME) $(GROUP)$$|root root|" | egrep -v "(s|d) none (/|/etc|/var|/usr|/usr/local) " >> Prototype

pkgmap: ../Prototype
	mkdir -p $(PACKDIR)/nagiosp
	cd $(PACKDIR)/nagiosp
	pkgmk -o -r / -f ../Prototype -d $(PACKDIR) nagiosp
	cd ..

nagios.SPARC.pkg.tar.gz: pkgmap
	cd $(PACKDIR) && tar -cf - nagiosp | gzip -9 -c > ../nagiosp.SPARC.pkg.tar.gz

pkgset: nagiosp.SPARC.pkg.tar.gz

pkgclean:
	rm -rf Prototype nagiosp.SPARC.pkg.tar.gz
