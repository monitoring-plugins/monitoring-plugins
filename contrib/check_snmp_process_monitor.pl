#!/usr/local/bin/perl
# author: Al Tobey <albert.tobey@priority-health.com>
# what:   monitor a process using the host-resources mib
# license: GPL - http://www.fsf.org/licenses/gpl.txt
#
# Todo:
# * implement memory and cpu utilization checks
# * maybe cache pids in DBM files if snmp agents get overworked
###############################################################################
# to get a list of processes over snmp try this command:
# snmptable -v2c -c public hostname hrSWRunTable
# for just a list of valid arguments for the '-e' option:
# snmpwalk -v2c -c public hostname hrSWRunName |perl -pe 's:.*/::'
###############################################################################

use strict;
require 5.6.0;
use lib qw( /opt/nagios/libexec /usr/local/libexec );
use utils qw(%ERRORS $TIMEOUT &print_revision &support &usage);
use SNMP 5.0;
use Getopt::Long;
use vars qw( $exit $opt_version $opt_timeout $opt_help $opt_command $opt_host $opt_community $opt_verbose $opt_warning $opt_critical $opt_memory $opt_cpu $opt_port $opt_regex $opt_stats %processes $snmp_session $PROGNAME $TIMEOUT );

$PROGNAME      = "snmp_process_monitor.pl";
$opt_verbose   = undef;
$opt_host      = undef;
$opt_community = 'public';
$opt_command   = undef;
$opt_warning   = [ 1, -1 ];
$opt_critical  = [ 1, -1 ];
$opt_memory    = undef;
$opt_cpu       = undef;
$opt_port      = 161;
%processes     = ();
$exit          = 'OK';

sub process_options {
    my( $opt_crit, $opt_warn ) = ();
    Getopt::Long::Configure( 'bundling' );
    GetOptions(
        'V'     => \$opt_version,       'version'     => \$opt_version,
        'v'     => \$opt_verbose,       'verbose'     => \$opt_verbose,
        'h'     => \$opt_help,          'help'        => \$opt_help,
        's'     => \$opt_stats,         'statistics'  => \$opt_stats,
        'H:s'   => \$opt_host,          'hostname:s'  => \$opt_host,
        'p:i'   => \$opt_port,          'port:i'      => \$opt_port,
        'C:s'   => \$opt_community,     'community:s' => \$opt_community,
        'c:s'   => \$opt_crit,          'critical:s'  => \$opt_crit,
        'w:s'   => \$opt_warn,          'warning:s'   => \$opt_warn,
        't:i'   => \$TIMEOUT,           'timeout:i'   => \$TIMEOUT,    
        'e:s'   => \$opt_command,       'command:s'   => \$opt_command,
        'r:s'   => \$opt_regex,         'regex:s'     => \$opt_regex,
        'cpu:i' => \$opt_cpu,           'memory:i'    => \$opt_memory,
    );
    if ( defined($opt_version) ) { local_print_revision(); }
    if ( defined($opt_verbose) ) { $SNMP::debugging = 1; }
    if ( !defined($opt_host) || defined($opt_help) || (!defined($opt_command) && !defined($opt_regex)) ) {
        print_help();
        exit $ERRORS{UNKNOWN};
    }

    if ( defined($opt_crit) ) {
        if ( $opt_crit =~ /,/ ) {
            $opt_critical = [ split(',', $opt_crit) ];
        }
        else {
            $opt_critical = [ $opt_crit, -1 ];
        }
    }
    if ( defined($opt_warn) ) {
        if ( $opt_warn =~ /,/ ) {
            $opt_warning = [ split(',', $opt_warn) ];
        }
        else {
            $opt_warning = [ $opt_crit, -1 ];
        }
    }
}

sub local_print_revision {
        print_revision( $PROGNAME, '$Revision$ ' )
}

sub print_usage {
    print "Usage: $PROGNAME -H <host> -C <snmp_community> -e <command> [-w <low>,<high>] [-c <low>,<high>] [-t <timeout>]\n";
}

sub print_help {
    local_print_revision();
    print "Copyright (c) 2002 Al Tobey <albert.tobey\@priority-health.com>\n\n",
          "SNMP Process Monitor plugin for Nagios\n\n";
    print_usage();
    print <<EOT;
-v, --verbose
   print extra debugging information
-h, --help
   print this help message
-H, --hostname=HOST
   name or IP address of host to check
-C, --community=COMMUNITY NAME
   community name for the host's SNMP agent
-e, --command=COMMAND NAME (ps -e style)
   what command should be monitored?
-r, --regex=Perl RE
   use a perl regular expression to find your process
-w, --warning=INTEGER[,INTEGER]
   minimum and maximum number of processes before a warning is issued (Default 1,-1)
-c, --critical=INTEGER[,INTEGER]
   minimum and maximum number of processes before a critical is issued (Default 1,-1)
EOT
}

sub verbose (@) {
    return if ( !defined($opt_verbose) );
    print @_;
}

sub check_for_errors {
    if ( $snmp_session->{ErrorNum} ) {
        print "UNKNOWN - error retrieving SNMP data: $snmp_session->{ErrorStr}\n";
        exit $ERRORS{UNKNOWN};
    }
}

# =========================================================================== #
# =====> MAIN
# =========================================================================== #
process_options();

alarm( $TIMEOUT ); # make sure we don't hang Nagios

$snmp_session = new SNMP::Session(
    DestHost => $opt_host,
    Community => $opt_community,
    RemotePort => $opt_port,
    Version   => '2c'
);

my $process_count = SNMP::Varbind->new( ['hrSystemProcesses', 0] );
$snmp_session->get( $process_count ); 
check_for_errors();

# retrieve the data from the remote host
my( $names, $index ) = $snmp_session->bulkwalk( 0, $process_count->val, [['hrSWRunName'], ['hrSWRunIndex']] );
check_for_errors();

alarm( 0 ); # all done with the network connection

my %namecount = ();
foreach my $row ( @$names ) {
    $processes{$row->iid}->{name} = $row->val;
    $processes{$row->iid}->{name} =~ s#.*/##; # strip path

    if ( defined($opt_regex) ||
        ($row->val =~ /(perl|\/usr\/bin\/sh|\/bin\/bash|\/bin\/sh)$/
        && $opt_command !~ /(perl|\/usr\/bin\/sh|\/bin\/bash|\/bin\/sh)$/) ) {

        # fetch the runtime parameters of the process
        my $parm_var = SNMP::Varbind->new( ['hrSWRunParameters', $row->iid] );
        $snmp_session->get( $parm_var );
        check_for_errors();

        # only strip if we're looking for a specific command
        if ( defined($opt_command) ) {
            verbose "process ",$row->iid," uses $1 as an interpreter - getting parameters\n";
            $processes{$row->iid}->{name} = $parm_var->val;
            # strip path name off the front
            $processes{$row->iid}->{name} =~ s#.*/##;
            # strip everything from the first space to the end
            $processes{$row->iid}->{name} =~ s/\s+.*$//;
        }
        else {
            # get the longer full-path style listing
            my $path_var = SNMP::Varbind->new( ['hrSWRunPath', $row->iid] );
            $snmp_session->get( $path_var );
            check_for_errors();

            # use the full 'ps -efl' style listing for regular expression matching
            $processes{$row->iid}->{name} = $path_var->val.' '.$parm_var->val;
        }
    }
}
foreach my $row ( @$index ) {
    $processes{$row->iid}->{pid}  = $row->val;
}

my @pids    = ();
my @matches = ();
foreach my $key ( keys(%processes) ) {
    if ( defined($opt_command) && $processes{$key}->{name} eq $opt_command ) {
        push( @matches, $processes{$key} );
        push( @pids, $processes{$key}->{pid} );
        verbose "process '$processes{$key}->{name}' has pid ",
            "$processes{$key}->{pid} and index $key\n";
    }
    elsif ( defined($opt_regex) && $processes{$key}->{name} =~ /$opt_regex/o ) {
        push( @matches, $processes{$key} );
        push( @pids, $processes{$key}->{pid} );
        verbose "process '$processes{$key}->{name}' has pid ",
            "$processes{$key}->{pid} and index $key\n";
    }
}
my $count = @matches;

# warning, critical
if ( ($opt_warning->[0] > 0 && $opt_warning->[0]  >  $count)
  || ($opt_warning->[1] > 0 && $opt_warning->[1]  <= $count) ) {
    $exit = 'WARNING';
}
if ( ($opt_critical->[0] > 0 && $opt_critical->[0]  >  $count)
  || ($opt_critical->[1] > 0 && $opt_critical->[1]  <= $count) ) {
    $exit = 'CRITICAL';
}

print "$exit - $count processes with pid(s) ",join(',',@pids);

# print the number of processes if statistics are requested
if ( defined($opt_stats) ) {
    print "|count:$count\n";
}
else {
    print "\n";
}

exit $ERRORS{$exit};


