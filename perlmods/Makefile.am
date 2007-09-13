perlmoduledir = $(exec_prefix)/perl

all-local:
	$(top_srcdir)/tools/build_perl_modules -d $(perlmoduledir) -m .

install-exec-local:
	$(top_srcdir)/tools/build_perl_modules -d $(perlmoduledir) -i .

test:
	$(top_srcdir)/tools/build_perl_modules -d $(perlmoduledir) -t .

clean-local:
	$(top_srcdir)/tools/build_perl_modules -d $(perlmoduledir) -c .
