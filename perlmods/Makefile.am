perlmoduledir = $(exec_prefix)/perl

all-local:
	$(top_srcdir)/tools/build_perl_modules -d $(perlmoduledir) -em .

install-exec-local:
	$(top_srcdir)/tools/build_perl_modules -d $(perlmoduledir) -i .

# Don't run test-debug differently here yet
test test-debug:
	$(top_srcdir)/tools/build_perl_modules -d $(perlmoduledir) -t .

clean-local:
	$(top_srcdir)/tools/build_perl_modules -d $(perlmoduledir) -c .
