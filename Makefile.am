## Process this file with automake to produce Makefile.in

SUBDIRS = plugins plugins-scripts

EXTRA_DIST = REQUIREMENTS acconfig.h package.def subst.in subst.sh Helper.pm nagios-plugins.spec contrib

test:
	cd plugins; $(MAKE) test
	cd plugins-scripts; $(MAKE) test
