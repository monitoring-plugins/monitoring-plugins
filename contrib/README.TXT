Contrib Plugins README
----------------------

This directory contains plugins which have been contributed by various people, but that
have not yet been incorporated into the core plugins distribution.

Most Perl plugins should work without modification.  Some of the C plugins may require
a few tweaks to compile.

If you have questions regarding the use of these plugins, try contacting the author(s)
or post a message to the nagiosplug-help mailing list (nagiosplug-help@lists.sourceforge.net)
requesting assistance.



Contrib Tarballs
----------------

In addition to the plugins located in this directory, there are some additional tarballs
containing plugins in the tarballs/ subdirectory.  They have not yet been organized.
A brief description of their contents follows.


berger-ping.tar.gz      - Perl script version of the check_ping plugin and a corresponding
                          CGI (mtr.cgi) that uses mtr to traceroute a path to a host.
                          (Gary Berger)

bowen-langley_plugins.tar.gz
			- Several C plugins including check_inode, check_boot, etc.
			  (Adam Bown & Thomas Langley)


check_bgp-1.0.tar.gz   - Perl script intended for monitoring BGP sessions on Cisco routers.
                         Uses Net::Telnet to telnet into a cisco router and
						 run "sh ip bgp"

check_memory.tgz        - C plugin to check available system memory

check_radius.tar.gz     - C program to check RADIUS authentication. This is a hacked version of 
	                  the Cistron Radiusd program radtest that acts as a plugin for Nagios.
 			  The vast majority of the code was written by someone at Livingston
			  Enterprises and Cistron.  NOTE: Due to the copyright restrictions in 
			  this code, it cannot be distributed under the GPL license, and thus 
		          will not appear in the core plugin distribution!
		          (Adam Jacob)

radius.tar.gz           - Code modifications necessary to make the radexample app 
			  supplied with the radiusclient code work as a RADIUS plugin
			  for Nagios (Nick Shore)

fetchlog-0.94.tar.gz   - C program: The fetchlog utility displays the last new messages of a
                         logfile. It is similar like tail(1) but offers some extra functionality  
                         for output formatting. fetchlog can be used standalone or as a Nagios
                         plugin to monitor local logfiles or together with Net-SNMP to monitor
                         remote logfiles. The README shows how to setup fetchlog for Nagios.
                         (Alexander Haderer)
