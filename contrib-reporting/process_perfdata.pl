#!/usr/local/bin/perl -w
# author: Al Tobey <albert.tobey@priority-health.com>
# what:    process perfdata from Nagios and put it into RRD files
# license: GPL - http://www.fsf.org/licenses/gpl.txt
#
# Todo:
# * more documentation (POD) & comments
# * clean up a bit, make it more configurable - possibly a config file

use strict;
use lib qw( /opt/nagios/libexec );
use utils qw( %ERRORS );
use vars qw( $debug_file %data $debug $rrd_base $process_func $rrd_type );
use RRDs;
$debug_file = undef; #"/var/opt/nagios/perfdata.out";
$rrd_base = '/var/opt/nagios/rrds';
$process_func = \&process_multi;
$rrd_type = 'GAUGE';
$data{hostname}  = shift(@ARGV);
$data{metric}    = shift(@ARGV);
$data{timestamp} = shift(@ARGV);
$data{perfdata}  = join( " ", @ARGV ); $data{perfdata} =~ s/\s+/ /g;

# make sure there's data to work with
exit $ERRORS{OK} if ( !defined($data{hostname}) || !defined($data{metric})
    || !defined($data{timestamp}) || !defined($data{perfdata})
    || $data{perfdata} eq ' ' || $data{perfdata} eq '' );

if ( defined($debug_file) ) {
    open( LOGFILE, ">>$debug_file" );
    print LOGFILE "$data{hostname}\t$data{metric}\t$data{timestamp}\t$data{perfdata}\n\n";
}

# make sure host directory exists
if ( !-d "$rrd_base/$data{hostname}" ) {
    mkdir( "$rrd_base/$data{hostname}" )
        || warn "could not create host directory $rrd_base/$data{hostname}: $!";
}
our $rrd_dir = $rrd_base .'/'. $data{hostname};

# --------------------------------------------------------------------------- #
# do some setup based on the name of the metric

# host data
if ( $data{metric} eq "HOSTCHECK" ) {
    $rrd_dir .= '/hostperf';
}
# processing disk information
elsif ( $data{metric} =~ /_DISK$/ ) {
    $rrd_dir .= '/disk';
}
# network interface information
elsif ( $data{metric} =~ /^IFACE_/ ) {
    $rrd_dir .= '/interfaces';
    $rrd_type = [ 'COUNTER', 'COUNTER' ];
}
# process performance statistics
elsif ( $data{metric} =~ /_PROC$/ ) {
    $rrd_dir .= '/processes';
    $process_func = \&process_single;
    $rrd_type = [ 'COUNTER', 'GAUGE' ];
}
# a resonable guess
elsif ( $data{perfdata} =~ /=/ ) {
    $process_func = \&process_single;
}
# everything else
else {
    $rrd_dir .= '/other';
}

# --------------------------------------------------------------------------- #
# call the proper processing function set up above (functions defined below)
our @processed = ( $process_func->() );

# --------------------------------------------------------------------------- #

if ( !-d $rrd_dir ) {
    mkdir( $rrd_dir ) || warn "could not mkdir( $rrd_dir ): $!";
}
foreach my $datum ( @processed ) {
    if ( defined($debug_file) ) {
        print LOGFILE $datum->{rrd_name}, " = ", join('--',@{$datum->{data}}), "\n"
    }

    my $rrdfile = $rrd_dir.'/'.$datum->{rrd_name};

    # create the RRD file if it doesn't already exist
    if ( !-e $rrdfile ) {
        # create a non-useful datasource title for each "part"
        RRDs::create( $rrdfile, "-b", $data{timestamp}, "-s", 300, $process_func->($datum, 1),
            "RRA:AVERAGE:0.5:1:600",
            "RRA:MAX:0.5:1:600",
            "RRA:AVERAGE:0.5:6:600",
            "RRA:MAX:0.5:6:600",
            "RRA:AVERAGE:0.5:24:600",
            "RRA:MAX:0.5:24:600",
            "RRA:AVERAGE:0.5:288:600",
            "RRA:MAX:0.5:288:600"
        );
        if ( my $ERROR = RRDs::error ) { print "ERROR: $ERROR\n"; exit $ERRORS{UNKNOWN}; }
    }
    else {
        # create a template to make sure data goes into the RRD as planned
        if ( defined($debug_file) ) {
            print LOGFILE "updating $rrdfile with data:\n\t",
                join(':', $data{timestamp}, @{$datum->{data}}), "\n";
        }
        # update the RRD file
        RRDs::update( $rrdfile, '-t', $process_func->($datum),
            join(':', $data{timestamp}, @{$datum->{data}}) );
        if ( my $ERROR = RRDs::error ) { print "ERROR: $ERROR\n"; exit $ERRORS{UNKNOWN}; }
    }
}

# --------------------------------------------------------------------------- #

if ( defined($debug_file) ) {
    print LOGFILE "-------------------------------------------------------------------------------\n";
    close( LOGFILE );
}

exit $ERRORS{OK};

# /opt=value,value,value:/=value,value,value - into multiple rrd's
sub process_multi {
    my( $datum, $create ) = @_;

    # return a string for creating new RRDs
    if ( defined($create) && $create == 1 ) {
        my @DS = ();
        for ( my $i=0; $i<scalar(@{$datum->{data}}); $i++ ) {
            # allow the RRD type to be set in the switch/if above
            my $tmp_rrd_type = $rrd_type;
            if ( ref($rrd_type) eq 'ARRAY' ) { $tmp_rrd_type = $rrd_type->[$i] } 
            # put the new datasource into the list
            push( @DS, "DS:$data{metric}$i:GAUGE:86400:U:U" );
        }
        return @DS;
    }
    # return a template for updating an RRD
    elsif ( defined($datum) && !defined($create) ) {
        my @template = ();
        for ( my $i=0; $i<scalar(@{$datum->{data}}); $i++ ) {
            push( @template, $data{metric}.$i );
        }
        return join( ':', @template );
    }
    # break the data up into parts for processing (updates and creates)
    else {
        my @processed = ();
        foreach my $part ( split(/:/, $data{perfdata}) ) { # break the line into parts
            my @parts = split( /,/, $part ); # break the part into parts
            my $rrd_name = $parts[0]; # figure out a good name for the RRD
            if ( $parts[0] =~ /^\// ) { # handle /'s in disk names
                $rrd_name = $parts[0];
                $rrd_name =~ s#/#_#g; $rrd_name =~ s/^_//; $rrd_name =~ s/_$//;
                if ( $parts[0] eq '/' ) { $rrd_name = 'root' };
            }
            # store our munged data in an array of hashes
            push( @processed, { rrd_name => $rrd_name, name => shift(@parts), data => [ @parts ] } );
        }
        return @processed;
    }
}

# name=value:name=value - into one rrd
sub process_single {
    my( $datum, $create ) = @_;

    my( @names, @values ) = ();
    foreach my $part ( split(/:/, $data{perfdata}) ) {
        my( $name, $value ) = split( /=/, $part );
        push( @names,  $name  );
        push( @values, $value );
    }

    if ( defined($create) && $create == 1 ) {
        my @DS = ();
        for( my $i=0; $i<scalar(@names); $i++ ) {
            my $tmp_rrd_type = $rrd_type;
            if ( ref($rrd_type) eq 'ARRAY' ) { $tmp_rrd_type = $rrd_type->[$i] } 
            push( @DS, 'DS:'.$names[$i].":$tmp_rrd_type:86400:U:U" );
        }
        return @DS;
    }
    elsif ( defined($datum) && !defined($create) ) {
        return join( ':', @names );
    }
    else {
        return( {rrd_name=>lc($data{metric}), name=>$data{metric}, data=>[@values]} );
    }
}
