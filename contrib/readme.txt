Contrib Plugins README
----------------------

This directory contains plugins which have been contributed by various people, but that
have not yet been incorporated into the core plugins distribution.

If you have questions regarding the use of these plugins, try contacting the author(s)
or post a message to the nagios-users mailing list (nagios-users@onelist.com)
requesting assistance.


Plugin Overview
---------------

berger-ping.tar.gz      - Perl script version of the check_ping plugin and a corresponding
                          CGI (mtr.cgi) that uses mtr to traceroute a path to a host.
                          (Gary Berger)

bowen-langley_plugins.tar.gz
			- Several C plugins including check_inode, check_boot, etc.
			  (Adam Bown & Thomas Langley)


check_bgpstate.tar.gz   - Perl script intended for monitoring BGP sessions on Cisco routers.
			  Only useful if you are using BGP4 as a routing protocol.  For 
			  critical alert the AS (autonomous system) number has to be in the
			  uplinks hash (see the source code).  Requires SNMP read community.
			  (Christoph Kron)

check_breeze.tar.gz	- Perl script to test signal strength on Breezecom wireless
			  equipment (Jeffrey Blank)

check_dns_random.tar.gz - Perl script to see if dns resolves hosts randomly from a list 
                          using the check_dns plugin (Richard Mayhew)

check_flexlm.tar.gz	- Perl script to check a flexlm licensing manager using lmtest
			  (Ernst-Dieter Martin)

check_hltherm.tar.gz    - C program to check the temperature on a Hot Little Therm temperature
                          probe.  The HLT device, along with temperature probes, can be obtained
 			  from Spiderplant at http://www.spiderplant.com
			  (Ethan Galstad)

check_ifoperstatus.tar.gz
			- Perl script that checks the operational interface status (up/down) for
			  one interface on cisco/ascend routers. Especially useful for monitoring 
			  leased lines.  Requires SNMP read community and SNMP interface key.
			  (Christoph Kron)	   

check_ifstatus.tar.gz   - Perl script that checks operational interface status for every interface
                          on cisco routers. Requires SNMP read community.
			  (Christoph Kron)

check_ipxping.tar.gz    - C program that it similiar to the check_ping plugin, except that it
                          send IPX ping packets to Novell servers or other IPX devices.  This
                 	  requires the ipxping binary for Linux systems.  It does NOT by work
                          without modification with the ipxping binary for SunOS/Solaris.
			  (Ethan Galstad)

check_maxchannels.tar.gz
			- Perl script that can only be used for monitoring Ascend/Lucent Max/TNT
			  access server. Checks ISDN channels and modem cards. Also shows ISDN and
			  modem usage.  Requires SNMP read community.
			  (Christoph Kron)

check_maxwanstate.tar.gz 
			- Perl script that can only be used for monitoring Ascend/Lucent Max/TNT
			  access server. Checks if every enabled E1/T1 interface is operational
			  (link active). Requires SNMP read community.
			  (Christoph Kron)

check_memory.tgz	- C program to check available system memory - RAM, swap, buffers,
			  and cache (Joshua Jackson)

check_nfs.tar.gz	- Perl script to test and NFS server using rpcinfo
			  (Ernst-Dieter Martin)
			  <NOW PART OF check_rpc IN CORE>

check_ntp.tar.gz        - Perl script to check an NTP time source (Bo Kernsey)
			  <MOVED TO CORE>

check_ora.tar.gz        - Shell script that will check an Oracle database and the TNS listener.
			  Unlike the check_oracle plugin, this plugin detects when a database is
			  down and does not create temp files (Jason Hedden)
			  <MOVED TO CORE>

check_pop3.tar.gz       - Perl script that checks to see if POP3 is running and whether or not
			  authentication can take place (Richard Mayhew)

check_radius.tar.gz     - C program to check RADIUS authentication. This is a hacked version of 
	                  the Cistron Radiusd program radtest that acts as a plugin for Nagios.
 			  The vast majority of the code was written by someone at Livingston
			  Enterprises and Cistron.  NOTE: Due to the copyright restrictions in 
			  this code, it cannot be distributed under the GPL license, and thus 
		          will not appear in the core plugin distribution!
		          (Adam Jacob)

check_real.tar.gz	- C program to check the status of a REAL media server 
			  (Pedro Leite)
			  <MOVED TO CORE>

check_rpc.pl.gz		- Perl script to check rpc services.  Will check to see if the a specified
			  program is running on the specified server (Chris Kolquist)

check_sap.tar.gz	- Shell script to check an SAP message or application server.  Requires
			  that you install the saprfc-devel-45A-1.i386.rpm (or higher) package 
			  on your system.  The package can be obtained from the SAP FTP site in 
			  the /general/misc/unsupported/linux directory. (Kavel Salavec)

check_uptime.tar.gz     - C program to check system uptime.  Must be compiled with the release
                          1.2.8 or later of the plugins. (Teresa Ramanan)

check_wave.tar.gz	- Perl script to test signal strength on Speedlan wireless
			  equipment (Jeffrey Blank)

hopcroft-plugins.tar.gz - Various example plugin scripts contributed by Stanley Hopcroft.
			  Includes a plugin to check Internet connectivity by checking various
			  popular search engines, a plugin to check the availability of login
 			  to a TN/3270 mainframe database using Expect to search for "usual"
			  screens, and another plugin to test the availability of a database
			  search via the web.
			  (Stanley Hopcroft)

maser-oracle.tar.gz     - This is a modification to the check_oracle plugin script that returns
			  the response time in milliseconds.  Requires the Oracle tnsping utility.
			  (Christoph Maser)

radius.tar.gz           - Code modifications necessary to make the radexample app 
			  supplied with the radiusclient code work as a RADIUS plugin
			  for Nagios (Nick Shore)

vincent-check_radius.tar.gz
			- C program to check RADIUS authentication.  Requires the radiusclient 
			  library available from ftp://ftp.cityline.net/pub/radiusclient/
			  (Robert August Vincent II)
			  <MOVED TO CORE>

weipert-mysql.tar.gz    - C program to check a connection to a MySQL database server, with an
			  optional username and password.  Requires mysql.h and libmysqlclient
			  to compile (Time Weipert)

wright-mysql.tar.gz     - Perl script to check MySQL database servers.  Requires that mysqladmin(1)
			  be installed on the system (included in the MySQL distribution).  This
			  plugin can accept warning and critical thresholds for the number of threads
			  in use by the server (Mitch Wright)


