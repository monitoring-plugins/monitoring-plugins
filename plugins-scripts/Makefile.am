## Process this file with automake to produce Makefile.in

SUFFIXES = .pl .sh

VPATH=$(top_srcdir) $(top_srcdir)/plugins-scripts $(top_srcdir)/plugins-scripts/t

libexec_SCRIPTS = check_breeze check_disk_smb check_flexlm check_ircd \
	check_log check_ntp check_oracle check_rpc check_sensors check_wave \
	utils.sh utils.pm

EXTRA_DIST=check_breeze.pl check_disk_smb.pl check_flexlm.pl check_ircd.pl \
	check_log.sh check_ntp.pl check_oracle.sh check_rpc.pl check_sensors.sh \
	check_wave.pl utils.sh.in utils.pm.in t

TESTS_ENVIRONMENT=perl -I $(top_builddir) -I $(top_srcdir)

TESTS = @SCRIPT_TEST@

test:
	perl -I $(top_builddir) -I $(top_srcdir) ../test.pl

CLEANFILES=$(libexec_SCRIPTS)

.pl :
	$(AWK) -f ./subst $< > $@
	chmod +x $@

.sh :
	$(AWK) -f ./subst $< > $@
	chmod +x $@
