## Process this file with automake to produce Makefile.in

VPATH = $(top_srcdir) $(top_srcdir)/lib $(top_srcdir)/plugins $(top_srcdir)/plugins/t 

INCLUDES = -I.. -I$(top_srcdir)/lib @LDAPINCLUDE@ @PGINCLUDE@ @SSLINCLUDE@ 

libexec_PROGRAMS = check_disk check_dummy check_http check_load \
	check_mrtg check_mrtgtraf check_nwstat check_overcr check_ping \
	check_procs check_real check_smtp check_ssh check_tcp check_time \
	check_udp check_ups check_users check_vsz negate urlize \
	@EXTRAS@

EXTRA_PROGRAMS = check_mysql check_radius check_pgsql check_snmp check_hpjd \
	check_swap check_fping check_ldap check_game check_dig \
	check_nagios check_by_ssh check_dns check_nt

check_tcp_programs = check_ftp check_imap check_nntp check_pop

EXTRA_DIST = t utils.c netutils.c popen.c utils.h netutils.h popen.h common.h 

PLUGINHDRS = common.h config.h

BASEOBJS = utils.o ../lib/libnagiosplug.a
NETOBJS = netutils.o $(BASEOBJS)
NETLIBS = $(NETOBJS) $(SOCKETLIBS)

TESTS_ENVIRONMENT = perl -I $(top_builddir) -I $(top_srcdir)

TESTS = @PLUGIN_TEST@

test:
	perl -I $(top_builddir) -I $(top_srcdir) ../test.pl

AM_INSTALL_PROGRAM_FLAGS = @INSTALL_OPTS@

##############################################################################
# the actual targets

check_dig_LDADD = $(BASEOBJS) popen.o
check_disk_LDADD = $(BASEOBJS) popen.o
check_dns_LDADD = $(BASEOBJS) popen.o
check_dummy_LDADD = $(BASEOBJS)
check_fping_LDADD = $(BASEOBJS) popen.o
check_game_LDADD = $(BASEOBJS)
check_http_LDADD = $(NETLIBS) $(SSLLIBS)
check_hpjd_LDADD = $(BASEOBJS) popen.o
check_ldap_LDADD = $(NETLIBS) $(LDAPLIBS)
check_load_LDADD = $(BASEOBJS) popen.o
check_mrtg_LDADD = $(BASEOBJS)
check_mrtgtraf_LDADD = $(BASEOBJS)
check_mysql_LDADD = $(BASEOBJS) $(MYSQLLIBS)
check_nagios_LDADD = $(BASEOBJS) popen.o
check_nt_LDADD = $(NETLIBS) 
check_nwstat_LDADD = $(NETLIBS)
check_overcr_LDADD = $(NETLIBS)
check_pgsql_LDADD = $(BASEOBJS) $(PGLIBS)
check_ping_LDADD = $(BASEOBJS) popen.o
check_procs_LDADD = $(BASEOBJS) popen.o
check_radius_LDADD = $(BASEOBJS) $(RADIUSLIBS)
check_real_LDADD = $(NETLIBS)
check_snmp_LDADD = $(BASEOBJS) popen.o
check_smtp_LDADD = $(NETLIBS)
check_ssh_LDADD = $(NETLIBS)
check_swap_LDADD = $(BASEOBJS) popen.o
check_tcp_LDADD = $(NETLIBS) $(SSLLIBS)
check_time_LDADD = $(NETLIBS)
check_udp_LDADD = $(NETLIBS)
check_ups_LDADD = $(NETLIBS)
check_users_LDADD = $(BASEOBJS) popen.o
check_vsz_LDADD = $(BASEOBJS) popen.o
check_by_ssh_LDADD = $(BASEOBJS) popen.o
negate_LDADD = $(BASEOBJS) popen.o
urlize_LDADD = $(BASEOBJS) popen.o

check_dig_DEPENDENCIES = check_dig.c $(BASEOBJS) popen.o $(DEPLIBS)
check_disk_DEPENDENCIES = check_disk.c $(BASEOBJS) popen.o $(DEPLIBS)
check_dns_DEPENDENCIES = check_dns.c $(BASEOBJS) popen.o $(DEPLIBS)
check_dummy_DEPENDENCIES = check_dummy.c  $(DEPLIBS)
check_fping_DEPENDENCIES = check_fping.c $(BASEOBJS) popen.o $(DEPLIBS)
check_game_DEPENDENCIES = check_game.c  $(DEPLIBS)
check_http_DEPENDENCIES = check_http.c $(NETOBJS) $(DEPLIBS)
check_hpjd_DEPENDENCIES = check_hpjd.c $(BASEOBJS) popen.o $(DEPLIBS)
check_ldap_DEPENDENCIES = check_ldap.c $(NETOBJS) $(DEPLIBS)
check_load_DEPENDENCIES = check_load.c $(BASEOBJS) popen.o $(DEPLIBS)
check_mrtg_DEPENDENCIES = check_mrtg.c  $(DEPLIBS)
check_mrtgtraf_DEPENDENCIES = check_mrtgtraf.c  $(DEPLIBS)
check_mysql_DEPENDENCIES = check_mysql.c  $(DEPLIBS)
check_nagios_DEPENDENCIES = check_nagios.c $(BASEOBJS) popen.o $(DEPLIBS)
check_nt_DEPENDENCIES = check_nt.c $(NETOBJS) $(DEPLIBS)
check_nwstat_DEPENDENCIES = check_nwstat.c $(NETOBJS) $(DEPLIBS)
check_overcr_DEPENDENCIES = check_overcr.c $(NETOBJS) $(DEPLIBS)
check_pgsql_DEPENDENCIES = check_pgsql.c  $(DEPLIBS)
check_ping_DEPENDENCIES = check_ping.c $(BASEOBJS) popen.o $(DEPLIBS)
check_procs_DEPENDENCIES = check_procs.c $(BASEOBJS) popen.o $(DEPLIBS)
check_radius_DEPENDENCIES = check_radius.c  $(DEPLIBS)
check_real_DEPENDENCIES = check_real.c $(NETOBJS) $(DEPLIBS)
check_snmp_DEPENDENCIES = check_snmp.c $(BASEOBJS) popen.o $(DEPLIBS)
check_smtp_DEPENDENCIES = check_smtp.c $(NETOBJS) $(DEPLIBS)
check_ssh_DEPENDENCIES = check_ssh.c $(NETOBJS) $(DEPLIBS)
check_swap_DEPENDENCIES = check_swap.c $(BASEOBJS) popen.o $(DEPLIBS)
check_tcp_DEPENDENCIES = check_tcp.c $(NETOBJS) $(DEPLIBS)
check_time_DEPENDENCIES = check_time.c $(NETOBJS) $(DEPLIBS)
check_udp_DEPENDENCIES = check_udp.c $(NETOBJS) $(DEPLIBS)
check_ups_DEPENDENCIES = check_ups.c $(NETOBJS) $(DEPLIBS)
check_users_DEPENDENCIES = check_users.c $(BASEOBJS) popen.o $(DEPLIBS)
check_vsz_DEPENDENCIES = check_vsz.c $(BASEOBJS) popen.o $(DEPLIBS)
check_by_ssh_DEPENDENCIES = check_by_ssh.c $(BASEOBJS) popen.o $(DEPLIBS)
negate_DEPENDENCIES = negate.c $(BASEOBJS) popen.o $(DEPLIBS)
urlize_DEPENDENCIES = urlize.c $(BASEOBJS) popen.o $(DEPLIBS)

##############################################################################
# secondary dependencies

popen.o: popen.c popen.h $(PLUGINHDRS)

utils.o: utils.c utils.h $(PLUGINHDRS)

netutils.o: netutils.c netutils.h $(PLUGINHDRS)

all-local: $(check_tcp_programs)

$(check_tcp_programs): check_tcp
	ln -sf check_tcp $@

install-exec-hook:
	cd $(DESTDIR)$(libexecdir) && \
	for i in $(check_tcp_programs) ; do rm -f $$i; ln -sf check_tcp $$i ; done

clean-local:
	rm -f $(check_tcp_programs)

uninstall-local:
	cd $(DESTDIR)$(libexecdir) && rm -f $(check_tcp_programs)
