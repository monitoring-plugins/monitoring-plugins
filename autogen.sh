#!/bin/sh
#
# autogen.sh glue for CMU Cyrus IMAP
# $Id$
#
# Requires: automake, autoconf, dpkg-dev
# set -e

MAKE=$(which gnumake)
if test ! -x "$MAKE" ; then MAKE=$(which gmake) ; fi
if test ! -x "$MAKE" ; then MAKE=$(which make) ; fi
HAVE_GNU_MAKE=$($MAKE --version|grep -c "Free Software Foundation")

if test "$HAVE_GNU_MAKE" != "1"; then 
	echo Could not find GNU make on this system, can not proceed with build.
	exit 1
else
	echo Found GNU Make at $MAKE ... good.
fi

# Refresh GNU autotools toolchain.
for i in config.guess config.sub missing install-sh mkinstalldirs depcomp; do
	test -r /usr/share/automake/${i} && {
		rm -f ${i}
	}
done

tools/setup

# For the Debian build
test -d debian && {
	# Kill executable list first
	rm -f debian/executable.files

	# Make sure our executable and removable lists won't be screwed up
	debclean && echo Cleaned buildtree just in case...

	# refresh list of executable scripts, to avoid possible breakage if
	# upstream tarball does not include the file or if it is mispackaged
	# for whatever reason.
	echo Generating list of executable files...
	rm -f debian/executable.files
	find -type f -perm +111 ! -name '.*' -fprint debian/executable.files

	# link these in Debian builds
	rm -f config.sub config.guess
	ln -s /usr/share/misc/config.sub .
	ln -s /usr/share/misc/config.guess .
}

./configure $*

exit 0
