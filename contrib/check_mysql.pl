#!/nyet/bin/perl 
#
# (c)1999 Mitch Wright, NetLine Corporation
# Read the GNU copyright stuff for all the legalese
#
# Check to see that our MySQL server(s) are up and running.
# This plugin requires that mysqladmin(1) is installed on the system.
# Since it is part of the MySQL distribution, that should be a problem.
#
# If no parameters are giving, a usage statement is output.
#
# Exit 0 on success, providing some informational output
# Exit 2 on failure, provide what we can...
#

require 5.004;

sub usage;

my $TIMEOUT = 15;
my $MYSQLADMIN = "/usr/local/bin/mysqladmin";

my %ERRORS = ('UNKNOWN' , '-1',
              'OK' , '0',
              'WARNING', '1',
              'CRITICAL', '2');

my $host = shift || &usage(%ERRORS);
my $user = shift || &usage(%ERRORS);
my $pass = shift || "";
my $warn = shift || 60;
my $crit = shift || 100;

my $state = "OK";

# Just in case of problems, let's not hang Nagios
$SIG{'ALRM'} = sub {
     print ("ERROR: No response from MySQL server (alarm)\n");
     exit $ERRORS{"UNKNOWN"};
};
alarm($TIMEOUT);

open (OUTPUT,
      "$MYSQLADMIN -h $host -u $user --password=\"$pass\" version 2>&1
      |");

while (<OUTPUT>) {
  if (/failed/) { $state="CRITICAL"; s/.*://; $status=$_; last; }
  next if /^\s*$/;
  if (/^Server version\s+(\d+.*)/) { $version = $1; next; }
  if (/^Uptime:\s+(\d.*)/) { $uptime = $1; next; }
  if (/^Threads:\s+(\d+)\s+/) { $threads = $1; next; }
}

$status = "Version $version -- $threads Threads <br>Uptime $uptime" if
$state ne "CRITICAL";

if ($threads >= $warn) { $state = "WARNING"; }
if ($threads >= $crit) { $state = "CRITICAL"; }

print $status;
exit $ERRORS{$state};

sub usage {
   print "Required arguments not given!\n\n";
   print "MySQL status checker plugin for Nagios, V1.01\n";
   print "Copyright (c) 1999-2000 Mitch Wright \n\n";
   print "Usage: check_mysql.pl <host> <user> [<pass> [<warn>
   [<crit>]]]\n\n"; print "       <pass> = password to use for <user> at
   <host>\n"; print "       <warn> = number of threads to warn us
   about\n"; print "       <crit> = number of threads to scream at us
   about\n"; exit $ERRORS{"UNKNOWN"};
}
