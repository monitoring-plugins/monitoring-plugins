#!/usr/bin/perl

# Oracle plugin submitted by Christopher Maser (maser@onvista.de)
# 12/31/1999

my $host=$ARGV[0];
my @test=`tnsping $host`;
my $arg=$test[6];
chomp $arg;
if ($arg =~ /^OK (.*)/)
{print "$arg"; exit 0}
else {exit 2;}

