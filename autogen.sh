#!/bin/sh
#
# Requires: automake, autoconf, dpkg-dev
# set -e

./tools/setup

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
