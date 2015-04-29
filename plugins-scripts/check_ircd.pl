#!@PERL@ -w
#
# Copyright (C) 1999 Richard Mayhew <netsaint@splash.co.za>
# Copyright (C) 2014, SUSE Linux Products GmbH, Nuremberg
# Author:       Richard Mayhew - South Africa
# rewritten by: Lars Vogdt <lars@linux-schulserver.de>
#
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
#
# * Neither the name of the Novell nor the names of its contributors may be
#   used to endorse or promote products derived from this software without
#   specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# Special thanks to Richard Mayhew for the original plugin written in 
# 1999/09/20. Some code taken from Charlie Cook (check_disk.pl).
#
use Getopt::Long;
use IO::Socket::INET6;
use strict;
use vars qw($PROGNAME $VERSION);
use vars qw($opt_V $opt_h $opt_t $opt_p $opt_H $opt_w $opt_c $ssl $verbose);
use lib '@libexecdir@';
use utils qw($TIMEOUT %ERRORS &print_revision &support &usage);

# ----------------------------------------------------[ Function Prototypes ]--
sub print_help ();
sub print_usage ();

# -------------------------------------------------------------[ Enviroment ]--
$ENV{PATH}     = '';
$ENV{ENV}      = '';
$ENV{BASH_ENV} = '';

# -----------------------------------------------------------------[ Global ]--
$PROGNAME        = 'check_ircd';
$VERSION         = '@NP_VERSION@';
my $nick         = "ircd$$";

# -------------------------------------------------------------[ print_help ]--
sub print_help ()
{
    print_revision($PROGNAME,$VERSION);
    print "Copyright (c) 2014 SUSE Linux Products GmbH, Nuremberg
based on the original work of Richard Mayhew/Karl DeBisschop in 2000

Perl Check IRCD monitoring plugin.

";
    print_usage();
    print "
-H, --hostname=HOST
   Name or IP address of host to check
-w, --warning=INTEGER
   Number of connected users which generates a warning state (Default: 50)
-c, --critical=INTEGER
   Number of connected users which generates a critical state (Default: 100)
-p, --port=INTEGER
   Port that the ircd daemon is running on <host> (Default: 6667)
-v, --verbose
   Print extra debugging information
-s, --ssl
   Use SSL for connection (NOTE: might need '-p 6697' option)
";
}

# ------------------------------------------------------------[ print_usage ]--
sub print_usage () {
    print "Usage: $PROGNAME -H <host> [-w <warn>] [-c <crit>] [-p <port>] [-s]\n";
}

# ------------------------------------------------------------------[ debug ]--
sub debug ($$)
{
    my ($string,$verbose) = @_;
    if ($verbose){
        print STDOUT "DEBUG: $string";
    }
}

# ----------------------------------------------------------------[ connect ]--
sub connection ($$$$$$) {
    my ($server,$port,$ssl,$ping_timeout,$nick,$verbose) = @_;
    my $user=-1;
    debug("Attempting connect.\n",$verbose);
    # Connect to server
    debug("Connecting ...........\n",$verbose);
    my $sock = IO::Socket::INET6->new( PeerAddr => $server,
                                       PeerPort => $port,
                                       Proto => 'tcp',
                                       Domain => AF_UNSPEC ) or return ($user);
    if($ssl) {
        use IO::Socket::SSL;
        debug("Starting SSL .........\n",$verbose);
        IO::Socket::SSL->start_SSL( $sock,
                                    SSL_verify_mode => 0, # Do not verify certificate
        ) or die "SSL handshake failed: $SSL_ERROR";
    }
    debug("Connected to server:   $server on port: $port\n",$verbose);
    # Set nick and username
    debug("Sending user info ....\n",$verbose);
    print $sock "NICK $nick\nUSER monitor localhost localhost : \n";
    # Catch SIGALRM from the OS when timeout expired.
    local $SIG{ALRM} = sub {$sock->shutdown(0);};
    # Send all incomming data to the parser
    while (<$sock>) {
        alarm 0;
        chomp($_);
        if (/^PING \:(.+)/) {
            debug("Received PING request, sending PONG :$1\n",$verbose);
            print $sock "PONG :$1\n";
        }
        elsif (/\:I have\s+(\d+)/){
            $user=$1;
            last;
        }
        alarm $ping_timeout;
    }
    debug("Closing socket.\n",$verbose);
    close $sock;
    return $user;
}

# ------------------------------------------------------------[ check_users ]--
sub check_users ($$$){
    my ($users,$crit,$warn)=@_;
	$users =~ s/\ //g;
    my ($state,$answer);
    if ($users >= 0) {
        if ($users > $crit) {
                $state = "CRITICAL";
                $answer = "Critical Number Of Clients Connected : $users (Limit = $crit)";
        
        } elsif ($users > $warn) {
                $state = "WARNING";
                $answer = "Warning Number Of Clients Connected : $users (Limit = $warn)";
        
        } else {
                $state = "OK";
                $answer = "IRCD ok - Current Local Users: $users";
        }
        $answer.="|users=$users;$warn;$crit;0\n";
    } else {
        $state = "UNKNOWN";
        $answer = "Server has less than 0 users! Something is Really WRONG!\n";
    }
	return ($answer,$state)
}

# ===================================================================[ MAIN ]==
MAIN:
{
    my $answer = 'IRCD UNKNOWN: Unknown error - maybe could not authenticate\n';
    my $state = 'UNKOWN';
    my $hostname;
    Getopt::Long::Configure('bundling');
    GetOptions
         (  "V"   => \$opt_V,  "version"    => \$opt_V,
            "h"   => \$opt_h,  "help"       => \$opt_h,
            "v"   => \$verbose,"verbose"    => \$verbose,
            "s"   => \$ssl,    "ssl"        => \$ssl,
            "t=i" => \$opt_t,  "timeout=i"  => \$opt_t,
            "w=i" => \$opt_w,  "warning=i"  => \$opt_w,
            "c=i" => \$opt_c,  "critical=i" => \$opt_c,
            "p=i" => \$opt_p,  "port=i"     => \$opt_p,
            "H=s" => \$opt_H,  "hostname=s" => \$opt_H);
    if ($opt_V) {
        print_revision($PROGNAME,$VERSION);
        exit $ERRORS{'OK'};
    }
    if ($opt_h) {print_help(); exit $ERRORS{'OK'};}
    ($opt_H) || ($opt_H = shift @ARGV) || usage("Host name/address not specified\n");
    my $server = $1 if ($opt_H =~ /([-.A-Za-z0-9]+)/);
    ($server) || usage("Invalid host: $opt_H\n");
    ($opt_w) || ($opt_w = shift @ARGV) || ($opt_w = 50);
    my $warn = $1 if ($opt_w =~ /^([0-9]+)$/);
    ($warn) || usage("Invalid warning threshold: $opt_w\n");
    ($opt_c) || ($opt_c = shift @ARGV) || ($opt_c = 100);
    my $crit = $1 if ($opt_c =~ /^([0-9]+)$/);
    ($crit) || usage("Invalid critical threshold: $opt_c\n");
    if ($crit < $warn){
	    usage("Invalid threshold: $crit for critical is lower than $warn for warning\n");
    }
    ($opt_p) || ($opt_p = shift @ARGV) || ($opt_p = 6667);
    my $port = $1 if ($opt_p =~ /^([0-9]+)$/);
    ($port) || usage("Invalid port: $opt_p\n");
    if ($opt_t && $opt_t =~ /^([0-9]+)$/) { $TIMEOUT = $1; }
    # Just in case of problems, let's not hang Nagios
    $SIG{'ALRM'} = sub {
        print "Somthing is Taking a Long Time, Increase Your TIMEOUT (Currently Set At $TIMEOUT Seconds)\n";
        exit $ERRORS{"UNKNOWN"};
    };
    alarm($TIMEOUT);
    my $ping_timeout=$TIMEOUT-1;
    my $users=connection($server,$port,$ssl,$ping_timeout,$nick,$verbose);
    ($answer,$state)=check_users($users,$crit,$warn);
    print "$answer";
    exit $ERRORS{$state};
}
