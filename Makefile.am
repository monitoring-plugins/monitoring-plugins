## Process this file with automake to produce Makefile.in

SUBDIRS = plugins plugins-scripts

EXTRA_DIST = REQUIREMENTS acconfig.h subst.in subst.sh Helper.pm contrib nagios-plugins.spec.in

dist-hook:
	sed "s/PACKAGE_VERSION/@PACKAGE_VERSION@/;s/PACKAGE_RELEASE/@PACKAGE_RELEASE@/;" $(srcdir)/nagios-plugins.spec.in > $(distdir)/nagios-plugins.spec

test:
	cd plugins; $(MAKE) test
	cd plugins-scripts; $(MAKE) test
