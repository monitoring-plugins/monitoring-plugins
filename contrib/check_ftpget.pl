#!/usr/bin/perl -w
## Written 12/5/00 Jeremy Hanmer 
# $Id$

use strict;
use Net::FTP;
use Getopt::Std;

use vars qw($opt_H $opt_u $opt_p $opt_f);
getopts("H:u:p:f:");

my $host = $opt_H || 
    die "usage: check_ftp.pl -h host [<-u user> <-p pass> <-f file>]\n";

my $username = $opt_u || 'anonymous';
my $pass = $opt_p || "$ENV{'LOGNAME'}\@$ENV{'HOSTNAME'}" ;

my $file = $opt_f;

my $status = 0;
my $problem;
my $output = "ftp ok";

my $ftp = Net::FTP->new("$host") ||
    &crit("connect");

$ftp->login("$username", "$pass") ||
    &crit("login");

$ftp->get($file) ||
    &crit("get") if $file;

sub crit() 
{
    $problem = $_[0];
    $status = 2;
    if ( $problem eq 'connect' ) {
        $output = "can't connect";
    } elsif ( $problem eq 'login' ) {
        $output = "can't log in";
    } elsif ( $problem eq 'get' ) {
        $output = "cant get $file";
    }
}

print "$output\n";
exit $status;

