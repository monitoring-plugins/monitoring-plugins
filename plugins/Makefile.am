## Process this file with automake to produce Makefile.in

if RELEASE_PRESENT
NP_VERSION = @NP_RELEASE@
else
NP-VERSION-FILE: .FORCE-NP-VERSION-FILE
	@$(SHELL_PATH) $(top_srcdir)/NP-VERSION-GEN
.FORCE-NP-VERSION-FILE:
-include NP-VERSION-FILE
endif

AM_CFLAGS = -DNP_VERSION='"$(NP_VERSION)"'

VPATH = $(top_srcdir) $(top_srcdir)/lib $(top_srcdir)/plugins $(top_srcdir)/plugins/t

AM_CPPFLAGS = -I.. \
			  -I$(top_srcdir)/lib \
			  -I$(top_srcdir)/gl \
			  -I$(top_srcdir)/intl \
			  -DNP_STATE_DIR_PREFIX=\"$(localstatedir)\" \
			  @LDAPINCLUDE@ \
			  @PGINCLUDE@ \
			  @SSLINCLUDE@

localedir = $(datadir)/locale
# gettext docs say to use AM_CPPFLAGS, but per module_CPPFLAGS override this
DEFS = -DLOCALEDIR=\"$(localedir)\" @DEFS@
# gettext docs say to use LDADD, but per module_LDADD override
LIBS = @LTLIBINTL@ @LIBS@
MATHLIBS = @MATHLIBS@

# This is not portable. Run ". tools/devmode" to get development compile flags
#AM_CFLAGS = -Wall

libexec_PROGRAMS = check_apt check_cluster check_disk check_dummy check_http check_load \
	check_mrtg check_mrtgtraf check_ntp check_ntp_peer check_ping \
	check_real check_smtp check_ssh check_tcp check_time check_ntp_time \
	check_ups check_users negate \
	urlize @EXTRAS@ \
	check_snmp

check_tcp_programs = check_ftp check_imap check_nntp check_pop \
	check_udp check_clamd @check_tcp_ssl@

EXTRA_PROGRAMS = check_mysql check_radius check_pgsql check_hpjd \
	check_swap check_fping check_ldap check_game check_dig \
	check_nagios check_by_ssh check_dns check_nt check_ide_smart	\
	check_procs check_mysql_query check_apt check_dbi check_curl \
	\
	tests/test_check_swap \
	tests/test_check_snmp \
	tests/test_check_disk

SUBDIRS = picohttpparser

np_test_scripts = tests/test_check_swap.t \
				  tests/test_check_snmp.t \
				  tests/test_check_disk.t

EXTRA_DIST = t \
			 tests \
			 $(np_test_scripts) \
			 negate.d \
			 check_swap.d \
			 check_ldap.d \
			 check_hpjd.d \
			 check_game.d \
			 check_radius.d \
			 check_disk.d \
			 check_time.d \
			 check_users.d \
			 check_load.d \
			 check_nagios.d \
			 check_dbi.d \
			 check_tcp.d \
			 check_real.d \
			 check_ssh.d \
			 check_nt.d \
			 check_dns.d \
			 check_mrtgtraf.d \
			 check_mysql_query.d \
			 check_mrtg.d \
			 check_ntp_peer.d \
			 check_apt.d \
			 check_pgsql.d \
			 check_procs.d \
			 check_ping.d \
			 check_by_ssh.d \
			 check_smtp.d \
			 check_snmp.d \
			 check_mysql.d \
			 check_ntp_time.d \
			 check_dig.d \
			 check_cluster.d \
			 check_ups.d \
			 check_fping.d

PLUGINHDRS = common.h

noinst_LIBRARIES = libnpcommon.a
noinst_PROGRAMS = @EXTRA_PLUGIN_TESTS@
# These two lines support "make check", but we use "make test"
check_PROGRAMS = @EXTRA_PLUGIN_TESTS@

libnpcommon_a_SOURCES = utils.c netutils.c sslutils.c runcmd.c	\
	popen.c utils.h netutils.h popen.h common.h runcmd.c runcmd.h


BASEOBJS = libnpcommon.a ../lib/libmonitoringplug.a ../gl/libgnu.a $(LIB_CRYPTO)
NETOBJS = $(BASEOBJS) $(EXTRA_NETOBLS)
NETLIBS = $(NETOBJS) $(SOCKETLIBS)
SSLOBJS = $(BASEOBJS) $(NETLIBS) $(SSLLIBS) $(LIB_CRYPTO)

TESTS_ENVIRONMENT = perl -I $(top_builddir) -I $(top_srcdir)

tap_ldflags = -L$(top_srcdir)/tap

TESTS = @PLUGIN_TEST@ @EXTRA_PLUGIN_TESTS@


test:
	perl -I $(top_builddir) -I $(top_srcdir) ../test.pl

test-debug:
	NPTEST_DEBUG=1 HARNESS_VERBOSE=1 perl -I $(top_builddir) -I $(top_srcdir) ../test.pl

##############################################################################
# the actual targets

check_apt_LDADD = $(BASEOBJS)
check_cluster_LDADD = $(BASEOBJS)
check_curl_CFLAGS = $(AM_CFLAGS) $(LIBCURLCFLAGS) $(URIPARSERCFLAGS) $(LIBCURLINCLUDE) $(URIPARSERINCLUDE) -Ipicohttpparser
check_curl_CPPFLAGS = $(AM_CPPFLAGS) $(LIBCURLCFLAGS) $(URIPARSERCFLAGS) $(LIBCURLINCLUDE) $(URIPARSERINCLUDE) -Ipicohttpparser
check_curl_LDADD = $(NETLIBS) $(LIBCURLLIBS) $(SSLOBJS) $(URIPARSERLIBS) picohttpparser/libpicohttpparser.a
check_dbi_LDADD = $(NETLIBS) $(DBILIBS)
check_dig_LDADD = $(NETLIBS)
check_disk_LDADD = $(BASEOBJS)
check_disk_SOURCES = check_disk.c check_disk.d/utils_disk.c
check_dns_LDADD = $(NETLIBS)
check_dummy_LDADD = $(BASEOBJS)
check_fping_LDADD = $(NETLIBS)
check_game_LDADD = $(BASEOBJS)
check_http_LDADD = $(SSLOBJS)
check_hpjd_LDADD = $(NETLIBS)
check_ldap_LDADD = $(NETLIBS) $(LDAPLIBS)
check_load_LDADD = $(BASEOBJS)
check_mrtg_LDADD = $(BASEOBJS)
check_mrtgtraf_LDADD = $(BASEOBJS)
check_mysql_CFLAGS = $(AM_CFLAGS) $(MYSQLCFLAGS)
check_mysql_CPPFLAGS = $(AM_CPPFLAGS) $(MYSQLINCLUDE)
check_mysql_LDADD = $(NETLIBS) $(MYSQLLIBS)
check_mysql_query_CFLAGS = $(AM_CFLAGS) $(MYSQLCFLAGS)
check_mysql_query_CPPFLAGS = $(AM_CPPFLAGS) $(MYSQLINCLUDE)
check_mysql_query_LDADD = $(NETLIBS) $(MYSQLLIBS)
check_nagios_LDADD = $(BASEOBJS)
check_nt_LDADD = $(NETLIBS)
check_ntp_LDADD = $(NETLIBS) $(MATHLIBS)
check_ntp_peer_LDADD = $(NETLIBS) $(MATHLIBS)
check_pgsql_LDADD = $(NETLIBS) $(PGLIBS)
check_ping_LDADD = $(NETLIBS)
check_procs_LDADD = $(BASEOBJS)
check_radius_LDADD = $(NETLIBS) $(RADIUSLIBS)
check_real_LDADD = $(NETLIBS)
check_snmp_SOURCES = check_snmp.c check_snmp.d/check_snmp_helpers.c
check_snmp_LDADD = $(BASEOBJS)
check_snmp_LDFLAGS = $(AM_LDFLAGS) `net-snmp-config --libs`
check_snmp_CFLAGS = $(AM_CFLAGS) `net-snmp-config --cflags`
check_smtp_LDADD = $(SSLOBJS)
check_ssh_LDADD = $(NETLIBS)
check_swap_SOURCES = check_swap.c check_swap.d/swap.c
check_swap_LDADD = $(MATHLIBS) $(BASEOBJS)
check_tcp_LDADD = $(SSLOBJS)
check_time_LDADD = $(NETLIBS)
check_ntp_time_LDADD = $(NETLIBS) $(MATHLIBS)
check_ups_LDADD = $(NETLIBS)
check_users_SOURCES = check_users.c check_users.d/users.c
check_users_LDADD = $(BASEOBJS) $(WTSAPI32LIBS) $(SYSTEMDLIBS)
check_by_ssh_LDADD = $(NETLIBS)
check_ide_smart_LDADD = $(BASEOBJS)
negate_LDADD = $(BASEOBJS)
urlize_LDADD = $(BASEOBJS)

if !HAVE_UTMPX
check_users_LDADD += popen.o
endif

tests_test_check_swap_LDADD = $(BASEOBJS) $(tap_ldflags) -ltap
tests_test_check_swap_SOURCES = tests/test_check_swap.c check_swap.d/swap.c
tests_test_check_snmp_LDADD = $(BASEOBJS) $(tap_ldflags) -ltap
tests_test_check_snmp_SOURCES = tests/test_check_snmp.c check_snmp.d/check_snmp_helpers.c
tests_test_check_disk_LDADD = $(BASEOBJS) $(tap_ldflags) check_disk.d/utils_disk.c -ltap
tests_test_check_disk_SOURCES = tests/test_check_disk.c

##############################################################################
# secondary dependencies

all-local: $(check_tcp_programs)

$(check_tcp_programs): check_tcp
	rm -f $@
	ln -s check_tcp $@

install-exec-hook:
	cd $(DESTDIR)$(libexecdir) && \
	for i in $(check_tcp_programs) ; do rm -f $$i; ln -s check_tcp $$i ; done ;\
	if [ -x check_ldap ] ; then rm -f check_ldaps ; ln -s check_ldap check_ldaps ; fi

clean-local:
	rm -f $(check_tcp_programs)
	rm -f NP-VERSION-FILE

uninstall-local:
	cd $(DESTDIR)$(libexecdir) && rm -f $(check_tcp_programs)
