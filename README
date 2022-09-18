Monitoring Plugins
==================

* For instructions on installing these plugins for use with your monitoring
  system, see below.  In addition, generic instructions for the GNU
  toolchain can be found in the `INSTALL` file.

* For major changes between releases, read the `NEWS` file.

* For information on detailed changes that have been made or plugins
  that have been added, read the `ChangeLog` file.

* Some plugins require that you have additional programs or
  libraries installed on your system before they can be used.  Plugins that
  are dependent on other programs/libraries that are missing are usually not
  compiled.  Read the `REQUIREMENTS` file for more information.

* Individual plugins are self-documenting.  All plugins that comply with
  the basic guidelines for development will provide detailed help when
  invoked with the `-h` or `--help` options.

You can check the latest plugins at:

* <https://www.monitoring-plugins.org/>

Send an email to <help@monitoring-plugins.org> for assistance.  Please
include the OS type and version that you are using.  Also, run the plugin
with the `-vvv` option and provide the resulting version information.  Of
course, there may be additional diagnostic information required as well.
Use good judgment.

Send an email to <devel@monitoring-plugins.org> for developer discussions.

For patch submissions and bug reports, please use the appropriate resources
at:

* <https://github.com/monitoring-plugins>


Installation Instructions
-------------------------

1.  If you are using the Git tree, you will need m4, gettext, automake, and
    autoconf.  To start out, run:

    	./tools/setup

    For more detail, see the developer guidelines at
    <https://www.monitoring-plugins.org/doc/guidelines.html>.

2.  Run the configure script to initialize variables and create a Makefile,
    etc.

    	./configure --prefix=BASEDIRECTORY --with-cgiurl=SOMEURL

    Replace `BASEDIRECTORY` with the path of the directory under which your
    monitoring system is installed (default is `/usr/local`), and replace
    `SOMEURL` with the path used to access the monitoring system CGIs with a
    web browser (default is `/nagios/cgi-bin`).

3.  Compile the plugins with the following command:

    	make

4.  Install the compiled plugins and plugin scripts with the following
    command:

    	make install

    The installation procedure will attempt to place the plugins in a
    `libexec/` subdirectory in the base directory you specified with the
    `--prefix` argument to the configure script.

5.  There are some plugins that require setuid.  If you run make install as
    a non-root user, they will not be installed.  To install, switch to root
    and run:

    	make install-root

That's it!  If you have any problems or questions, feel free to send an
email to <help@monitoring-plugins.org>.


License Notice
--------------

You can redistribute and/or modify this software under the terms of the GNU
General Public License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version; with the
additional exemption that compiling, linking, and/or using OpenSSL is
allowed.

This software is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.

See the `COPYING` file for the complete text of the GNU General Public
License, version 3.
