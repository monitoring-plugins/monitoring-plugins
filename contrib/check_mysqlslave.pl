#!/usr/bin/perl -w
#
# check_mysqlslave.pl - nagios plugin
#
# 
# Copyright 2002 Mario Witte
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
#
# Credits: 
# - Thanks to Christoph Kron <ck@zet.net> for check_ifstatus.pl
#   I used check_ifstatus.pl as a layout when writing this
#
# Report bugs to: chengfu@users.sourceforge.net
#
# 20.09.2002 Version 0.1


use strict;
use lib "/usr/local/nagios/libexec";
use utils qw($TIMEOUT %ERRORS &print_revision &support);

use DBI;
use DBD::mysql;
use Getopt::Long;
Getopt::Long::Configure('bundling');

# Predeclare some variables
my $PROGNAME = 'check_mysqlslave';
my $REVISION = '0.1';
my $status;
my $state = 'UNKNOWN';
my $opt_V;
my $opt_h;
my $port = 3306;
my $hostname;
my $user = 'root';
my $pass = '';
my $driver;
my $dbh;
my $query;
my $result;
my $data;

# Just in case of problems, let's not hang Nagios
$SIG{'ALRM'} = sub {
     print ("ERROR: No response from $hostname (alarm timeout)\n");
     exit $ERRORS{"UNKNOWN"};
};
alarm($TIMEOUT);

$status = GetOptions(
		"V"	=> \$opt_V, "version"	=> \$opt_V,
		"h"	=> \$opt_h, "help"	=> \$opt_h,
		"p=i"	=> \$port, "port=i"	=> \$port,
		"H=s"	=> \$hostname, "hostname=s"	=> \$hostname,
		"u=s"	=> \$user, "user=s"	=> \$user,
		"P=s"	=> \$pass, "pass=s"	=> \$pass,
		);

		
if ($status == 0) {
	print_help() ;
	exit $ERRORS{'OK'};
}

if ($opt_V) {
	print_revision($PROGNAME,'$Revision$REVISION .' $ ');
	exit $ERRORS{'OK'};
}

if ($opt_h) {
	print_help();
	exit $ERRORS{'OK'};
}

if (! utils::is_hostname($hostname)){
	usage();
	exit $ERRORS{"UNKNOWN"};
}


$driver = 'DBI:mysql::'. $hostname; 

eval {
	$dbh = DBI->connect($driver, $user, $pass, { RaiseError => 1, PrintError => 0});
};
if ($@) {
	$status = $@;
	if ($status =~ /^.*failed:\ (.+)\ at\ $0/i) { $status = $1; }
	$state='CRITICAL';
	print $state .': Connect failed: '."$status\n";
	exit ($ERRORS{$state});
}

eval {
	$query = 'SHOW SLAVE STATUS';
	$result = $dbh->prepare($query);
	$result->execute;
	$data = $result->fetchrow_hashref();
	$result->finish();
	$dbh->disconnect();
};
if ($@) {
	$status = $@;
	$status =~ s/\n/ /g;
	if ($status =~ /^DB[ID].*(failed|prepare):\ (.+)\ at\ $0/i) { $status = $2; }
	$state = 'CRITICAL';
	print $state .': Couldn\'t check slave: '."$status\n";
	exit($ERRORS{$state});
}

if ($data->{'Slave_Running'} eq 'Yes') {
	$status = 'Replicating from '. $data->{'Master_Host'};
	$state = 'OK';
	print $state .': '. $status ."\n";
	exit($ERRORS{$state});
} elsif ($data->{'Slave_Running'} eq 'No') {
	if (length($data->{'Last_error'}) > 0) {
		$status = 'Slave stopped with error message';
		$state = 'CRITICAL';
		print $state .': '. $status ."\n";
		exit($ERRORS{$state});
	} else {
		$status = 'Slave stopped without errors';
		$state = 'WARNING';
		print $state .': '. $status ."\n";
		exit($ERRORS{$state});
	}
} else {
	$status = 'Unknown slave status: (Running: '. $data->{'Slave_Running'} .')';
	$state = 'UNKNOWN';
	print $state .': '. $status ."\n";
	exit($ERRORS{$state});
}

sub usage {
	printf "\nMissing arguments!\n";
	printf "\n";
	printf "check_mysqlslave -H <hostname> [-p <port> -u <username> -P <password>]\n";
	printf "Copyright 2002 Mario Witte\n";
	printf "\n\n";
	support();
	exit $ERRORS{"UNKNOWN"};
}

sub print_help {
	printf "check_mysqlslave plugin for Nagios checks \n";
  	printf "if the replication on a backup mysql-server\n";
	printf "is up and running\n";
	printf "\nUsage:\n";
	printf "   -H (--hostname)   Hostname to query\n";
	printf "   -p (--port)       mysql port (default: 3306)\n";
	printf "   -u (--user)       username for accessing mysql host\n";
	printf "                     (default: root)\n";
	printf "   -P (--pass)       password for accessing mysql host\n";
	printf "                     (default: '')\n";
	printf "   -V (--version)    Plugin version\n";
	printf "   -h (--help)       usage help \n\n";
	print_revision($PROGNAME, '$Revision$REVISION .' $');
	
}
