## Process this file with automake to produce Makefile.in

if RELEASE_PRESENT
NP_VERSION = @NP_RELEASE@
else
NP-VERSION-FILE: .FORCE-NP-VERSION-FILE
	@$(SHELL_PATH) $(top_srcdir)/NP-VERSION-GEN
.FORCE-NP-VERSION-FILE:
-include NP-VERSION-FILE
endif

SUFFIXES = .pl .sh

VPATH=$(top_srcdir) $(top_srcdir)/plugins-scripts $(top_srcdir)/plugins-scripts/t

libexec_SCRIPTS = check_breeze check_disk_smb check_flexlm check_ircd \
	check_log check_oracle check_rpc check_sensors check_wave \
	check_ifstatus check_ifoperstatus check_mailq check_file_age \
	utils.sh utils.pm

EXTRA_DIST=check_breeze.pl check_disk_smb.pl check_flexlm.pl check_ircd.pl \
	check_log.sh check_ntp.pl check_oracle.sh check_rpc.pl check_sensors.sh \
	check_ifstatus.pl check_ifoperstatus.pl check_wave.pl check_mailq.pl check_file_age.pl \
	utils.sh.in utils.pm.in t

TESTS_ENVIRONMENT=perl -I $(top_builddir) -I $(top_srcdir)

TESTS = @SCRIPT_TEST@

test:
	perl -I $(top_builddir) -I $(top_srcdir) ../test.pl
	perl -I $(top_builddir) -I $(top_srcdir) ../test.pl t/utils.t	# utils.t is excluded from above, so manually ask to test

test-debug:
	NPTEST_DEBUG=1 HARNESS_VERBOSE=1 perl -I $(top_builddir) -I $(top_srcdir) ../test.pl
	NPTEST_DEBUG=1 HARNESS_VERBOSE=1 perl -I $(top_builddir) -I $(top_srcdir) ../test.pl t/utils.t	# utils.t is excluded from above, so manually ask to test

CLEANFILES=$(libexec_SCRIPTS)

.pl :
	NP_VERSION=$(NP_VERSION) $(AWK) -f ./subst $< > $@
	chmod +x $@

.sh :
	NP_VERSION=$(NP_VERSION) $(AWK) -f ./subst $< > $@
	chmod +x $@

clean-local:
	rm -f NP-VERSION-FILE

