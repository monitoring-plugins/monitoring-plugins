#!/usr/bin/perl -w -I .. -I ../..
#
# Wrapper for running the test harnesses
#

use strict;

use Getopt::Long;

use NPTest qw(DetermineTestHarnessDirectory TestsFrom);

$ENV{LC_ALL} = 'C';

my @tstdir;

if ( ! GetOptions( "testdir:s" => \@tstdir ) )
{
  print "Usage: ${0} [--testdir=<directory>] [<test_harness.t> ...]\n";
  exit 1;
}

my @tests;

if ( scalar( @ARGV ) )
{
  @tests = @ARGV;
}
else
{
  my @directory = DetermineTestHarnessDirectory( @tstdir );

  if ( @directory == 0 )
  {
    print STDERR "$0: Unable to determine the test harness directory - ABORTING\n";
    exit 2;
  }

  for my $d ( @directory )
  {
    push (@tests, TestsFrom( $d, 1 ));
  }
}

if ( ! scalar( @tests ) )
{
  print STDERR "$0: Unable to determine the test harnesses to run - ABORTING\n";
  exit 3;
}

use Test::Harness;

runtests( @tests );
